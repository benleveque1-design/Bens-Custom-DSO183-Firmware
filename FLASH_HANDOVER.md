# Flash hand-over - DSO183 image (rebuilt 2026-11-11, regression fix + 50us sample-time + timebase cap 50s)

The user performs the flash.  Use the safe page-wise flasher (never
`st-flash write` on STM32F1). The current rebuilt image below has **not**
been flashed by the agent from this exact build state.

## 1. What is being flashed

| item | value |
|---|---|
| image | `build/dso183.bin` (37004 bytes), `build/dso183.hex`, `build/dso183.elf` |
| target range | 0x08000000 .. 0x080093FF (37 pages of 1 KiB) |
| calibration page | 0x0800F800..0x0800FBFF - written by the calibration menu only; 40-byte CRC record holding the base offset/gain pair AND the nine-entry per-sensitivity zero table |
| waveform slots | 0x0800E800..0x0800EBFF (slot 0) and 0x0800EC00..0x0800EFFF (slot 1) - persistent, CRC-protected, preserved across image flashes |
| settings slots | 0x0800F000..0x0800F3FF and 0x0800F400..0x0800F7FF - alternating, CRC-protected |
| device record | 0x0800FC00..0x0800FFFF - NEVER written |
| flash use | 60 % of the 58 KiB application area (37000 B) |
| RAM use | 81 % of 20 KiB (16664 B) |
| build identity | magic `0x4453424D`, build sequence `0xB71E` (46878) -> row 0 must read `B=4453424D S=B71E` |
| vector table | SP `0x20005000`, reset `0x08008115` (thumb bit set), both verified in the built .bin |
| MD5 | `5ec9cb7360ba88478139e2666962efd0` |

Current candidate (unflashed by the agent): md5 5ec9cb7360ba88478139e2666962efd0,
S=B71E (46878), reset `0x08008115`, 37004 bytes / 37 pages. Builds on S=63A1
(A11E regression fix + per-timebase ADC sample time so 50 µs/div can acquire
correctly with SMP 1.5) and **caps the timebase range at 50 s/div** (removes
100 s/200 s/500 s, which made synchronous capture/UI impractically slow at 10
div/window). Retains adaptive trace smoothing, AUTO wait/rearm responsiveness
tuning (60 ms / ≤20 ms), one-sided slope-aware trigger band, anchored nearest-
event refinement, IWDG feeds, fixed measurement cells, and incremental
settings-card painting. **Known limits:** 50 s/div still implies long
capture/fill waits by nature; 20 µs and faster remain unsupported (need
REPT/equivalent-time sampling); periodic wave may still jump horizontally
between equivalent edges. Bench verification required.

* **One-sided, slope-aware hardware trigger band** (`app/acq.c`, `app/acq.h`):
  the ADC analog watchdog is now armed as one edge at a time
  (rising → HTR = level + hyst, LTR = 0; falling → LTR = level − hyst,
  HTR = 4095; AUTO maps to rising for repeatable alignment) instead of the
  old two-sided `[level−hyst, level+hyst]` window.  The two-sided band fired
  on every conversion whenever the AC-coupled signal sat below the default
  level, so the latch was random-phase and `trig_refine` had no edge to find.
  Hysteresis is `ACQ_TRIGGER_HYST = 8`.  The slope is synced from the UI via
  `acq_set_trigger_slope()`.
* **Bounded rearm wait** (`app/acq.c`): before arming, acquisition waits
  (bounded) for the input to reach the waiting side of the band, so the
  first conversion after arming cannot re-latch a spurious crossing.  Slow
  timebases poll at 1 ms instead of spinning.
* **Anchored software edge refinement** (`app/main.c`, `app/trigger.c`,
  `app/trigger.h`): the edge search is centred on `gate->trigger_index`
  (±window) instead of a blind 512-sample backward scan, and
  `trig_scan_nearest()` selects the crossing closest to the hardware event
  so a multi-period window cannot lock onto an unrelated earlier edge.
  `gate->triggered == 0` yields no refinement.
* **AUTO re-centre** (`app/main.c`): AUTO moves the trigger level to the
  captured midpoint not only on timeout but whenever the level is outside the
  captured `[min,max]`, so a level beyond the signal can no longer leave the
  display at a random phase.
* **No temporal trace ghosting** (`app/main.c`): the `(3*old + new)/4` IIR
  trace smoothing is removed and the waveform is erased/drawn once per frame
  (not twice), eliminating the second/ghosted trace produced by frame-to-frame
  phase shifts.
