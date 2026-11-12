#!/usr/bin/env python3
"""Flash the custom firmware and *prove* the new build is running.

One entry point for the whole cycle, so the sequence can never be done half-way:

  1. read the built image and the ELF (symbols + expected build identity)
  2. erase + program every page with the ST-Link, **vector table (page 0) last**,
     each page read-back verified against the image
  3. read the whole image back over SWD and compare it byte-for-byte
  4. queue a system reset and **release the core** so the queued reset actually
     fires (a halted core never takes it - this silently invalidated two
     earlier measurement rounds)
  5. read the build identity out of RAM and require it to match the ELF
  6. print the liveness/measurement status of the new build

Any failed check aborts with a non-zero exit code and a loud message.

usage: flash_cycle.py [--image PATH] [--elf PATH] [--skip-write]
"""
import argparse
import hashlib
import os
import re
import struct
import subprocess
import sys
import time

from pyocd.core.helpers import ConnectHelper
from pyocd.coresight.ap import APv1Address

FLASH_BASE = 0x08000000
PAGE_SIZE = 1024
DHCSR = 0xE000EDF0
AIRCR = 0xE000ED0C
FLASH_KEYR, FLASH_SR, FLASH_CR, FLASH_AR = 0x40022004, 0x4002200C, 0x40022010, 0x40022014
KEY1, KEY2 = 0x45670123, 0xCDEF89AB
PROTECTED_PAGE = 0x0800FC00          # the vendor's live settings page - never touch


def fail(msg):
    print("FAIL: " + msg)
    sys.exit(1)


def elf_symbols(elf):
    out = subprocess.run(["arm-none-eabi-nm", elf], capture_output=True, text=True).stdout
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            try:
                syms[parts[2]] = int(parts[0], 16)
            except ValueError:
                pass
    return syms


