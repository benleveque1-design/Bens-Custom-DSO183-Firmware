# ETS Stage B4.23 — Interrupted Flash Diagnosis and Recovery Plan

**Stage:** B4.23 (investigation, evidence preservation, read-only inspection/analysis only). **No erase, program, reset, release, or recovery attempted.** No firmware source or existing flash tools modified.

**Scope inputs:** B4.22 run `run_20261113T100711Z` (Pi `benabus@192.168.1.201`, `/tmp/b4_22_campaign`, artifacts synced to `evidence/stage1_ets_campaign_20261113T033944Z/B4_22_FULL_SYSTEM_CAMPAIGN/run_20261113T100711Z/`), B4.22 report `evidence/stage1_ets_campaign_20261113T033944Z/ETS_STAGE_B4_22_FULL_SYSTEM_VALIDATION_REPORT.md`, writer/readback implementations, and independent full-range post-flash readback.

**Authorization boundary respected:** read-only analysis of existing evidence/files/logs and page-by-page comparison performed. No additional hardware operations executed in this stage (see §3 for read-only inspection considerations and uncertainty).

---

## 1. Evidence preservation and integrity verification

### 1.1 Preservation status
- All B4.22 logs, binaries, readbacks, preservation images, helper artifacts, and reports preserved unchanged in-place. No evidence overwritten or regenerated.
- Key B4.22 artifacts and hashes (verified against recorded/local copies):

| Artifact | SHA256 | Match recorded? |
|---|---|---|
| `B4_22.../run_20261113T100711Z/flash/flash_meta.txt` | `ac8853a9021ffb71682e5834a09fc4b9e87985cdb7f8c6fc5525de8583f413d9` | Yes |
| `B4_22.../run_20261113T100711Z/flash/gate1_flash.log` | `8d21cf641281ceb12652a34c9b099d39bb2efb85b335207cf0aa72cad174e957` | Yes |
| `B4_22.../run_20261113T100711Z/readback/readback_result.json` | `27d8b611b208e26dbfdd884f70d0555b6dd82cb44b44e87db7d9b5b4fdc6f2ca` | Yes |
| `B4_22.../run_20261113T100711Z/readback/postflash_actual_0x08000000_len38912.bin` | `b96adbd6bebf49f325374df29ef21c9b117ba219d07e29cd997d975849b7c90d` | Yes |
| `B4_22.../run_20261113T100711Z/readback/postflash_actual_padded.bin` | `b96adbd6bebf49f325374df29ef21c9b117ba219d07e29cd997d975849b7c90d` | Yes |
| `B4_22.../run_20261113T100711Z/preservation/preflash_app_0x08000000_len38912.bin` | `fbb60d91c342f7b6f5a1123f353f8540091c6d53cf277fcb0332e4e82ddb0060` | Yes |
| `B4_22.../run_20261113T100711Z/preservation/userdata_0x0800E800_len2048.bin` | `e6e9986291f6b601a13ad5c12ba9584e5f32845c511703c05cc0236378df4007` | Yes |
| `B4_22.../run_20261113T100711Z/preservation/settings_0x0800F000_len4096.bin` | `fd63ea7c80771a3474eef5194345fae04509b40a27d424db043f88a9855fd876` | Yes |
| `B4_22.../run_20261113T100711Z/preservation/read_app_pre.json` | `84177fa38498835f8ed3866f5c9b98d3261009cae696d3f8ee8c604958b80bf6` | Yes |
| `B4_22.../run_20261113T100711Z/preservation/read_userdata.json` | `61d8e8cb332454a8e29ed0bc47b1dad86bfce788fd67c981f8fef162f257cdcf` | Yes |
| `B4_22.../run_20261113T100711Z/preservation/read_settings.json` | `902245a1e7fc34ff1dafddbe8142bf545e0b0663325f7fc94a3720decfbd19ed` | Yes |
| `B4_22.../run_20261113T100711Z/_read_region_ro.py` | `dd58a0ce2cfeb5f9dfa49f5acabf1d8c9ca1c1e1afb61438a783f762221c671c` | Yes |
| `ETS_STAGE_B4_22_FULL_SYSTEM_VALIDATION_REPORT.md` | `b0e6cbbdb085e31c6727bc3d9a7f5b65161e0686bc0cd09685252347c0f62985` | Yes |

