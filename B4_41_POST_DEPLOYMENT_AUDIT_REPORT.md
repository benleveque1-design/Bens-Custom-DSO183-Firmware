# B4.41 — Post-Deployment Audit and Release Readiness Report (additive, read-only)

**Date (UTC):** 2026-11-15 (audit performed read-only)  
**Scope:** Strictly read-only audit of completed B4.40 deployment/validation artifacts and reports.  
**Actions not taken (per constraints):** no production code modification, no flash/reset/program, no additional hardware tests, no commit, no push. All existing evidence preserved.

**Audited materials (primary):**
- Reports: `B4_40_DEPLOYMENT_AND_VALIDATION_REPORT.md`, `B4_37_RAM_AUDIT_AND_OPTIMISATION_REPORT.md` (B4.40 additive section)
- Evidence: `evidence/B4_40_DEPLOY_AND_VALIDATION_20261115T202159Z/` (candidate/build/test/hash/size artifacts, `deploy_flash_cycle.log`, UART/mem/streaming/postcheck logs & JSON, `pi_campaign_run/` summaries/raw/captures/logs, `pi_campaign_run_attempt1_aborted/`, `validate_b440.py`)
- References: `evidence/B4_36_HW_VALIDATION_20261115T053222Z/run/` summaries & `functional_checks.json`, recovery `evidence/stage1_ets_campaign_20261113T033944Z/B4_29_CONTROLLED_DEPLOYMENT/preflash_recovery/installed_14139_flash_0x08000000_len0x10000.{bin,sha256}`
- Working tree (observed, not modified): ` M app/ets_epoch.c`, ` M app/ets_epoch.h`, ` M app/main.c`, ` M tools/size_report.sh`, `?? B4_37_RAM_AUDIT_AND_OPTIMISATION_REPORT.md`, `?? B4_40_DEPLOYMENT_AND_VALIDATION_REPORT.md`, `?? B4_41_POST_DEPLOYMENT_AUDIT_REPORT.md` (this report), `?? evidence/B4_40_DEPLOYMENT_AND_VALIDATION_20261115T000000Z/` (empty), `?? evidence/B4_40_DEPLOYMENT_AND_VALIDATION_20261115T202159Z/`

---

## Executive summary

- B4.40 deployment/trigger-campaign core claims are **supported by preserved raw evidence**: candidate identity/hashes/size, `tools/flash_cycle.py` page-wise verification (page 0 last) + whole-image verify + identity readout, protected region below image end, recovery SHA256 provenance match, UART/streaming/runtime diagnostics, and **12/12 × 50 = 600/600 coherent** trigger captures with `no_frame=0`, `diag_not_ok=0`, `cmd_errors=0`, requested crossings within ±2 of index 96 (incl. 50 µs rising AUTO/NORM), comparable to B4.36 41320 on equivalent acceptance criteria.
- ETS evidence is **sampled hardware behaviour** (not full hardware validation); TB0/TB2 showed **zero frames obtained in the 6-attempt postcheck window** but **non-zero ETS diagnostic/counter activity** (must not be conflated). Source-level safety reasoning for the B4.38 `cnt` narrowing exists in prior audit context but is distinct from hardware proof.
- Report quality: largely accurate and appropriately caveated (display limitation stated, ETS limits noted), with **minor evidence-link/naming issues** (missing `candidate_test.log` artifact referenced; empty 0-byte duplicate `evidence/B4_40_DEPLOYMENT_AND_VALIDATION_REPORT.md`; test count not explicitly printed in `test.log` tail though pass evidence present). No contradictory numeric campaign aggregates found; no overstated "full ETS equivalence" claim found.
- Historical evidence directories appear present and not overwritten by B4.40 artifacts (spot-checked). Release readiness: **not ready for public release as a clean tagged/audited artifact state** while candidate source/report changes remain uncommitted/unpushed and minor report/evidence hygiene issues exist; hardware-validated trigger scope is strong but ETS/display have documented limitations.

---

## Findings

### 1. B4.40 report claims vs preserved raw evidence/logs

