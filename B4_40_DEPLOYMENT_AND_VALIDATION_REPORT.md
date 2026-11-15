# B4.40 — B4.37/B4.38 Hardware Deployment and Validation Report (additive)

**Date (UTC):** 2026-11-15  
**Candidate:** B4.38 RAM-optimisation build on B4.37 baseline (audited B4.39, no production changes in B4.40)  
**Build:** `STATUS_BUILD_SEQ 58810` (`0x0000E5BA`)  
**Instrument:** Pi host `benabus@192.168.1.201`, serial `/dev/ttyUSB0`, ST-Link (pyOCD)  
**Authorisation:** User explicitly authorised deployment (flash/reset) contingent on pre-deployment safety checks (2026-11-15).

This report is **additive** and preserves all prior evidence (`B4_37_RAM_AUDIT_*`, `B4_38_RAM_AUDIT_*`, B4.36 validation, recovery/stage artifacts). No commit/push performed. No production code changes were made in B4.40; working tree remains the audited B4.37/B4.38 candidate.

---

## 1. Pre-deployment checks (all passed)

### 1.1 Working-tree diff vs audited B4.37/B4.38 changes
- Modified (only): `app/ets_epoch.c`, `app/ets_epoch.h`, `app/main.c`, `tools/size_report.sh`
- Untracked (additive): `B4_37_RAM_AUDIT_AND_OPTIMISATION_REPORT.md`
- Diff content matches audited changes only:
  - `app/ets_epoch.h`: `struct ets_epoch::cnt` `uint16_t[ETS_EPOCH_WINDOW]` → `uint8_t[ETS_EPOCH_WINDOW]` (comment updated).
  - `app/ets_epoch.c`: saturation guard `0xFFFFU` → `0xFFU` in `ets_epoch_add`.
  - `app/main.c`: B4.37 `TRIG_SEARCH_LEN 512→320`, `uint16_t` y-tracking arrays narrowed to `uint8_t`/`const uint8_t *`; B4.38 removed `s_means[UI_PLOT_W]` and inlined equivalent column-mean computation into `scope_draw` (same inputs/output semantics: mean of up to 8 samples per column, same horizontal smoothing, same 4/7/8 settle-fraction rendering rules).
  - `tools/size_report.sh`: RAM warning budget `RAM_WARN=18432`, hard limit remains 20480.
- No unexpected source modifications found.

### 1.2 Clean build and full host test suite
- `make clean && make`: success (evidence: `evidence/B4_40_DEPLOY_AND_VALIDATION_20261115T202159Z/candidate_build.log`).
- `make test`: completed; the preserved log `evidence/B4_40_DEPLOY_AND_VALIDATION_20261115T202159Z/test.log` records suite pass lines (e.g. `*: all tests passed`, `All ... tests passed`) with no recorded failures in the preserved output. No aggregate summary line (suite/check totals) is explicitly present in that log; this report does not claim an aggregate count beyond what the log explicitly establishes.

### 1.3 Candidate binary/ELF identity and memory
- `build/build_seq.h`: `STATUS_BUILD_SEQ 58810`
- `build/dso183.bin`: **40904 bytes**, SHA-256 `00a1e11da6a97e727125ebda40c62fbc9c638ce04ca0b652ebdb616c23afabba`
- `build/dso183.elf`: SHA-256 `d5b21402b0fd99b36c6e47e3d146b8f50472e08f2b6ee53c3eb31694d2aeea7b`
- `arm-none-eabi-size`: flash `40900 B / 61440 B (66%)` `[text 40764 + data 136]`, RAM `16184 B / 20480 B (79%)` `[data 136 + bss 16048]`
- `tools/size_report.sh`: `OK: within the flash and RAM budget`
- Evidence: `candidate_sha256.txt`, `candidate_size.txt`, `candidate_size_A.txt`, `candidate_size_report.txt`, `candidate_bin_size.txt`, `build_seq.h`.