### 1.2 Candidate identity and working tree
- **Candidate (verified unchanged):** `build/dso183.bin` 38,880 B SHA256 `478bc957624cf4c8711e283e4ff3bedcbae13e12cf3ed4b67a84b2a23fa51a68`; padded 38,912 B SHA256 `ebda88c22310246abc4e02c6c4aa3e1a75a6747395a924a8566858de432b916e` (tail 32 B `0xFF`). Designated base `e037afc78368caedfd01f22cb55fb5665564a8bd`.
- **Working tree:** `HEAD e037afc78368caedfd01f22cb55fb5665564a8bd`, dirty (modified: Makefile, app/acq.{c,h}, app/link_cmd.{c,h}, app/main.c, app/menu.c, app/stm32f1xx_it.c, tests/test_link_cmd.c, tests/test_link_e2e.c, tools/remote_proto.py; untracked: app/ets_phase.{c,h}, app/memdiag.{c,h}, app/sysmem.c, docs/trigger_50us_diagnostics.md, tests/test_ets_diag.c, tests/test_ets_integration.c, tests/test_ets_phase.c, plus evidence/report artifacts). Not modified/cleaned in this stage.

---

## 2. Confirmed failure sequence (facts)

1. **2026-11-13T10:08:56Z (remote, Pi .201):** mandated Gate 1 command started with `AUTHORIZED=1`, `--no-reset`, image `dso183.bin`, base `0x08000000`, page-size 1024, from-page 0, page-count 38 (`flash/flash_meta.txt`: `START_UTC` only).
2. **Flash log (`gate1_flash.log`, 8,337 B, 21 `verify ok` entries):** sequential per-page sessions reported erase/program/flash-lock/verify-ok/no-reset for:
   - page 1/38 `0x08000000` … page 21/38 `0x08005000` (page indices 0–20), each with probe `12010C1C8418303030303032`, `STLinkMemoryInterface`, `IWDG frozen: DBGMCU_CR bit 8 set`, `target halted: True`, pre-page SHA256, `verify ok (read-back matches the image)`, `no reset: core left halted, IWDG frozen`.
3. **Log termination:** file ends immediately after page 21/38 (`0x08005000`) `no reset` line. No `--- page 22/38 ...` header, no error/fail/mismatch/abort/traceback/exception text, no `RC=`, no `END_UTC`, no `all N page(s) written and verified`.
4. **Post-flash independent full-range readback (authorized, read-only):** `0x08000000–0x080097FF` (38,912 B) read completed. Actual SHA256 `b96adbd6bebf49f325374df29ef21c9b117ba219d07e29cd997d975849b7c90d` ≠ expected padded `ebda88c22310246abc4e02c6c4aa3e1a75a6747395a924a8566858de432b916e` (RC=2 HASH MISMATCH). Byte-for-byte first difference at offset `0x00005C00` (actual `0x1e`, expected `0xad`). Actual ≠ pre-flash preserved `fbb60d91…`.
5. **No reset/release performed.** No retry/restore attempted.

---

## 3. Current target-state assessment and uncertainty

### 3.1 Established from existing evidence (confirmed/high-confidence)
- **Flash content (page-by-page, actual vs expected padded vs pre-flash; 38 × 1 KiB):**

| Pages (idx) | Address range | Classification | Expected == pre-flash? | Count |
|---:|---|---|---:|---:|
| 0–22 | `0x08000000–0x08005BFF` | **MATCH_EXPECTED** (all 23) | idx 1,2 true; others differ from pre-flash | 23 |
| 23–37 | `0x08005C00–0x080097FF` | **MATCH_PREFLASH** (all 15) | expected differs from pre-flash | 15 |

