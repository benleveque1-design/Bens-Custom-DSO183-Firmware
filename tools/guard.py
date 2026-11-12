#!/usr/bin/env python3
"""Static guard rails for the DSO183 tree - fast, no hardware."""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
issues = []


def scan(files, rx, why, skip=()):
    for p in files:
        if p.name in skip:
            continue
        for n, line in enumerate(p.read_text(errors="ignore").splitlines(), 1):
            if re.search(rx, line):
                issues.append("%s:%d  %s" % (p.relative_to(root), n, why))


scan(list(root.glob("app/*.c")) + list(root.glob("drivers/**/*.c")),
     r"0x0800F800|0x0800FC00", "settings page address in code")
scan(list(root.glob("tools/**/*.py")),
     r"st-flash\s+(write|erase)|--mass-erase", "forbidden flash tool",
     skip=("guard.py",))
scan([p for p in root.glob("app/*.c") if p.name != "main.c"],
     r"while\s*\(\s*1\s*\)|for\s*\(\s*;\s*;\s*\)", "unbounded loop")
scan(list(root.glob("drivers/display/*.c")),
     r"GPIOB->ODR\s*=\s*v\b", "display write must preserve GPIOB upper byte")
scan([root / "app/main.c"], r"0x0800F800|0x0800FC00", "settings page address in code")

for i in issues:
    print("GUARD:", i)
print("guard: %d issue(s)" % len(issues))
sys.exit(1 if issues else 0)