| Claim (B4.40 report) | Evidence checked | Classification | Notes |
|---|---|---|---|
| Diff limited to audited B4.37/B4.38 files + additive untracked audit report; no unexpected source modifications | `git status --short` (4 modified source/tool files, untracked reports/evidence dirs only); prior B4.38/B4.39 descriptions consistent with stated edits (cnt narrowing/saturation, TRIG_SEARCH_LEN/y-tracking narrowing, `s_means` removal/inlined mean, `RAM_WARN=18432`) | **Confirmed** (at audit time, read-only) | Source not re-diffed line-by-line against B4.38 baseline commit here; working tree state matches stated scope. |
| `make clean && make` success; `make test` 36 suites, 1318 checks, 0 failures | `candidate_build.log` present; `test.log` present (57 lines, shows suite pass lines incl. "All cal_table/crc16/flash layout/runstate/settings tests passed" and many `*: all tests passed`; warnings only). No `candidate_test.log` file present. | **Confirmed with limitations** | Pass evidence is present in `test.log`, but explicit aggregate line "36 suites, 1318 checks, 0 failures" not found in preserved log and referenced `candidate_test.log` artifact missing (see §7). Count consistent with prior B4.37/B4.38 (36/1318). |
| Candidate hashes/size/seq: 58810, bin 40904 B `00a1e11d…`, elf `d5b21402…`, flash 40900/61440 (66%), RAM 16184/20480 (79%) | `build_seq.h`, `candidate_sha256.txt`, `candidate_bin_size.txt`, `candidate_size.txt`, `candidate_size_A.txt`, `candidate_size_report.txt` all consistent | **Confirmed** | End address `0x08000000+40904=0x08009FC8` < `0x0800FC00`. |
| Recovery 14139 verified (65536 B, SHA matches) and not modified | `sha256sum` of recovery bin equals recorded `.sha256` (`e2bd129f…9949f8`) | **Confirmed** | Provenance path preserved under stage1 B4.29 preflash recovery. |
| Deployment via `tools/flash_cycle.py` only; 40 pages, page 0 last, per-page verified, whole-image read-back OK (40904 B), reset/release, identity `magic 0x4453424D seq 0x0000E5BA`, status_ok | `deploy_flash_cycle.log` shows pages 1–39 then page 0 `@0x08000000 written + verified (vector table)`, `image read-back: OK (40904 bytes identical)`, identity seq `0x0000E5BA`, `OK: new image running and verified`; `pi_candidate_verify.log`/`scp_to_pi.log` show staged files hashed on Pi matching local | **Confirmed** | Flash tool/script contents not re-executed; log evidences required behavior. |
| Immediate UART identity/mem: BUILD_SEQ 58810, STATUS 1; heap 0/0; stack/MSP values as reported | `uart_identity_and_mem.log` | **Confirmed** | Values match (5344/4880/0/0/19048/4920). |
| Campaign functional checks: 58810, STATUS 1, tb5/6/9 stability 12/0, 5/5 stream toggles, ETS diag snapshot | `pi_campaign_run/functional_checks.json`, `signal_characterization.json` | **Confirmed** | |
| Post-campaign re-verify: identity/mem/streaming 3/3 frames | `b440_postchecks.json` (`identity_mem`, `streaming.frames_ok==3`, toggles frame_ok with replies) | **Confirmed** | Minor runtime variation in mins/MSP low water vs immediate check (4840/19008 vs 4880/19048) is expected and reported. |
| Trigger campaign 12×50: 600/600 coherent, no_frame 0, diag_bad 0, cmd_count 13157 cmd_errors 0, requested crossings within ±2 of 96 incl. 50 µs rising AUTO/NORM | `pi_campaign_run/campaign.log` (`DONE cmd_count=13157 cmd_errors=0`), all 12 `summary_*.json` (each 50/50/0/0), crossing stats: tb5/tb6/tb9 primary nr/nf 96,96,96.0 as applicable; tb9 opposite edges vary 80–112 (secondary) | **Confirmed** | "Requested crossings" interpreted per config (rise configs→rising, fall configs→falling) is satisfied; opposite-edge variability correctly noted as not requested. |
| Aborted first attempt (serial timeout) and tooling fixes documented; aborted run preserved on Pi/not copied as primary run | `pi_campaign_run_attempt1_aborted/{run_stdout.log,campaign.log}` present (SerialError timeout, BUILD_SEQ 58810 STATUS 3 at start); successful run in `pi_campaign_run/` | **Confirmed with limitations** | Report states "preserved on Pi; not copied as primary run" — accurate that it is not primary, but artifacts are now also copied into evidence dir; wording could be clearer (minor). |
| Display limitation stated; no remote visual observation | Report §6 | **Confirmed** | Appropriately caveated. |

### 2. Candidate hashes, build identity, flash verification, protected-page handling, recovery provenance