- **Key implications:**
  - Pages 0–20 (addrs `0x08000000..0x08005000`) match expected **and** are consistent with flash-log-verified pages 1–21/38.
  - **Pages 21–22** (indices 21,22; `0x08005400`, `0x08005800`) **match expected** despite **not being recorded** as completed in `gate1_flash.log` (log last completed entry is index 20 / `0x08005000`). This is a material discrepancy between on-target flash state and captured writer log.
  - **Pages 23–37** (`0x08005C00..0x08009400`, including final page containing expected `0xFF` tail bytes) **match pre-flash preserved image**, not the designated candidate padded image. Final 32 bytes of intended `0xFF` padding region (`0x080097E0–0x080097FF`, within page 37) therefore do **not** satisfy the candidate’s expected padding state as a whole-page comparison (page 37 matches pre-flash).
  - No page classified `MIXED_NEITHER`; observed app-range content is a clean split: first 23 pages = expected candidate, remaining 15 pages = pre-flash.
- **Reserved regions (read-only preservation evidence):** USERDATA (`0x0800E800–0x0800EFFF`, `e6e99862…`) and SETTINGS (`0x0800F000–0x0800FFFF`, `fd63ea7c…`) were read and preserved before flash attempt; **no write operation targeted them** by plan/command/authorization. No post-flash readback of these regions was performed in B4.22/B4.23 (to avoid extra probe interaction without necessity), so byte-for-byte post-flash confirmation is **not directly evidenced** here — but there is **no evidence of any write path touching them** (writer bounds enforced; command range limited to 0x08000000..0x080097FF; readback tool bounds checks would reject reserved writes, and only reads performed).

### 3.2 Halt/watchdog/probe accessibility (uncertain; not re-probed)
- **Best available evidence:** every completed `write_page.py` session (21 logged) reported `IWDG frozen: DBGMCU_CR bit 8 set`, `target halted: True`, and exited with `--no-reset` leaving `core left halted, IWDG frozen`. Session close behavior in pyocd may release/reset depending on backend/version/options, but the script does not explicitly run the core; no reset command was issued.
- **Not confirmed now:** whether target remains halted, DBGMCU IWDG-freeze bit remains set, or ST-Link/target remain currently accessible.
- **Read-only hardware inspection (not executed):** possible operations would be (a) `pyocd list`/`st-info --probe` on Pi to confirm probe visibility (host-side; minimal), and/or (b) opening a pyocd session and reading DHCSR `0xE000EDF0` / DBGMCU_CR `0xE0042004` and selected flash words read-only. **Potential side effects to disclose:** pyocd probe connect/session setup can affect debug interface state; some connect modes may halt or otherwise perturb the target even without explicit write/reset commands. Because B4.23 restricts to read-only inspection and the target is in a sensitive partially-programmed state, **no additional probe/session operation was executed** without a separately explicit, operation-specific go-ahead. Current-state conclusions therefore rely on preserved logs/readbacks.

---

## 4. Interruption diagnosis

### 4.1 Implementation facts (`write_image.py`, `write_page.py`)
- `write_image.py` (B4.22 staged hash `fb2276d2513ce6e1a4f1943a372c6b8293884a3c3d77b42211cc5c609f07b917`): iterates pages sequentially, prints `--- page i/N at addr ---` to stdout, writes temp page file, runs `subprocess.call([sys.executable, WRITE_PAGE, --image, page_file, --page, addr, ...])` with inherited stdout/stderr, checks `rc != 0` and on failure calls `fail()` (prints ERROR to stderr, `sys.exit(1)` after cleanup). `--no-reset` passed to all but final page when not requested globally; with global `--no-reset`, every child gets `--no-reset`. On full success prints `all N page(s) written and verified`.
- `write_page.py` (staged hash `1bf79bb9508329c553cca336aee50c16d6b74f0fa9b29fb0102f8cdf5e713b54`): requires `AUTHORIZED=1`; for each invocation opens pyocd session/probe, connects, gets memory interface, **sets DBG_IWDG_STOP in DBGMCU_CR before halt**, halts core, unlocks flash, clears SR, erases one page (PER/STRT, wait BSY), programs halfword-by-halfword with BSY waits, checks SR, locks flash, **read-verifies full page against image bytes in memory**, prints `verify ok` only on match, leaves core halted/frozen with `--no-reset` (no reset), closes session in `finally`, returns 0 on success (nonzero on errors/verify fail/no probe/cannot halt/cannot freeze IWDG).
- **Output capture in B4.22:** remote command used `python3 write_image.py ... 2>&1 | tee "$F/gate1_flash.log"` inside an SSH session; child `write_page.py` output inherited pipeline stdout, so successful page output should appear in tee’d log when flushed/written before pipeline death.