* **Fixed measurement cells** (`app/main.c`): the footer measurement bar uses
  four fixed-width cells repainted only when a cell's value changes, so
  fields no longer jump sideways when a value changes width.
* **Incremental settings card** (`app/menu.c`): a full card paint happens only
  on menu entry; page moves repaint only the old/new selected rows and the
  RUN badge, and value edits repaint only changed rows, removing the grey
  flash on SEL navigation.
* **IWDG feeds during long waits** (`app/acq.c`): the watchdog is fed inside
  the bounded prime/pre-fill/rearm/trigger/post-fill waits and at stage
  boundaries, so stacked slow-timebase waits cannot exceed the ~10 s IWDG.

Host verification (this tree): `make -j4` clean (no app warnings), `make test`
all suites pass (including new `trig_scan_nearest` coverage), `make guard`
0 issues. `make check` matches this document to the built artefact.
**Unverified on hardware:** physical confirmation at the bench (stable edge at
1 ms/div, no ghost trace, fixed measurement fields, no menu gray flash, no
freeze) and remote bench suites require the instrument/SWD/UART and have not
been run by the agent in this session; flash only with explicit user
authorization.

Previously recorded bench results (2026-11-09, S=4F83 image): boot clean, zero
init/acquisition errors, `s_stream=0` at boot with the decode live
(`rng=0`=1V, `gain=1`=x2), span `424`, and the known 3.3 V / 1 kHz test signal
displayed as ~1.7 divisions at 2 V/div with `Vpp 3.24 V` on the footer.  The
last flashed image on the bench remains S=0782 (35,916 B, md5
`e9385ef04665e7a66bcb9d950caf11a6`) until the user explicitly authorizes a
flash of this candidate.

**J7/TSIG "1 kHz 3.3 V" header:** user-verified working with PA1 candidate.
Per JYE Tech `DSO183_HowTheCircuitsWork_en.pdf`, "Test signal output (TSIG)
... port J7" (distinct from SQR/J6 and waveform-generator outputs). This build
drives a fixed 1 kHz, 50% duty, 0–3.3 V push-pull square via TIM4 update IRQ
on **PA1 (TSIG/J7 candidate, user/schematic), PA11 (SQR/J6, documented),
PA15 (JTDI freed), PA8/PA12** (safe fallbacks). TIM3 remains
acquisition-owned. **PA7 is not driven**: it is the CPL slide-switch ladder
input (ADC_IN7), so TIM4 must never toggle it.

**Momentary buttons (DSO183, top to bottom OK, +, -, SEL):** per JYE Tech
"How The Circuits Work" and schematic, BTN1-BTN4 are on **PB12-PB15** (active
low, internal pull-ups). Mapping in this build: **PB12=OK, PB13=+, PB14=-,
PB15=SEL**. PB12-15 are inputs (LCD uses PB0-11/PB9 reset per driver and
PC13-15 control; PB12-15 not driven as LCD outputs). Main screen: SEL cycles
focus (TB/Trigger mode/Level), +/- adjusts, OK toggles RUN/HOLD, SEL hold
enters settings/cal menu. In menu: SEL cycles pages, +/- adjust, OK
action/capture (CAL ZERO), SEL hold closes.

