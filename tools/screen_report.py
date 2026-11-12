#!/usr/bin/env python3
"""Summarise a camera frame: locate the instrument screen and report colours.

The DSO183 is filmed by an ESP32-S3 camera; we need a quick, numeric read of
what is on the instrument's own display.

usage: screen_report.py <image.jpg>
"""
import sys
from collections import Counter

from PIL import Image


def classify(r, g, b):
    if r > 110 and g < 95 and b < 95:
        return "R"
    if g > 110 and r < 95 and b < 95:
        return "G"
    if b > 110 and r < 95 and g < 105:
        return "B"
    if r > 110 and g > 110 and b > 110:
        return "W"
    if (r + g + b) / 3 > 70:
        return "-"
    return " "


def main():
    path = sys.argv[1]
    im = Image.open(path).convert("RGB")
    W, H = im.size
    print("image %s  %dx%d" % (path, W, H))

    px = im.load()
    census = Counter()
    for y in range(0, H, 3):
        for x in range(0, W, 3):
            census[classify(*px[x, y])] += 1
    total = sum(census.values())
    print("census (fraction of frame):")
    for k, v in census.most_common():
        print("   %s  %6.2f%%" % (k, 100.0 * v / total))

    # Find bright, saturated (non-grey) regions: the screen content.
    best = None
    step = 8
    for y in range(0, H - 40, step):
        for x in range(0, W - 40, step):
            hits = 0
            for j in range(0, 40, 8):
                for i in range(0, 40, 8):
                    if classify(*px[x + i, y + j]) in "RGB":
                        hits += 1
            if hits >= 12 and (best is None or hits > best[0]):
                best = (hits, x, y)
    if best:
        print("densest colour block: %d/25 cells at (%d,%d)" % best)
    else:
        print("densest colour block: none found (no R/G/B area in frame)")


if __name__ == "__main__":
    main()
