# B4.36 — 50 µs/div Rising-Edge Trigger Fix and Host Validation Report

**Date:** 2026-11-15 (UTC)
**Type:** Source fix + host validation. **No instrument interaction of any kind** (no flash, erase, program, reset, halt, or serial command). No deployment.
**Workspace:** `/home/print/dso183-firmware-project`
**Candidate:** build_seq **41320**, `build/dso183.bin` (40,960 B)
**Instrument state:** verified build **14139** remains installed (64 KiB readback SHA-256 `e2bd129f…9949f8`, UART `BUILD_SEQ 14139`, `STATUS 1`). Untouched by this session.

---

## 1. Executive summary

- The 50 µs/div rising-edge failure is a **software anchoring defect that is exposed by a late hardware latch**, not a threshold, polarity, ETS, or buffer-overwrite problem.
- The ADC analog watchdog (AWD) is a **level detector**. The index it latches is guaranteed only to be a sample on the trigger side of the threshold, not the edge sample. Pre-B4.36 firmware published **that latch index as the display anchor**.
- Preserved campaign frames pin the geometry exactly: the failing 320-sample frames contain a falling crossing at index **285–287** and no rising crossing. With `BENCH_PRE=96` and a 640-sample period this places the latch **129–131 samples after the true rising edge**. The historic local refinement searched `[latch−128, latch+48)`, which therefore began at **rise+1 … rise+3** — one to three samples after the edge — and could not find it. Its simple-threshold fallback scanned the same window and also failed. The mid-plateau latch index was then published.
- **Fix:** anchor on a *verified requested-slope edge* instead of the raw AWD index. A local pass preserves the historic behaviour exactly for a healthy latch. If it fails, a **span-bounded recovery pass** searches behind the latch for the nearest hysteresis-qualified edge. The recovered edge is validated against the measured capture span; if it is not publishable, the frame is **rejected instead of publishing a mid-plateau window**.
- **Diagnostics** are now coherent: `GETV_DIAG_LATCH` (46) copies the whole per-capture record in one command; the field GETVs read that latched copy. The old 11-field bracket could never be coherent because each GETV costs ~110 ms while a capture completes in ~100 ms.
- **Host validation:** 36 test suites, **1,318 CHECK sites, 0 failures**. The new anchoring suite alone has 82 assertions and covers the measured +129/+130/+131 geometry, falling slope, healthy latch, recovery bounds, ETS disable, ring wrap, tb6/tb9, no-edge rejection, effective span, invalid arguments, and end-to-end frame placement.
- **Resource cost:** flash 40,168 → 40,956 B (+788), RAM 18,648 → 18,680 B (+32). Both within budget (66% flash, 91% RAM).
- **Disposition: `FIX IMPLEMENTED — HOST VALIDATED — HARDWARE VALIDATION PENDING`.**
  Host tests prove the *policy and arithmetic*; they do **not** prove physical correctness. The candidate has not been deployed, and the physical origin of the ~129-sample latch offset remains unproven.

---

## 2. Scope, constraints, and explicit non-claims

### 2.1 In scope

- Diagnose the 50 µs/div rising-edge failure from preserved evidence and source.
- Implement a source fix that does not rely on tuning thresholds or enlarging a blind search window.
- Preserve B2, B-REC, ETS integrity, existing acquisition behaviour, and flash safety protections.
- Make per-capture diagnostics coherent so hardware trigger events can be correlated with frames.
- Prove the policy with host tests; document exact before/after architecture, candidate identity, resource use, residual uncertainty, and the deployment/validation procedure.

### 2.2 Out of scope

- Any physical instrument interaction. The instrument remains on build 14139.
- Claiming hardware validation, physical correctness, or that the ~129-sample latch offset has been explained.
- Weakening the B4.34 acceptance criteria.

### 2.3 Non-claims

1. Host tests exercise pure logic on synthetic/sampled rings. They cannot reproduce ADC analog behaviour, AWD interrupt latency, DMA/timer phase, or the physical signal source.
2. The fix makes alignment **robust to** a late latch. It does **not** remove the underlying latch delay.
3. No statement in this report should be read as “the instrument now passes the 50 µs rising criterion”. That requires the campaign in §14.

---

## 3. Evidence baseline and integrity

### 3.1 Hashes (re-verified this session)

