# B4.34 Deployment and Hardware Validation Report — Candidate 48462

**Date (UTC):** 2026-11-15 (validation completed 2026-11-15T03:31:32Z per remote DONE_MARKER)  
**Candidate:** `48462` (quiescent deterministic arming, A1) — `build/dso183.bin` 40,172 B, SHA-256 `85610ebc07382b00b1cc13737e503d61191ae3461f75be91fbb546c41fffee81`, ELF `05b739710f0dff43e3a40460f528219d4cfbf190aecb53d7f61499453fbca906`  
**Instrument access:** Pi `benabus@192.168.1.201`, ST-LINK `12010C1C8418303030303032`, UART `/dev/ttyUSB0`  
**Scope:** collect complete B4.34 validation evidence for deployed 48462, analyze per acceptance criteria, and (if required by criteria) perform controlled rollback to verified 14139. No firmware source modified.

## 1. Evidence integrity and collection (additive, preserved)
- **Raw remote evidence preserved (not overwritten):** `evidence/B4_34_DEPLOY_48462_20261114T013600Z/validation/remote_collected/b434_validate_48462/` (tar-copied from `/tmp/b434_validate_48462`).
  - `captures.jsonl` (646 records; SHA-256 `ac57037d2f5aac0a855f24288ebb6d88651d7d24fe80af38d2e50df5c3f9c6b9`), `campaign.log` (`73cbeb64…ed78`), `functional_checks.json`, `identity.json`, `signal_characterization.json`, `summaries_index.json`, `DONE_MARKER`, 12× `summary_*.json`, `raw/`.
- **Local run capture:** `evidence/B4_34_DEPLOY_48462_20261114T013600Z/validation/run.stdout.log` (partial early segment only; remote `campaign.log` is the authoritative run log). Script: `validation/validate_48462.py`. Poll/progress notes: `validation/poll*.log`.
- **Pre-deploy/recovery evidence:** `evidence/B4_34_HW_GATE_20261114T005319Z/` (failed gate readback `37001977…d08cd`), `evidence/B4_34_RECOVERY_20261114T010000Z/` (14139 recovery scripts/logs, `post_recovery_readback.bin` SHA-256 `e2bd129fd044f44246e9c5a85547d92f2559f6b25ee72ced4d7fe0eff09949f8`), `evidence/B4_34_DEPLOY_48462_20261114T013600Z/{preflight,deploy,readback}/` (deploy log, candidate hashes).
- **Rollback reference image:** `evidence/stage1_ets_campaign_20261113T033944Z/B4_29_CONTROLLED_DEPLOYMENT/preflash_recovery/installed_14139_flash_0x08000000_len0x10000.bin` (SHA-256 `e2bd129f…49f8`, 65,536 B).

## 2. Safety gate, recovery, and deployment (verified)
- **Initial safety gate (FAIL/STOP):** `flash_cycle.py` not running (PASS), but independent 64 KiB readback `3700197741d74d6f3c18199bee124f25dbd8e377224f9aa5e9d8ece9b8ad08cd` != recovery `e2bd129f…49f8` and UART unresponsive (`SerialError` reply timeout). No reset/halt/flash/disruptive attach beyond read-only readback.
- **Controlled recovery to 14139 (verified):** first attempt refused protected page `0x0800FC00` (correct). `recover_14139_v2.py` programmed all non-protected pages with **page 0 written last**, per-page + whole-image verify, protected page preserved, reset+core released. Post-recovery independent 64 KiB readback == **`e2bd129f…49f8`**; UART **`BUILD_SEQ 14139`, `STATUS 1`**.
- **48462 deployment (approved `tools/flash_cycle.py`, verified):** page 0 last, per-page+whole-image verify, reset+release, RAM identity/build_seq/magic match. Flash 65%, RAM 91% (~1832 B headroom). Independent readback (40,172 B) == `85610ebc…ee81`; UART **`BUILD_SEQ 48462`, `STATUS 1`**.