### 1.4 Recovery image and protected region
- 14139 recovery image verified: `evidence/stage1_ets_campaign_20261113T033944Z/B4_29_CONTROLLED_DEPLOYMENT/preflash_recovery/installed_14139_flash_0x08000000_len0x10000.bin` = 65536 B, SHA-256 `e2bd129fd044f44246e9c5a85547d92f2559f6b25ee72ced4d7fe0eff09949f8` (matches recorded `.sha256`). Image not modified.
- Candidate image ends at `0x08000000 + 40904 = 0x08009FC8`, below protected/settings region `0x0800FC00`; `0x0800FC00` not written.

---

## 2. Deployment (tools/flash_cycle.py only)

- Staging: candidate `dso183.bin`, `dso183.elf`, `tools/flash_cycle.py` copied to Pi `/tmp/b440_candidate/` and SHA-256/size verified against local hashes before flashing.
- Flash method: `python3 flash_cycle.py --image dso183.bin --elf dso183.elf` (pyOCD/ST-Link). Page-wise erase/program of 40 pages, **page 0 (vector table) written last**, per-page read-back verified for every page, whole-image read-back and byte-for-byte compare OK (40904 B), core reset/release and identity readout.
- Reported identity: `running:   magic 0x4453424D  seq 0x0000E5BA` (58810), `status_ok: True`.
- No `st-flash write` or alternative flashing path used.
- Evidence: `evidence/B4_40_DEPLOY_AND_VALIDATION_20261115T202159Z/deploy_flash_cycle.log`, `scp_to_pi.log`/`pi_candidate_verify.log`.

**Result:** deployment succeeded with required verification steps.

---

## 3. Post-deploy functional checks (UART, streaming, runtime diagnostics)

### 3.1 UART identity and initial diagnostics
- Immediate post-deploy (serial): `BUILD_SEQ 58810` (id 11), `STATUS 1` (id 4). Memory: `MEM_STACK_FREE_NOW 5344`, `MEM_STACK_FREE_MIN 4880`, `MEM_HEAP_USED 0`, `MEM_HEAP_HIGHWATER 0`, `MEM_MSP_LOW_WATER 19048`, `MEM_MSP_FREE_MIN 4920`. Evidence: `uart_identity_and_mem.log`.

### 3.2 Campaign embedded functional/protocol checks (Pi campaign run)
- `signal_characterization.json`: 10 frames captured (n=320), vpp ~174–178, means ~995–998; measurement GETV snapshot recorded (`BUILD_SEQ 58810`, `STATUS 1`).
- `functional_checks.json`: `build_seq 58810`, `status 1`, stability `tb5/tb6/tb9 got=12 errs=0`, stream toggles 5/5 frame_ok with stream on/off ACKs, ETS diag snapshot recorded.
- Evidence: `pi_campaign_run/signal_characterization.json`, `pi_campaign_run/functional_checks.json`.

### 3.3 Post-campaign clean re-verification (serial port free)
- `b440_postchecks.json`: identity/memory `BUILD_SEQ 58810`, `STATUS 1`, `MEM_STACK_FREE_NOW 5344`, `MEM_STACK_FREE_MIN 4840`, `MEM_HEAP_USED 0`, `MEM_HEAP_HIGHWATER 0`, `MEM_MSP_LOW_WATER 19008`, `MEM_MSP_FREE_MIN 4880`.
- Streaming: 3/3 toggles produced valid frames (n=320, vpp 177–183) with correct stream on/off replies.
- Evidence: `b440_postchecks.json`, `b440_postchecks.err` (empty).

**Result:** UART and streaming verified; runtime heap remains 0 and stack/MSP diagnostics healthy (no regression signal).

---

## 4. Hardware validation — B4.36 trigger campaign (12 configs × 50 captures)

- Method: adapted `tools/validate_b436.py` → `validate_b440.py` (`TARGET_BUILD=58810`), run on Pi (`/dev/ttyUSB0`), read-mostly after deploy. Output: `/tmp/b440_hw_validation_20261115T203330Z/` (copied to `pi_campaign_run/`).
- Configurations (50 captures each): tb5/tb6/tb9 × (auto/norm) × (rise/fall), level 970, avg 1. Coherence requires fresh frame + coherent diag snapshot (seq-consistent).