| Artifact | SHA-256 |
|---|---|
| 14139 recovery image (64 KiB) | `e2bd129fd044f44246e9c5a85547d92f2559f6b25ee72ced4d7fe0eff09949f8` |
| 48462 candidate bin (failed) | `85610ebc07382b00b1cc13737e503d61191ae3461f75be91fbb546c41fffee81` |
| 48462 candidate ELF | `05b739710f0dff43e3a40460f528219d4cfbf190aecb53d7f61499453fbca906` |
| **41320 candidate bin** | `bfa6e7bf8bf681d202dba6267b5b8443d6ccd7fd2a2e234d9f1b0b7cc40e7efd` |
| **41320 candidate ELF** | `7d2f6d1eddd82f59505c0a55e1345bdf10b2dd584230e01829ca084be3166d87` |
| Candidate map | `6fd079ef90eb915cf95fae696a990902f6248db4811865f4eecda688e33cb719` |

The candidate copies under `evidence/B4_36_TRIGGER_FIX_AND_VALIDATION_20261114/candidate/` are byte-identical to `build/dso183.bin` and `build/dso183.elf`.

### 3.2 Evidence used

- `evidence/B4_34_DEPLOY_48462_20261114T013600Z/validation/remote_collected/b434_validate_48462/captures.jsonl` — 646 records, 600 captures, 12 configs × 50.
- `evidence/B4_33_PROGRAMME_20261114T035819Z/stage_a_trigger/hw_correlation_20261114T064528Z/raw/cap_tb5_auto_rise_000.json` — representative failing frame (fall at 286).
- `evidence/B4_33_PROGRAMME_20261114T035819Z/.../captures.jsonl` — historical 12066/12639 signature.
- `docs/trigger_50us_validation.md` — threshold sweep (140/140 rising captures at fall ≈287).
- B4.34 rollback evidence — 14139 readback hash and UART identity.

All historical evidence is preserved unmodified.

---

## 4. Before: the trigger path as implemented (48462 / 14139 lineage)

```
requested slope/level
        │
        ▼
one-sided AWD band (LTR/HTR)  ──► AWD is a LEVEL detector
        │                        (first sample outside the band)
        ▼
AWD interrupt: one-shot disarm, latch TIM3->CNT and
CNDTR-derived ring index  ──────────────► gate.trigger_index
        │
        ▼
post-fill: wait post_samples (224) ring advance, then freeze
        │
        ▼
trig_refine(): scan [latch−128, latch+48) for nearest Schmitt
edge of the requested slope; if none, simple-threshold crossing
in the SAME window; if still none, found=0
        │
        ▼
publication: frame_build(anchor = refined ? refined : latch)
```

The defect is the last line: **the latch index was treated as the edge index.** The refinement only corrected a small ADC-to-ISR latency; it had no way to express “the latch is late but the edge is behind the search window”. When the local pass failed, the caller fell back to the raw latch index and published it. On 50 µs/div rising that index is mid-plateau, so the frame begins ~33 samples after the true edge, contains only the high plateau and the next falling edge, and places the requested rising edge *outside the frame* — a deterministic, repeatable symptom.

---

## 5. Failure geometry — proof from preserved frames

### 5.1 Window equation

For a published frame:

```
frame[i] = ring[anchor − BENCH_PRE + i]        BENCH_PRE = 96
```

If the true rising edge is at ring position `R` and the true falling edge at `R + 320` (tb5, 640-sample period), the falling crossing appears at

```
i_fall = (R + 320) − (anchor − 96) = 416 − (anchor − R)
```

so

```
anchor − R = 416 − i_fall
```

| Observed `i_fall` | Implied latch offset `anchor − R` | Local window start | Edge position relative to window |
|---|---|---|---|
| 287 | **+129** | `R + 1` | **1 sample before the window** |
| 286 | **+130** | `R + 2` | **2 samples before the window** |
| 285 | **+131** | `R + 3` | **3 samples before the window** |

A rising crossing is detected at the *first high sample*, using the preceding low sample. With the window starting at `R+1`, `R+2`, or `R+3`, the transition sample `R` (and, for the later starts, the following high samples) is not in the scan, so neither the Schmitt scan nor the simple-threshold fallback can produce a crossing. The forward extent reaches only `R+177`, i.e. it cannot reach the next rising edge at `R+640`. **Both passes therefore fail by construction** — no threshold adjustment can help, which is exactly what the 140/140 threshold sweep showed.

### 5.2 Complete B4.34 result set (600 captures, 48462)

| Config | Captures | Rising crossings | Falling crossings | Placement |
|---|---|---|---|---|
| tb5_auto_rise | 50 | 0 | 50 | fall at 285–286 (mean 285.94, σ 0.237) — **FAIL** |
| tb5_norm_rise | 50 | 0 | 50 | fall at 286 (σ 0) — **FAIL** |
| tb5_auto_fall | 50 | 0 | 50 | fall at 96 — pass |
| tb5_norm_fall | 50 | 0 | 50 | fall at 96 — pass |
| tb6_auto_rise | 50 | 50 | 50 | rise at 96 — pass |
| tb6_norm_rise | 50 | 50 | 50 | rise at 96 — pass |
| tb6_auto_fall | 50 | 50 | 50 | fall at 96 — pass |
| tb6_norm_fall | 50 | 50 | 50 | fall at 96 — pass |
| tb9_auto_rise | 50 | 50 | 50 | rise at 96 — pass |
| tb9_norm_rise | 50 | 50 | 50 | rise at 96 — pass |
| tb9_auto_fall | 50 | 50 | 50 | fall at 96 — pass |
| tb9_norm_fall | 50 | 50 | 50 | fall at 96 — pass |