### 4.2 Confirmed facts
- 21 page sessions completed with in-tool verify-ok and no-reset/halted/frozen state as logged.
- No logged tool error/traceback/nonzero indication; log terminates mid-campaign (no completion line, no RC/end meta).
- Actual flash contains expected content for **23 pages (0–22)** but log records verification for only **21 pages (0–20)**. Pages 23–37 remain pre-flash.
- Flash log SHA256/size stable as preserved; post-flash readback independently confirms on-target content.

### 4.3 Ranked hypotheses for interruption (evidence-based; not claimed as proven)

| Rank | Hypothesis | Supporting evidence | Contradicting / limiting evidence | Plausibility |
|---:|---|---|---|---|
| 1 | **Host-side pipeline/SSH/session interruption killed the writer process tree mid-run** (e.g. SSH disconnect, remote shell/session termination, controlling terminal loss, or parent command timeout/kill on the invoking side), truncating log capture after last flushed block | Log ends cleanly after page 21 `no reset` with no tool error/RC/end; 38× single-page pyocd sessions are long-running and were executed under a remote SSH pipeline that had already experienced tool-timeout-driven interruptions in the same campaign context; campaign notes indicate flash outlived an initial local tool timeout and was being monitored | Exact terminating signal/PID/SSH event not recorded in preserved artifacts; cannot distinguish from local process kill without host process accounting | **High** |
| 2 | **Transport/probe communication disruption during a later page session** (page 22 or 23 in-progress) caused `write_page.py` or pyocd to block/hang or fail in a way not captured before pipeline death, or left parent waiting on child | 23 pages ended up correctly written (including 21,22 beyond last fully logged page 21 completion block) suggesting progress continued past logged point; page 23 onward not written | No pyocd exception/timeout text in log; `write_page.py` would normally emit ERROR and return nonzero on probe/session/erase/program/verify failures (if output captured). A hard hang with simultaneous output loss is possible but less directly evidenced | **Medium** |
| 3 | **Remote process killed/OOM/resource issue** on Pi | Would explain abrupt stop without graceful RC | No OOM/kernel/process evidence preserved in artifacts; speculative | **Low–Medium** |
| 4 | **Output buffering/tee artifact hiding a reported failure** | Unflushed buffers can be lost on kill | Would not explain absence of RC/end markers in wrapper’s own meta file written after pipeline in the same shell (meta recorded only START_UTC; if shell survived to run RC echo it would be present). Consistent with shell/pipeline killed before post-command lines executed | **Low as sole explanation** |

**Most consistent explanation:** an external interruption terminated the flash command/pipeline after at least page 21 completed and while work had progressed to write pages 21–22 on target (flash state evidence), before page 23 was successfully committed and before wrapper completion/RC handling/log flush of later progress was durably captured. Root cause **not proven to a single mechanism** with available evidence.

### 4.4 Whether page 22 was ever started
- **Flash content:** page index 21 (`0x08005400`) and index 22 (`0x08005800`) **MATCH_EXPECTED**.
- **Log:** no `--- page 22/38 at 0x08005400 ---` or any page-22 session lines; last recorded completion is page 21/38 (`0x08005000`, index 20).
- **Assessment:** **high confidence page 22 was started and completed successfully on target** (and page 21/index 21 also completed despite only page 20 being the last fully logged verified page boundary in the obvious sequence — noting logged numbering is 1-based over all pages and page 21/index 20 is logged; pages 21 and 22 by index ended up expected). **Confirmed:** expected bytes present for indices 21–22. **Not confirmed in logs:** start/completion/verify printout for those pages due to capture/truncation.