| Item | Evidence | Classification | Notes |
|---|---|---|---|
| Candidate hashes/build seq/size | `candidate_sha256.txt`, `candidate_bin_size.txt`, `build_seq.h` (58810), `candidate_size*.txt` | **Confirmed** | bin/elf hashes and seq consistent across artifacts. |
| Flash verification & method | `deploy_flash_cycle.log`: per-page "written + verified" for pages 1–39 and page 0 last, whole-image identical, identity/magic/seq readback, final OK | **Confirmed** | Executed path/tool name recorded in logs; independent re-run not performed (and prohibited). |
| Protected-page handling | Reported end `0x08009FC8` < `0x0800FC00`; flash log covers pages 0x08000000–0x08009C00 only (40×1KB) | **Confirmed** | No evidence of writes at/above `0x0800FC00`. |
| Recovery image provenance | Recovery bin+sha in `evidence/stage1_ets_campaign_20261113T033944Z/B4_29_CONTROLLED_DEPLOYMENT/preflash_recovery/` with matching hash | **Confirmed** | Provenance tied to preserved stage1/B4.29 recovery artifacts. |

### 3. 600-capture campaign aggregates and per-configuration results agreement

| Check | Evidence | Classification | Notes |
|---|---|---|---|
| Per-config counts and coherence | 12 `summary_*.json`: each `total=50, coherent_n=50, incoherent_n=0, no_frame_n=0, diag_not_ok_n=0` | **Confirmed** | Aggregate 600/600 follows directly (12×50). |
| Campaign completion/errors | `campaign.log`/`run_stdout.log` tail: all 12 SUMMARY lines present, final `DONE cmd_count=13157 cmd_errors=0` | **Confirmed** | |
| Crossing statistics consistency | Summary `nr_nearest96_i`/`nf_nearest96_i` match table claims (primary edges at 96; tb9 secondary edges 80–112 as reported) | **Confirmed** | `captures.jsonl` and raw samples exist but were not exhaustively re-analyzed; summaries agree with reported aggregates. |

### 4. Comparison against B4.36 uses equivalent acceptance criteria

| Check | Evidence | Classification | Notes |
|---|---|---|---|
| Same campaign structure/configs | B4.36 and B4.40 both have 12 summary configs: tb5/tb6/tb9 × auto/norm × rise/fall, 50 captures each (`COUNT 12` in both run dirs) | **Confirmed** | |
| Same pass criteria (coherence, no_frame/diag_bad, requested crossing within ±2 of 96) | B4.36 summaries: all 50/0/0; primary requested crossings at/within ±2 (e.g. tb5_auto_rise nr 96; tb5_norm_rise nr min 95 max 96 mean 95.98; tb9 requested edges at 96). B4.40: all 50/0/0; primary requested edges 96,96,96.0 | **Confirmed** | B4.40 tb5_norm_rise mean 96.0 vs B4.36 95.98 — both within ±2 and pass; comparison stated as "equivalent pass outcomes", which is accurate (not claiming identical distributions). |
| Functional/protocol checks comparable | Both `functional_checks.json`: BUILD_SEQ 41320 vs 58810, STATUS 1, stability tb5/6/9 12/0, 5/5 stream toggles OK | **Confirmed** | ETS diag snapshot fields differ in values (expected runtime state) but presence/structure comparable. |

### 5. ETS evidence: source-level proof vs sampled HW behaviour vs full HW validation; TB0/TB2 zero-frame results