Only the tb5 rising case fails, and it fails unanimously. Falling at the same timebase is exact, so the AWD, DMA, timer, and frame path are intact. The asymmetry is explained entirely by where the latch lands relative to the requested edge and the fixed 128-sample backward search.

### 5.3 Alternatives excluded by this geometry

| Hypothesis | Status | Reason |
|---|---|---|
| Threshold too high/low | Excluded | Levels 891, 948, 968, 988, 1045 all produced fall ≈287 (140/140). |
| Slope inversion in the AWD band | Excluded | tb5 falling and tb6/tb9 rising are correct; the band code is polarity-consistent. |
| Ring overwrite / DMA splice | Excluded | Measured tb5 span ~640 < the 928 publishability bound, window contiguous, single crossing. |
| NORM publishing untriggered frames | Excluded | NORM publishes only on `triggered=1`; `frame_seq` advances ~99 per capture. |
| Arming mid-high (48462 quiescent arm) | Excluded as origin | Same signature pre-exists in builds 12066, 12639 and seq 25505. |
| **Anchor = late AWD latch** | **Supported** | Fall 285–287 ⇔ latch 129–131 after the edge; local window starts 1–3 samples too late. |

---

## 6. Root-cause disposition

### 6.1 Proven at code/geometry level

> The 50 µs rising failure is caused by publishing the raw AWD latch index when the software edge search cannot reach the true edge. At this signal/timebase the latch is 129–131 samples after the rising edge, so the historic `[latch−128, latch+48)` window excludes the edge by 1–3 samples and both the Schmitt and simple-threshold passes fail.

This is a **software anchoring defect** (the code assumed the latch is the edge), exposed by a late latch.

### 6.2 Not proven

> **Why** the AWD/ISR path latches ~129 samples after the edge at tb5 rising remains unproven. H-A (AWD interrupt / latch delay interacting with timer phase) remains the only surviving candidate; the physical source cannot be established without instrumented hardware.

The fix is deliberately designed so that correctness does not depend on resolving that question: it verifies the edge on the captured ring and bounds the recovery by the measured span.

---

## 7. After: anchoring architecture (B4.36)

```
requested slope/level
        │
        ▼
AWD latch  ──► gate.trigger_index, gate.post_span, gate.span_valid
        │
        ▼
trig_anchor()
  ├─ 1. LOCAL pass   [latch−128, latch+48)
  │      Schmitt nearest, then the historic simple-threshold
  │      fallback. Healthy latch ⇒ bit-identical result to pre-B4.36.
  │
  ├─ 2. RECOVERY pass  (only if local found nothing)
  │      [latch−recover_back, latch+48)
  │      recover_back = max(0, min(512, ring−1−pre−post_span))
  │      Schmitt only; nearest edge wins.
  │      Disabled on ETS ranges (recover_back = 0).
  │
  ▼
effective span = post_span − offset        (offset < 0 ⇒ span grows)
        │
        ▼
acq_win_span_ok(effective, 1024, 96, 224)?
        │
   ┌────┴────┐
   ▼         ▼
publish    reject
           ├─ NORM: hold last good frame
           ├─ SINGLE: re-arm
           └─ AUTO: show unaligned tail, keep refreshing,
                    re-centre level if it no longer intersects
```

### 7.1 Local pass — behaviour parity

- `ACQ_ANCHOR_LOCAL_BACK = 128`, `ACQ_ANCHOR_LOCAL_FWD = 48`.
- The scan covers `latch−128 … latch+47`, centre = `latch`, exactly the pre-B4.36 window.
- If the Schmitt scan fails, the same simple-threshold nearest crossing as before is tried.
- A healthy latch (offset 0–2) therefore produces **the same anchor index and offset as the old code**; the fix only changes the failure path.

### 7.2 Recovery pass — bounded and deliberate