### 4.5 Silent-stop / exit-status / per-page verification trustworthiness
- **Can `write_image.py` silently stop without reporting a nonzero exit status to the caller?** Yes, **if the process/pipeline is killed or the shell running it dies before** `RC=${PIPESTATUS[0]}` / `echo RC=...` executes (as observed: START_UTC present, no RC/end). In normal execution, it reports nonzero via `fail()`/sys.exit on child failure and returns 0 after printing completion on success. It does **not** emit an explicit failure summary when externally terminated.
- **Can it report incomplete success?** It prints `all %d page(s) written and verified` only after loop completes; that line is absent. No false success printed.
- **Per-page verification evidence trustworthiness (completed sessions):** `write_page.py` performs erase, program, lock, then **full page read-back compare** (`after != data` => VERIFY FAILED) and prints `verify ok` only on match; it returns nonzero on verify failure. For the **21 logged `verify ok` pages**, in-tool verification evidence is **strong/trustworthy assuming pyocd read-back correctness**. For **indices 21–22 (expected on flash, not logged)**, correctness is **confirmed by independent full-range readback comparison** (MATCH_EXPECTED), not by writer log. For **index 23+**, state is pre-flash (confirmed).
- **Not trustworthy as sole completion proof:** wrapper log completeness and absence of wrapper RC/end; external supervision of process liveness/exit is required.

---

## 5. Page-by-page flash comparison (obtainable, completed)

**Method:** compared `postflash_actual_0x08000000_len38912.bin` (`b96adbd6…`) page-wise (1 KiB) against expected padded candidate (`ebda88c2…`, derived from `build/dso183.bin` + `0xFF` pad to 38,912 B) and pre-flash preserved image (`fbb60d91…`).

| Idx | Address | Actual vs Expected | Actual vs Pre-flash | Expected==Pre-flash | Assessment |
|---:|---|---|---|---:|---|
| 0 | 0x08000000 | MATCH | differs | no | Candidate written |
| 1 | 0x08000400 | MATCH | same | **yes** | Candidate-consistent (identical page) |
| 2 | 0x08000800 | MATCH | same | **yes** | Candidate-consistent (identical page) |
| 3 | 0x08000C00 | MATCH | differs | no | Candidate written |
| 4 | 0x08001000 | MATCH | differs | no | Candidate written |
| 5 | 0x08001400 | MATCH | differs | no | Candidate written |
| 6 | 0x08001800 | MATCH | differs | no | Candidate written |
| 7 | 0x08001C00 | MATCH | differs | no | Candidate written |
| 8 | 0x08002000 | MATCH | differs | no | Candidate written |
| 9 | 0x08002400 | MATCH | differs | no | Candidate written |
| 10 | 0x08002800 | MATCH | differs | no | Candidate written |
| 11 | 0x08002C00 | MATCH | differs | no | Candidate written |
| 12 | 0x08003000 | MATCH | differs | no | Candidate written |
| 13 | 0x08003400 | MATCH | differs | no | Candidate written |
| 14 | 0x08003800 | MATCH | differs | no | Candidate written |
| 15 | 0x08003C00 | MATCH | differs | no | Candidate written |
| 16 | 0x08004000 | MATCH | differs | no | Candidate written |
| 17 | 0x08004400 | MATCH | differs | no | Candidate written |
| 18 | 0x08004800 | MATCH | differs | no | Candidate written |
| 19 | 0x08004C00 | MATCH | differs | no | Candidate written |
| 20 | 0x08005000 | MATCH | differs | no | Candidate written (**last logged verify**) |
| 21 | 0x08005400 | MATCH | differs | no | Candidate written (**not logged**) |
| 22 | 0x08005800 | MATCH | differs | no | Candidate written (**not logged**) |
| 23 | 0x08005C00 | differs | **MATCH** | no | Pre-flash retained |
| 24 | 0x08006000 | differs | **MATCH** | no | Pre-flash retained |
| 25 | 0x08006400 | differs | **MATCH** | no | Pre-flash retained |
| 26 | 0x08006800 | differs | **MATCH** | no | Pre-flash retained |
| 27 | 0x08006C00 | differs | **MATCH** | no | Pre-flash retained |
| 28 | 0x08007000 | differs | **MATCH** | no | Pre-flash retained |
| 29 | 0x08007400 | differs | **MATCH** | no | Pre-flash retained |
| 30 | 0x08007800 | differs | **MATCH** | no | Pre-flash retained |
| 31 | 0x08007C00 | differs | **MATCH** | no | Pre-flash retained |
| 32 | 0x08008000 | differs | **MATCH** | no | Pre-flash retained |
| 33 | 0x08008400 | differs | **MATCH** | no | Pre-flash retained |
| 34 | 0x08008800 | differs | **MATCH** | no | Pre-flash retained |
| 35 | 0x08008C00 | differs | **MATCH** | no | Pre-flash retained |
| 36 | 0x08009000 | differs | **MATCH** | no | Pre-flash retained |
| 37 | 0x08009400 | differs | **MATCH** | no | Pre-flash retained (includes BIN tail region/padding area as pre-flash state) |

