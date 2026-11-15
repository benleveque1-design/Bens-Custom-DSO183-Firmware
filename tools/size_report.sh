#!/bin/sh
# Flash/RAM budget report for the DSO183 custom firmware.
#
# Budgets (STM32F103C8, 64 KiB flash / 20 KiB SRAM):
#   flash: 60 KiB application image budget (reserved/calibration regions live
#          outside the app image area per linker/layout).
#   RAM  : 20 KiB total. Static usage reported by `size` (data+bss) includes
#          linker-reserved heap+stack (._user_heap_stack: 1 KiB heap + 2 KiB
#          stack by default). Recommended development budget: warn above 18 KiB
#          (18432 B) to preserve ~2 KiB margin for stack depth/fragmentation and
#          future changes; hard fail above 20 KiB.
#
# usage: size_report.sh <path-to.elf>
set -e

ELF="${1:-build/dso183.elf}"
FLASH_MAX=$((60 * 1024))
RAM_MAX=$((20 * 1024))
# Recommended RAM warning budget (B4.37): 18 KiB = 90% of 20 KiB SRAM, leaving
# ~2 KiB margin beyond the reported static reservation for safety headroom.
RAM_WARN=${RAM_WARN:-18432}

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
if [ "$ram" -gt "$RAM_WARN" ]; then
    printf 'WARN: RAM above recommended budget %d B (%d B remaining to hard limit)\n' \
        "$RAM_WARN" "$((RAM_MAX - ram))" >&2
fi
echo "OK: within the flash and RAM budget"