- Runs only when the local pass produced no edge.
- `recover_back = max(0, min(ACQ_ANCHOR_RECOVER_MAX=512, ring_size−1−BENCH_PRE−post_span))`.
- The span-derived bound is what keeps the recovered edge publishable: moving the anchor `d` samples earlier increases the effective span by `d`, and `acq_win_span_ok` requires `effective_span + 96 < 1024`.
- The pass uses the **Schmitt state machine only**. A late latch must be anchored on a hysteresis-qualified edge, not on a single noisy sample.
- The nearest edge to the latch wins (the most recent real edge before it, within the same ±48-sample forward extent).
- On ETS ranges `recover_back = 0`, so the association window stays the historic local window and ETS acceptance behaviour is bit-identical.

### 7.3 Span validation and publication gate

For a triggered, non-ETS capture the frame is publishable only if **all** hold:

1. a requested-slope edge was found (`verified`);
2. the capture has a measured span (`span_valid`);
3. `acq_win_span_ok(effective_span, ring_size, 96, 224)` is true, i.e.
   `effective_span >= 224` (post-fill complete) and `effective_span + 96 < 1024` (pre-trigger samples not overwritten).

Otherwise the capture is **recorded in diagnostics and not published**. This is the key behavioural change: a mid-plateau index can no longer reach the display, the measurement cache, wave store, or stream as if it were an aligned frame.

### 7.4 Mode policy on rejection

| Mode | On rejection |
|---|---|
| NORM | Hold the last good frame; diagnostics record `fallback=3/4`. |
| SINGLE | Re-arm for the next press; nothing is published. |
| AUTO | Publish an unaligned tail window so the display keeps refreshing; the existing AUTO re-centre can move a level that no longer intersects the signal. |
| ETS | Unchanged: the ETS path builds its own window and its own acceptance check decides. Recovery is disabled. |

### 7.5 Supporting robustness changes

- **ISR index masking:** `idx = (ACQ_SAMPLES − CNDTR) % ACQ_SAMPLES` in the ISR. Previously `CNDTR == 0` could yield 1024, an out-of-range ring index (rare off-by-one).
- **Tight post-fill polling:** when the post-fill need is ≤2 ms (`ACQ_POST_FILL_SPIN_NS`), poll CNDTR 64 times per tick instead of sleeping a whole millisecond. At tb5 a 1 ms sleep overshot the 224 requested samples by ~640, leaving almost no pre-trigger margin and making the span unpredictable.
- **Freeze without a blind sleep:** drain `ADC_SR_EOC` after gating DMA, then require CNDTR to be stable for 3 consecutive reads (bounded spins). Every snapshot is taken at the same transfer phase.
- **Measured span:** `post_span = acq_ring_advance(s_trigger_index, s_snap_pos, 1024)` and `span_valid = 1` only for triggered captures. This is the span of the snapshot that was actually copied, not a re-derivation from a later CNDTR.

### 7.6 Compatibility with B2, B-REC, ETS, and flash

| Area | Guarantee |
|---|---|
| B2 (ETS Stage B2 contract) | ETS placement and association use the same local window, the same slope/level inputs, and the same sample-index semantics as before; C1–C5 behaviour is unchanged. Recovery is disabled on ETS ranges (`recover_back = 0`), so no sweep can be moved to a different period. |
| B-REC (AC-4 recording policy) | No acceptance criterion, coverage gate, or classification is changed. Host results in this report are explicitly separate from hardware results; the physical campaign in §14 keeps the B2/B-REC gate and does not apply a PASS/FAIL where coverage is insufficient. |
| Wave store / stream / measurements | Unchanged interfaces and formulas. Measurements still come from `s_frame` after publication. A rejected capture never publishes `s_frame`, so a mid-plateau frame cannot be saved, streamed, or measured as if it were aligned. |
| Flash layout / recovery | No linker, section, or protected-page change. The image is 40,960 B, ends at `0x0800A000`, and remains far below the protected calibration page at `0x0800FC00`. |
| Instrument state | The candidate was not deployed. Build 14139 and all historical evidence remain intact. |

---

## 8. Implementation map

