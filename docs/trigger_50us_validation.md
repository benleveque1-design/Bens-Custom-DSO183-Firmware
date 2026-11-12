# 50 µs/div Trigger Bench Validation Report

**Date:** 2026-11-11  
**Firmware under test:** S=63A1 (seq 25505, md5 `c61e044572e121f11ad7e6bd07c2a71c`, flashed; includes per-timebase ADC sample time fix for 50 µs/div). **No acquisition pipeline or trigger logic modified during this investigation.**  
**Access:** remote UART (`/dev/ttyUSB0`, 115200) via `tools/remote_client.py` on the bench host (`<bench-host>`, e.g. a Pi on the local LAN). Freeze monitor stopped during captures to avoid UART contention.  
**Input signal:** ~1 kHz continuous waveform present on input (Vpp ~172–175 counts, min ~881–885, max ~1054–1058 observed across tests). Signal source not reconfigured by agent; treated as stable/repetitive for this run.

## 1. Objectives & method

- Investigate anomalous 50 µs/div trigger behavior reported in prior characterisation.
- Compare against 0.1 ms/div and 1 ms/div baselines; validate rising vs falling, AUTO vs NORM, multiple thresholds.
- Capture and inspect **actual frame sample data** (320 samples/frame), not only summary stats.
- **Limitation (instrumentation):** raw hardware `trigger_index` and refined trigger index are not exposed over the existing UART protocol, and adding instrumentation would require code changes (excluded in initial investigation). Analysis therefore uses frame content: positions of rising/falling crossings relative to pre-trigger target index **96** (`BENCH_PRE`), crossing counts, and waveform shape.

**Definitions:** "nr_lvl/nf_lvl" = index of crossing through `GETV_TRIG_LEVEL` nearest to 96 (rising/falling). "nr_mid/nf_mid" = crossing through per-frame midpoint nearest to 96. A correctly aligned edge-triggered capture should place the selected edge near index 96.

## 2. Test configurations and results

### 2.1 Baselines (known-good reference)

| Test | TB | Mode | Slope | Req lvl | Frames (ok/req) | Errors | Elapsed | `GETV_TRIG_LEVEL` | Freq | Vpp | nr_lvl (n/min/max/mean/p2p/stdev) | nf_lvl | Crossings/frame (rise, fall) | Assessment |
|---|---:|---|---|---:|---:|---:|---:|---:|---:|---:|---|---|---|---|
| 1ms_auto_rise | 9 (1 ms/div, Ts 31.25 µs) | AUTO | RISE | auto | 40/40 | 0 | 6.46 s | 970 | 1000 Hz | 172 | 40 / 96 / 96 / 96 / 0 / 0 | 40 / 80 / 80 / 80 / 0 / 0 | 9, 10 | **PASS** – rising edge locked at pre-trigger (96), consistent |
| 0.1ms_auto_rise | 6 (0.1 ms/div, Ts 3.125 µs) | AUTO | RISE | auto | 40/40 | 0 | 6.09 s | 970 | 0* | 173 | 40 / 95 / 96 / 95.975 / 1 / 0.156 | 40 / 255 / 257 / 256.05 / 2 / 0.312 | 1, 1 | **PASS** – rising edge at 95–96 (≤1 sample = ≤3.125 µs p2p) |

\* 1 ms window at 1 kHz contains ~1 cycle; reported freq can be 0/unreliable in this window — expected.

**Waveform evidence (baselines):** examples show transition at index 96 (e.g. 1 ms: `s96[7]=887, s96[8]=1055`; 0.1 ms: `s96[7]=888, s96[8]=996/1058`) consistent with rising edge at pre-trigger point.

### 2.2 50 µs/div matrix (Ts 1.5625 µs, 0.5 ms window)

| Test | Mode | Slope | Req lvl | Frames | Err | Elapsed | Actual lvl | Vpp | nr_lvl | nf_lvl | Crossings/frame (rise, fall) | Edge at 96? | Assessment |
|---|---|---|---:|---:|---:|---:|---:|---:|---|---|---|---|---|
| 50us_auto_rise_autoLvl | AUTO | RISE | auto | 50/50 | 0 | 6.86 s | 970 | 174 | n=0 | 50 / 287 / 287 / 287 / 0 / 0 | 0, 1 | **No** (falling @287) | **FAIL** |
| 50us_auto_fall_autoLvl | AUTO | FALL | auto | 50/50 | 0 | 6.90 s | 970 | 173 | n=0 | 50 / 96 / 96 / 96 / 0 / 0 | 0, 1 | **Yes** (falling @96) | **PASS** |
| 50us_norm_rise_autoLvl | NORM | RISE | auto | 50/50 | 0 | 6.87 s | 970 | 174 | n=0 | 50 / 287 / 287 / 287 / 0 / 0 | 0, 1 | **No** (falling @287) | **FAIL** |
| 50us_norm_fall_autoLvl | NORM | FALL | auto | 50/50 | 0 | 3.89 s | 970 | 173 | n=0 | 50 / 96 / 96 / 96 / 0 / 0 | 0, 1 | **Yes** (falling @96) | **PASS** |

