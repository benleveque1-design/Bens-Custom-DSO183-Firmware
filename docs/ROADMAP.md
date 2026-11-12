# Roadmap

**DSO183 custom firmware — future development**

This roadmap is a statement of intent, not a promise. Items are roughly
ordered, but priorities will shift with community contributions and bench
findings. Nothing here has been implemented unless it is also listed in
`docs/FEATURES.md` as verified.

## Near term (the v0.1.x line)

1. **Close the three audit P0s** (remediated in source; bench confirmation
   still needed):
   - settings and calibration in separate flash pages with CRC/version/sequence
   - Vpp readout dimensionally correct (pure count ratio)
   - bidirectional trigger level with a sane default
2. **Persistence integrity** — CRC + version + sequence + alternating slots
   (done in storage layer); save-on-release rather than per-menu-step; check
   flash write return values.
3. **Product parity with the stock UI:**
   - Vrms, frequency, duty readouts on the panel (maths exist in `measure.c`)
   - `AV+`/`AV−`/`Cycle`/`All Disp.` display modes
   - `Save WF` / `Load WF` to the two waveform slots
   - `Clr Offset` / `Default` / `Default restored` menu actions
   - per-sensitivity offset table applied to the trace (CAL-03/CAL-04)
4. **Trigger:** bench-validate and fix the 50 µs/div rising-edge misalignment;
   expose trigger telemetry (HW index, refined index, fallback flag) over the
   link or SWD so the root cause can be isolated non-invasively.

## Mid term

5. **Display quality:** dirty-rectangle or previous-trace erase to remove the
   full-frame clear/redraw flicker; optional rectangle-only repaint.
6. **PC link:** query commands for measurements, trigger level, calibration
   and settings; error replies beyond the status word; `STREAM` replies echoing
   the stream state.
7. **Link/UX polish:** the five equivalent-time-only timebases hidden in the
   UI/link; `SET_SPAN` surviving ladder moves; text statistics throttled.

## Long term (foundation work)

8. **Equivalent-time / repetitive (ETS/REPT) sampling** for the five fastest
   timebases (1–20 µs/div, indices 0–4). These need a sample interval shorter
   than one ADC conversion (1.3125 µs), which real-time single-shot acquisition
   cannot provide. ETS/REPT would re-acquire the same repetitive waveform over
   multiple triggers, shifting the sampling phase slightly each time, and
   reconstruct a fine-grained waveform — the classic approach for fast
   repetitive signals on a slow ADC. This is **not implemented** and is the
   largest remaining hardware-visible gap before the full 1 µs/div … 50 s/div
   published range is trustworthy.
9. **Per-range ADC sample-time programming** (`timebase_smp_code()` exists;
   `acq.c` still uses a fixed aperture) so the fastest single-shot ranges can
   use the 1.5-cycle minimum aperture.
10. **Encoder / pushbutton confirmation** — if PA8 (or another pin) is
    confirmed as the menu button on real hardware, wire it up.
11. **More measurements** displayed (RMS, frequency, duty) with bench
    validation on real signals.

## Explicitly not planned (staying small)

This is a community project, not a race to the most sophisticated firmware.
The following are deliberately out of scope unless a contributor champions
them:

- color themes / modern smartphone-style UI (the design brief is a retro
  laboratory-instrument aesthetic, see `ui_design/UI_SPECIFICATION.md`)
- a second channel (the DSO183 front end is single-channel)
- USB host/device (the DSO183 USB socket is power-only)
- real-time operating system (the superloop is intentional and simple)

Contributions are welcome at every experience level — see `CONTRIBUTING.md`.