**This build adds (UI-SPEC-001 production integration + stock parity):** a persistent header (RUN/HOLD, trigger mode/slope glyph, trigger level, TRIG'D/WAIT, LINK status) and a two-row footer (CH1 scale/coupling, TB, frequency, and measurement bar / quick-focus guidance) around a 300×192 dotted graticule with cyan differential trace, `1▶` ground and `◀T` trigger margin flags, and AC/GND display handling. Quick focus (SEL cycles TB→TRIG→LVL→SLOPE, +/- adjusts, 3 s timeout) and a settings card (timebase, trigger mode/level/slope, zero/offset/gain cal, wave save/load, measurement mode, cal reset, defaults, display mode, system diag) replace the old strip menu. Stock-parity functions: **Vrms/Freq/Duty/Cycle**, **AV+/AV-**, measurement modes (ALL/AV+/AV-/Cycle+Duty/All Disp.), **Save WF/Load WF** (two CRC slots at 0x0800E800/EC00), **Clr Offset** + **Default restored**, CPL/SW1 position reported, **Running/Stopped**, **Display1/Display2**, and user-reachable **Board info / CFW: 113-18301-070** plus telemetry/cal matrix in SYSTEM DIAG. Software edge refinement (`trig_scan`) with selectable slope gives stable trigger phase; AUTO/NORM/SINGLE/HOLD semantics preserved. Absolute volts/div scaling from the base calibration pair is still not applied to the scale label (hardware/reference-dependent).

**Not driven (preserved):** PA0/2/5/6/7 ladder/ADC (PA7 = CPL), PA3/PA4
analog, PA9/10 UART, PA13/14 SWD, PB0–PB11 LCD/control, PC13-15 LCD
control.

This build includes the 2026-11-03 audit fixes (64 MHz TIM3 timebase
table, UI geometry, ADON read-back, DMA start order, dead telemetry
removed) plus the flash-path / bring-up fixes v2: the single-page writer
now sets `DBGMCU_CR` bit 8 (`DBG_IWDG_STOP`) BEFORE halting the core, the
wrapper resets only after the FINAL page (never mid-image), `SystemInit`
is called from the startup code (VTOR + SystemCoreClock), and the IWDG is
no longer fed from SysTick (a lockup is now actually recovered by the
watchdog).  The firmware now includes the **2026-11-04 8-channel ADC switch mapping**: all 8 ADC channels (ch0-ch7) are read during acquisition pause, identifying gain (ch5), range (ch6), and coupling (ch7) channels with **verified hardware confirmation**.  The panel displays decoded state (e.g., "0.1V x2 Vpp 0.4d") and Vpp measurements are scaled with the current volts/division setting.

**PHASE 1 ACQUISITION SNAPSHOT (2026-11-04):** `acq_cycle()` now freezes
the acquisition before the caller reads it.  After the analog-watchdog
event (or the trigger timeout) it waits for the requested post-trigger
samples, stops TIM3 and DMA1 channel 1, copies the 1024-sample ring into
a private `s_snap` buffer, and only then restarts pacing.  `acq_samples()`
returns that immutable copy, so frame-building and measurements can no
longer race the DMA write pointer.  On a trigger timeout the caller takes
the newest `window` samples from the frozen ring (`frame_build_tail()`),
so AUTO displays fresh data instead of a stale first capture.  The
per-frame `acq_set_trigger_level(mean)` override in `main.c` was removed:
the configured trigger level is now respected.

**PHASE 2 TRIGGER MODES (2026-11-04):** AUTO, NORM and SINGLE are now
honoured by `acq_cycle()`.  AUTO takes triggered windows and falls back to
the newest samples after a timeout; NORM keeps the previous displayed
frame until a trigger arrives; SINGLE captures exactly one triggered
window and then parks (further `acq_cycle()` calls return immediately with
`gate.fresh == 0` until `acq_trigger_arm()` or re-selection).  The mode is
chosen by the front-panel menu (`acq_set_trigger_mode()`) and applied after
`acq_init()`; the AWD remains the hardware event source and the user
trigger level is never overridden.

**DIAGNOSTIC SCREEN UPGRADE:** Rows 7..9 now carry the 8-channel ADC evidence:
the decoded switch positions, sensitivity combination, and the new error
register.  The raw channel rows (5, 6) stay on screen next to them, so if
the mapping is wrong the moving channel is visible immediately.

## 2. Dry run (no hardware)

```bash
# run from the repository root
python3 tools/flash/write_image.py --image build/dso183.bin --dry-run
```

Expected: 36 pages listed, `0x08000000` .. `0x08008BFF`, storage pages not
touched, "dry run: nothing was written".

## 3. The real flash

```bash
# run from the repository root
AUTHORIZED=1 python3 tools/flash/write_image.py --image build/dso183.bin
```

The wrapper calls `tools/flash/write_page.py` once per page; each call:
sets `DBGMCU_CR` bit 8 (`DBG_IWDG_STOP`) so the ~8 ms v6 watchdog cannot
reset the target mid-erase, halts the core, erases one 1 KiB page,
programs it, reads it back and verifies it.  The wrapper stops at the
first failure and resets the target AFTER THE FINAL PAGE ONLY - never
mid-image, because page 0's reset vector lives in page 9 (a mid-image
reset would run erased flash and lock up).  `st-flash` is NOT used and
must not be used: on the STM32F1 path it mass-erases the chip, which would
destroy the vendor calibration data and the storage pages.

Check the output: 36 pages, each "written and verified", then
"all 36 page(s) written and verified".  Each page also prints
`IWDG frozen : DBGMCU_CR bit 8 set`; if that line instead reads
`could not freeze the IWDG`, stop - the target would reset mid-write.

Optional two-step form (leaves the core halted and the IWDG frozen, then
resets and releases the core only when you ask):