### 2.3 50 µs/div NORM rising, threshold sweep

| Test | Req lvl | Actual lvl | Frames | Err | nr_lvl | nf_lvl | Crossings/frame | Assessment |
|---|---:|---:|---:|---:|---|---|---|---|
| 50us_norm_rise_lvl891 | 891 (near min) | 891 | 30/30 | 0 | n=0 | 30 / 287 / 287 / 287 / 0 / 0 | 0 rise, 1 fall | **FAIL** (same misalignment) |
| 50us_norm_rise_lvl948 | 948 | 948 | 30/30 | 0 | n=0 | 30 / 287 / 287 / 287 / 0 / 0 | 0 rise, 1 fall | **FAIL** |
| 50us_norm_rise_lvl968 | 968 (~mid) | 968 | 30/30 | 0 | n=0 | 30 / 287 / 287 / 287 / 0 / 0 | 0 rise, 1 fall | **FAIL** |
| 50us_norm_rise_lvl988 | 988 | 988 | 30/30 | 0 | n=0 | 30 / 287 / 287 / 287 / 0 / 0 | 0 rise, 1 fall | **FAIL** |
| 50us_norm_rise_lvl1045 | 1045 (near max) | 1045 | 30/30 | 0 | n=0 | 30 / 287 / 287 / 287 / 0 / 0 | 0 rise, 1 fall | **FAIL** |

Threshold variation (low/mid/high, within signal range) did **not** change the outcome.

## 3. Waveform evidence (50 µs/div)

### 3.1 Failing case: rising requested (AUTO & NORM, all thresholds)
Representative frame (50us_norm_rise, lvl 970): `min 881 max 1055`. No rising crossing through 970 (or 900/mid) anywhere in frame; single falling crossing at index **287**.
- `s[0..20]`  = [1054,1054,1055,1054,1055,1055,1054,1054,1055,1055,1055,1054,1054,1054,1054,1054,1054,1054,1054,1054] (high plateau)
- `s[80..110]` ≈ 1053–1054 (still high through pre-trigger region)
- `s[270..300]` = […,1054,1054,1054,1053,1054,**1054,881**,882,883,…] (falling edge at 287)
- `s[300..320]` ≈ 884–885 (low)
- Large transitions detected: **no big upward steps (>50 counts), one big downward step at (287, 1054→881)** in every inspected frame.

**Interpretation:** for a 1 kHz waveform, 0.5 ms window = half cycle. These captures contain the **high plateau followed by the falling edge** but **no rising edge**. A correctly rising-triggered capture should place the rising edge near index 96 and contain (low/approach)→rising@96→(high), with the falling edge ~320 samples later (outside window). Instead, frames are consistently phase-shifted/misaligned so that a falling transition appears at 287.

### 3.2 Passing case: falling requested (AUTO & NORM)
Representative frame (50us_auto_fall): `min 883 max 1055`. Single falling crossing at **96** (no rising).
- `s[0..20]` ≈ 1053–1055 (high, pre-trigger before edge)
- `s[80..110]` around 96 = […,1055,1055,1054,1055,1055,1054,1055,1055,**883,884**,884,885,…] (falling edge at 96)
- `s[300..320]` ≈ 885–887 (low)

**Interpretation:** falling-edge trigger is correctly detected and aligned to pre-trigger position 96 with **0-sample observed variation** over 50 frames (both AUTO and NORM).

## 4. Repeatability, missing/false triggers

- **Frame delivery:** all requested frames received (40/40, 50/50, 30/30) with **0 UART/frame read errors** reported by the test harness in these runs.
- **NORM mode behavior:** NORM rising and NORM falling both produced fresh streamed frames at expected rates (NORM returns/holds without fresh capture when no trigger; sustained frame production implies trigger events are being detected/taken). Notably, NORM rising produced 50 frames in 6.87 s (comparable to AUTO rising 6.86 s) despite every frame lacking the requested rising-edge alignment — consistent with the acquisition reporting triggered captures whose aligned content is wrong for rising slope.
- **Consistency:** 50 µs falling alignment was perfectly repeatable (nf at 96 in 100/100 frames across AUTO+NORM). 50 µs rising misalignment was perfectly repeatable (nf at 287, nr absent in 140/140 rising-slope captures across modes/thresholds). This indicates a **deterministic defect**, not random jitter.

## 5. Root-cause analysis

