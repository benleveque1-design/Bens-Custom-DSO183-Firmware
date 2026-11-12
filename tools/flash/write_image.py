#!/usr/bin/env python3
"""Flash a firmware image PAGE BY PAGE through the approved writer.

This is a thin, safety-checked wrapper around tools/flash/write_page.py - the
only write path this project uses.  It does NOT do a mass erase and it does
NOT touch the option bytes; every page is erased and programmed individually
and read back for verification by write_page.py.

Why this wrapper exists: write_page.py deliberately handles ONE page per
invocation (that is what makes a failed flash survivable), but our image is
~12 KiB, so the page loop would otherwise be typed by hand - and a typo in a
page address is exactly the kind of mistake that destroys a working device.

Guard rails enforced here (agent.md):
  * AUTHORIZED=1 must be set in the environment, otherwise nothing is written;
  * the storage pages 0x0800F000..0x0800FFFF are NEVER written by an image:
    two settings slots at 0x0800F000/0x0800F400, the calibration page at
    0x0800F800 (only with --cal-store) and the vendor device record at
    0x0800FC00 (never, under any flag) - an image that would touch them is
    rejected outright;
  * the image is padded with 0xFF, never with zeros, so unwritten tail bytes
    read back as erased flash;
  * the loop stops at the first failure (the tool verifies every page).

Usage:
    AUTHORIZED=1 python3 tools/flash/write_image.py --image build/dso183.bin
    AUTHORIZED=1 python3 tools/flash/write_image.py --image build/dso183.bin \
        --base 0x08000000 --no-reset
    AUTHORIZED=1 python3 tools/flash/reset_run.py        # after --no-reset

The target is reset only after the FINAL page.  v6 runs an ~8 ms IWDG and its
patch image's reset vector lives in a later page, so a reset after page 0 would
run erased flash and lock the core up; the writer freezes the IWDG first (see
write_page.py) and keeps the core halted between pages.

Recovery (put a known-good image back, app area only).  The backup image is
NOT part of this repository - supply your own path:
    AUTHORIZED=1 python3 tools/flash/write_image.py \
        --image <path-to-known-good-image.bin> \
        --base 0x08000000 --page-count 60
"""

import argparse
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
WRITE_PAGE = os.path.join(HERE, "write_page.py")

PAGE_SIZE = 1024
# Reserved user-data region (preserved across normal image flashes):
# waveform slots E800/EFFF, settings A/B F000/F400, calibration F800,
# vendor device record FC00 (NEVER written).
USERDATA_BASE = 0x0800E800
STORAGE_END = 0x08010000
CAL_STORE_PAGE = 0x0800F800   # reserved page for calibration persistence
DEVICE_PAGE = 0x0800FC00      # vendor device record - never written
STORAGE_BASE = USERDATA_BASE   # back-compat for messages/checks below


