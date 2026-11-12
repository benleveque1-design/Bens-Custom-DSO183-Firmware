#!/usr/bin/env python3
"""Read the DSO183 status counters over SWD - READ ONLY, never writes.

The hand-over (FLASH_HANDOVER.md) promises an objective, eyes-free check of
the diagnostic firmware.  This is that check: it reads the `status_*`
variables out of RAM while the instrument is running and prints them, so the
liveness of the firmware can be confirmed over SWD even when nobody can see
the panel.

Safety properties (deliberate):
  * READ ONLY - the script never writes memory, never writes flash, never
    resets the target and never halts the core;
  * no AUTHORIZED gate is needed because nothing is modified;
  * with --symbols-only it does not touch the probe at all, so it can be
    used to check the symbol lookup offline.

usage:
  live_check.py [--elf build/dso183.elf] [--symbols-only] [--interval 1.0]
"""
import argparse
import os
import re
import subprocess
import sys

SYMBOLS = [
    ("status_build_magic", 4, "build magic (expect 0x4453424D)"),
    ("status_build_seq", 4, "build sequence (must match the panel's S=)"),
    ("status_boot_count", 4, "boot count (climbing = watchdog reset loop)"),
    ("status_heartbeat", 4, "SysTick heartbeat (must advance)"),
    ("status_main_loops", 4, "main-loop counter (must advance)"),
    ("status_display_id", 4, "raw display ID read (0x00934108 = ILI9341)"),
    ("status_init_errors", 4, "init error flags (0 = all init steps ok)"),
    ("status_acq_hw_errors", 4, "acquisition hardware errors"),
]

ADC_SYMBOL = ("status_adc_raw", 16, "raw ADC1 counts, PA0..PA7")

EXPECT_MAGIC = 0x4453424D


def elf_symbols(path):
    """name -> address, from `arm-none-eabi-nm`."""
    out = subprocess.run(["arm-none-eabi-nm", path], capture_output=True, text=True)
    if out.returncode != 0:
        raise RuntimeError("arm-none-eabi-nm failed: %s" % out.stderr.strip())
    table = {}
    for line in out.stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and re.fullmatch(r"[0-9a-fA-F]+", parts[0]):
            table[parts[2]] = int(parts[0], 16)
    return table


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", default=os.path.join("build", "dso183.elf"))
    ap.add_argument("--symbols-only", action="store_true",
                    help="print the symbol addresses and exit (no probe)")
    ap.add_argument("--interval", type=float, default=1.0,
                    help="seconds between the two samples (liveness check)")
    args = ap.parse_args()

    if not os.path.exists(args.elf):
        print("ERROR: %s not found - build the firmware first" % args.elf)
        return 2
    try:
        syms = elf_symbols(args.elf)
    except Exception as exc:
        print("ERROR: %s" % exc)
        return 2

    wanted = SYMBOLS + [ADC_SYMBOL]
    missing = [name for name, _, _ in wanted if name not in syms]
    if missing:
        print("ERROR: symbol(s) not found in %s: %s" % (args.elf, ", ".join(missing)))
        return 2

    for name, size, note in wanted:
        print("%-20s @ 0x%08X  (%d bytes)  %s" % (name, syms[name], size, note))
    if args.symbols_only:
        return 0

    try:
        from pyocd.core.helpers import ConnectHelper
        from pyocd.coresight.ap import APv1Address
    except ImportError as exc:
        print("ERROR: pyocd not available: %s" % exc)
        return 2

    session = ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
                                                      blocking=False)
    if session is None:
        print("ERROR: no probe found")
        return 1
    try:
        probe = session.probe
        probe.open()
        probe.connect()
        mi = probe.get_memory_interface_for_ap(APv1Address(0))
        print("probe         :", probe.unique_id)
        print("read-only     : no writes, no halt, no reset")
        print()

        def snapshot():
            values = {}
            for name, size, _ in SYMBOLS:
                values[name] = mi.read32(syms[name]) & 0xFFFFFFFF
            # the ADC array is uint16_t[8] and may sit at an odd address, so it
            # is read byte-wise - a 32-bit read there would straddle into the
            # neighbouring variable
            raw = 0
            for i in range(8):
                lo = mi.read8(syms[ADC_SYMBOL[0]] + 2 * i)
                hi = mi.read8(syms[ADC_SYMBOL[0]] + 2 * i + 1)
                raw |= (lo | (hi << 8)) << (16 * i)
            values[ADC_SYMBOL[0]] = raw
            return values

        a = snapshot()
        import time
        time.sleep(max(0.0, args.interval))
        b = snapshot()

        print("%-20s %12s %12s %s" % ("counter", "sample A", "sample B", "delta"))
        for name, _, note in SYMBOLS:
            delta = (b[name] - a[name]) & 0xFFFFFFFF
            print("%-20s %12d %12d %12d   %s" % (name, a[name], b[name], delta, note))
        adc = b[ADC_SYMBOL[0]]
        print("%-20s %s" % ("adc raw (LE)",
                            " ".join("%04X" % ((adc >> (16 * i)) & 0xFFFF) for i in range(4))))

        print()
        ok = True
        if b["status_build_magic"] != EXPECT_MAGIC:
            print("FAIL: build magic 0x%08X != 0x%08X - this is NOT our firmware"
                  % (b["status_build_magic"], EXPECT_MAGIC))
            ok = False
        else:
            print("OK  : build magic 0x%08X (sequence 0x%02X)"
                  % (b["status_build_magic"], b["status_build_seq"] & 0xFF))
        if ((b["status_heartbeat"] - a["status_heartbeat"]) & 0xFFFFFFFF) == 0:
            print("FAIL: heartbeat did not advance - SysTick/core is not running")
            ok = False
        else:
            print("OK  : heartbeat advancing")
        if ((b["status_main_loops"] - a["status_main_loops"]) & 0xFFFFFFFF) == 0:
            print("FAIL: main-loop counter did not advance")
            ok = False
        else:
            print("OK  : main loop advancing")
        if b["status_init_errors"] != 0:
            print("WARN: init error flags 0x%02X set" % b["status_init_errors"])
        return 0 if ok else 1
    finally:
        try:
            session.close()
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())