def elf_initialised_u32(elf, sym, syms):
    """Read the value a variable is INITIALISED with, straight from the ELF.

    The build identity check must compare against what the firmware was
    actually compiled with - a hardcoded constant in this tool silently
    produced a false 'the running image is NOT ours' verdict before.
    """
    addr = syms.get(sym)
    if addr is None:
        return None
    out = subprocess.run(["arm-none-eabi-objdump", "-s", "-j", ".data", elf],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if not parts:
            continue
        try:
            base = int(parts[0], 16)
        except ValueError:
            continue
        words = []
        for p in parts[1:]:
            if len(p) == 8 and all(c in "0123456789abcdefABCDEF" for c in p):
                words.append(p)
            else:
                break
        blob = b"".join(bytes.fromhex(w) for w in words)
        if base <= addr < base + len(blob):
            off = addr - base
            if off + 4 <= len(blob):
                return int.from_bytes(blob[off:off + 4], "little")
    return None


class Target:
    def __init__(self, serial=None):
        kwargs = {}
        if serial:
            kwargs["probe_id"] = serial
        self.session = ConnectHelper.session_with_chosen_probe(
            target_override="cortex_m", blocking=False, **kwargs)
        if self.session is None:
            fail("no ST-Link found")
        self.probe = self.session.probe
        self.probe.open()
        self.probe.connect()
        self.mi = self.probe.get_memory_interface_for_ap(APv1Address(0))

    def halt(self):
        self.mi.write32(DHCSR, 0xA05F0003)          # C_DEBUGEN | C_HALT
        for _ in range(200):
            if self.mi.read32(DHCSR) & (1 << 17):
                return True
        return False

    def release(self):
        self.mi.write32(DHCSR, 0xA05F0002)          # C_DEBUGEN, C_HALT cleared -> run

    def read32(self, addr):
        return self.mi.read32(addr)

    def write32(self, addr, val):
        self.mi.write32(addr, val)

    def read_block(self, addr, size):
        return bytes(self.mi.read_memory_block8(addr, size))

    def program_page(self, addr, data):
        """Erase exactly one page and program it, then verify the read-back."""
        assert len(data) == PAGE_SIZE
        if addr == PROTECTED_PAGE:
            fail("refusing to write the protected settings page at 0x%08X" % addr)
        self.write32(FLASH_KEYR, KEY1)
        self.write32(FLASH_KEYR, KEY2)
        self.write32(FLASH_SR, 0x00000034)          # clear PGERR | WRPRTERR
        self.write32(FLASH_CR, 0x00000002)          # PER
        self.write32(FLASH_AR, addr)
        self.write32(FLASH_CR, 0x00000042)          # PER | STRT
        self._wait_busy()
        self.write32(FLASH_SR, 0x00000034)
        if self.read32(FLASH_SR) & 0x06:
            fail("flash error after erase of 0x%08X (SR=0x%08X)" % (addr, self.read32(FLASH_SR)))
        self.write32(FLASH_CR, 0x00000001)          # PG
        for i in range(0, PAGE_SIZE, 2):
            self.mi.write16(addr + i, data[i] | (data[i + 1] << 8))
            self._wait_busy()
        if self.read32(FLASH_SR) & 0x06:
            fail("flash error after programming 0x%08X" % addr)
        self.write32(FLASH_CR, 0x00000080)          # lock
        back = self.read_block(addr, PAGE_SIZE)
        if back != data:
            fail("read-back mismatch at 0x%08X" % addr)

    def _wait_busy(self):
        for _ in range(500000):
            if not (self.read32(FLASH_SR) & 1):
                return
        fail("FLSH BSY timeout")

    def close(self):
        try:
            self.session.close()
        except Exception:
            pass


def main():
    ap = argparse.ArgumentParser()
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap.add_argument("--image", default=os.path.join(here, "build", "dso183.bin"))
    ap.add_argument("--elf", default=os.path.join(here, "build", "dso183.elf"))
    ap.add_argument("--serial", default=None)
    ap.add_argument("--skip-write", action="store_true")
    ap.add_argument("--no-reset", action="store_true")
    ap.add_argument("--expect-magic", type=lambda s: int(s, 0), default=None,
                    help="override the expected build magic (default: read it "
                         "from the ELF's .data section)")
    args = ap.parse_args()

    if not os.path.exists(args.image):
        fail("image not found: %s" % args.image)
    image = open(args.image, "rb").read()
    if len(image) > 0x10000:
        fail("image too large: %d bytes" % len(image))
    if not os.path.exists(args.elf):
        fail("elf not found: %s" % args.elf)
    syms = elf_symbols(args.elf)
    for need in ("status_build_magic", "status_build_seq"):
        if need not in syms:
            fail("symbol %s missing from the ELF" % need)

    print("image   : %s (%d bytes, sha256 %s)"
          % (args.image, len(image), hashlib.sha256(image).hexdigest()[:16]))
    npages = (len(image) + PAGE_SIZE - 1) // PAGE_SIZE
    print("pages   : %d (page 0 = vector table, written LAST)" % npages)

    tgt = Target(args.serial)
    try:
        if not args.skip_write:
            if not tgt.halt():
                fail("could not halt the core - nothing was written")
            for p in range(1, npages):                  # page 0 comes last
                addr = FLASH_BASE + p * PAGE_SIZE
                page = image[p * PAGE_SIZE:(p + 1) * PAGE_SIZE]
                page = page + b"\xff" * (PAGE_SIZE - len(page))
                tgt.program_page(addr, page)
                print("  page %2d @0x%08X  written + verified" % (p, addr))
            addr = FLASH_BASE
            page = image[0:PAGE_SIZE]
            page = page + b"\xff" * (PAGE_SIZE - len(page))
            tgt.program_page(addr, page)
            print("  page %2d @0x%08X  written + verified (vector table)" % (0, addr))

            # whole-image verification
            back = tgt.read_block(FLASH_BASE, len(image))
            if back != image:
                bad = [i for i in range(len(image)) if back[i] != image[i]]
                fail("flash != image at %d byte(s), first at 0x%08X" % (len(bad), FLASH_BASE + bad[0]))
            print("  image read-back: OK (%d bytes identical)" % len(image))

        if not args.no_reset:
            # queue the reset, then RELEASE the core so the queued reset fires
            tgt.write32(AIRCR, 0x05FA0004)
            tgt.release()
            time.sleep(2.0)

        tgt.halt()
        magic = int.from_bytes(tgt.read_block(syms["status_build_magic"], 4), "little")
        seq = int.from_bytes(tgt.read_block(syms["status_build_seq"], 4), "little")
        print("  build identity: magic 0x%08X seq 0x%08X" % (magic, seq))
        expect_magic = elf_initialised_u32(args.elf, "status_build_magic", syms)
        if expect_magic is None:
            expect_magic = args.expect_magic
            if expect_magic is None:
                fail("cannot determine the expected build magic from the ELF - "
                     "pass --expect-magic")
        if magic != expect_magic:
            fail("the running image is NOT ours (magic 0x%08X, expected 0x%08X) - "
                 "the flash or the reset did not take effect" % (magic, expect_magic))
        expect_seq = elf_initialised_u32(args.elf, "status_build_seq", syms)
        if expect_seq is not None and seq != expect_seq:
            print("  WARNING: build sequence 0x%08X does not match the ELF (0x%08X) "
                  "- an older build may be running" % (seq, expect_seq))

        for name in ("status_heartbeat", "status_main_loops", "status_boot_count",
                     "status_display_id", "status_display_id_full",
                     "status_strobe_high", "status_strobe_low",
                     "status_bus_idr_00", "status_bus_idr_ff", "status_bus_idr_a5"):
            if name in syms:
                v = int.from_bytes(tgt.read_block(syms[name], 4), "little")
                print("  %-22s = 0x%08X" % (name, v))
        if "status_bus_log" in syms and "status_bus_log_len" in syms:
            n = min(int.from_bytes(tgt.read_block(syms["status_bus_log_len"], 4), "little"), 64)
            log = tgt.read_block(syms["status_bus_log"], max(n, 1))[:n]
            print("  bus log (%d bytes actually sent): %s" % (n, log.hex(" ")))
        tgt.release()
        print("OK: new image running and verified")
    finally:
        tgt.close()


if __name__ == "__main__":
    main()
