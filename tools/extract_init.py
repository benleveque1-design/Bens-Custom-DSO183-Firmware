#!/usr/bin/env python3
"""Extract the vendor firmware's display-init byte stream from the disassembly.

The vendor's init routine (0x08001d1c) calls two bus primitives:
  0x08001cb9 = "write command byte"  (RS = 0)
  0x08001ced = "write data byte"     (RS = 1)
and passes the byte in r0.  This script walks the routine, tracks the register
values, and prints the resulting stream so it can be replayed byte-for-byte.

usage: extract_init.py [--bin VENDOR.BIN] [--asm DISASM.ASM] [start_hex] [end_hex]

The vendor firmware binary and its Ghidra disassembly are NOT part of this
repository (they are JYE Tech's copyrighted code, kept outside the tree for
licensing reasons).  Pass their paths with --bin/--asm, or set the
DSO183_VENDOR_BIN / DSO183_VENDOR_ASM environment variables.
"""
import argparse
import os
import re
import struct
import sys

BASE = 0x08000000
CMD_WRITER = 0x08001CB9
DATA_WRITER = 0x08001CED
LINE = re.compile(r"^\s*([0-9a-f]{6,8}):\s+(.*)$")


def fail(msg):
    print("ERROR: %s" % msg, file=sys.stderr)
    sys.exit(1)


def pool(addr):
    off = addr - BASE
    if off < 0 or off + 4 > len(data):
        return None
    return struct.unpack_from("<I", data, off)[0]


def main():
    ap = argparse.ArgumentParser(description="Extract the vendor firmware's display-init byte stream from the disassembly.")
    ap.add_argument("--bin", default=os.environ.get("DSO183_VENDOR_BIN"),
                    help="path to the vendor firmware .bin (env: DSO183_VENDOR_BIN)")
    ap.add_argument("--asm", default=os.environ.get("DSO183_VENDOR_ASM"),
                    help="path to the Ghidra thumb disassembly .asm (env: DSO183_VENDOR_ASM)")
    ap.add_argument("start", nargs="?", type=lambda s: int(s, 0),
                    default="0x08001D1C", help="start address (default 0x08001D1C)")
    ap.add_argument("end", nargs="?", type=lambda s: int(s, 0),
                    default="0x08001E90", help="end address (default 0x08001E90)")
    args = ap.parse_args()

    if not args.bin:
        fail("no vendor binary: pass --bin or set DSO183_VENDOR_BIN")
    if not args.asm:
        fail("no disassembly: pass --asm or set DSO183_VENDOR_ASM")
    if not os.path.isfile(args.bin):
        fail("vendor binary not found: %s" % args.bin)
    if not os.path.isfile(args.asm):
        fail("disassembly not found: %s" % args.asm)

    with open(args.bin, "rb") as fh:
        data = fh.read()

    regs = {}
    ptr = {}
    out = []
    for raw in open(args.asm, errors="replace"):
        m = LINE.match(raw)
        if not m:
            continue
        addr = int(m.group(1), 16)
        if not (start <= addr < end):
            continue
        body = m.group(2).split(";")[0].strip()
        head = body.split()[0] if body.split() else ""

        mm = re.match(r"(movs|movw|mov\.w)\s+(r\d+),\s*#(0x[0-9a-f]+|\d+)", body)
        if mm:
            regs[mm.group(2)] = int(mm.group(3), 0)
            continue
        mm = re.match(r"mov\s+(r\d+),\s*(r\d+)$", body)
        if mm:
            regs[mm.group(1)] = regs.get(mm.group(2), 0)
            continue
        mm = re.match(r"ands\.w\s+(r\d+),\s*(r\d+),\s*#(0x[0-9a-f]+|\d+)", body)
        if mm:
            regs[mm.group(1)] = regs.get(mm.group(2), 0) & int(mm.group(3), 0)
            continue
        mm = re.match(r"ldr\s+(r\d+),\s*\[pc,\s*#\d+\]", body)
        if mm:
            pm = re.search(r"@ \(0x([0-9a-f]+)\)", raw)
            if pm:
                v = pool(int(pm.group(1), 16))
                if v is not None:
                    ptr[mm.group(1)] = v
                    regs[mm.group(1)] = v
            continue
        mm = re.match(r"blx\s+(r\d+)", body)
        if mm:
            target = ptr.get(mm.group(1))
            val = regs.get("r0", 0) & 0xFF
            if target == CMD_WRITER:
                out.append(("CMD", val))
            elif target == DATA_WRITER:
                out.append(("DATA", val))
            continue

    print("/* vendor init stream: %d writes */" % len(out))
    lines = []
    for kind, val in out:
        lines.append("0x%02X%s" % (val, " /*cmd*/" if kind == "CMD" else ""))
    for i in range(0, len(lines), 6):
        print("    " + ", ".join(lines[i:i + 6]) + ",")
    print()
    print("/* grouped as commands with their parameters */")
    i = 0
    while i < len(out):
        kind, val = out[i]
        if kind == "CMD":
            params = []
            j = i + 1
            while j < len(out) and out[j][0] == "DATA":
                params.append(out[j][1])
                j += 1
            if params:
                print("    0x%02X, %d, %s," % (val, len(params),
                      ", ".join("0x%02X" % p for p in params)))
            else:
                print("    0x%02X, 0," % val)
            i = j
        else:
            i += 1


if __name__ == "__main__":
    main()