**Summary:** 23 MATCH_EXPECTED (pages 0–22), 15 MATCH_PREFLASH (pages 23–37), 0 mixed. First byte difference vs expected padded: `0x5C00` (page 23 start, index 23).

---

## 6. Assessment of writer reliability

### Strengths (confirmed by design/evidence)
- Page-granular erase/program with per-page read-verify in `write_page.py`; IWDG freeze before halt; no mass erase/option-byte operations in scope; strong bounds/reserved-region guards in `write_image.py`; 0xFF padding; `AUTHORIZED=1` gating; stops on child nonzero rc in normal flow.
- Per-page verify evidence trustworthy when logged/completed (and consistent with independent readback for pages written).

### Weaknesses / reliability gaps (confirmed)
- **38 separate probe sessions/process invocations** (one per `write_page.py` call) increase duration and exposure to transport/session failures and host interruptions.
- **No durable supervision/heartbeat/progress checkpoint file** independent of stdout log; `flash_meta.txt` only recorded start in observed run.
- **Wrapper cannot distinguish external termination from success** and cannot self-report "incomplete" if killed (no RC/end written). Log completeness is not guaranteed under pipeline/session death.
- **stdout/stderr inheritance + tee pipeline**: buffered/unflushed output can be lost on abrupt termination; mismatch observed (flash pages 21–22 present on target but not logged) demonstrates log is not a complete execution record.
- **No automatic post-run independent full-range readback/compare inside the writer flow**; verification is per-page during write only (valuable, but a final whole-image independent check is required separately — as B4.22 mandated).
- **Resume semantics exist (`--from-page`/`--page-count`) but resumption without an authoritative on-target page map/log is risky**; in this incident, log alone would understate actual written pages (21–22 already expected).

**Conclusion:** `write_image.py`/`write_page.py` are safety-conscious for per-page correctness but **not sufficiently supervised/robust for unattended 38-session remote execution** where host/SSH interruptions are plausible. External supervision and final independent readback are essential (and readback correctly caught the failure).

---

## 7. Proposed recovery procedure (design only — DO NOT EXECUTE in B4.23)

### 7.1 Guiding constraints
- Use designated B4.22 candidate only after SHA256 verification.
- Preserve USERDATA/SETTINGS as-is; use existing B4.22 preservation artifacts as reference (`userdata e6e99862…`, `settings fd63ea7c…`, `preflash app fbb60d91…`). No calibration/vendor/settings writes.
- Program complete intended 38-page application range `0x08000000–0x080097FF` (38 KiB pages) with final 32 B `0xFF` padding included in verification target (padded 38,912 B).
- Keep programming and independent full-range readback/verification separate; **reset/release only after successful full-range verification and (recommended) post-flash reserved-region verification**.
- No silent success: require explicit success markers, exit codes, and independent byte-for-byte verification. Stop safely on any mismatch/incomplete operation.
- Do **not** modify existing writer/tools in this stage; recommend smallest justified change only.

