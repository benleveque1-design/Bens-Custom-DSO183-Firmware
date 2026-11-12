# Architecture

**DSO183 custom firmware — design and data paths**

## Overview

The firmware is a **modular, host-testable** design. Pure-logic modules
(acquisition, trigger, timebase, measurements, waveform decimation/mapping,
rendering, calibration, PC-link framing, settings, run state) contain no HAL
dependency and compile and run on a host computer for unit testing. Hardware
glue lives only in `main()` and the board support files (`board_*.c`, `stm32f1xx_it.c`,
the display driver). `--gc-sections` drops any module `main()` does not wire in,
so unused code never costs flash.

The design follows rules written down during the audit remediation:

- **No unbounded waits.** Every busy wait has an explicit `HAL_GetTick()`
  deadline. A wait that exceeds its deadline returns an error rather than
  spinning forever (the vendor's unbounded auto-zero wait is one of the defects
  this replaces).
- **No status-byte gates, no ATSHA204A dependency.** Calibration and settings
  live in local flash pages; the firmware starts in a safe default state even
  if the secure element is absent.
- **Watchdog always armed, fed once per main-loop iteration.** A hung loop
  resets; feeding from SysTick (which would keep a hung loop alive) is not
  done.
- **No shared "scratch slot" aliasing.** Each flash record has its own page,
  version, sequence number and CRC.

## MCU and clocking

**STM32F103C8T6**, HSI-based clock tree (a deliberate, independent choice from
the vendor's HSE/PLL):

- `SYSCLK = 64 MHz` (HSI/2 × 16)
- `APB1 = 32 MHz`, `APB2 = 64 MHz` (PCLK2)
- Timer clock: on the F1, timers on APB1 run at **2 × PCLK1 when the prescaler
  is not 1**, so **TIM3 = 64 MHz**, tick = 15.625 ns/count. (An earlier
  revision assumed 32 MHz and was 2× fast — fixed.)
- ADC clock = PCLK2 / 6 = 10.67 MHz, inside the 14 MHz limit. Shortest
  conversion = 14 cycles (SMP 1.5 + 12.5) = 1.3125 µs.

## Acquisition pipeline

```
TIM3 (TRGO on update)  →  ADC1 regular conversion (channel 2, PA2)
                           →  DMA1 channel 1  →  1024-sample circular buffer (RAM)
                                           →  acq_cycle() freezes DMA on trigger
                                           →  frame_build() → linear window
                                           →  measure()/cal()/waveform()/render()
                                           →  display
```

- **ADC1 channel 2 (PA2)** is the signal input and the analog-watchdog
  (trigger) channel. The vendor configures the same channel for both.
- **DMA1 channel 1** runs circular into a 1024-sample `uint16` buffer
  (2 KiB of the 20 KiB RAM).
- **TIM3 TRGO** paces exactly one conversion per update event
  (`EXTTRIG = 1`, `EXTSEL = 100` = TIM3_TRGO — the same external-trigger
  source the vendor uses, measured live on the bench; the vendor paces from
  TIM2_CC2, so the two cores never share a timer).
- `acq_cycle()` re-arms the watchdog, waits for a trigger (bounded timeout),
  then **freezes** the sample path (stops TIM3, parks DMA1 channel 1), copies
  the ring into an immutable snapshot, and restarts acquisition. The caller
  reads a consistent window that DMA can no longer modify.
- An **ADC1 ownership token** (`app/adc_owner.c`) makes the acquisition engine
  and the front-panel ladder reader explicitly arbitrate ADC1: the ladder is
  read during a short bounded acquisition pause (`acq_pause()`/`acq_resume()`),
  because this clone's injected ADC group does not convert (JSWSTART sets JEOC
  but JDR1..3 stay at zero).

## Trigger system

- **Hardware event:** ADC1 analog watchdog (AWD), one channel, one-sided band
  with hysteresis (`ACQ_TRIGGER_HYST = 8` counts):
  - rising → `LTR = 0`, `HTR = level + hyst` (fires on a rise out of the band)
  - falling → `LTR = level − hyst`, `HTR = 4095` (fires on a fall)
  
  A two-sided band would latch on the first conversion after arming whenever
  the signal rests outside it, giving random phase. The one-sided band gives
  genuine edge-direction selectivity.
