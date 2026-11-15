# Roadmap

**DSO183 custom firmware — future development**

This roadmap is a statement of intent, not a promise. Items are roughly
ordered, but priorities will shift with community contributions and bench
findings. Nothing here has been implemented unless it is also listed in
`docs/FEATURES.md` as verified.

## Near term

1. **Product parity / UX completeness:**
   - Vrms, frequency, duty readouts on the panel (maths exist in `measure.c`)
   - `AV+`/`AV−`/`Cycle`/`All Disp.` display modes
   - `Save WF` / `Load WF` parity and menu actions
   - `Clr Offset` / `Default` behaviours fully aligned with intent
   - per-sensitivity/base calibration applied to volts/div and trace/readouts
2. **Persistence integrity hardening:** save-on-release where appropriate, robust error handling/feedback for flash writes, continued separation of reserved regions.
3. **Trigger/diagnostics polish:** keep coherent diagnostics useful; extend validation to other signal shapes/levels/duty cycles and confirm behaviour across more conditions (recovery-path exercise on hardware if a late-latch case is observed).
4. **Documentation and reproducibility:** keep reports/docs aligned with evidenced behaviour; maintain safe flashing/recovery guidance.

## Mid term

5. **ETS validation and improvement:** broader hardware campaigns on TB0–TB4 (and on current builds) across waveforms/conditions; address coverage/acceptance limitations evidenced in B4.32 where feasible without violating B2/B-REC constraints.
6. **Display quality:** reduce redraw/flicker (dirty regions/trace erase strategies) where practical within RAM/bandwidth limits.
7. **PC link evolution:** richer query/status semantics and tooling; maintain protocol compatibility/versioning as features grow.
8. **Measurement/display completeness:** surface computed measurements consistently with bench validation.

## Long term

9. **ETS robustness/performance:** improve equivalent-time reconstruction quality and usability on the fastest ranges, within hardware limits (ADC conversion time, timing/phase constraints), and with honest range-specific validation.
10. **Calibration workflow:** reference-based calibration applied end-to-end (trace and readouts), with clear UI/feedback.
11. **Hardware confirmation/portability:** re-verify pin mappings, signal path, and display/controller behaviour on other units (without claiming compatibility prematurely).

## Explicitly not planned (staying small)

- color themes / modern smartphone-style UI (retro laboratory-instrument aesthetic)
- a second channel (single-channel front end)
- USB host/device data path (USB socket is power-only)
- real-time operating system (superloop is intentional)

Contributions are welcome at every experience level — see `CONTRIBUTING.md`.
