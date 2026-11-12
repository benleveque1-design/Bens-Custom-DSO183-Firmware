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

## 1. Verified — bench-verified on the instrument

These were observed working on real hardware (the single tested unit, an
STM32F103 medium-density rev A DSO183 with a 0x858552 display controller):

- **Boot and watchdog:** the image boots, the IWDG watchdog is armed (10 s)
  and fed every superloop iteration; boot counter advances; heartbeat/main
  loop advances; zero error flags (`ERR 00`).
- **Display:** ILI9341-compatible panel works; text is upright and left-to-right
  (MADCTL 0x68); grid + trace draw; read path works (`ID 858552`, `M 68`);
  SYSTEM DIAG screen shows build identity, boot counter, loop counter, display
  ID, MADCTL, ADC raw channels, error register.
- **Acquisition path:** ADC1 → DMA1 → 1024-sample circular buffer runs, paced
  by TIM3 TRGO; `DMA_CNDTR` moves; conversions start within the 20 ms
  start-up bound; no hardware errors (`err 0`).
- **Front-panel ladder:** the gain/range/coupling slide switches respond and
  decode to live values (combo and coupling shown on UART and panel); all nine
  sensitivity combinations appear; the panel's scale label follows switch
  position (e.g. `3mV/d 1ms/d`).
- **Trace:** a continuous waveform draws on the panel, centred on the frame
  mean, with a dashed trigger-level marker and a bottom scale label.
- **PC link:** protocol v1 verified end-to-end over `/dev/ttyUSB0` at
  115200 8N1: GET_STATUS, SET_SPAN, STREAM, RUN/STOP all return correct
  replies; STREAM produced 53 valid 520-byte CRC-checked frames in ~3 s
  (~17.7 frames/s).
- **RUN/STOP control:** verified over the link with replies echoing the
  resulting run state.
- **Host test suite:** all suites pass (`make test`), `make guard` reports
  0 issues, `make check` passes and cross-checks `FLASH_HANDOVER.md` against
  the built image.
- **Flash layout:** reserved pages (settings, calibration, waveform slots,
  vendor device record) are never touched by an image flash; the page-wise
  writer freezes the IWDG during erase so a page erase cannot reset the target.

## 2. Verified — host-tested only (logic proven, hardware path not yet exercised)

These modules have full host unit-test coverage, but the complete hardware
path has not been flashed/bench-verified from this tree:

- **Trigger engine** (`app/trigger.c`): edge detection, hysteresis, re-arm,
  AUTO/NORM/SINGLE policies — host-tested. Bench verification pending;
  note the 50 µs/div rising-edge misalignment below.
- **Timebase table** (`app/timebase.c`): 24-step 1-2-5 table, tick accuracy,
  per-range sample-time policy — host-tested. TIM3 pacing bench-verified down
  to 50 µs/div; faster single-shot rates not yet paced.
- **Measurements** (`app/measure.c`): Vpp/mean/RMS/min/max integer maths —
  host-tested against exact squares and a `double` reference; not yet
  displayed beyond Vpp.
- **Waveform decimation/mapping/rendering** (`app/waveform.c`, `app/render.c`,
  `app/frame.c`): min/max-per-column decimation (spikes survive), y-mapping
  with clamp guards — host-tested; not yet wired into the live draw path from
  this tree.
- **Calibration maths** (`app/cal.c`): `cal_fit()`, `cal_raw_to_uv()`
  with saturation guards — host-tested; base offset/gain pair stored and
  editable but not yet applied to the volts/div label or Vpp.
- **PC-link codec & command policy** (`app/link.c`, `app/link_cmd.c`,
  `app/link_host.c`): framing, CRC, argument validation — host-tested including
  end-to-end round trips; the device side is also bench-verified (above).
- **Settings & calibration storage** (`app/settings.c`, `app/cal_store.c`,
  `app/cal_table.c`, `app/flash_store.c`): CRC/version/sequence/alternating
  slots — host-tested including corruption and slot-order cases.
- **Flash layout separation** (`app/flash_store.c`): settings and calibration
  live in different 1 KiB pages — pinned by `tests/test_flash_layout.c`.
- **Continuity detector** (`app/continuity.c`): hysteresis thresholds —
  host-tested; reported visually (`CONT` suffix), no buzzer exists on the board.

## 3. Experimental / partially verified

- **Trigger stability:** AUTO/NORM/SINGLE work, but the **50 µs/div rising
  edge is deterministically misaligned** (frames land ~191 samples off, with
  no rising edge in the window). Falling edge at 50 µs/div is verified
  aligned. Do not rely on 50 µs/div rising-edge triggering until root cause
  is isolated (see `docs/trigger_timing_characterisation.md`).
- **Trigger level:** adjustable 0–4095 and bidirectional (fixed in
  remediation), but the default (2048) sits above the typical idle level
  (~1195 counts), so NORM mode can appear dead until the level is lowered.
- **Vpp readout:** now reports a pure count ratio (`stats.vpp × 80 / s_span`)
  across the 8 divisions; matches the trace. Absolute volts/div scaling from
  the base calibration pair is **not applied** (needs a measured reference).
- **AC coupling:** the software AC branch (`wave_remove_mean()`) is wired to
  the CPL switch, but on this board the coupling switch is analogue-only and
  its position is not observable by the MCU — the AC branch is effectively
  unreachable and GND coupling is not implemented.
- **Timebase label:** now derived from `timebase_label()` (was hardcoded
  `1 ms/d`); label correctness across all 24 steps is host-tested, bench
  spot-checked only.
- **Menu navigation:** 5 pages, tap/double-tap/long-hold queued via SysTick
  at 1 kHz with debounce; auto-closes after 20 s idle. Short presses under
  the old 500 ms loop were lost; now polled every pass (1 ms loop).

## 4. Known limitations (do not claim these)

| Limitation | Detail |
|------------|--------|
| Product parity missing | RMS/frequency/duty readouts are computed but not all displayed; `AV+`/`AV−`/`Cycle`/`All Disp.` display modes absent; `Save WF`/`Load WF` absent; `Clr Offset`/`Default` menu actions present but per-sensitivity calibration not applied to the trace |
| Five fastest timebases | 1–20 µs/div need equivalent-time sampling, not implemented; selecting them produces aliased data — rejected by `acq_set_timebase()` and clamped out of the UI/link |
| Coupling not observable | AC/DC/GND is analogue-only; firmware cannot read or confirm it |
| Pushbutton pin unknown | PA8 is the leading candidate (vendor reads GPIOA bit 8 at boot); unproven on hardware |
| Signal channel | ADC1 ch2 (PA2) confirmed in vendor binary and read live, but its landing point as the physical BNC input is an open bench item |
| RAM heavily used | 81% of 20 KiB static; no framebuffer; further feature growth needs care |
| Loop rate | ~1 ms non-streaming (was 500 ms); adequate but not high-frame-rate |
| Single instrument tested | Compatibility with other DSO183 revisions/clones/display panels not tested, not claimed |
| No host tests | `menu.c`, `acq.c` (engine/HAL), and HAL-level modules lack host tests |
| USB | power-only on the DSO183; PC link is UART only |
| Buzzer | none on the board; continuity is reported visually, not audibly |

## 5. Not in scope (deliberately out of this firmware)

- Equivalent-time / repetitive sampling for 1–20 µs/div (planned as ETS/REPT
  work — see `docs/ROADMAP.md`)
- The vendor's TIM4_CCR1 trigger-position mechanism (this framework records
  the buffer write position instead)
- Any dependency on the ATSHA204A secure element (removed by design)
- `st-flash write` / mass-erase flashing (prohibited — destroys calibration)