| File | Change |
|---|---|
| `app/acq_anchor.h` | **New.** Policy contract, `ACQ_ANCHOR_LOCAL_BACK/FWD`, `ACQ_ANCHOR_RECOVER_MAX`, `struct acq_anchor_cfg`, `struct acq_anchor`, `acq_anchor_pick()`, `acq_anchor_effective_span()`. |
| `app/acq_anchor.c` | **New.** Local pass (Schmitt + historic simple fallback), recovery pass (Schmitt only), wrap-safe ring arithmetic, effective-span clamp. |
| `app/trigger.h/.c` | **New `trig_scan_ring_nearest()`.** Ring-direct nearest crossing scan; no scratch copy, wrap-safe, search length not limited by the old 512-entry buffer. |
| `app/acq_window.h/.c` | **New `acq_win_span_ok()`.** Rejects zero windows and rings without one sample of slack; encodes “post complete” and “pre not overwritten”. |
| `app/acq.h` | `struct acq_gate` gained `post_span` and `span_valid`; post-fill/freeze constants (`ACQ_POST_FILL_SPIN_NS 2000000`, `ACQ_POST_FILL_SPIN_ITERS 64`, `ACQ_FREEZE_DRAIN_SPINS 64`, `ACQ_FREEZE_STABLE_SPINS 64`, `ACQ_FREEZE_STABLE_READS 3`). |
| `app/acq.c` | ISR index reduced mod `ACQ_SAMPLES`; tight CNDTR polling for short post-fill; freeze drains EOC and proves CNDTR stability; measures `post_span`; zeroes the new fields on every early-return path. |
| `app/main.c` | `trig_refine` replaced by `trig_anchor`; recovery bound computed as `max(0, min(512, ring−1−pre−post_span))` and 0 on ETS; verification/span publication gate; AUTO/NORM/SINGLE rejection policy; single `struct acq_diag` plus latched `s_diag_pub`; `GETV_DIAG_LATCH`; all diag GETVs read the latched copy; new GETVs 47–50; ETS association check uses the anchor constants. |
| `app/link_cmd.h` | GETV 46–50, `GETV_COUNT = 51`; fallback/anchor-kind documentation. |
| `tools/remote_proto.py` | Mirrors GETV 46–50 and `GETV_COUNT = 51`; offline self-tests updated. |
| `tests/test_acq_anchor.c` | **New.** 82 assertions (see §10.2). |
| `tests/test_trigger.c` | Ring-scan tests; campaign reproduction (fall 287 ⇔ +129, fall 286 ⇔ +130). |
| `tests/test_acq_window.c` | Span-guard boundaries and degenerate arguments. |
| `tests/test_ets_diag.c` | GETV indices 46–50 valid, 51 invalid, `GETV_COUNT = 51`. |

Design note: `s_acq_scratch.trig_search[512]` is no longer used for anchoring (the ring is scanned in place). It remains the ETS transient buffer, so RAM was not reclaimed and no ETS alias contract changed.

---

## 9. Coherent per-capture diagnostics

### 9.1 Why the old protocol was structurally torn

A capture completes in roughly 100 ms at the campaign rate. Each `GETV` request/response costs roughly 110 ms at 115200 baud. Reading 11 fields sequentially therefore spans ~1.2 s — many captures — so fields could belong to different acquisitions. The B4.34 campaign recorded this as `diag_snapshot_ok=false`, `seq_mismatch` in 600/600 captures and could not correlate hardware state with frames.

### 9.2 New protocol

1. Send **`GETV 46` (`GETV_DIAG_LATCH`)**. This copies the live `s_diag` struct into `s_diag_pub` and returns its `seq`.
2. Read the fields. Every one comes from that single latched copy:
   - 18 `seq`
   - 19 `gate_triggered`
   - 20 `hw_index`
   - 21 `anchor_index`
   - 22 `refined_found`
   - 23 `fallback`
   - 24 `timebase`
   - 25 `trig_slope`
   - 26 `trig_level`
   - 27 `trig_mode`
   - 45 `hw_count`
   - 47 `post_span`
   - 48 `span_valid`
   - 49 `anchor_kind`
   - 50 `recover_back`
3. Field reads do not change the latched copy. A later capture updates only `s_diag`; the latched copy stays stable until the next `GETV 46`.

Both structs are main-loop objects; no ISR touches them, so the struct copy is atomic with respect to every reader.

### 9.3 Field semantics

| GETV | Field | Meaning |
|---|---|---|
| 19 | `gate_triggered` | 1 if the AWD fired for this capture. |
| 20 | `hw_index` | Raw `gate.trigger_index` (ring index at ISR latch). |
| 21 | `anchor_index` | Ring index finally used (verified edge, HW index, or tail). |
| 22 | `refined_found` | 1 if a requested-slope edge was verified. |
| 23 | `fallback` | 0 verified edge, 1 HW index (ETS path), 2 tail/untriggered, 3 span-invalid, **4 no-edge** (3/4 = rejected). |
| 45 | `hw_count` | Raw TIM3 count at latch (latency marker, not a phase). |
| 47 | `post_span` | Measured ring advance latch → frozen snapshot (samples, mod 1024). |
| 48 | `span_valid` | 1 if 47 is a measurement for a triggered capture. |
| 49 | `anchor_kind` | 0 local pass, 1 recovery pass, 2 HW index, 3 none. |
| 50 | `recover_back` | Recovery bound offered to the policy (0 on ETS). |

### 9.4 Deriving the anchor offset and effective span

```
d_mod  = (anchor_index − hw_index) mod 1024
offset = d_mod            if d_mod <= 512
         d_mod − 1024     otherwise
effective_span = post_span − offset        (offset < 0 ⇒ span increases)
```

