# 50 µs/div Trigger Validation (B4.36 update)

**Status:** **Hardware-validated on build 41320 (2026-11-15, single tested instrument).** Prior deterministic rising-edge misalignment at 50 µs/div is resolved in the tested campaign.

## Current validated result (41320)

Campaign: 12 configs × 50 captures (600 total), 1 kHz 50% square, level 970, tb5/tb6/tb9, AUTO/NORM, rise/fall. Evidence: `evidence/B4_36_HW_VALIDATION_20261115T053222Z/run/` (gitignored), report: `B4_36_TRIGGER_FIX_AND_VALIDATION_REPORT.md`.

| Config | n | coherent | requested edge within ±2 of 96 | placement | fallback | anchor_kind |
|---|---:|---:|---:|---|---|---|
| tb5_auto_rise | 50 | 50 | **50/50 (100%)** | 96.00 | 0 | 0 (local) |
| tb5_norm_rise | 50 | 50 | **50/50 (100%)** | 95.98 | 0 | 0 (local) |
| tb5_auto_fall | 50 | 50 | **50/50 (100%)** | 96.00 | 0 | 0 (local) |
| tb5_norm_fall | 50 | 50 | **50/50 (100%)** | 96.00 | 0 | 0 (local) |
| tb6 (all 4) | 50 ea | 50 | **50/50** | 96.00 | 0 | 0 |
| tb9 (all 4) | 50 ea | 50 | **50/50** | 96.00 | 0 | 0 |

- **Primary criterion (B4.34):** tb5 rising AUTO 50/50 and NORM 50/50 within ±2 — **PASS** (was 0/100 on 48462).
- **Coherence:** 600/600 coherent, 0 no-frame, 0 diag errors, 0 rejected (`fallback` 3/4 not observed).
- **Diagnostics:** `refined_found=1`, `fallback=0`, `anchor_kind=0` (local), offsets −1…−2, publishable effective spans throughout this run. Recovery path not exercised on this run but is host-validated for late-latch geometries.

## Fix summary (B4.36)

- The AWD latch index is a level/sample event, not guaranteed to be the edge. Prior code could publish the raw mid-plateau latch when the true edge fell outside the local search window (129–131 samples late in the 48462 failure geometry).
- B4.36 anchors on a **verified requested-slope edge**: local pass (parity with historic refinement) then, if needed, a **span-bounded recovery** pass behind the latch with effective-span validation; unpublishable captures are rejected (not mis-published). Coherent per-capture diagnostics are latched via `GETV_DIAG_LATCH`.
- ETS recovery is disabled to preserve bounded association semantics.

## Historical context (pre-fix)

- S=63A1 / 48462 campaigns showed deterministic 50 µs/div rising misalignment (no requested rising crossing in-frame, falling near 286–287) across modes/thresholds while falling and slower baselines passed. See `B4_34_DEPLOYMENT_AND_HARDWARE_VALIDATION_REPORT.md`, `B4_35_TRIGGER_ROOT_CAUSE_INVESTIGATION_REPORT.md`, and archived notes below.

## Limitations

- Single tested instrument and signal/condition set in this campaign; broader waveform/amplitude/duty/temperature validation remains good practice.
- Host tests do not prove physical correctness by themselves; this result is evidenced by the hardware campaign above.
- TB0–TB4 ETS behaviour is separate and range/waveform dependent (see B4.32 evidence / README).

## Archived notes (pre-B4.36, superseded for 50 µs rising outcome)

Earlier versions of this document recorded failed/inconclusive diagnostic flashing and the pre-fix failure matrix. They are retained in git history and in B4.33–B4.35 reports/evidence for provenance.
