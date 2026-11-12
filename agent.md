# DSO183 Custom Firmware Project — Agent Instructions

## Project Overview
This project contains the custom firmware under development for the JYE Tech DSO183 (18301K) oscilloscope. The firmware is a working scope core (acquisition, snapshot, trigger modes, display, front-panel menu, PC link), host-tested and partly bench-verified; it is not yet a product. The current state, defects and remaining work are in `docs/AUDIT_2026-11-05.md`. Critical BUG-001 fixes (removing the status-byte gate, always-arming the IWDG) are baked into the firmware design rules.

---

## 📷 Camera — Tunable Snapshot Interface
A live ESP32-CAM snapshot interface is available on the **local bench LAN** at:

> **http://<bench-camera-host>/**

This is a **read-only** camera stream for reference while working on the firmware. Do **not** attempt to control it, issue POST requests, or modify its configuration. Use it only as a visual reference for signal timing, wave shapes, or display behaviour if needed.

**Safety rule:** The camera interface is on a separate network segment. Do not connect the DSO183 development board to the same network interface as the camera during firmware flashing or bring-up. Keep the ST-Link host and the DSO183 board isolated unless the flashing procedure explicitly requires network access.

---

## ✋ Strict Guard Rails — Never Brick the DSO183
**We have one instrument. Do not risk bricking it.**

The following rules are **mandatory**. Violation of these rules will result in the agent being stopped and the project being reverted to the last known-good state.

### 1. Flashing Policy
- **Never use `st-flash write`** to program the DSO183. On the STM32F1 path, `st-flash` performs a **mass erase** of the chip, which will destroy the reserved user data at `0x0800E800..0x08010000` (wave slots, settings slots, calibration, vendor record), and render the instrument unusable without an external programmer.
- **Always use `make flash-dry` first** to verify the build output before any device programming.
- **Real write path** (only after `AUTHORIZED=1` is set): `tools/flash/write_page.py` (pyocd, page-wise, read-back verified). Do not proceed without explicit authorization.
- **Keep the v6 recovery image** at `artifacts/patch/dso183_patched_full_v6.bin` as a known-good backup. The restore procedure is `../RESTORE.md`.
- **Never flash without a verified backup.** Before any flash operation, confirm the current device image SHA-256 matches the expected v6 image or is blank/erased.

### 2. JTAG/SWD Pin Protection
- The vendor firmware has JTAG **disabled** with `AFIO->MAPR = 0x02000000u` to free PB3/PB4/PA15 for the display data bus PB0..PB7.
- **Never change the MAPR value** unless you have verified on hardware that JTAG is not needed for debug. If PB3/PB4 are stolen by JTAG, two bits of every byte sent to the display will be corrupted, causing a permanent white screen or garbled colours.
- The custom firmware `board_free_jtag_pins()` writes exactly this value. Do not modify it without a hardware verification step.

### 3. Power-On / Reset Safety
- **Always connect the ST-Link first, then apply power** to the DSO183. Do not hot-swap the ST-Link while the instrument is powered.
- After a watchdog reset, the core will be halted. A queued reset is required before the core runs again. Do not assume the instrument will reset cleanly — verify the splash screen appears.
- If the splash screen does **not** appear after a reset, **stop immediately** and seek guidance. A hung core may indicate a firmware bug that could lead to a permanent freeze.

### 4. No Unbounded Waits
- The custom firmware **must not contain any unbounded waits** in the superloop. Every delay must have a deadline. If a wait exceeds its deadline, the IWDG should trigger a reset rather than spinning forever.
- The unbounded auto-zero wait in `0x08003714` (vendor) is **not** present in the custom firmware. If calibration logic is added, it must use timed polls or event-based completion.

### 5. Status Byte Gate — Absolute Prohibition
- **The byte at `0x20000346` (ATSHA204A slot 4 byte 8) must never gate control flow.** The vendor firmware uses `0x20000346 == 'G'` to decide whether the instrument keeps running; on this unit the byte is `'F'` and never changes, which is the root cause of BUG-001.
- The custom firmware **must not replicate this guard**. If calibration or state machine logic requires a status check, use a different mechanism (e.g., a flag in RAM that is explicitly set/cleared by the acquisition task).

### 6. ATSHA204A Dependency
- **No runtime dependency on the ATSHA204A** for core functionality. The custom firmware reads calibration data from the local flash page at `0x0800F800` at boot and never requeries the secure element during operation.
- If the ATSHA204A is absent or fails to respond, the firmware must start in a safe default state (e.g., sensitivity combo 12, coupling 0, timebase 19) and display a warning on the screen, not freeze.

