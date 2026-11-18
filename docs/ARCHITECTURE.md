# Architecture

**DSO183 custom firmware — design and data paths**

## Overview

The firmware is a **modular, host-testable** design. Pure-logic modules
(acquisition helpers, trigger/anchoring, ETS logic, timebase, measurements,
waveform/mapping, calibration, PC-link framing, storage helpers, run state)
contain no HAL dependency where possible and are exercised by host tests.
Hardware glue lives in `main()` and board/support files (`app/acq.c` engine/HAL
integration, `board_*.c`, `stm32f1xx_it.c`, display driver).

Design rules:

- **No unbounded waits.** Busy waits are bounded/deadline-based (IWDG recovers hangs).
- **No status-byte gates, no ATSHA204A dependency.** Calibration/settings live in local flash; safe defaults on missing/invalid data.
- **Watchdog always armed and fed** in bounded loops/iterations as designed.
- **No shared "scratch slot" aliasing.** Flash records use separate pages/slots with CRC/version/sequence.

## MCU and clocking

**STM32F103C8T6** (Cortex-M3, 64 KiB flash, 20 KiB SRAM, no FPU), HSI-based clocking:

- `SYSCLK = 64 MHz`, `APB1 = 32 MHz`, `APB2 = 64 MHz`
- TIM3 timer clock 64 MHz (15.625 ns/count) on APB1 prescaled config
- ADC clock = PCLK2 / 6 = 10.666 MHz (14-cycle conversion at fastest sample time)

## Acquisition pipeline

```
TIM3 (TRGO on update)  →  ADC1 regular conversion (ch2 / PA2)
                           →  DMA1 channel 1  →  1024-sample circular buffer (RAM)
                                           →  acq_cycle() freeze/snapshot on event
                                           →  frame placement / ETS accumulation
                                           →  measure()/display/stream/store
```

- **ADC1 channel 2 (PA2)** is the signal and analog-watchdog channel.
- **DMA1 channel 1** runs circular into a 1024-sample ring.
- **TIM3 TRGO** paces conversions.
- `acq_cycle()` arms/waits (bounded), captures post-fill, freezes, snapshots, and restarts; the frozen snapshot is immutable for consumers.
- ADC ownership is arbitrated for ladder reads/engine use as required by the board.

## Trigger and anchoring (B4.36)

- **Hardware event:** ADC1 analog watchdog (AWD), single channel, one-sided band with hysteresis (`ACQ_TRIGGER_HYST = 8`), slope-selective by banding.
- **Re-arm:** bounded wait for rearm-side condition before enabling AWDIE to avoid immediate re-latch on a standing out-of-window signal.
- **ISR latch:** one-shot AWD handling latches the ring index/count context for the capture.
- **Span-bounded edge anchoring (`app/acq_anchor.c`, `app/trigger.c`, `app/acq_window.c`):**
  - Local pass around the hardware latch (historic window/parity for healthy latches), nearest requested-slope edge.
  - Recovery pass only when local fails: searches behind the latch within a span-bounded limit derived from the measured post-fill span (disabled on ETS ranges), taking the nearest hysteresis-qualified edge.
  - Effective span is validated (`acq_win_span_ok`) before publication. Unpublishable/rejected captures are not presented as aligned frames (policy differs by mode: NORM hold, SINGLE re-arm, AUTO tail refresh).
  - This fixes the B4.36 failure mode where a late AWD latch placed the true edge outside the local window and the raw mid-plateau index was published.
- **Policy FSM:** AUTO (timeout/fallback behaviour), NORM (hold until trigger), SINGLE (one capture then park/re-arm semantics).
- **Coherent diagnostics:** a latched per-capture diagnostic snapshot (GETV latch protocol) provides consistent HW index/count, anchor, fallback, span, and context fields.

B4.36 hardware validation (41320, 50 µs/div rise/fall, tb6/tb9) showed correct alignment in the tested campaign; on that run local anchoring sufficed (recovery path not exercised) and remains host-validated for late-latch cases.

## Timebase and ETS