### 5.1 Established facts
1. **Baselines (1 ms, 0.1 ms) are reliable:** rising-edge alignment at/near index 96 with ≤1-sample variation under 1 kHz input.
2. **50 µs falling-edge trigger is reliable:** 0-sample variation, correct pre-trigger placement (edge at 96) in AUTO and NORM.
3. **50 µs rising-edge trigger is systematically misaligned:** for all tested modes (AUTO/NORM) and thresholds, every capture is a half-cycle segment containing only a falling edge at fixed index ~287 and no rising edge anywhere in the 320-sample window. The pattern is invariant across 140 rising-request captures.
4. **HW trigger is firing in rising cases:** sustained fresh frame production in NORM (which holds without a trigger) implies the watchdog/event path is taking captures; however, without exposed HW/refined trigger indices, the exact event/alignment point cannot be confirmed non-invasively.

### 5.2 Probable causes (ranked, evidence-based hypotheses)
- **H1 (most likely): slope-dependent misalignment in trigger/refinement path at 50 µs/div.** Falling slope (HW band, pre-arm, SW refinement slope selection, and/or frame placement around the resulting index) yields correct alignment; rising slope yields a consistent phase/error offset producing frames aligned ~191 samples away from the falling edge (nf@287) with no rising edge present. Possible contributors: effective trigger edge used (HW vs refined fallback), refinement search outcome for rising vs falling in the 176-sample window at this Ts/signal scale, or a slope/condition asymmetry in rearm/arming that causes capture around a non-rising event while still satisfying "triggered". **Requires instrumentation (HW trigger index, refined index, found/fallback flag) or targeted code trace to confirm.**
- **H2: pre-arm/arming race specific to rising configuration.** If `wait_trigger_rearm` returns without true rearm in edge cases and AWD is armed while already past threshold, an immediate/ill-timed latch can occur. Budget at 50 µs/div is ~2 ms (from tick-derived calc), sufficient for 1 kHz, and falling (symmetric rearm condition) works — making this less likely but not excluded.
- **H3: interaction with recent 50 µs SMP (1.5-cycle) sampling.** 50 µs/div is newly made viable by per-timebase sample time; timing/margin at 1.5625 µs sample interval could expose a latent path issue. Falling works at 50 µs, arguing against a generic sampling failure.

**Root cause status:** **not conclusively isolated without HW/refined index telemetry**. The defect is **confidently localized** to the rising-edge trigger/alignment behavior at 50 µs/div (falling unaffected; baselines unaffected).

## 6. Remaining timing uncertainties

- **HW trigger index and refined trigger index not observed:** cannot distinguish HW-latch error, refinement miss/wrong selection, or frame-build alignment error.
- **Sub-sample jitter unresolved:** 0-sample p2p at 0.1/1 ms/div and 0-sample p2p at 50 µs falling confirm repeatability within one sample but not sub-sample/ns jitter.
- **Single test signal:** results based on available ~1 kHz waveform; behavior with different amplitudes, offsets, duty cycles, noise, or higher-frequency periodic signals (relevant to 50 µs/div) not characterized here.
- **50 µs rising not revalidated on a known-different edge after fix attempts:** no code changes made (by constraint).

## 7. Recommended next engineering changes (smallest targeted)

1. **Add non-invasive debug telemetry (preferred first step):** expose per-capture `gate.triggered`, `gate.trigger_index` (HW), refined index, `refined` flag, and fallback reason via UART `GET_VALUE`/debug op or a lightweight status struct readable over SWD. This directly discriminates H1 sub-causes without altering trigger decisions.
2. **If telemetry shows refinement fallback/miss on rising:** audit `trig_refine` slope handling and `trig_scan_nearest` behavior at 50 µs/div (window centering, rearm-state at buffer start, edge cases) for rising vs falling symmetry; fix only the identified condition.
3. **If telemetry shows HW latch/index anomaly on rising:** audit AWD band/rearm sequencing and ISR latch timing for rising configuration at 1.5625 µs sampling.
4. **Bench revalidation:** repeat this matrix (rising/falling, AUTO/NORM, thresholds, ≥50 frames each, 0.1/1 ms baselines) plus additional signal shapes/frequencies before declaring 50 µs/div trigger trustworthy for ETS groundwork.

## 8. Conclusion

- **0.1 ms/div and 1 ms/div:** trigger alignment **validated** (rising edge repeatable within ≤1 sample under test conditions).
- **50 µs/div falling:** **validated** (perfectly repeatable, correctly aligned).
- **50 µs/div rising:** **not trustworthy** — deterministic misalignment (no rising edge at pre-trigger; consistent falling edge at index ~287) across AUTO/NORM and all tested thresholds. **Do not rely on 50 µs/div rising-edge triggering until root cause is isolated and fixed.**
- The goal of a trustworthy, repeatable trigger at the fastest real-time single-shot timebase is **partially met (falling) / not met (rising)**; ETS investment should follow correction and revalidation of rising-edge behavior.
