# Verified features, experimental features & known limitations

**DSO183 custom firmware — technical honesty statement**

This document breaks the firmware down into three categories so you can decide
what to trust:

1. **Verified** — observed working on the actual instrument (bench) or proven
   by the host test suite where the hardware path is identical.
2. **Experimental / host-tested only** — the logic is sound and tested, but
   has not been exercised on real hardware, or depends on an unconfirmed
   hardware assumption.
3. **Known limitations** — things that do not work, are only partly implemented,
   or are explicitly not claimed.

If it is not listed here as verified, assume it is unproven on hardware.

---

## 1. Verified — bench-verified on the instrument (single tested unit)

- **Boot and watchdog:** the image boots, the IWDG watchdog is armed and fed; boot/heartbeat/main-loop state advances; acquisition reports healthy status in validated runs.
- **Display:** ILI9341-compatible 320×240 panel works (controller readback 0x858552 on tested unit); graticule, trace, header/footer and SYSTEM DIAG operate as implemented.
- **Acquisition path:** ADC1 (PA2) → DMA1 channel 1 → 1024-sample circular buffer, TIM3-paced; immutable snapshot frozen for frame building/measurement.
- **Front-panel ladder:** gain/range/coupling slide switches decode from ADC ladder channels; sensitivity combinations and panel labels follow switch position. Coupling is analogue-only (not MCU-observable).
- **PC link (UART 115200 8N1):** CRC-framed commands/replies, status/value queries, streaming, and coherent diagnostic reads (including GETV latch protocol) verified in use.
- **Real-time trigger/alignment (B4.36, build 41320, 2026-11-15):** 12 configs × 50 captures (tb5/tb6/tb9, AUTO/NORM, rise/fall, 1 kHz 50% square, level 970) — **600/600 coherent, 0 no-frame, 0 diag errors, 0 rejected frames**. Requested edge within ±2 of index 96 in **all 600 captures**. **tb5 (50 µs/div) rising:** 50/50 AUTO and 50/50 NORM at 96 (previously 0/100 on 48462). Diagnostics showed `refined_found=1`, `fallback=0`, `anchor_kind=0` (local) throughout this run.
- **Host test suite:** `make test` passes (36 suites, 1,318 checks on the B4.36 tree), including anchoring/window/trigger/ETS/diag/protocol coverage.
- **Flash layout/safety:** reserved pages (settings, calibration, waveform slots, vendor device record) are separate from the application image; page-wise, read-back-verified flashing used (page 0 last). Recovery image for 14139 preserved.

## 2. Verified — host-tested only (logic proven, hardware path not fully exercised from this tree)

- **Trigger/anchoring policy:** span-bounded edge anchoring (`app/acq_anchor.c`), local pass parity, recovery pass for late latches, effective-span validation, and rejection/publish policy are host-tested (including the 129–131-sample late-latch geometries). Recovery was not exercised in the 41320 hardware campaign.
- **ETS accumulation/phase/mapping logic:** epoch/source/phase/diagnostic modules and integration logic are host-tested. Hardware behaviour is range-, waveform-, and campaign-dependent (see below).
- **Timebase table/maths:** 24-step table, tick/sampling relationships and policies host-tested.
- **Measurements/mapping/calibration maths/link codec:** core pure-logic modules host-tested; not all user-facing measurement/product-parity features are complete.
- **Settings/calibration/wave storage logic:** CRC/version/sequence/alternating-slot behaviour host-tested.

## 3. Experimental / partially verified

- **ETS / TB0–TB4 (1–20 µs/div):** implemented as equivalent-time (REPT) ranges. **Not re-campaigned on 41320.** B4.32 hardware validation (build 12066, B2 Amendment A + B-REC) found: TB4 fast-undersampled square met coverage/ready in captured coherent evidence (fill 128, ready); TB2/TB3 did not reach the 80-bin gate in captured windows; TB0/TB1 fast aperture-unresolved square were coverage-limited (80-bin metric reported, no PASS/FAIL per B-REC); resolved sine/triangle (AC-2) not tested. Treat ETS as range/waveform dependent, not blanket-proven.
- **Vpp/readout scaling:** Vpp is shown; absolute volts/div scaling from the base calibration pair is not yet applied to the trace/label (needs a measured reference). RMS/frequency/duty maths exist but not all are fully surfaced as product features.
- **AC coupling:** CPL switch is analogue-only (not observable by the MCU); software cannot confirm or fully implement coupling selection in hardware terms.
- **Trigger level UX:** adjustable 0–4095 and bidirectional; defaults/usability depend on signal level (AUTO recentring helps, but behaviour is signal-dependent).
- **Single-instrument evidence:** all bench results are from one tested unit (STM32F103 rev A, display ID 0x858552). Other revisions/clones/panels are untested.

## 4. Known limitations (do not claim these)

| Limitation | Detail |
|------------|--------|
| Product parity incomplete | RMS/frequency/duty/AV+/AV-/Cycle/All Disp./Save WF/Load WF and some stock menu actions are only partially implemented or not fully surfaced |
| TB0–TB4 are ETS-only | 1–20 µs/div require equivalent-time sampling; not real-time single-shot, and hardware-proven performance is range/waveform limited per prior evidence |
| Calibration not fully applied | Per-sensitivity/base calibration stored but absolute volts/div scaling not fully applied to trace/readouts |
| Coupling not observable | AC/DC/GND is analogue-only; firmware cannot read or confirm it |
| RAM pressure | ~91% of 20 KiB used (41320); little headroom for large features without reclamation |
| Host-test gaps | HAL-level modules and some hardware-integrated paths (`acq.c` engine/HAL, `menu.c`, display/UI integration) lack host tests |
| No USB data | DSO183 USB is power-only; PC link is UART |
| Single instrument tested | Compatibility with other DSO183 revisions/clones/display panels not tested or claimed |

## 5. Not in scope (deliberately out of this firmware)

- Vendor TIM4_CCR1 trigger-position mechanism (buffer write position/anchoring is used instead)
- Any dependency on the ATSHA204A secure element (removed by design)
- `st-flash write` / mass-erase flashing (prohibited — destroys reserved data)
- Claims of universal hardware compatibility or blanket ETS correctness beyond evidenced cases
