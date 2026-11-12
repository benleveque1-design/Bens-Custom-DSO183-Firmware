#!/usr/bin/env python3
"""Find every 8080-style byte writer in the vendor firmware.

A display bus writer has the shape:
    * a byte is placed on some GPIO's ODR (offset 0x0C), and
    * a strobe bit is toggled on a *different* GPIO port.

This lists all of them with the ports and pins involved, so we can check whether
PB0..PB7 + PC14/PC15 really is the panel bus, or whether the panel is on other
pins (e.g. driven by the GPIOA bit-bang driver).

usage: find_8080_writers.py [vendor.bin] [--asm DISASM.ASM]

The vendor firmware binary and its Ghidra disassembly are NOT part of this
repository (they are JYE Tech's copyrighted code, kept outside the tree for
licensing reasons).  Pass the vendor binary as the first argument and the
disassembly with --asm (or set DSO183_VENDOR_ASM).
"""
import argparse
import argparse
import os
import re
import struct
import sys

BASE = 0x08000000
ASM = os.environ.get("DSO183_VENDOR_ASM")
LINE = re.compile(r"^\s*([0-9a-f]{6,8}):\s+([0-9a-f ]+?)\s{2,}(\S+)\s*(.*)$")


def fail(msg):
    print("ERROR: %s" % msg, file=sys.stderr)
    sys.exit(1)

PORTS = {
    0x40010800: "GPIOA", 0x40010C00: "GPIOB", 0x40011000: "GPIOC",
    0x40011400: "GPIOD", 0x40011800: "GPIOE",
}


def main():
    ap = argparse.ArgumentParser(description="Find every 8080-style byte writer in the vendor firmware.")
    ap.add_argument("vendor_bin", nargs="?",
                    help="path to the vendor firmware .bin (NOT part of this repo)")
    ap.add_argument("--asm", default=ASM,
                    help="path to the Ghidra thumb disassembly .asm (env: DSO183_VENDOR_ASM)")
    args = ap.parse_args()

    path = args.vendor_bin
    if not path:
        fail("no vendor binary: pass it as the first argument (NOT part of this repo)")
    if not args.asm:
        fail("no disassembly: pass --asm or set DSO183_VENDOR_ASM")
    if not os.path.isfile(path):
        fail("vendor binary not found: %s" % path)
    if not os.path.isfile(args.asm):
        fail("disassembly not found: %s" % args.asm)

    d = open(path, "rb").read()

    def pool(a):
        o = a - BASE
        return struct.unpack_from("<I", d, o)[0] if 0 <= o <= len(d) - 4 else None

    rows = []
    for raw in open(ASM, errors="replace"):
        m = LINE.match(raw)
        if not m:
            continue
        a = int(m.group(1), 16)
        body = (m.group(3) + " " + m.group(4)).split(";")[0].strip()
        pm = re.search(r"@ \(0x([0-9a-f]+)\)", raw)
        rows.append((a, body, pool(int(pm.group(1), 16)) if pm else None))

    # a register holds a GPIO base; track the last base loaded into each register
    reg = {}
    base_of = {}
    hits = []
    for i, (a, body, pv) in enumerate(rows):
        lm = re.match(r"ldr\s+(r\d+),\s*\[pc,\s*#\d+\]", body)
        if lm and pv in PORTS:
            reg[lm.group(1)] = pv
            base_of[lm.group(1)] = pv
        mm = re.match(r"movs\s+(r\d+),\s*#(0x[0-9a-f]+|\d+)", body)
        if mm:
            reg[lm.group(1) if lm else ""] = reg.get("")
        # detect: write a byte to <gpio>+0x0C
        st = re.match(r"str\s+(r\d+),\s*\[(r\d+),\s*#12\]", body)
        if st:
            port = base_of.get(st.group(2))
            if port:
                # look back/forward for a strobe write to another port
                for j in range(max(0, i - 12), min(len(rows), i + 12)):
                    b2 = rows[j][1]
                    m2 = re.match(r"str\s+(r\d+),\s*\[(r\d+),\s*#(16|20)\]", b2)
                    if m2 and base_of.get(m2.group(2)) and base_of.get(m2.group(2)) != port:
                        hits.append((a, PORTS[port], PORTS[base_of[m2.group(2)]],
                                     rows[j][0], int(m2.group(3))))
                        break
    print("8080-style writers found (byte to a port ODR + strobe on another port):")
    seen = set()
    for a, dataport, strobeport, at, off in hits:
        key = (dataport, strobeport, off)
        if key in seen:
            continue
        seen.add(key)
        print("  data=%-6s strobe=%-6s (BSRR/BRR offset %d)  first at 0x%08X" % (dataport, strobeport, off, a))
    if not hits:
        print("  (none)")


if __name__ == "__main__":
    main()
