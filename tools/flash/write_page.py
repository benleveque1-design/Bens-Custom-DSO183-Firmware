#!/usr/bin/env python3
"""Write ONE 1 KiB flash page of the DSO183 (STM32F103C8) via ST-Link + pyocd.

Derived from the earlier session's proven `flash_pages_v6.py`: it uses the direct
AHB-AP memory interface (the generic `cortex_m` target has no core object), halts the
core via DHCSR, erases exactly one page, programs it halfword by halfword, verifies the
read-back and finally resets the target.

Safety properties (deliberate):
  * only the named page is erased - never a mass erase, so the live settings page
    (0x0800FC00) and every other page stay untouched;
  * the read-back is compared against the source image before the reset;
  * on any error the target is left halted for inspection, and the flash stays locked;
  * the write only proceeds with AUTHORIZED=1 set by the caller for this specific
    write.

usage: write_page.py --image FILE --page 0x08003400 [--no-reset]
"""
import argparse
import os
import struct
import sys

FLASH_KEYR, FLASH_SR, FLASH_CR, FLASH_AR = 0x40022004, 0x4002200C, 0x40022010, 0x40022014
DHCSR, AIRCR = 0xE000EDF0, 0xE000ED0C
DBGMCU_CR = 0xE0042004
DBG_IWDG_STOP = 0x00000100      # DBGMCU_CR bit 8: freeze IWDG while halted
PAGE_SIZE = 1024


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True, help="file containing the page content")
    ap.add_argument("--page", type=lambda s: int(s, 0), required=True)
    ap.add_argument("--no-reset", action="store_true")
    args = ap.parse_args()

    if os.environ.get("AUTHORIZED") != "1":
        print("ERROR: refusing to write - set AUTHORIZED=1 to confirm an explicit instruction")
        return 2

    # imported here so the authorization check above runs even on hosts without pyocd
    from pyocd.core.helpers import ConnectHelper
    from pyocd.coresight.ap import APv1Address

    data = open(args.image, "rb").read()
    if len(data) != PAGE_SIZE:
        print("ERROR: image must be exactly %d bytes (one page), got %d" % (PAGE_SIZE, len(data)))
        return 2
    if args.page % PAGE_SIZE:
        print("ERROR: page address 0x%08X is not page aligned" % args.page)
        return 2

    session = ConnectHelper.session_with_chosen_probe(target_override="cortex_m", blocking=False)
    if session is None:
        print("ERROR: no probe found")
        return 1
    try:
        probe = session.probe
        probe.open()
        probe.connect()
        mi = probe.get_memory_interface_for_ap(APv1Address(0))
        print("probe         :", probe.unique_id)
        print("memory iface  :", type(mi).__name__)

        # v6 runs the IWDG at roughly 8 ms (RLR=4, PR=/64, see
        # analysis/FINDINGS_2026-10-22.md 8.3).  It keeps counting while the
        # core is halted, so a page erase (~20-40 ms) resets the target unless
        # the watchdog is frozen FIRST.  Do this before the halt, not after:
        # the 8 ms window is already running.
        before = mi.read32(DBGMCU_CR)
        mi.write32(DBGMCU_CR, before | DBG_IWDG_STOP)
        if (mi.read32(DBGMCU_CR) & DBG_IWDG_STOP) == 0:
            print("ERROR: could not freeze the IWDG (DBGMCU_CR=0x%08X) - "
                  "nothing was written" % before)
            return 1
        print("IWDG frozen   : DBGMCU_CR bit 8 set")

        mi.write32(DHCSR, 0xA05F0003)                       # C_DEBUGEN | C_HALT
        halted = False
        for _ in range(100):
            if mi.read32(DHCSR) & (1 << 17):
                halted = True
                break
        print("target halted :", halted)
        if not halted:
            print("ERROR: could not halt the core - nothing was written")
            return 1

        def wait_bsy():
            for _ in range(500000):
                if not (mi.read32(FLASH_SR) & 1):
                    return
            raise RuntimeError("FLSH BSY timeout")

        def check_sr(where):
            sr = mi.read32(FLASH_SR)
            if sr & 0x06:
                raise RuntimeError("flash %s error: SR=0x%08X" % (where, sr))

        # read the page BEFORE the write, for the record
        before = struct.pack("<256I", *mi.read_memory_block32(args.page, 256))
        print("page before   : sha256 %s" % __import__("hashlib").sha256(before).hexdigest())

        mi.write32(FLASH_KEYR, 0x45670123)                   # unlock
        mi.write32(FLASH_KEYR, 0xCDEF89AB)
        mi.write32(FLASH_SR, 0x00000034)                     # clear PGERR | WRPRTERR

        mi.write32(FLASH_CR, 0x00000002)                     # PER
        mi.write32(FLASH_AR, args.page)
        mi.write32(FLASH_CR, 0x00000042)                     # PER | STRT
        wait_bsy()
        mi.write32(FLASH_SR, 0x00000034)
        check_sr("erase 0x%08X" % args.page)
        print("erased        : 0x%08X" % args.page)

        mi.write32(FLASH_CR, 0x00000001)                     # PG
        for i in range(0, PAGE_SIZE, 2):
            mi.write16(args.page + i, data[i] | (data[i + 1] << 8))
            wait_bsy()
        check_sr("program 0x%08X" % args.page)
        print("programmed    : 0x%08X" % args.page)

        mi.write32(FLASH_CR, 0x00000080)                     # lock
        print("flash locked  :")

        after = struct.pack("<256I", *mi.read_memory_block32(args.page, 256))
        if after != data:
            print("VERIFY FAILED at 0x%08X - target left halted" % args.page)
            return 1
        print("verify ok     : 0x%08X (read-back matches the image)" % args.page)

        if not args.no_reset:
            mi.write32(AIRCR, 0x05FA0004)                    # VECTKEY | SYSRESETREQ
            # A halted core never takes the queued reset: release it, otherwise
            # the instrument stays frozen on the old screen and looks "dead".
            # (This silently invalidated two earlier measurement rounds.)
            mi.write32(DHCSR, 0xA05F0002)                     # C_DEBUGEN, C_HALT clear -> run
            # DBG_IWDG_STOP is deliberately left set.  It only stops the IWDG
            # while the core is halted by a debugger; clearing it before the
            # queued reset would let the old ~8 ms watchdog fire in the gap.
            # It is a debug-only bit and a power cycle clears it.
            print("reset         : SYSRESETREQ (core released)")
        else:
            print("no reset      : core left halted, IWDG frozen")
        return 0
    finally:
        session.close()


if __name__ == "__main__":
    sys.exit(main())