### 7.2 Pre-execution checks (mandatory, before any recovery write)
1. Verify candidate: `build/dso183.bin` SHA256 `478bc957…`, size 38,880; padded SHA256 `ebda88c2…`, size 38,912, tail 32 `0xFF`.
2. Verify probe/target reachable and uniquely identified (ST-Link UID expected `12010C1C8418303030303032` unless explicitly changed and recorded); record versions/state.
3. Confirm B4.22 preservation artifacts present and hashes match (§1.1).
4. (Recommended, read-only, explicitly documented) re-read and hash USERDATA and SETTINGS and compare to preserved references; re-read full app range to confirm/refresh current page map.
5. Run mandated dry-run and confirm pages 0–37 only, reserved untouched.

### 7.3 Execution strategy evaluation
- **Existing 38-session writer (`write_image.py` as-is):** functionally capable of correct per-page writes but **higher operational risk** for this recovery given prior interruption, lack of supervision, and log incompleteness. Suitable only with strong external supervision and explicit acceptance of session count.
- **Safer/better-supervised method (recommended, smallest justified operational change first; code changes only if approved later):**
  - Run under a persistent supervisor on the Pi (e.g. `nohup`/`setsid` + detached session, or `systemd-run --wait`/`tmux`) with stdout/stderr redirected to an append-safe log **and** `stdbuf -oL -eL`/unbuffered Python to reduce loss; capture PID, start/end timestamps, and explicit `echo RC=$?` in the same supervised unit.
  - Emit a machine-readable progress/checkpoint file (page index, addr, result) updated after each verified page — **requires small tool change** (recommended but not performed now).
  - Prefer a **single supervised session** that performs page-wise erase/program/verify in one probe connection where feasible to reduce 38-session churn — **requires implementation change/new supervised path** (evaluate against `write_page.py` safety model) and should be separately validated; **not assumed ready**.
  - **Practical recovery recommendation now:** use existing writer logic/command only under robust supervision and with mandatory immediate independent full-range readback+compare; if any interruption/ambiguity occurs, stop and re-read full range (do not guess progress from log alone).

### 7.4 Exact proposed recovery command(s) (for review; not executed)
**Supervised full-range reprogram (preferred initial recovery attempt, app range only, no reset):**
```bash
# On Pi, /tmp/b4_22_campaign (or controlled dir), after pre-checks and hash verification
AUTHORIZED=1 stdbuf -oL -eL python3 write_image.py \
  --image dso183.bin \
  --base 0x08000000 \
  --page-size 1024 \
  --from-page 0 \
  --page-count 38 \
  --no-reset > recovery_flash.log 2>&1; echo RC=$? >> recovery_flash.meta
```
Run the above under a detached/persistent supervisor (e.g. `nohup`/`tmux`/`systemd-run --wait --collect`) so SSH disconnect does not terminate it; capture supervisor/job status and full log.

**If and only if a supervised run is interrupted but independent readback provides an authoritative page map (all pages verifiably MATCH_EXPECTED up to K-1 and remainder clearly identified), a bounded resume may be considered:**
```bash
AUTHORIZED=1 stdbuf -oL -eL python3 write_image.py \
  --image dso183.bin \
  --base 0x08000000 \
  --page-size 1024 \
  --from-page <K> \
  --page-count <38-K> \
  --no-reset > recovery_resume.log 2>&1; echo RC=$? >> recovery_resume.meta
```
**Do not resume from log line counts alone.** Resume start must be justified by fresh read-only page comparison.

**Independent full-range verification (mandatory, before reset/release):**
```bash
AUTHORIZED=1 python3 readonly_flash_readback.py \
  --probe 12010C1C8418303030303032 \
  --base 0x08000000 --length 38912 \
  --output recovery_postflash_0x08000000_len38912.bin \
  --expected-sha256 ebda88c22310246abc4e02c6c4aa3e1a75a6747395a924a8566858de432b916e \
  --padded-output recovery_postflash_padded.bin --padded-length 38912 \
  --execute-read --i-acknowledge-hardware-read-risk --json > recovery_readback.json 2>&1; echo RC=$?
```
Also perform byte-for-byte compare against expected padded image and confirm zero differences including final 32 `0xFF` bytes (tool/hash + explicit compare artifact).

