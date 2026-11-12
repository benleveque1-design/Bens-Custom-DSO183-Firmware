#!/usr/bin/env python3
"""Turn the rendered diagnostic panel into a PNG preview.

The pixels are NOT drawn here: tools/panel_render.c renders them with the
same app/diag.c and fonts/glyph.c code the firmware uses, so this preview
cannot disagree with what the display driver actually paints.  (The first
version of this script re-implemented the font in Python and therefore
showed the intended picture while the driver produced garbage - that is
how the glyph scaling bug survived the audit.)
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def read_ppm(path):
    with open(path, "rb") as handle:
        data = handle.read()
    if not data.startswith(b"P6"):
        raise SystemExit("not a P6 PPM: %s" % path)
    fields = []
    pos = 2
    while len(fields) < 3:
        while pos < len(data) and data[pos : pos + 1].isspace():
            pos += 1
        if data[pos : pos + 1] == b"#":
            while pos < len(data) and data[pos : pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos : pos + 1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    pos += 1  # exactly one whitespace byte separates the header and the data
    width, height, maxval = fields
    if maxval != 255:
        raise SystemExit("unsupported maxval %d" % maxval)
    need = width * height * 3
    pixels = data[pos : pos + need]
    if len(pixels) != need:
        raise SystemExit("truncated PPM: %d of %d bytes" % (len(pixels), need))
    return width, height, pixels


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ppm", default=os.path.join(HERE, "build", "panel.ppm"))
    parser.add_argument("--out", default=os.path.join(HERE, "build", "diag_preview.png"))
    args = parser.parse_args()

    if not os.path.exists(args.ppm):
        raise SystemExit(
            "rendered panel not found: %s (run: make panel-render)" % args.ppm
        )
    width, height, pixels = read_ppm(args.ppm)

    try:
        from PIL import Image
    except ImportError:
        print("PIL is not available - keeping the PPM (%dx%d): %s"
              % (width, height, args.ppm))
        return 0

    image = Image.frombytes("RGB", (width, height), pixels)
    image.save(args.out)
    print("wrote %s (%dx%d)" % (args.out, width, height))
    return 0


if __name__ == "__main__":
    sys.exit(main())