| Aspect | Evidence | Classification | Notes |
|---|---|---|---|
| **Distinction preserved in reports** | B4.40 report §5 states sampled post-campaign behaviour, waveform/range-dependent, "recorded as observed, not claimed equivalent beyond evidence", "not a full equivalence proof to B4.36 for every TB/condition"; B4.37 audit B4.40 section states "sampled with no critical regression indicated (waveform/range-dependent; see report)" | **Confirmed** | No claim of full ETS hardware validation/equivalence found. |
| **Source-level proof (separate from HW)** | B4.37 audit report B4.38/B4.39 context documents cnt narrowing rationale (sweeps target 16, per-bin <=16, saturation, host tests pass) — code-level/safety argument, not hardware validation | **Confirmed with limitations** | Source-level reasoning is recorded in audit history but not re-proved by this read-only audit; it must remain distinguished from sampled/full HW validation (as required). |
| **Sampled HW behaviour (TB0–TB4 + TB5/6/9)** | `b440_postchecks.json`: per-TB frames_got and diag counters (34–50 etc.) collected in 6-attempt window per TB; rejected(35)==0 in snapshots; counters/diag active | **Confirmed** | Sampling method/script preserved (`/tmp/b440_postchecks.py` logic captured in JSON context and described). |
| **TB0/TB2 zero-frame results (explicit)** | TB0: `frames_got 0` (6 attempts) but diag shows `accepted(34)=7, progress(36)=7, filled(37)=12, ready(38)=0, reset(39)=64, seq(40)=28070, span_valid(48)=1` etc. TB2: `frames_got 0` but `accepted=11, progress=11, filled=96, ready=0, reset=16, seq=35650, span_valid=1` etc. TB1/3/4 got 6/6 frames with active diag. | **Confirmed** | **Zero frames obtained in this short sampling window**; **not** zero ETS activity. B4.40 report states this ("TB0/TB2 produced no frames in this short sampling window but showed ETS accepted/progress/filled/reset/seq activity...") — accurate and must remain explicit. |
| **Full HW validation of ETS** | No comprehensive TB0–TB4 ETS campaign/equivalence matrix vs B4.36 in B4.40 evidence beyond this sampling | **Unverified** (by design of performed validation) | Correctly not claimed as complete; remains a limitation. |

### 6. Display inspection limitation clearly stated

| Check | Evidence | Classification | Notes |
|---|---|---|---|
| Limitation documented | B4.40 report §6 and overall outcome explicitly state remote validation cannot observe on-device LCD rendering/smoothing/differential-redraw; recommends direct visual inspection | **Confirmed** | Also noted in audit report B4.40 section. |

### 7. Reports: inaccurate claims, missing evidence links, contradictory numbers, overstated conclusions

| Item | Evidence | Classification | Notes |
|---|---|---|---|
| Missing/incorrect evidence link: `candidate_test.log` | B4.40 report §1.2 lists `candidate_test.log (copied from build/test outputs as recorded), test.log in evidence dir`; evidence dir contains `test.log` but **no `candidate_test.log`** | **Incorrect / needs correction** | Should reference existing artifact(s) accurately (e.g. `test.log`) or note omission. Pass itself is evidenced, but artifact naming/link is inaccurate. |
| Test aggregate count traceability | Report claims "36 suites, 1318 checks, 0 failures"; `test.log` shows pass lines but not that explicit aggregate summary line | **Confirmed with limitations** | Claim plausible/consistent with history, but not directly printed in preserved log; minor traceability gap. |
| Empty duplicate report in evidence root | `evidence/B4_40_DEPLOYMENT_AND_VALIDATION_REPORT.md` is **0 bytes** and differs from root `B4_40_DEPLOYMENT_AND_VALIDATION_REPORT.md` (12574 bytes) | **Incorrect / needs correction** (artifact hygiene) | Read-only audit cannot fix; empty file may confuse consumers. Timestamped evidence dir does not contain a copy. |
| Aborted-run wording | Report says "preserved on Pi; not copied as primary run" while `pi_campaign_run_attempt1_aborted/` is now in evidence dir | **Confirmed with limitations** | Factually not misleading (not primary), but could explicitly mention preserved copy location in evidence. |
| Numeric consistency (hashes/seq/sizes/counts/cmd stats/crossings) | Cross-checked against raw files/summaries/logs as above | **Confirmed** | No contradictory numbers found. |
| Overstated conclusions | ETS qualified as sampled/not full equivalence; display limited; host tests not sole basis; "no critical regression observed" scoped to collected evidence | **Confirmed** | Conclusions appropriately bounded. |

### 8. Historical evidence not overwritten

| Check | Evidence | Classification | Notes |
|---|---|---|---|
| Prior evidence directories present | `evidence/` lists B4.36/B4.37/B4.38/stage0/stage1 (incl. recovery), B4.33/B4.34, and other historical artifacts alongside new B4.40 dirs | **Confirmed** (spot-check) | No deletion/overwrite indicated by listing; B4.40 work added new dirs/files and empty placeholder `B4_40_DEPLOY_AND_VALIDATION_20261115T000000Z`. Exhaustive byte-level immutability proof not performed. |
| Recovery image intact | Recovery bin hash still matches `.sha256` | **Confirmed** | |

### 9. Public release readiness

**Assessment: _Not ready for a clean public release in current repository state_** (hardware-validated trigger scope is strong, but release hygiene and evidence completeness have gaps).

