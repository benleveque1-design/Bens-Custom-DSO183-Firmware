#!/bin/sh
# Flash/RAM budget report for the DSO183 custom firmware.
#
# Budgets (from CUSTOM_FIRMWARE_PLAN.md section 6):
#   flash: 60 KiB of code+rodata+data (the last 4 KiB page is reserved for the
#           calibration store) out of 64 KiB
#   RAM  : 20 KiB total (F103C8)
#
# usage: size_report.sh <path-to.elf>
set -e

ELF="${1:-build/dso183.elf}"
FLASH_MAX=$((60 * 1024))
RAM_MAX=$((20 * 1024))

if [ ! -f "$ELF" ]; then
    echo "size_report: $ELF not found - run make first" >&2
    exit 1
fi

read -r text data bss dec hex filename <<EOF
$(arm-none-eabi-size "$ELF" | tail -1)
EOF

flash=$((text + data))
ram=$((data + bss))

printf 'flash : %6d B / %6d B  (%d%%)   [text %d + data %d]\n' \
       "$flash" "$FLASH_MAX" $((flash * 100 / FLASH_MAX)) "$text" "$data"
printf 'ram   : %6d B / %6d B  (%d%%)   [data %d + bss %d]\n' \
       "$ram" "$RAM_MAX" $((ram * 100 / RAM_MAX)) "$data" "$bss"

if [ "$flash" -gt "$FLASH_MAX" ]; then
    echo "FAIL: flash budget exceeded" >&2
    exit 1
fi
if [ "$ram" -gt "$RAM_MAX" ]; then
    echo "FAIL: RAM budget exceeded" >&2
    exit 1
fi
echo "OK: within the flash and RAM budget"
