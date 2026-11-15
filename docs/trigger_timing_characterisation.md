# Trigger Timing Characterisation (updated for B4.36)

**Status:** B4.36 (build 41320) implements span-bounded edge anchoring and coherent diagnostics and is **hardware-validated** for the 50 µs/div rising/falling regression campaign (600/600 coherent, all requested edges within ±2 of 96 in tested cases). Earlier conclusions about a deterministic 50 µs/div rising misalignment are **superseded by the B4.36 fix/validation** for the tested conditions, while the underlying physical latch-timing variability remains something to characterise if needed.

## 1. Current trigger/anchoring architecture (41320)

- **Detection:** ADC1 analog watchdog (single channel), one-sided slope-aware band with hysteresis; bounded rearm; one-shot ISR latch.
- **Capture:** TIM3-paced ADC → DMA ring, bounded post-fill, freeze/snapshot, mode policy (AUTO/NORM/SINGLE).
- **Anchoring/publication (`app/acq_anchor.c`, `app/trigger.c`, `app/acq_window.c`, `app/main.c`):**
  - Local nearest-edge search around the hardware latch (historic parity for healthy latches).
  - If local fails, span-bounded recovery behind the latch (Schmitt-qualified, bounded by measured span; disabled on ETS ranges).
  - Effective span validated before publication; unpublishable/rejected captures are not mis-published as aligned frames.
- **Diagnostics:** coherent latched per-capture snapshot (GETV 46 latch + fields 18–27,45,47–50) exposing hw/anchor indices, offset-derivable alignment, fallback/anchor-kind, span and context.

## 2. Hardware-validated timing/alignment (B4.36, 41320)

**Setup:** 1 kHz 50% square, level 970, tb5/tb6/tb9, AUTO/NORM, rise/fall, 50 captures each (600 total). UART campaign on tested instrument.

- **tb5 (50 µs/div):** requested edge at 96 in 50/50 for all four configs (rising 96.00 / 95.98, falling 96.00). `anchor_kind=0` (local), offsets −2, post_span 225, effective span 227, `fallback=0`, coherent 50/50 each.
- **tb6 (0.1 ms/div):** requested edge at 96 in 50/50 all configs. `anchor_kind=0`, offsets −1, post_span 224.
- **tb9 (1 ms/div):** requested edge at 96 in 50/50 all configs. `anchor_kind=0`, offsets −1, spans 238–255 as measured.

**Interpretation:** under these conditions, alignment is stable and correct; the B4.36 anchoring/publish gate prevents publication of misaligned/mid-plateau frames. The recovery path was not required in this run (healthy latch) but is host-validated for 129–131-sample late-latch cases.

## 3. Theoretical/structural notes (still relevant)

- Sampling is quantized to `Ts`; AWD/ISR/DCNTR introduce small, implementation-dependent latency/jitter relative to the analog crossing. Anchoring/refinement corrects to the nearest verified edge within policy bounds.
- 50 µs/div: `Ts` 1.5625 µs (quantization ±0.781 µs) — sub-sample jitter not directly resolved by index-based campaign metrics.
- ETS (TB0–TB4) requires equivalent-time reconstruction and has range/waveform-dependent hardware evidence (see B4.32).

## 4. Open items

- Broader validation across amplitudes, duty cycles, frequencies, noise, and environmental conditions.
- If recovery (`anchor_kind=1`) is observed on hardware, record offsets/spans and confirm placement.
- Deeper ns-grade timing characterisation would require dedicated instrumentation beyond index/crossing statistics.
- ETS performance/coverage remains a separate track (B4.32 evidence).

## 5. Historical note

Pre-B4.36 characterisation documented a deterministic 50 µs/div rising misalignment on earlier builds; that specific failure mode is addressed and no longer reproduced in the 41320 campaign. Historical analysis remains valuable in git history and B4.33–B4.35 reports for provenance.