### 4.1 Aggregate results
- **600/600 coherent captures** (12 configs × 50), **`no_frame=0`, `diag_not_ok=0`**, `cmd_count=13157 cmd_errors=0`. All summaries report `coherent_n=50`.

| Config | Coherent | no_frame | diag_bad | Primary crossing @96 (min,max,mean) |
|---|---:|---:|---:|---|
| tb5_auto_rise | 50/50 | 0 | 0 | nr (96,96,96.0) |
| tb5_norm_rise | 50/50 | 0 | 0 | nr (96,96,96.0) |
| tb5_auto_fall | 50/50 | 0 | 0 | nf (96,96,96.0) |
| tb5_norm_fall | 50/50 | 0 | 0 | nf (96,96,96.0) |
| tb6_auto_rise | 50/50 | 0 | 0 | nr (96,96,96.0) |
| tb6_norm_rise | 50/50 | 0 | 0 | nr (96,96,96.0) |
| tb6_auto_fall | 50/50 | 0 | 0 | nf (96,96,96.0) |
| tb6_norm_fall | 50/50 | 0 | 0 | nf (96,96,96.0) |
| tb9_auto_rise | 50/50 | 0 | 0 | nr (96,96,96.0) |
| tb9_norm_rise | 50/50 | 0 | 0 | nr (96,96,96.0) |
| tb9_auto_fall | 50/50 | 0 | 0 | nf (96,96,96.0) |
| tb9_norm_fall | 50/50 | 0 | 0 | nf (96,96,96.0) |

- Requested crossings are within **±2 samples of index 96** in all required cases, including **50 µs (tb5) rising AUTO/NORM** (`nr_nearest96_i` 96,96,96.0). Opposite-edge crossings in tb6/tb9 vary with waveform/period as expected (e.g. tb9_auto_rise nf 80–112 mean 101.8) and are not the requested crossing for that config.

### 4.2 Comparison vs B4.36 (build 41320)
- B4.36 reference: 12×50 = 600/600 coherent, requested crossings within ±2 of 96 (e.g. `tb5_auto_rise` nr 96,96,96.0; `tb5_norm_rise` nr 95–96 mean 95.98; `tb9_auto_rise` nr 96,96,96.0, nf 80–112). Functional checks: BUILD_SEQ 41320, STATUS 1, tb5/tb6/tb9 stability 12/0, 5/5 stream toggles OK.
- B4.40 shows equivalent pass outcomes on the same campaign structure and acceptance criteria; no trigger regression observed.

**Result:** trigger campaign acceptance criteria met.

---

## 5. ETS validation (TB0–TB4, with TB5/6/9 samples)

- Post-campaign sampling via `b440_postchecks.py` (AUTO, rise, level 970, avg 1; per timebase: configure/run, attempt 6 frame reads, collect ETS/diag GETVs 34–44,18–27,45–50). ETS behaviour is range/waveform-dependent; results are recorded as observed, not claimed equivalent beyond evidence.

| TB | frames_got (6 attempts) | accepted(34) | rejected(35) | progress(36) | filled(37) | ready(38) | reset(39) | seq(40) | src_b1(41) | src_q(42) | unbound(43) | rollover(44) | hw_count(45) | span(47) | span_valid(48) | recover_back(50) |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 0 | 7 | 0 | 7 | 12 | 0 | 64 | 28070 | 2 | 7 | 0 | 7 | 269 | 225 | 1 | 0 |
| 1 | 6 | 3 | 0 | 16 | 26 | 0 | 64 | 28885 | 4 | 0 | 0 | 16 | 25 | 225 | 1 | 0 |
| 2 | 0 | 11 | 0 | 11 | 96 | 0 | 16 | 35650 | 1 | 5 | 0 | 16 | 33 | 225 | 1 | 0 |
| 3 | 6 | 16 | 0 | 14 | 256 | 0 | 16 | 36113 | 1 | 6 | 0 | 2 | 21 | 225 | 1 | 0 |
| 4 | 6 | 16 | 0 | 15 | 192 | 0 | 16 | 36507 | 0 | 15 | 0 | 6 | 33 | 225 | 1 | 0 |
| 5 | 6 | 16 | 0 | 16 | 256 | 1 | 16 | 36643 | 2 | 14 | 0 | 16 | 31 | 225 | 1 | 512 |
| 6 | 6 | 16 | 0 | 16 | 256 | 1 | 16 | 36643 | 2 | 14 | 0 | 16 | 33 | 225 | 1 | 512 |
| 9 | 6 | 16 | 0 | 16 | 256 | 1 | 16 | 36643 | 2 | 14 | 0 | 16 | 69 | 239 | 1 | 512 |