- **Re-arm:** before arming, `acq_cycle()` waits (bounded) for the input to be
  on the rearm side of the threshold, so the first conversion after arming
  cannot re-latch a spurious crossing.
- **ISR latch:** `ADC1_2_IRQHandler` → `acq_isr_adc()` latches the buffer write
  position at ISR entry and disables AWDIE (one-shot latch, no interrupt storm
  on a standing out-of-window signal). The latched index lags the true
  crossing by the ADC-to-ISR latency (open item U-04b — measure against a
  known square before trusting sub-sample placement).
- **Software edge refinement** (`app/trigger.c`): a Schmitt state machine with
  rearm-and-cross hysteresis scans a window centred on the hardware event and
  selects the crossing nearest to it (rising/falling/selectable slope).
- **Policy FSM** (`trig_fsm_action`): AUTO (fall back to newest samples on
  timeout), NORM (hold until a trigger), SINGLE (one triggered window then
  hold).
- **Trigger-aligned averaging** (1/2/4/8) and HOLD/recalled waveform display.

Known limitation: the 50 µs/div rising-edge path has a deterministic
misalignment under investigation (falling edge verified aligned). See
`docs/FEATURES.md` and `docs/trigger_timing_characterisation.md`.

## Timebase

- 24 steps of the 1-2-5 sequence, index 0 = 1 µs/div … index 23 = 50 s/div
  (the full published range extends to 500 s/div, but the firmware caps at
  50 s/div because slower ranges make synchronous capture/UI impractical at
  10 divisions/window).
- Each step carries baked `(PSC, ARR)` for the 64 MHz TIM3 tick, error-free in
  integers.
- Indices 0–4 (1–20 µs/div) need a sample interval shorter than one ADC
  conversion (1.3125 µs), so they require **equivalent-time / repetitive (ETS/REPT)**
  sampling which the engine does not implement. They are flagged
  `single_shot == 0` and `acq_set_timebase()` rejects them rather than pacing
  TIM3 faster than the ADC can convert. Index 5 (50 µs/div) is the first
  physically usable single-shot rate.
- Per-range ADC sample-time selection: shortest aperture that still settles
  (7.5 cycles normally; 1.5 cycles only on the fastest single-shot range).

## Measurements

Pure integer maths, no floating point, no `malloc` in the sampling path (the
F1 has no FPU):

- `min`, `max`, `Vpp`, `mean`, `RMS` over a window, using 64-bit sums
  (1024 × 4095² does not fit in 32 bits) and a bit-by-bit integer square root.
- `Vpp` is shown on the panel; RMS/frequency/duty are computed by `measure.c`
  but not all displayed (product-parity gap).
- Frequency/duty use a midpoint + hysteresis edge detector with a
  timebase-derived period; they require a measurable periodic signal — bench
  validation required.

## Calibration and persistence

Reserved flash (never erased by image writes):

| Range | Owner |
|-------|-------|
| 0x0800E800..0x0800EBFF | waveform save slot 0 (CRC, 256 samples) |
| 0x0800EC00..0x0800EFFF | waveform save slot 1 (CRC, 256 samples) |
| 0x0800F000..0x0800F3FF | settings slot A (alternating, CRC record) |
| 0x0800F400..0x0800F7FF | settings slot B (alternating, CRC record) |
| 0x0800F800..0x0800FBFF | calibration page (40-byte CRC record: base offset/gain + 9-entry per-sensitivity zero table) |
| 0x0800FC00..0x0800FFFF | vendor device record — NEVER written |

Settings and calibration each use two alternating slots with magic, version,
sequence number and CRC-16/CCITT-FALSE, so a torn write always leaves the
previous valid record intact. The calibration record is CRC-checked on read.

**Not yet applied:** the base offset/gain pair is stored and editable but does
not yet change the volts/div label or Vpp number — applying it needs a measured
reference to fix the divider ratio.

## PC-link protocol (UART)