```bash
AUTHORIZED=1 python3 tools/flash/write_image.py --image build/dso183.bin --no-reset
AUTHORIZED=1 python3 tools/flash/reset_run.py
```

## 4. What the screen must show

```
B=4453424D S=B71E    build identity (magic + source checksum; MUST read S=B71E
                     for THIS image - the sequence is a checksum of app/*.c
                     and app/*.h, so any source edit changes it: rebuild
                     first and compare the ELF value with the panel)
BOOT 1              1 on a cold boot; CLIMBING = the IWDG is resetting the MCU
LOOP 000123 T 000456  both advance -> the core and SysTick are alive
ID 858552 M 68        controller ID + MADCTL read back over the bus
PA0-15 xxxx         raw GPIOA input register
A0-3 .... .... .... ....   raw ADC1 counts, channels 0..3
A4-7 .... .... .... ....   raw ADC1 counts, channels 4..7
SW s1 s2 cpl        decoded switch positions (0..2, 3 = no reading)
CMB n CPL k         sensitivity combination 4..12, coupling 0..2
VPP x.y d          scaled Vpp reading (with current mV/d label)
ERR 00              init flags; anything else = a failed init step
```

**Panel State Display:** The bottom bar now shows the decoded front-panel state:
`0.1V x2 Vpp 0.4d` (range × gain × scaled Vpp).  This shows the current switch
positions and the Vpp measurement in the current units.

Interpretation rules:

* `ID FFFF M 00` (or `ID 0000 M 00`) = the display read path failed.
* `BOOT` climbing = the firmware is resetting itself (watchdog) - capture
the numbers, do not touch the switches.
* `LOOP` advancing but `T` frozen = SysTick is dead.
* `T` advancing but `LOOP` frozen = the main loop is stuck.
* Flip a slide switch: the channel whose count changes is the ladder
  input (open question U-02a).  **Now verified: ch5 = gain, ch6 = range, ch7 = coupling**
  (from the 2026-11-04 gain sweep with 105 samples).
* The signal input is ADC channel 2 (PA2) - the vendor's regular group uses
  SQR3 = 2 and its watchdog watches AWDCH = 2.
* `A0-3`/`A4-7` showing 0xFFFF means the ADC conversion timed out.

**Physical switch order (user-verified 2026-11-09, top → bottom):**

| Switch | Top | Middle | Bottom |
|---|---|---|---|
| SW1 CPL | DC | AC | GND |
| SW2 SEN1 (range) | 10 mV | 0.1 V | 1 V |
| SW3 SEN2 (gain) | ×1 | ×2 | ×5 |

The raw ladder level is inverted relative to the physical order: a switch at
the **top** reads the **highest** raw code (level 2).  Hence the decode tables
are `SEN1: level 0→1V, 1→0.1V, 2→10mV`, `SEN2: level 0→x5, 1→x2, 2→x1`,
and `CPL: coupling = 2 − level` (level 2→DC, 1→AC, 0→GND).

**ADC Channel Mapping (verified 2026-11-04):**

| Channel | Function | From Gain Sweep |
|---------|----------|------------------|
| ch5     | Gain (SEN2) | ×1/×2/×5, multiplies the range |
| ch6     | Range (SEN1) | 10mV/0.1V/1V |
| ch7     | Coupling (CPL) | DC/AC/GND |

All 9 (range × gain) combinations, matching the vendor's own 9-entry label
table (`5V, 2V, 1V, 0.5V, 0.2V, 0.1V, 50mV, 20mV, 10mV`): the gain
**multiplies** the range.
- 1V ×1/×2/×5 → 1V / 2V / 5V per division (1000 / 2000 / 5000 mV/d)
- 0.1V ×1/×2/×5 → 100 / 200 / 500 mV/d
- 10mV ×1/×2/×5 → 10 / 20 / 50 mV/d

An earlier revision divided by the gain (1V, 500mV, 200mV) and clipped large
signals at x5; that was the bug corrected on 2026-11-09.

Interpretation rules:

* `ID FFFF M 00` (or `ID 0000 M 00`) = the display read path failed.
* `BOOT` climbing = the firmware is resetting itself (watchdog) - capture
  the numbers, do not touch the switches.
* `LOOP` advancing but `T` frozen = SysTick is dead.
* `T` advancing but `LOOP` frozen = the main loop is stuck.
* Flip a slide switch: the channel whose raw count changes is the ladder
  input (open question U-02a).  The vendor's map says SEN1 = channel 6
  (PA6), SEN2 = channel 5 (PA5), CPL = channel 7 (PA7) - decoded from the
  vendor's JSQR = 0x002394C0 (JL = 2 -> three injected conversions, ranks
  in JSQ2/JSQ3/JSQ4).  If a different channel moves, the `SW`/`CMB` rows
  are wrong and the map must be corrected.