- Observations: no rejected samples (`35==0`) in collected snapshots; counters progressed across timebases; TB1/3/4/5/6/9 produced valid frames in this window; TB0/TB2 produced no frames in this short sampling window but showed ETS accepted/progress/filled/reset/seq activity (range/waveform-dependent). This is consistent with known ETS variability and does not indicate a critical regression from the evidence collected; it is **not** a full equivalence proof to B4.36 for every TB/condition.
- Evidence: `b440_postchecks.json`.

---

## 6. Display rendering, smoothing, differential redraw

- **Limitation:** remote validation (UART/SWD over Pi) cannot directly observe on-device LCD rendering, visual smoothing quality, or differential-redraw artifacts.
- No display-related faults or protocol anomalies were observed in UART/status/streaming/trigger campaign evidence. The B4.38 `scope_draw` change preserved mean/smoothing/render-state logic (audited equivalent); host rendering-related tests pass (36/1318). Direct visual inspection remains recommended if available.

---

## 7. Runtime RAM, heap, stack diagnostics

- Post-deploy: heap used/highwater 0/0; stack free now 5344 B, min 4880 B; MSP low water 19048, MSP free min 4920 B.
- Post-campaign: heap 0/0; stack free now 5344 B, min 4840 B; MSP low water 19008, MSP free min 4880 B.
- Static image RAM 16184 B / 20480 B (79%), -2496 B from 41320 baseline; margins remain healthy and consistent with no dynamic heap use.

---

## 8. Issues, failures, and anomalies encountered

- **Validation tooling/setup (resolved, not firmware regressions):** (1) initial Pi campaign run aborted with `Unresponsive: SerialError('reply timeout for seq 23 op 9')` during signal characterization (`/tmp/b440_hw_validation_20261115T202735Z/` on Pi), preserved in evidence as `evidence/B4_40_DEPLOY_AND_VALIDATION_20261115T202159Z/pi_campaign_run_attempt1_aborted/` (not the primary run). Device remained responsive on subsequent checks. (2) Adapted `/tmp/validate_b440.py` had unquoted dict-key typos (`res[build_seq]` etc.) causing early script failure; fixed and rerun successfully. (3) Ad-hoc streaming probes during setup saw transient `SerialException('device reports readiness to read but returned no data...')`; later campaign and postchecks showed stable streaming (5/5 toggles in campaign, 3/3 postchecks, 12/0 stability per tb5/6/9, 600/600 coherent frames).
- **Critical trigger/ETS/acquisition/display/communication regressions:** **none observed** in collected evidence. No rollback performed.

---

## 9. Overall outcome (separated)

- **Deployment:** successful via `tools/flash_cycle.py` with required verification; build 58810 running, STATUS 1.
- **Functional checks:** UART identity/protocol and streaming verified; runtime heap/stack diagnostics healthy.
- **Hardware validation (trigger):** PASS against stated acceptance criteria (12×50 coherent, requested crossings within ±2 of index 96 incl. 50 µs rising AUTO/NORM), comparable to B4.36 41320.
- **ETS (TB0–TB4+):** sampled and recorded; no critical regression indicated, waveform/range-dependent behavior and limited direct equivalence proof noted.
- **Display:** not remotely observable; no contradictory evidence.

Host tests alone were **not** used to claim success; hardware evidence above supports the validated scope. All historical evidence preserved; no commit/push.