## 3. Validation run (complete)
- **Matrix:** 12 configs × 50 captures = 600 captures (all completed). Configs: `tb5_auto_rise`, `tb5_norm_rise`, `tb5_auto_fall`, `tb5_norm_fall`, `tb6_auto_rise`, `tb6_norm_rise`, `tb6_auto_fall`, `tb6_norm_fall`, `tb9_auto_rise`, `tb9_norm_rise`, `tb9_auto_fall`, `tb9_norm_fall` (tb=5→50 µs, tb=6→0.1 ms, tb=9→1 ms; mode 0=AUTO, 1=NORM; slope 0=rise, 1=fall; `level_req=970`).
- **Completion:** remote `DONE_MARKER` `done 2026-11-15T03:31:32Z`; all 12 per-config summaries and `summaries_index.json` present; `captures.jsonl` contains 600 capture records (+46 non-capture/meta records = 646 total). `campaign.log` shows all configs summarized through `tb9_norm_fall`.

### 3.1 Identity / BUILD_SEQ / STATUS
- `identity.json`: `build_seq=48462`, poll `{"11":48462,"4":1,...}` (STATUS=1). Confirmed at run start (`campaign.log`: `BUILD_SEQ=48462`).

### 3.2 Functional / protocol / stream / ETS checks
- `functional_checks.json`: `build_seq=48462`, `status=1`.
- **Stability:** tb=5 got 12 errs 0; tb=6 got 12 errs 0; tb=9 got 12 errs 0.
- **Stream toggles:** 5/5 cycles stream on → frame OK → stream off OK.
- **ETS diag snapshot (pre-capture check phase):** readable (`ets_diag_snapshot` populated, e.g. `11:48462`, `4:1`; `45:273`), indicating protocol/ETS read path functional at check time.

### 3.3 Signal characterization
- 10 sampled frames, n=320 each: min ~869–870, max ~1049–1051, mean ~1029.1–1030.8, Vpp 179–181; meas level/poll consistent (`0:970`, `11:48462`, `4:1`). Signal present and level region suitable for ~970 crossing tests.

## 4. Per-configuration results (all 12 configs, 50/50 captures each)

| Config | TB | Mode | Req edge | n | frame_ok | no_frame | coherent | diag_seq_mismatch | req crossing found | req within ±2 of 96 | rise crossings | fall crossings | placement (requested edge nearest idx 96) |
|---|---:|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| tb5_auto_rise | 5 (50 µs) | AUTO | rise | 50 | 50 | 0 | 0 | 50 | **0** | **0 (0.0%)** | 0 | 50 | **none** (fall-only at i≈286, mean 285.94) |
| tb5_norm_rise | 5 (50 µs) | NORM | rise | 50 | 50 | 0 | 0 | 50 | **0** | **0 (0.0%)** | 0 | 50 | **none** (fall-only at i=286.0) |
| tb5_auto_fall | 5 (50 µs) | AUTO | fall | 50 | 50 | 0 | 0 | 50 | 50 | 50 (100.0%) | 0 | 50 | i=96.00 (all) |
| tb5_norm_fall | 5 (50 µs) | NORM | fall | 50 | 50 | 0 | 0 | 50 | 50 | 50 (100.0%) | 0 | 50 | i=96.00 (all) |
| tb6_auto_rise | 6 (0.1 ms) | AUTO | rise | 50 | 50 | 0 | 0 | 50 | 50 | 50 (100.0%) | 50 | 50 | rise i=96.00 (fall ~256.02) |
| tb6_norm_rise | 6 (0.1 ms) | NORM | rise | 50 | 50 | 0 | 0 | 50 | 50 | 50 (100.0%) | 50 | 50 | rise i=96.00 (fall ~255.96) |
| tb6_auto_fall | 6 (0.1 ms) | AUTO | fall | 50 | 50 | 0 | 0 | 50 | 50 | 50 (100.0%) | 50 | 50 | fall i=96.00 (rise ~256.02) |
| tb6_norm_fall | 6 (0.1 ms) | NORM | fall | 50 | 50 | 0 | 0 | 50 | 50 | 50 (100.0%) | 50 | 50 | fall i=96.00 (rise ~256.0) |
| tb9_auto_rise | 9 (1 ms) | AUTO | rise | 50 | 50 | 0 | 0 | 50 | 50 | 50 (100.0%) | 50 | 50 | rise i=96.00 (fall mean ~83.2) |
| tb9_norm_rise | 9 (1 ms) | NORM | rise | 50 | 50 | 0 | 0 | 50 | 50 | 50 (100.0%) | 50 | 50 | rise i=96.00 (fall mean ~81.92) |
| tb9_auto_fall | 9 (1 ms) | AUTO | fall | 50 | 50 | 0 | 0 | 50 | 50 | 50 (100.0%) | 50 | 50 | fall i=96.00 (rise mean ~110.08) |
| tb9_norm_fall | 9 (1 ms) | NORM | fall | 50 | 50 | 0 | 0 | 50 | 50 | 50 (100.0%) | 50 | 50 | fall i=96.00 (rise mean ~107.52) |

