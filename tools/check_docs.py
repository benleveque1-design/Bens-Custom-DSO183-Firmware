#!/usr/bin/env python3
"""Cross-check the hand-over document against the artefacts it describes.

Every rebuild changes the image size, the build sequence, the md5 and often
the reset vector, and each of those is written into FLASH_HANDOVER.md by hand.
A stale document is worse than no document: it tells the person at the bench
to accept the wrong screen.  This check makes drift a build failure.

It verifies, for build/dso183.bin and build/build_seq.h:
  * image size (bytes)          vs the "| image |" row
  * page count / last page      vs the "| target range |" row
  * build sequence              vs the "build sequence" row
  * reset vector (thumb bit)    vs the "reset" text
  * md5                         vs the md5 line
and that the ELF's status_build_seq symbol carries the same sequence as
build_seq.h (i.e. the identity really is compiled into the image).

READ ONLY: touches no hardware.

usage: check_docs.py [--doc FLASH_HANDOVER.md] [--bin build/dso183.bin]
                     [--elf build/dso183.elf] [--seqh build/build_seq.h]
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys

PAGE = 1024


def fail(msg):
    print("FAIL: " + msg)
    return 1


def read_u32(data, off):
    return int.from_bytes(data[off:off + 4], "little")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--doc", default="FLASH_HANDOVER.md")
    ap.add_argument("--bin", default="build/dso183.bin")
    ap.add_argument("--elf", default="build/dso183.elf")
    ap.add_argument("--seqh", default="build/build_seq.h")
    args = ap.parse_args()

    for path in (args.doc, args.bin, args.seqh):
        if not os.path.exists(path):
            return fail("%s does not exist (build first)" % path)

    data = open(args.bin, "rb").read()
    doc = open(args.doc, "r", encoding="utf-8").read()

    size = len(data)
    md5 = hashlib.md5(data).hexdigest()
    sp = read_u32(data, 0)
    reset_raw = read_u32(data, 4)
    reset = reset_raw & ~1
    pages = (size + PAGE - 1) // PAGE
    last_page = 0x08000000 + (pages - 1) * PAGE

    m = re.search(r"STATUS_BUILD_SEQ\s+(\d+)", open(args.seqh, encoding="utf-8").read())
    if m is None:
        return fail("no STATUS_BUILD_SEQ in %s" % args.seqh)
    seq = int(m.group(1))

    problems = []

    def expect(label, actual, pattern, what):
        mm = re.search(pattern, doc)
        if mm is None:
            problems.append("%s: pattern %r not found in %s" % (label, pattern, args.doc))
            return
        got = mm.group(1)
        if got.lower() != actual.lower():
            problems.append("%s: doc says %s, artefact says %s" % (label, got, actual))
        else:
            print("  %-22s %s  (ok)" % (label, actual))

    expect("image size", str(size), r"\| image \|[^|]*\((\d+) bytes\)", "bytes")
    expect("build sequence", "0x%04X" % (seq & 0xFFFF),
           r"build sequence `(0x[0-9A-Fa-f]+)`", "sequence")
    expect("panel S=", "%04X" % (seq & 0xFFFF),
           r"MUST read S=([0-9A-Fa-f]+)", "panel text")
    if (reset_raw & 1) == 0:
        problems.append("reset vector 0x%08X has no thumb bit" % reset_raw)
    else:
        print("  %-22s 0x%08X  (ok, thumb)" % ("reset vector", reset))
    expect("reset vector", "0x%08X" % reset_raw,
           r"reset `(0x[0-9A-Fa-f]{8})`", "vector")
    expect("md5", md5, r"md5 ([0-9a-f]{32})", "checksum")
    expect("page count", str(pages), r"(\d+) pages", "pages")
    expect("target end", "0x%08X" % (0x08000000 + pages * PAGE - 1),
           r"target range \| 0x08000000 \.\. (0x[0-9A-Fa-f]{8})", "end address")

    if not (0x20000000 <= sp <= 0x20005000):
        problems.append("stack pointer 0x%08X is outside SRAM" % sp)
    else:
        print("  %-22s 0x%08X  (ok)" % ("stack pointer", sp))

    # The identity must be in the ELF, not only in build_seq.h: that is what
    # proves the .bin being flashed carries this sequence.
    try:
        out = subprocess.check_output(
            ["arm-none-eabi-nm", args.elf], text=True, stderr=subprocess.DEVNULL)
        m = re.search(r"^([0-9a-fA-F]+)\s+\w+\s+status_build_seq$", out, re.M)
        if m is None:
            problems.append("status_build_seq missing from %s" % args.elf)
        else:
            addr = int(m.group(1), 16)
            # .data has a RAM VMA and a flash LMA; the initialised value the
            # startup code copies is at the LMA, so read the section bytes and
            # index them by (symbol address - VMA).
            hdr = subprocess.check_output(
                ["arm-none-eabi-objdump", "-h", args.elf],
                text=True, stderr=subprocess.DEVNULL)
            sec = re.search(
                r"^\s*\d+\s+\.data\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)", hdr, re.M)
            if sec is None:
                problems.append("no .data section in %s" % args.elf)
            else:
                vma = int(sec.group(2), 16)
                tmp = os.path.join(os.path.dirname(args.bin) or ".", "_check_data.bin")
                subprocess.check_call(
                    ["arm-none-eabi-objcopy", "-O", "binary", "--only-section=.data",
                     args.elf, tmp], stderr=subprocess.DEVNULL)
                blob = open(tmp, "rb").read()
                os.unlink(tmp)
                off = addr - vma
                if off < 0 or off + 4 > len(blob):
                    problems.append("status_build_seq @0x%X outside .data" % addr)
                else:
                    got = int.from_bytes(blob[off:off + 4], "little")
                    if got != (seq & 0xFFFFFFFF):
                        problems.append(
                            "ELF status_build_seq is %d, build_seq.h says %d"
                            % (got, seq))
                    else:
                        print("  %-22s %d  (ok)" % ("ELF build sequence", seq))
    except (OSError, subprocess.CalledProcessError) as exc:
        problems.append("cannot inspect the ELF: %s" % exc)

    if problems:
        for p in problems:
            print("  MISMATCH: " + p)
        return fail("%d document/artefact mismatch(es)" % len(problems))

    print("OK: %s matches the built image" % args.doc)
    return 0


if __name__ == "__main__":
    sys.exit(main())