Example (expected tb5 rising, post_span ≈ 230): anchor = hw − 129 ⇒ offset = −129 ⇒ effective span ≈ 359. The publication gate then checks `359 + 96 < 1024`.

The latch→snapshot span is a modulo-1024 ring advance, as it was implicitly before. At tb5 the ring is replaced in ~1.6 ms and the post-fill target is 224 samples, so the measured value is small and unambiguous.

---

## 10. Host validation

### 10.1 Run

```
make test
```

Full log: `evidence/B4_36_TRIGGER_FIX_AND_VALIDATION_20261114/host_tests.log`.

Result: **36 suites passed, 0 failures**, exit status 0. Across `tests/test_*.c` there are **1,318 CHECK sites**.

### 10.2 New anchoring coverage (`tests/test_acq_anchor.c`, 82 assertions)

| Test | What it proves |
|---|---|
| `test_tb5_late_rising_latch` | Latch at +129 anchors to the true edge (`recovered=1`, offset −129); effective span 769 is publishable; end-to-end `frame_build` places the edge at trigger column 96. |
| (same test, +130/+131) | The campaign's fall-at-286 and fall-at-285 geometries (offsets −130 and −131) are recovered identically. |
| `test_tb5_late_falling_latch` | Falling polarity: latch +129 after the falling edge anchors to it; frame high before 96, low at 96. |
| `test_healthy_latch_is_unchanged` | Latch at +1 and at 0 anchors with `recovered=0`, exactly the pre-B4.36 index. |
| `test_recovery_bound_and_disable` | Too-short bound (64) refuses; `recover_back <= local_back` (ETS) refuses; 130 is just enough to reach −129. |
| `test_no_edge_is_rejected` | DC and below-threshold signals return `found=0` rather than a fabricated edge. |
| `test_ring_wrap` | Latch 129 after the edge at 960 wraps correctly and anchors at 960. |
| `test_other_timebases` | tb6 (+100) and tb9 (+5) stay in the local pass; nearest crossing wins. |
| `test_effective_span` | Clamp low/high and sign behaviour of `acq_anchor_effective_span`. |
| `test_invalid_arguments` | NULL/short/out-of-range inputs return 0 and leave the output untouched. |

### 10.3 Regression-characterisation coverage

- `tests/test_trigger.c` builds the 640-sample square wave and reproduces **both** observed failure phases:
  - latching at `rise+129` publishes `frame[287]` falling (campaign `tb5_auto_rise`, fall 285–286);
  - latching at `rise+130` publishes `frame[286]` falling (B4.33, fall 286).
  Both frames have `rise_count=0` and `fall_count=1`, matching the recorded symptom.
- `tests/test_trigger.c` also covers `trig_scan_ring_nearest`: wrapped segments, centre selection, falling slope, `start ≥ ring_size` reduction, no-crossing, NULL/zero/one-sample/slope-invalid rejection.
- `tests/test_acq_window.c` covers `acq_win_span_ok`: exact minimum span, incomplete post-fill, last legal span (927), overwritten pre-trigger (928), zero window, zero buffer, ring with no slack, and the last legal span for a tiny ring.
- `tests/test_ets_diag.c` covers the append-only GETV contract: 0–50 valid, 51 invalid, and the exact values 46–50.
- `tools/remote_proto.py` offline self-tests pass with `GETV_COUNT = 51`.

### 10.4 What the host tests do **not** cover

- ADC analog behaviour, AWD threshold crossing, interrupt latency, TIM3 phase, DMA wrap timing, UART timing.
- The physical existence or magnitude of the ~129-sample latch offset.
- The real signal source, probe, front end, or calibration.
- ETS electrical behaviour (only the logic/acceptance path is exercised).

---

## 11. Candidate identity and resource budget

```
build_seq: 41320
bin: 40,960 B
text 40,820   data 136   bss 18,544
flash 40,956 B / 61,440 B  (66%)
RAM   18,680 B / 20,480 B  (91%)
```

| Metric | 48462 (before) | 41320 (after) | Delta |
|---|---|---|---|
| text | 40,032 | 40,820 | +788 |
| data | 136 | 136 | 0 |
| bss | 18,512 | 18,544 | +32 |
| flash (text+data) | 40,168 | 40,956 | +788 |
| RAM (data+bss) | 18,648 | 18,680 | +32 |
| bin length | 40,172 | 40,960 | +788 |

- Flash headroom to the 60 KiB code budget: **20,484 B**.
- RAM headroom: **1,800 B** unallocated. The linker's reserved 1 KiB heap and 2 KiB main stack are already inside the bss figure; the 1,800 B is the remaining margin, not a claim of 1,800 B of free heap.
- The build emits one warning: `wait_trigger_rearm defined but not used`. This is a pre-existing intentional compatibility wrapper unrelated to this fix.