The DSO183 USB socket is power-only, so host communication goes over the
console UART: **USART1, PA9 TX / PA10 RX, 115200 8N1, 3.3 V**.

### Framing (both directions)

```
offset  size  field
0       2     preamble 0xA5 0x5A
2       1     type       (0x01 = sample frame, 0x02 = command, 0x03 = reply)
3       1     sequence   (wraps)
4       2     payload length n, little-endian
6       n     payload
6+n     2     CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over
              type..payload, little-endian
```

`link_crc16("123456789") == 0x29B1`.

### Commands (type 0x02, 10-byte packets)

`A5 5A | 0x02 | seq | op | arg_lo | arg_hi | crc_lo | crc_hi`

| Opcode | Name | Argument | Behaviour |
|--------|------|----------|-----------|
| 0x00 | NOP | — | replies OK (link check) |
| 0x01 | SET_TIMEBASE | 0..26 (only 5..26 accepted) | `acq_set_timebase()` |
| 0x02 | SET_TRIGGER | 0..4095 | `acq_set_trigger_level()` |
| 0x03 | SET_SPAN | 16..4095 | display span |
| 0x04 | RUN | — | leave HOLD, acquisition continues |
| 0x05 | STOP | — | HOLD: freeze the displayed frame |
| 0x06 | GET_STATUS | — | replies current op/timebase index |
| 0x07 | STREAM | 0/1 | binary frame bursts instead of text |

Replies (type 0x03) echo `seq` and `op`, carry a status word and a value,
and are validated against `link_cmd_arg_valid()` before dispatch — out-of-range
arguments return `BAD_ARG` instead of being silently clamped.

### Sample streaming (type 0x01)

While STREAM is on, the instrument emits one binary sample frame per
acquisition (~17.7 frames/s at 115200 baud with 256-sample frames, close to
the UART ceiling of ~22 frames/s). The text console is suppressed while
streaming so the stream stays binary-clean.

## Display and UI

- Zero framebuffer: all rendering writes directly to ILI9341 GRAM.
- 320×240 landscape, 10×8 dotted graticule (dots every 4 px, ~81% less bus
  traffic than a solid grid).
- Adafruit GFX 5×7 glyph font streamed to GRAM via a zero-RAM iterator
  (`glyph_init`/`glyph_next`).
- Persistent header: RUN/HOLD badge, trigger mode/slope/level, TRIG'D/WAIT,
  link status, firmware identity (`B=4453424D S=xxxx`).
- Two-row footer: CH1 scale/coupling/timebase/frequency; measurement bar or
  quick-focus guidance.
- Four-button non-modal navigation: SEL cycles focus, +/− adjust, OK toggles
  RUN/HOLD, SEL-hold opens/closes menu.
- SYSTEM DIAG: build identity, boot counter, loop/heartbeat, display ID,
  ADC raw channels, decoded switch positions, error register.

## Module boundaries (host-testable)

`app/acq.c/.h`, `app/trigger.c/.h`, `app/timebase.c/.h`, `app/measure.c/.h`,
`app/waveform.c/.h`, `app/render.c/.h`, `app/frame.c/.h`, `app/cal.c/.h`,
`app/cal_table.c/.h`, `app/link.c/.h`, `app/link_cmd.c/.h`, `app/crc16.c/.h`,
`app/continuity.c/.h`, `app/runstate.c/.h`, `app/settings.c/.h`,
`app/ladder.c/.h`, `app/diag.c/.h`, `app/bench.c/.h`, `fonts/glyph.c/.h`.

Hardware-only: `app/main.c`, `app/board_*.c`, `app/stm32f1xx_it.c`,
`drivers/display/ili9341.{c,h}`, the vendored HAL and CMSIS, `startup/`,
`linker/`.

## Open architectural items

- ADC1 signal channel (PA2) confirmed in the vendor binary and read live, but
  its landing point as the physical BNC input is an open bench item.
- Ladder channel mapping is vendor-derived; bench re-confirmation pending.
- Pushbutton pin unknown (PA8 is the leading candidate; unproven).
- Equivalent-time sampling not implemented (indices 0–4 not physically usable).
- No host tests yet for `acq.c`, `menu.c`, or HAL-level modules.