**AUTO vs NORM:** no material behavioral difference observed between AUTO and NORM at the same TB/slope — both 50 µs rising configs fail identically (0% requested rising crossing, falling-only at ~286); all other 10 configs place the requested edge at index 96 in 100% of valid frames in both modes.

## 5. Analysis

### 5.1 Primary criterion — 50 µs rising (requested rising crossing within ±2 of index 96 in ≥95% valid captures)
- **tb5_auto_rise:** 0/50 (0.0%) valid captures meet criterion; **0/50 frames contain any rising crossing**; all 50 frames contain only a falling crossing near i≈286. **FAIL.**
- **tb5_norm_rise:** 0/50 (0.0%); 0 rising crossings, 50 falling-only at i=286. **FAIL.**
- **Combined 50 µs rising:** 0/100 (0.0%). Primary acceptance criterion is **not met**.

### 5.2 0.1 ms and 1 ms regressions
- **0.1 ms (tb6):** rising and falling, AUTO and NORM — requested edge at index 96 in 50/50 (100%) valid frames each; both edge types present in frames as expected. **No placement regression observed.**
- **1 ms (tb9):** rising and falling, AUTO and NORM — requested edge at index 96 in 50/50 (100%) valid frames each. **No placement regression observed.**

### 5.3 50 µs falling behavior
- tb5 AUTO/NORM falling: requested falling edge at index 96 in 50/50 (100%) valid frames (fall-only frames at this TB/sample view). Requested-edge placement for 50 µs **falling** is correct in captured frames; failure is specific to 50 µs **rising** request.

### 5.4 Diagnostic coherence failures and effect on conclusions
- **All 600/600 captures:** `diag_snapshot_ok=false`, `diag_snapshot_status="seq_mismatch"`, `diag={}`, `coherent=false`. No per-capture `no_frame`/frame errors (600/600 `frame_ok=true`).
- **Effect:** per-capture correlated hardware diagnostics (AWD/arming state, DMA/CNDTR/edge/hand-off internals, refinement internals tied to the same capture) are **unavailable**. Coherence between hardware trigger state and reported frame/refinement cannot be established from these capture records. Placement conclusions below are drawn from **frame content** (crossings present in captured buffers) rather than correlated HW diags.

### 5.5 Evidence distinguishing AWD/arming failure from later trigger handling/refinement
- **Observed (frame-level):** for 50 µs rising requests, frames contain **no rising crossing** and a consistent, far-off falling crossing (~286). If the correct rising edge had been detected/captured at the intended phase, the buffer should contain a rising crossing near center (≈96) even with imperfect refinement; refinement adjusts position but cannot explain absence of the requested edge type. This is **consistent with wrong-event/wrong-phase arming or capture alignment for rising at 50 µs** (trigger path not latching the requested rising transition correctly), **not** with “rising edge detected correctly at HW level but lost during refinement.”
- **Not provable from this dataset:** because all per-capture HW diagnostics are incoherent/empty (`seq_mismatch`), we **cannot directly confirm** the exact AWD interrupt firing point/slope/timing or internal arming state at capture time. The data supports a trigger/arming/capture-path failure for 50 µs rising but does **not** isolate the precise internal fault location (e.g. AWD firing on wrong edge vs arm timing/alignment) without correlated diagnostics.
- **Contrast:** 0.1/1 ms rising and all falling configs show correct requested-edge placement at index 96 in frame content, indicating the gross capture/display path and requested-edge placement are functional at those settings despite missing coherent per-capture diags.

## 6. Acceptance-criteria disposition (explicit)