#### Remaining blockers (separate)
1. **Uncommitted/unpushed candidate and documentation state:** audited B4.37/B4.38 source changes (`app/ets_epoch.{c,h}`, `app/main.c`, `tools/size_report.sh`) and key reports/evidence directories are untracked/modified in git (`git status` shows 4 modified + untracked reports and B4.40 evidence dirs). A public release should be from a clearly identified, committed/tagged revision with traceable artifacts.
2. **Evidence/report hygiene issues requiring correction:** B4.40 report references missing `candidate_test.log`; `evidence/B4_40_DEPLOYMENT_AND_VALIDATION_REPORT.md` is a 0-byte duplicate (confusing). These should be corrected/clarified in an additive report revision before release packaging.
3. **Validation scope limitations (must remain explicit in release notes):** ETS not fully hardware-validated (TB0–TB4 sampled only; TB0/TB2 zero frames in short window despite diag activity), and display rendering/smoothing/differential redraw not visually verified remotely. These are not new regressions evidenced, but they bound what can be claimed.

#### Optional improvements (non-blocking, recommended)
- Capture an explicit host-test aggregate summary artifact (36 suites/1318 checks) or include full `make test` output with summary line in evidence.
- Add a brief index/readme in the B4.40 evidence dir linking primary artifacts (deploy log, summaries, postchecks, reports) and clarifying aborted vs primary run locations.
- Perform on-device visual inspection of display/smoothing/differential redraw when hardware access allows, and/or run a fuller ETS TB0–TB4 hardware campaign if release criteria require stronger ETS equivalence claims.
- Remove or populate the empty placeholder `evidence/B4_40_DEPLOYMENT_AND_VALIDATION_20261115T000000Z/` and avoid empty duplicate report files in release bundles.
- Tag the released commit and include build hashes/seq in release notes mapped to evidence.

---

## Classification summary

- **Confirmed:** candidate hashes/seq/size and protected-region bound; recovery SHA provenance; flash_cycle deployment verification (page 0 last, per-page, whole-image, identity); UART/streaming/runtime diagnostics evidence; 600/600 campaign aggregates and per-config pass results; comparison to B4.36 on equivalent acceptance criteria (structure/outcomes); ETS sampling recorded with TB0/TB2 zero-frame-but-active-diag detail present; display limitation clearly stated; no contradictory campaign numbers or overstated ETS/display conclusions found; historical evidence present (spot-check).
- **Confirmed with limitations:** clean build/test pass evidenced but aggregate count not explicitly in preserved log; aborted-run "not copied as primary run" wording less explicit than current evidence layout; source-level ETS cnt-narrowing proof exists in prior audit but distinct from HW validation; historical non-overwrite verified by spot-check not exhaustive proof.
- **Unverified:** full ETS hardware validation/equivalence across TB0–TB4 conditions (beyond sampling); on-device visual display/smoothing/differential-redraw behaviour; exhaustive byte-level immutability of all historical evidence.
- **Incorrect / needs correction:** B4.40 report references non-existent `candidate_test.log` artifact; `evidence/B4_40_DEPLOYMENT_AND_VALIDATION_REPORT.md` is an empty (0-byte) duplicate inconsistent with root report.

## B4.42 correction status (additive, documentation/hygiene only)

Applied after this audit (no production code changes, no firmware rebuild/deploy, no hardware interaction, no commit/tag/push):
- **Resolved:** B4.40 report test-evidence wording corrected to reference preserved `test.log` without claiming an aggregate count not explicitly established in that log; aborted campaign preservation/location clarified (`pi_campaign_run_attempt1_aborted/` in evidence). Empty duplicate `evidence/B4_40_DEPLOYMENT_AND_VALIDATION_REPORT.md` (0-byte) removed; empty placeholder directory `evidence/B4_40_DEPLOY_AND_VALIDATION_20261115T000000Z/` removed (no meaningful evidence). `evidence/B4_40_DEPLOY_AND_VALIDATION_20261115T202159Z/EVIDENCE_INDEX.md` added. `B4_42_RELEASE_READINESS_REPORT.md` added.
- **Still applicable:** ETS not fully HW-validated (sampled only; TB0/TB2 zero-frame caveat remains explicit) and display visual inspection not remotely verified — validation-scope limitations must remain stated. Repository state remains uncommitted/unpushed/unreleased (see B4.42 report).

*End of B4.41 read-only audit (additive; B4.42 correction status appended).*
