# DSO183 custom firmware

Replacement firmware for the JYE Tech DSO183 oscilloscope. Built from the verified
analysis in `docs/AUDIT_2026-11-05.md` (the stock-vs-custom audit) plus the
reverse-engineering corpus that fed it. The original vendor binary and the
Ghidra reverse-engineering workspace are NOT part of this repository (licensing
notes: see LICENSE and docs/LICENSING.md).

**Status (2026-11-07, source tree): UI-SPEC-001 production UI integrated; stock-parity
gaps addressed in software; host tests/guard/check pass.** The image in `build/` has
**not been flashed** by the agent (physical flash/bench validation remain the user's
responsibility).

## What this build provides

- **Acquisition/display core:** ADC1+DMA1 ring, immutable snapshot, hardware AWD event
  gated by `acq_cycle()`, AUTO/NORM/SINGLE policy, software edge refinement (`trig_scan`)
  with selectable rising/falling/auto slope, trigger-aligned averaging (1/2/4/8), and
  HOLD/recalled waveform display.
- **UI (DSO183-UI-SPEC-001, production):** persistent header (RUN/HOLD, TRIG mode/slope,
  trigger level, TRIG'D/WAIT/STOPPED, LINK, milestone), 300×192 dotted graticule, cyan
  differential trace, `1▶` ground and `◀T` trigger margin flags, AC/GND display handling,
  two-row footer (CH1 scale/coupling/TB/frequency; measurement bar or quick-focus guidance),
  settings card, and full-screen SYSTEM DIAG (**Board info / CFW: 113-18301-070** plus
  telemetry and 9-point zero matrix).
- **Controls:** four buttons PB12=OK, PB13=+, PB14=−, PB15=SEL (active low). Main screen:
  SEL cycles focus (TB→TRIG→LVL→SLOPE), +/− adjusts, OK toggles RUN/HOLD, SEL-hold opens
  menu. Menu: SEL next, +/− edit/select, OK act/capture, SEL-hold exits.
- **Measurements (software):** Vpp/Vrms/Vmax/Vmin, frequency/duty/cycle (midpoint+hysteresis
  detector, timebase-derived period), AV+/AV−, and measurement modes ALL/AV/CYCLE/ALL DISP.
  Values are trace-consistent (same counts-to-mV scale and 0 V reference); GND reports
  near-zero readings. Frequency/duty require a measurable periodic signal — **bench
  validation required**.
- **Calibration/persistence:** CRC records in reserved flash (`0x0800E800..0x0800FFFF`
  never erased by image writes). Two alternating settings slots, calibration record with
  base offset/gain and 9-entry per-sensitivity zero table, two waveform slots (Save WF/
  Load WF). **Clr Offset** clears base cal and zero table; **Defaults** restores defaults.
  CPL/SW1 is hardware-decoded (ch7) and reported; absolute volts/div scaling from the
  base cal pair is **not** applied to the scale label (needs measured reference).
- **PC link:** CRC-framed commands/replies and sample streaming over the console UART
  (115200). Host offline suites pass (`make remote-test`); live suites require hardware.

## Build identity (current `build/` artefacts)

| item | value |
|---|---|
| image | `build/dso183.bin` (35276 bytes) |
| pages | 35 × 1 KiB, `0x08000000..0x08008BFF` |
| build sequence | `0x531C` (`S=B227`), magic `0x4453424D` |
| reset | `0x08007A0D` (thumb), SP `0x20005000` |
| md5 | `f05061a8ab96654d3d5c31b818ea5e68` |
| flash | 35272 B / 61440 B (54%) of app area |
| RAM | 15064 B / 20480 B (73%) |

## Layout

```
app/         firmware (acq, trigger, measure, render/UI in main+ui, menu, link, stores)
drivers/     CMSIS + STM32F1 HAL + ILI9341
fonts/       5×7 glyph/font
linker/      58 KiB app + reserved userdata/settings (E800..FFFF)
tests/       host unit tests (native, no HAL)
tools/       flash (authorized, page-wise), guards, PC-link host tests
ui_design/   UI-SPEC-001 reference assets/spec (not build inputs)
docs/        audit/history
```

## Building / verification

```bash
make            # build image + size report
make test       # host unit tests
make guard      # static safety checks
make check      # build + tests + size + doc/artefact cross-check
make remote-test
```

All host tests, `make guard`, and `make check` pass for the current source tree and
artefacts. `make remote-live` requires the instrument on `/dev/ttyUSB0`.

## Flashing (user-controlled only)

**The agent never flashes.** User route:
`AUTHORIZED=1 python3 tools/flash/write_image.py --image build/dso183.bin`
(page-wise, verifies, preserves `0x0800E800..0x08010000`; never `st-flash write`).

## Known software limitations / hardware validation still required

- Absolute volts/div scaling from base calibration pair not applied to labels (deliberate).
- Frequency/duty on real signals, trigger stability/noise/weak-signal behaviour, button feel,
  CPL switch decode on hardware, display legibility, persistence/power-loss behaviour, and
  PC-link live operation require **bench validation**.
- RAM is 84% used (static). No framebuffer added; any further feature growth needs care.
- UI is a production adaptation of UI-SPEC-001 (14-item settings card, Display2 mode,
  added stock-parity items) rather than a pixel-identical clone of every mockup.