* The signal input is ADC channel 2 (PA2) - the vendor's regular group uses
  SQR3 = 2 and its watchdog watches AWDCH = 2.
* `A0-3`/`A4-7` showing 0xFFFF means the ADC conversion timed out.

## 5. Objective verification (no eyes required)

* Screen photo: `tools/screen_report.py` reads the camera photos of the
  instrument screen (the ESP32-S3 camera films the DSO183).
* Firmware liveness over SWD: `tools/flash/live_check.py` reads the counters
  in `status_*` (heartbeat, main loops, boot count, display ID, ADC raw)
  over the probe.  It is strictly read-only: no writes, no halt, no reset.
  The build magic must be `0x4453424D`; the build sequence must match the
  `S=` value on the panel.
  `python3 tools/flash/live_check.py --symbols-only` resolves the addresses
  offline (no probe contact at all).

## 6. Recovery

If the screen stays blank or the device misbehaves, put the known-good vendor
image back.  It is a full 64 KiB dump, so the storage pages must be skipped -
the wrapper refuses the range otherwise:

```bash
# run from the repository root; supply your own known-good backup image
AUTHORIZED=1 python3 tools/flash/write_image.py \
  --image <path-to-known-good-backup.bin> \
  --page-count 60
```

`--page-count 60` covers 0x08000000..0x08008BFF and stops below reserved user data at 0x0800E800.  The last 6 KiB (0x0800E800..0x0800FFFF)
is reserved user data and is never part of a normal image flash (preserved across
firmware updates):

| range | owner |
|---|---|
| 0x0800E800..0x0800EBFF | waveform save slot 0 (CRC, 256 samples) |
| 0x0800EC00..0x0800EFFF | waveform save slot 1 (CRC, 256 samples) |
| 0x0800F000..0x0800F3FF | settings slot A (alternating, CRC record; v1 includes trigger slope) |
| 0x0800F400..0x0800F7FF | settings slot B (alternating, CRC record) |
| 0x0800F800..0x0800FBFF | calibration record (base pair + nine-entry zero table) |
| 0x0800FC00..0x0800FFFF | vendor device record - never written by this project |

## Note

While preparing the wrapper, one invocation was made with `AUTHORIZED=1`
before the tool was fully checked.  It failed in `probe.connect()` with
"STLink error (9): Get IDCODE error" - the target was not connected or
powered - so no page was erased, programmed or reset; the wrapper aborted on
page 1.  Since then the writer has only been exercised through `--dry-run`.

## Build-provenance note (added during the re-audit)

The build system previously had NO header dependency tracking: editing a
header (`app/ladder.h`, `app/acq.h`, ...) did not rebuild the objects that
include it, so the image could silently keep the OLD header values.  The
Makefile now uses `-MMD -MP` plus `-include $(wildcard build/*.d)`, and the
image was rebuilt from clean afterwards.  The shipped `build/dso183.bin`
therefore matches the audited sources - verified directly in the linked ELF
(the ladder channel map shows immediates 6 / 5 / 7 for SEN1 / SEN2 / CPL).
Rebuilding is deterministic: two clean builds produce byte-identical .bin
files (checksum recorded in §1; re-check with `md5sum`).  This is the 2026-11-05 calibration-table + remote-control image
on top of the audit remediation: flash page split (settings slots F000/F400,
calibration F800), CRC/version records, bidirectional trigger level,
responsive menu and timebase label, plus the nine-entry per-sensitivity zero
table stored in the calibration record (CAL ZERO menu page), a single
RUN/STREAM state module so the PC link and the menu cannot overwrite each
other, and SET_SPAN surviving until the front-panel switches move.  The PC
link now enforces each opcode's argument policy before dispatch, so the five
equivalent-time-only timebases, a trigger level above 4095, a span outside
16..4095 and a stream argument other than 0/1 are answered with BAD_ARG
instead of being clamped or silently truncated into a valid value; the
`alive` keepalive is suppressed while streaming so the sample stream stays
binary-clean.  The previous image (md5 `f05061a8ab96654d3d5c31b818ea5e68`,
S=B227, reset 0x08004F3D) was the calibration-table build; the link-hardening
change moved the source checksum, so only the S=B227 image matches the
current tree.