### 7. Page-Aligned Flash Writes
- The flash storage region is the last 4 KiB (`0x0800F000`–`0x0800FFFF`), split into four 1 KiB pages: `F000` and `F400` are the alternating settings slots, `F800`–`FBFF` calibration, `FC00`–`FFFF` the vendor device record (never written). The linker script `linker/STM32F103C8_64k_20k.ld` must reserve this region so no code or rodata lands in it.
- **Fixed (2026-11-05):** the settings record now lives in two alternating pages (`0x0800F000`/`0x0800F400`) with a CRC and sequence number, separate from the calibration page (`0x0800F800`) and the vendor device record (`0x0800FC00`, never written). See `docs/AUDIT_2026-11-05.md`.
- Flash writes to the calibration page must be page-aligned, journaled, and read-back verified.

### 8. Option Bytes
- The vendor firmware has option bytes `USER = 0x5A`, `WDG_SW = 1` (IWDG can only be started by software). The vendor firmware configures the IWDG but **never starts it**, which is why a freeze on a stock unit is permanent.
- **Never write option bytes** on the DSO183 unless you have a hardware recovery path (external programmer, backup chip). Writing `WDG_SW = 0` would allow the hardware watchdog to auto-reset, but it could also mask bugs that should be fixed in firmware.

### 9. Superloop Feed
- The IWDG is **always armed and fed every superloop iteration**. If the superloop stops feeding the watchdog, a reset will occur after the IWDG timeout. This is intentional and safe — it is the only mechanism that can recover from a hang.
- Do not modify the IWDG feed pattern without understanding the timeout implications. The default timeout is configured via `board_iwdg_init(timeout_ms)` in `app/board_iwdg.c`.

### 10. Build & Test Before Hardware
- Always run `make test` (host unit tests) and `make size` (flash/RAM budget report) **before** any hardware bring-up.
- Run all host tests (`make test`), `make guard`, and `make check`. Do not skip this step.
- If the size report exceeds `flash 61440 B / 61440 B` or `RAM 20480 B / 20480 B`, the build must be revised before flashing.

---

## 🛡️ Escalation & Recovery
If any of the above guard rails are violated or the instrument shows abnormal behaviour (no splash screen, garbled display, frozen UI):

1. **Stop immediately.** Do not attempt further flash or reset operations.
2. **Power cycle the instrument.** Observe whether the splash screen appears.
3. **Restore the v6 recovery image** if the device is unresponsive — through the
   safe page-wise path only (never `st-flash erase`, which mass-erases the
   settings page). `tools/flash/write_page.py` (pyocd) erases exactly one 1 KiB
   page per invocation, programs it, verifies the read-back and re-locks the
   flash:
   ```bash
   AUTHORIZED=1 python tools/flash/write_page.py \
       --image <v6_page1k.bin> --page 0x08000NNN
   ```
   The device record page at `0x0800FC00` is never written, so the device record survives.
4. **Notify the project lead** with the current SHA-256 of the device flash image and a description of the symptom.

---

## 📁 Project Structure
```
dso183-firmware-project/
├── agent.md                  ← This file
├── Makefile                  portable GNU Make build
├── linker/                   64 KiB flash / 20 KiB RAM; last 4 KiB page reserved
├── startup/                 vector table + reset handler
├── drivers/
│   ├── CMSIS/              vendored CMSIS core + STM32F1xx device headers
│   └── STM32F1xx_HAL_Driver/ vendored STM32Cube HAL (from DSO-150)
├── app/
│   ├── main.c               superloop, scope screen, acquisition/display/menu wiring
│   ├── board.h              verified pin map
│   ├── board_iwdg.c/.h      minimal watchdog driver
│   ├── board_uart.c         console UART instance
│   └── ladder.c/.h          front-panel ladder decoding (unit tested)
├── tests/                   host unit tests (no HAL, native compiler)
└── tools/
    ├── size_report.sh     flash/RAM budget gate
    ├── remote_run.py      PC-link host test system (make remote-test / remote-live)
    ├── remote_proto.py    PC-link wire codec + offline self-checks
    ├── remote_client.py   PC-link serial transport + client + frame reader
    └── remote_tests.py    PC-link suites (codec/control/serial/stream)
```

---

## 📬 Contact & Authorization
- **Project lead** must grant `AUTHORIZED=1` before any device programming proceeds.
- The known-good images in `artifacts/patch/` (vendor `74ff77c3…`, v6 `eeca3214…`) are the immutable references.
- The restore procedure is documented in `../RESTORE.md`.

**Remember: We have one instrument. Protect it as if it were the only one.**