| Criterion | Disposition | Basis |
|---|---|---|
| 50 µs rising meets primary acceptance: ≥95% valid captures with requested rising crossing within ±2 samples of index 96 (AUTO and/or as tested; both tested) | **FAIL** | tb5_auto_rise 0/50 (0%), tb5_norm_rise 0/50 (0%); no rising crossing present in any 50 µs rising frame (falling-only at ~286). |
| 50 µs falling behavior/placement | **PASS (frame placement, limited by diag incoherence)** | 50/50 AUTO and 50/50 NORM with requested falling crossing at i=96 in valid frames; no frame errors. Coherent HW attribution unavailable. |
| 0.1 ms (tb6) configurations not regressed | **PASS (frame placement, limited by diag incoherence)** | All four tb6 configs 50/50 with requested edge at i=96 in valid frames (100%). No placement regression observed. |
| 1 ms (tb9) configurations not regressed | **PASS (frame placement, limited by diag incoherence)** | All four tb9 configs 50/50 with requested edge at i=96 in valid frames (100%). No placement regression observed. |
| Identity (BUILD_SEQ/STATUS) | **PASS** | 48462 / STATUS 1 confirmed (identity, functional checks, deploy verification). |
| Functional/protocol/stream/ETS checks | **PASS (check-phase)** | Stability 12/12 errs 0 at tb5/6/9; 5/5 stream toggles OK; ETS diag snapshot readable in pre-capture checks. |
| Per-capture diagnostic coherence / HW-attributable trigger evidence | **FAIL / INCONCLUSIVE for HW attribution** | 600/600 `diag_seq_mismatch`, empty per-capture diag, 0 coherent captures; prevents direct AWD/arming-point confirmation, but does not negate frame-level placement findings. |

**Overall (primary objective):** **FAIL** — deployed 48462 does not fix the 50 µs rising-edge trigger defect (primary acceptance criterion not met). No regression observed in 0.1 ms/1 ms requested-edge placement or 50 µs falling placement in captured frames.

## 7. Rollback
- Because completed evidence confirms 50 µs rising fails the primary acceptance criterion, controlled rollback to verified build **14139** was required and executed per established plan.
- **Procedure used (safeguarded, no `st-flash write`, no alternative flashing):** `recover_14139_v2.py` (preserved copy + hashes in `evidence/B4_34_ROLLBACK_14139_20261115T034700Z/`) flashed recovery image `installed_14139_flash_0x08000000_len0x10000.bin` (SHA-256 `e2bd129fd044f44246e9c5a85547d92f2559f6b25ee72ced4d7fe0eff09949f8`, 65,536 B). Protected settings page **`0x0800FC00` preserved/not written** (current == recovery, `matches=True`), all other pages programmed with **page 0 written last**, per-page read-back verified, whole-image read-back OK vs expected (protected preserved) and `matches_full_recovery=True`, reset queued and core released. `EXIT_RC=0`, `RECOVERY_FLASH_OK` (`rollback_flash.log`).

## 8. Rollback execution and verification (independent)
- **Independent full 64 KiB flash readback (post-rollback):** `st-flash --serial 12010C1C8418303030303032 read ... 0x08000000 0x10000` (RC 0, 65,536 B)  
  - Readback SHA-256: **`e2bd129fd044f44246e9c5a85547d92f2559f6b25ee72ced4d7fe0eff09949f8`** — **matches required recovery image**.  
  - Evidence: `evidence/B4_34_ROLLBACK_14139_20261115T034700Z/post_rollback_readback_64k.bin`, `post_rollback_verification_hashes.txt`.
- **Independent UART identity:** `BUILD_SEQ 0 14139`, `STATUS 0 1`, `UART_OK` (`uart_verification.txt`).
- **Result:** **Rollback to 14139 VERIFIED** (independent full-image hash match **and** UART `BUILD_SEQ=14139`, `STATUS=1` both confirmed). Instrument restored to known-good 14139 per safeguarded procedure. No verification failures observed.

---
*Report generated from preserved artifacts under `evidence/B4_34_HW_GATE_20251114T005319Z/` (note: gate dir name in workspace), `evidence/B4_34_RECOVERY_20261114T010000Z/`, and `evidence/B4_34_DEPLOY_48462_20261114T013600Z/`. Frame/placement analysis computed from collected `summary_*.json` and `captures.jsonl`; raw evidence retained.*