**Reserved-region confirmation (recommended before reset/release, read-only):**
- Read back `0x0800E800` len 2048 and `0x0800F000` len 4096 and verify SHA256 equals preserved `e6e99862…` and `fd63ea7c…` respectively (using an approved read-only method with bounds appropriate to those regions).

**Reset/release (only after all verifications pass):**
```bash
AUTHORIZED=1 python3 reset_run.py  # (or approved reset path) after successful full-range + reserved-region verification
```

### 7.5 Verification and acceptance criteria
Recovery may be declared successful only when **all** hold:
1. Writer supervised run exits with RC 0 and records completion of all intended pages (or resume completes remaining pages with RC 0) with no errors.
2. Independent full-range readback of `0x08000000–0x080097FF` returns complete 38,912 B, raw SHA256 == `ebda88c22310246abc4e02c6c4aa3e1a75a6747395a924a8566858de432b916e`.
3. Byte-for-byte comparison vs expected padded candidate shows **zero differences** (including final 32 bytes at `0x080097E0–0x080097FF` all `0xFF`).
4. Post-flash readback of USERDATA and SETTINGS (if performed) matches preserved hashes `e6e9986291f6b601a13ad5c12ba9584e5f32845c511703c05cc0236378df4007` and `fd63ea7c80771a3474eef5194345fae04509b40a27d424db043f88a9855fd876`.
5. No writes outside `0x08000000–0x080097FF`; no mass erase/option-byte/calibration/vendor/USERDATA/SETTINGS modifications.
6. All artifacts, logs, hashes, and comparisons preserved.

### 7.6 Explicit stop conditions
Stop immediately and do **not** reset/release/continue programming if:
- Candidate hash/size/padding verification fails.
- Probe/target identity unexpected or inaccessible in an unexplained way.
- Any page erase/program/verify failure or nonzero tool RC.
- Supervision indicates interruption/incomplete run and current on-target state is not authoritatively mapped by fresh readback.
- Full-range readback incomplete, SHA mismatch, or any byte difference.
- USERDATA/SETTINGS readback (if checked) differs from preserved references.
- Any indication of write outside intended range or any request/need for out-of-scope operation (mass erase, option bytes, etc.) — escalate/report.

---

## 8. Recommendation

**NOT READY FOR RECOVERY (as an unsupervised/repeat of B4.22 execution).**

**Conditionally READY FOR RECOVERY** only after the following are explicitly satisfied under supervision/review:
1. A persistent, interruption-resistant execution/supervision method on the Pi is in place (detached session, durable logging, explicit RC capture, and no reliance on fragile SSH foreground pipelines).
2. Pre-recovery read-only checks (probe identity, candidate hashes, and recommended current-state/reserved-region re-read) are completed and recorded.
3. Full 38-page program is executed with immediate independent 38,912-byte readback + byte-for-byte/padded verification and reserved-region confirmation before any reset/release.
4. (Preferred) small tooling improvements approved/implemented and validated: progress checkpointing and/or single-session supervised write path — otherwise accept and mitigate 38-session risk operationally.

Given current evidence (23/38 pages already match expected, 15/38 remain pre-flash, target un-reset/unverified), a carefully supervised full reprogram of the complete 38-page range plus mandatory independent verification is the most reliable recovery path; resumption is possible in principle but should be driven only by a fresh authoritative readback map, not by the truncated log.

---

## 9. B4.23 deliverable summary
- Confirmed sequence: 21 logged verified pages, interrupted/incomplete wrapper execution, 23 pages actually expected on flash (including 2 not logged), 15 pages pre-flash, full-range readback mismatch.
- Root cause: **not definitively proven**; highest-ranked hypothesis is host-side pipeline/SSH/session interruption killing the supervised command with incomplete log capture; transport disruption during later page(s) remains plausible.
- Target state: app flash split (0–22 expected, 23–37 pre-flash), unverified/inconsistent overall, not reset/released; halted/IWDG-freeze and live accessibility not re-probed in this stage.
- Recovery: concrete supervised procedure, commands, verification/acceptance criteria, and stop conditions provided above. **No recovery executed.**