- 24-step 1-2-5 table (1 µs/div … 50 s/div). TB5+ are real-time single-shot ranges; TB0–TB4 are equivalent-time (ETS/REPT) ranges (sample interval below a single ADC conversion at those settings).
- ETS accumulation/reconstruction logic and diagnostics are implemented and host-tested. B4.32 hardware evidence (12066, B2 Amendment A + B-REC) showed range/waveform-dependent results (TB4 ready/coverage met in captured coherent evidence for fast undersampled square; TB2/TB3 below 80-bin gate in captured windows; TB0/TB1 coverage-limited per B-REC; AC-2 not tested). ETS was not re-campaigned on 41320.
- Per-range/sample-time and timing policies are encoded in timebase/acquisition logic; behaviour should be treated as evidenced per range/campaign.

## Measurements

Pure integer maths (no FPU): min/max/Vpp/mean/RMS and derived values over windows, with 64-bit sums where needed. Some measurements are computed but not all are fully surfaced in the UI/product feature set.

## Calibration and persistence

Reserved flash regions (never erased by image writes):

| Range | Owner |
|-------|-------|
| 0x0800E800..0x0800EBFF | waveform save slot 0 (CRC) |
| 0x0800EC00..0x0800EFFF | waveform save slot 1 (CRC) |
| 0x0800F000..0x0800F3FF | settings slot A (CRC/sequence) |
| 0x0800F400..0x0800F7FF | settings slot B (CRC/sequence) |
| 0x0800F800..0x0800FBFF | calibration page (CRC record) |
| 0x0800FC00..0x0800FFFF | vendor device record — NEVER written |

Settings/calibration use CRC/version/sequence/alternating-slot schemes for robustness. Base calibration data is stored but absolute volts/div scaling is not fully applied.

## PC-link protocol (UART)

**USART1, PA9 TX / PA10 RX, 115200 8N1, 3.3 V.** CRC-16/CCITT-framed commands/replies and sample streaming.

- Commands include NOP, SET_TIMEBASE, SET_TRIGGER, SET_SPAN, RUN, STOP, GET_STATUS, STREAM, GET_VALUE, etc., with argument validation and fixed 10-byte command/reply packets.
- `GET_VALUE` exposes status, measurements, memory, and diagnostic indices (including coherent per-capture latch diagnostics and ETS epoch diagnostics).
- Read-only queries (`NOP`, `GET_STATUS`, `GET_VALUE`) are serviced immediately; mutating commands are deferred while an acquisition cycle is active using a single-slot overwrite queue (late replies are possible under long waits).
- Streaming emits binary sample frames; console text is suppressed while streaming for binary cleanliness.

For the authoritative wire/field/semantic contract (opcodes, status/error behavior, complete `GET_VALUE` map, snapshot procedures, timing/deferral/streaming behavior, and compatibility notes), see [`docs/PROTOCOL.md`](PROTOCOL.md).

## Display and UI

- Direct-to-GRAM rendering on 320×240 ILI9341-compatible panel (no framebuffer).
- Graticule, trace, header/footer/status elements, menu and diagnostics screens as implemented.
- UI/product parity is incomplete relative to stock in several areas (see `docs/FEATURES.md`).

## Module boundaries (host-testable where noted)

Pure/logic and widely tested modules include: `app/acq_anchor.c/.h`, `app/acq_window.c/.h`, `app/trigger.c/.h`, `app/timebase.c/.h`, `app/measure.c/.h`, `app/frame.c/.h`, `app/cal.c/.h`, `app/link.c/.h`, `app/link_cmd.c/.h`, `app/crc16.c/.h`, ETS/epoch/phase/source modules, and others under `tests/`.

Hardware-integrated / limited host coverage: `app/main.c`, `app/acq.c` (engine/HAL), `app/menu.c`, board/display/HAL glue.

## Open architectural items

- Signal input physical landing (PA2/ADC ch2) and ladder mapping are bench-derived/vendor-derived and should be re-confirmed on other units.
- ETS performance is range/waveform dependent; TB0–TB4 are not blanket-validated.
- Absolute calibration scaling not fully applied to displayed volts.
- RAM headroom is limited (~91% on 41320).
- Single-instrument hardware evidence.