def fail(msg):
    print("ERROR: %s" % msg, file=sys.stderr)
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser(description="page-wise image writer (write_page.py wrapper)")
    ap.add_argument("--image", required=True, help="binary image to write")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=0x08000000,
                    help="flash address of the first byte (default 0x08000000)")
    ap.add_argument("--page-size", type=lambda s: int(s, 0), default=PAGE_SIZE,
                    help="page size in bytes (default 1024)")
    ap.add_argument("--no-reset", action="store_true",
                    help="do not reset the target when finished")
    ap.add_argument("--dry-run", action="store_true",
                    help="list the pages that would be written; touch no hardware")
    ap.add_argument("--from-page", type=lambda s: int(s, 0), default=0,
                    help="first page index of the image to write (default 0)")
    ap.add_argument("--cal-store", action="store_true",
                    help="allow writing ONLY the calibration page (0x0800F800)")
    ap.add_argument("--page-count", type=lambda s: int(s, 0), default=0,
                    help="number of pages to write (default: all remaining pages)")
    args = ap.parse_args()

    if os.environ.get("AUTHORIZED") != "1" and not args.dry_run:
        fail("refusing to write - set AUTHORIZED=1 to confirm an explicit instruction")

    if not os.path.isfile(args.image):
        fail("image not found: %s" % args.image)
    with open(args.image, "rb") as fh:
        data = fh.read()
    if len(data) == 0:
        fail("image is empty")

    if args.page_size <= 0 or (args.page_size & (args.page_size - 1)) != 0:
        fail("page size must be a power of two")

    total_pages = (len(data) + args.page_size - 1) // args.page_size
    first = args.from_page
    if first < 0 or first >= total_pages:
        fail("--from-page %d is outside the image (%d pages)" % (first, total_pages))
    count = args.page_count if args.page_count > 0 else (total_pages - first)
    if first + count > total_pages:
        fail("--page-count %d runs past the end of the image (%d pages)"
             % (count, total_pages))
    last = first + count

    lo_addr = args.base + first * args.page_size
    hi_addr = args.base + last * args.page_size
    # USERDATA_BASE..STORAGE_END is reserved (waveform, settings, cal, vendor).
    # The calibration page may be written explicitly with --cal-store; nothing
    # else in this region is touched by a normal image flash.
    if hi_addr > USERDATA_BASE and lo_addr < STORAGE_END:
        allowed = (args.cal_store
                   and lo_addr == CAL_STORE_PAGE
                   and hi_addr <= (CAL_STORE_PAGE + PAGE_SIZE))
        if not allowed:
            fail("page range 0x%08X..0x%08X overlaps reserved user data "
                 "(0x%08X..0x%08X: wave E800/EFFF, settings F000/F400, cal F800, "
                 "vendor FC00 never written) - refused%s. App must fit below 0x%08X."
                 % (lo_addr, hi_addr, USERDATA_BASE, STORAGE_END,
                    "" if args.cal_store else " (only the calibration page can "
                    "be written, with --cal-store)", USERDATA_BASE))
    if lo_addr >= DEVICE_PAGE:
        fail("refusing to write the vendor device record at 0x%08X" % DEVICE_PAGE)
    if lo_addr == CAL_STORE_PAGE and not args.cal_store:
        fail("refusing to write calibration page 0x%08X: add --cal-store for explicit calibration persistence" % CAL_STORE_PAGE)

    pages = count
    print("image   : %s (%d bytes, %d pages)" % (args.image, len(data), total_pages))
    print("target  : 0x%08X, pages %d..%d (%d page(s) of %d bytes)"
          % (lo_addr, first, last - 1, pages, args.page_size))
    if lo_addr == CAL_STORE_PAGE:
        print("NOTE    : calibration page 0x%08X WILL be written (--cal-store)" % CAL_STORE_PAGE)
    else:
        print("NOTE    : reserved user data 0x%08X..0x%08X is not touched (wave/settings/cal/vendor preserved)"
              % (USERDATA_BASE, STORAGE_END))
    if not args.no_reset:
        print("NOTE    : reset happens after the FINAL page only (never mid-image)")

    if args.dry_run:
        for i in range(first, last):
            addr = args.base + i * args.page_size
            print("would write page %2d/%d at 0x%08X" % (i + 1, total_pages, addr))
        print("dry run: nothing was written")
        return

    env = dict(os.environ)
    env["AUTHORIZED"] = "1"
    tmpdir = tempfile.mkdtemp(prefix="dso183_pages_")
    try:
        for i in range(pages):
            # first + i, NOT i: --from-page/--page-count are used to resume or
            # to restore a known-good image, and the old slicing wrote page 0
            # content into the resumed region.
            page_index = first + i
            chunk = data[page_index * args.page_size:
                         (page_index + 1) * args.page_size]
            chunk = chunk + b"\xff" * (args.page_size - len(chunk))   # 0xFF = erased
            page_file = os.path.join(tmpdir, "p%02d.bin" % i)
            with open(page_file, "wb") as fh:
                fh.write(chunk)
            addr = args.base + page_index * args.page_size
            cmd = [sys.executable, WRITE_PAGE, "--image", page_file,
                   "--page", "0x%08X" % addr]
            # Reset ONLY after the final page.  Page 0 is written first and its
            # vector table points at a reset handler in a later page; a reset
            # after page 0 would run erased flash and lock the core up.
            if args.no_reset or (i != (pages - 1)):
                cmd.append("--no-reset")
            print("--- page %d/%d at 0x%08X ---" % (i + 1, pages, addr))
            rc = subprocess.call(cmd, env=env)
            if rc != 0:
                fail("page at 0x%08X failed (rc=%d) - stopping, nothing further written"
                     % (addr, rc))
    finally:
        for name in os.listdir(tmpdir):
            os.unlink(os.path.join(tmpdir, name))
        os.rmdir(tmpdir)

    print("all %d page(s) written and verified" % pages)


if __name__ == "__main__":
    main()