---

## 12. Risks and residual uncertainties

1. **The ~129-sample latch offset is unexplained.** The fix tolerates it up to the recovery bound. If the offset ever exceeds the bound (e.g. a much slower signal or a different failure mode), the local and recovery passes both fail and the frame is **rejected**, not misaligned. NORM will then hold the last frame; diagnostics will show `fallback=4` and `anchor_kind=3`.
2. **Recovery ambiguity when the signal period is shorter than the recovery window.** Recovery takes the nearest requested-slope edge to the latch. If the latch were more than one period late, the “nearest” edge could be one period early. That is only possible if the latch delay exceeds a full period, which is far outside the observed few-hundred-sample offset; at tb5 the 512-sample cap is shorter than the 640-sample period, so at most one rising edge can be in the recovery region. The physical campaign must confirm `anchor_kind=1` frames place the edge at 96.
3. **ETS recovery is disabled by design.** ETS still relies on the bounded local association. If ETS ever shows the same late-latch signature, it will reject sweeps rather than accept a wrong-period edge — safe, but it would need a separate fix.
4. **AUTO rejection displays an unaligned tail.** This is intentional: it keeps the display refreshing and lets AUTO re-centre a level that no longer intersects the signal. It is distinguishable from a valid capture by `fallback=4`/`anchor_kind=3`.
5. **Span measurement is modulo 1024.** If a capture ever spanned more than one ring lap, `post_span` would alias. At tb5 the ring is replaced in 1.6 ms and post-fill targets 224 samples; with the tight poll the measured span is expected in the low hundreds. The same modulo assumption existed before B4.36.
6. **91% RAM.** The fix costs only 32 B, but there is little room for further features without reclaiming the ETS scratch buffer or other storage.
7. **Host tests are not hardware validation.** See §10.4 and §16.

---

## 13. Safeguarded deployment procedure (for a future operator — not executed here)

### 13.1 Preconditions

1. Instrument is on verified build 14139 (`BUILD_SEQ 14139`, `STATUS 1`).
2. Recovery image hash matches `e2bd129fd044f44246e9c5a85547d92f2559f6b25ee72ced4d7fe0eff09949f8`.
3. Candidate hashes match §3.1 and the bin is 40,960 B.
4. Protected flash page `0x0800FC00` contains the calibration store and must never be written.
5. Use the project flash tool only. Do **not** use a raw `st-flash write`.

### 13.2 Deploy

```
python3 tools/flash_cycle.py \
  --image evidence/B4_36_TRIGGER_FIX_AND_VALIDATION_20261114/candidate/dso183.bin \
  --elf   evidence/B4_36_TRIGGER_FIX_AND_VALIDATION_20261114/candidate/dso183.elf
```

`flash_cycle.py` erases/programs pages, verifies each page on readback, writes page 0 **last**, reads the whole image back, queues a reset and releases the core, then reads the build identity from RAM and requires it to match the ELF. Because the image ends below `0x0800A000`, it cannot reach the protected calibration page.

### 13.3 Post-deploy identity gate

- UART `BUILD_SEQ` must report **41320**.
- `STATUS` must report `1` (acquisition healthy).
- A short stream toggle and one ETS diagnostic snapshot must succeed.
- If any gate fails: stop, capture the error, and roll back.

### 13.4 Rollback

Restore the preserved 14139 image with the same tool, then independently verify:
- 64 KiB readback SHA-256 equals `e2bd129f…9949f8`;
- UART `BUILD_SEQ 14139`, `STATUS 1`.

Keep the readback file in a new evidence directory; do not overwrite B4.34 rollback evidence.

---

## 14. Physical validation procedure and acceptance criteria

### 14.1 Setup

- 1 kHz, 50% square wave (TIM4-derived source used in B4.33/B4.34, same 64 MHz domain as SysTick).
- Trigger level 970, hysteresis 8 (unless the test explicitly sweeps levels).
- Timebases tb5, tb6, tb9.
- Modes AUTO and NORM; slopes rising and falling.

### 14.2 Campaign

Run the B4.34 12-configuration matrix, 50 captures per configuration (600 captures). For each capture:

1. Record `frame_seq` before and after.
2. Send `GETV 46`, then read 18–27, 45, 47–50.
3. Require the latched `seq` to be consistent with the capture bracket; if not, re-latch.
4. Record frame crossings and the requested-edge placement.

Use the B4.36 `remote_proto.py` (GETV_COUNT 51), not the B4.34 copy.

### 14.3 Primary acceptance criterion (unchanged)

For **tb5 rising**, tested in **both AUTO and NORM**:

> At least **95% of valid captures** must contain a requested **rising** crossing within **±2 samples of index 96**.

This is the B4.34 criterion verbatim; it is not weakened here, and the same 50-capture sample size per mode applies.

### 14.4 Regression criteria

| Check | Criterion |
|---|---|
| tb5 falling AUTO/NORM | Requested falling crossing within ±2 of 96 in ≥95% (historically 100%). |
| tb6/tb9 rising and falling | Same, both modes. |
| Frame errors / missing frames | 0 unexpected errors; no repeated timeouts. |
| Diagnostics coherence | 100% of latched snapshots internally consistent; no `seq` mismatch in the bracket. |
| Rejected captures | `fallback=3/4` may occur but must be reported; for a healthy 1 kHz signal at these timebases it should be 0. |
| ETS | Accepted-sweep behaviour and `ready` unchanged; no rise in `reject_unbound`. |
| Display/stream/wave store | Functional checks pass; no visible mid-plateau frame. |

### 14.5 Expected diagnostic signature on tb5 rising (falsifiable prediction)

| Field | Expected |
|---|---|
| 19 `gate_triggered` | 1 |
| 22 `refined_found` | 1 |
| 23 `fallback` | 0 |
| 49 `anchor_kind` | **1 (recovery)** |
| 50 `recover_back` | 287–512 (512 unless `post_span` was large) |
| 48 `span_valid` | 1 |
| 47 `post_span` | ~224–300 with tight polling (previously ~640 from the 1 ms sleep) |
| 20 vs 21 | `(hw − anchor) mod 1024 ≈ 129–131` |
| Effective span | `post_span + 129…131`, well under 928 |

If `anchor_kind` is 0 (local) on tb5 rising, the latch is no longer late — record it; the fix still works. If `fallback=4` appears, the recovery bound did not reach the edge — capture `post_span`, `recover_back`, and the ring around the latch before drawing conclusions.

### 14.6 Decision

- **Pass:** primary + regression criteria met, diagnostics coherent, no rejected frames on the healthy signal → candidate is acceptable for the programme.
- **Fail:** keep the evidence, roll back to 14139, and reopen the investigation with the new coherent diagnostics. Do not tune the acceptance criteria.

---

## 15. Evidence index

| Path | Contents |
|---|---|
| `evidence/B4_36_TRIGGER_FIX_AND_VALIDATION_20261114/build_identity.txt` | Hashes, sizes, size report, bin length. |
| `evidence/B4_36_TRIGGER_FIX_AND_VALIDATION_20261114/host_tests.log` | Full `make test` output. |
| `evidence/B4_36_TRIGGER_FIX_AND_VALIDATION_20261114/candidate/` | `dso183.bin`, `dso183.elf`, `dso183.map`, `build_seq.h`. |
| `evidence/B4_34_DEPLOY_48462_20261114T013600Z/` | Failed 48462 deployment, 600-capture dataset, preflight binaries. |
| `evidence/B4_34_ROLLBACK_14139_20261115T034700Z/` | Verified rollback readback. |
| `evidence/B4_33_PROGRAMME_20261114T035819Z/` | Historical signature and representative failing frame. |
| `evidence/stage1_ets_campaign_20261113T033944Z/B4_29_CONTROLLED_DEPLOYMENT/preflash_recovery/installed_14139_flash_0x08000000_len0x10000.bin` | Preserved 14139 recovery image (unmodified). |

Reproduce the build and tests:

```
make clean && make
make test
sha256sum build/dso183.bin build/dso183.elf
sh tools/size_report.sh build/dso183.elf
python3 tools/remote_proto.py
```

Note: `build_seq` is derived from source content. A clean rebuild from the exact reported tree is expected to reproduce the hashes; if the digest inputs change, the build sequence (and therefore the binary) will change even though the behaviour does not.

---

## 16. Final disposition

**`FIX IMPLEMENTED — HOST VALIDATED — HARDWARE VALIDATION PENDING`**

- The software defect is proven: the raw AWD latch index was published as the edge, and at 50 µs/div rising the latch is 129–131 samples late, outside the historic 128-sample window.
- The fix anchors on a verified requested-slope edge with a span-bounded recovery pass, validates the effective span, and rejects rather than publishes a mid-plateau frame. Healthy latches behave exactly as before; ETS recovery is disabled; NORM/AUTO/SINGLE have explicit, documented rejection policies.
- 36 host suites / 1,318 CHECK sites pass, including the exact measured failure geometry in all three observed phases.
- Candidate **41320** is built, hashed, and preserved. It is **not deployed**. The instrument remains on verified build **14139**.
- **Host tests do not prove physical correctness.** The hardware campaign in §14 is required before any claim that the 50 µs rising-edge criterion passes.
