# 50 µs/div Trigger Diagnostic Report (instrumented build)

**Date:** 2026-11-11/12 (local)  
**Requested instrumented build:** `build/dso183.bin` (local) — **md5 `5ec9cb7360ba88478139e2666962efd0`**, 37004 bytes, 37 pages, flash `0x08000000..0x080093FF` (end exclusive `0x08009400`, within bounds and below reserved `0x0800E800` / vendor `0x0800FC00`). **Not successfully validated on hardware.**  
**Preserved rollback (S=63A1, pre-flash):** Pi `~/dso183-firmware-project/build/backups/dso183_s63a1_pre_diag_20261112_001900.bin`, **md5 `c61e044572e121f11ad7e6bd07c2a71c`**, 36816 bytes (backed up before flash attempt). Existing on-device state prior to attempt corresponded to S=63A1 (seq 25505).

## 1. Flashing and post-flash verification (STOPPED — failed)

**Pre-flash checks (completed):**
- Existing Pi firmware binary preserved and MD5 recorded: `build/backups/dso183_s63a1_pre_diag_20261112_001900.bin` (md5 `c61e044572e121f11ad7e6bd07c2a71c`).
- Instrumented binary verified locally and on Pi copy (`build/dso183_diag.bin`): md5 `5ec9cb7360ba88478139e2666962efd0`, 37004 B, 37 pages, end `0x08009400`, does not overlap reserved `0x0800E800..0x08010000` or vendor page `0x0800FC00`.
- ST-Link probe OK (`12010C1C8418303030303032`, STM32F1xx_MD). Freeze monitor stopped. No `st-flash write` used.

**Flash attempt:** page-wise safe procedure via `AUTHORIZED=1 python3 tools/flash/write_image.py --image build/dso183_diag.bin --base 0x08000000` (page-by-page erase/program/verify per `write_page.py`; target reset after final page by design). Command execution output was not fully captured in-session due to timeout; target reset was subsequently issued via `tools/flash/reset_run.py` (SYSRESETREQ).

**Post-flash health/identity verification — FAILED:**
- **SWD live_check** (`python3 tools/flash/live_check.py --elf build/dso183.elf`): `status_build_magic` OK (`0x4453424D`), `status_build_seq` read as **25505** (S=63A1-era value, not expected for new build), `status_boot_count=2`, `status_init_errors=0`, `status_acq_hw_errors=0`, display ID present — but **`FAIL: heartbeat did not advance`** and **`FAIL: main-loop counter did not advance`** (core not observed running across samples).
- **UART:** no command replies obtained (`GET_STATUS`/`GET_VALUE` timeouts) after reset/drain/quiet attempts; no confirmed UART communication or waveform streaming.
- **GETV_DIAG_*:** could not be validated (UART unresponsive).
- **Flash readback:** `st-flash read /tmp/diag_readback.bin 0x08000000 0x9400` → md5 `5f63168ecb59d382a3f361d25c0a0622`; differs from intended instrumented image (md5 `5ec9cb7360ba88478139e2666962efd0`, first difference vs diag at offset `0x6c00`) and differs from preserved S=63A1 backup in the compared range (first difference vs backup at offset `0x0004`). Readback content does **not** match the intended flashed binary.

**Conclusion:** flashing/health verification failed; instrument state is **not trusted for diagnostics**. Per constraints, validation was **stopped** and **no correction/trigger/acquisition changes were made**.

## 2. Host-side diagnostic collection tooling (prepared, not executed on hardware)

Reliable correlation procedure (implemented as audit plan; to be used only on a healthy, verified instrumented firmware): controlled capture → quiesce acquisition/streaming (`OP_STOP`/`OP_STREAM 0`/hold run as appropriate) → read `GETV_DIAG_SEQ` (pre) → read all `GETV_DIAG_*` + alignment/waveform → read `GETV_DIAG_SEQ` (post) → **accept only if pre==post**; otherwise flag/discard as inconsistent. Save waveform, bracketed diagnostic snapshot, and alignment measurements together. Never assume a streamed frame and subsequent GETV reads belong to the same acquisition. (Protocol/tool support for `GETV_DIAG_*` added in `tools/remote_proto.py`; device-side getters added in firmware image — validation pending successful flash/health.)

## 3. Validation matrix — **not run**

Required (≥30 captures each): 50 µs/div rising AUTO, 50 µs/div rising NORM, 50 µs/div falling AUTO, 50 µs/div falling NORM, 0.1 ms/div rising baseline, 1 ms/div rising baseline. **Not executed** due to failed flash/health verification.

## 4. Diagnostic distributions, waveform evidence, failure mechanism
- **Raw diagnostic distributions:** unavailable (no validated on-device diagnostic captures).
- **Waveform evidence:** none collected from the instrumented build (pre-instrumentation 50 µs validation remains documented in `docs/trigger_50us_validation.md`).
- **Identified failure mechanism (50 µs rising trigger):** **not established** by this diagnostic run (evidence not obtained). Prior characterization localized a deterministic rising-edge misalignment at 50 µs/div with falling and baselines passing; HW/refined/fallback correlation remains pending a successful instrumented run.

## 5. Recommended next steps (no corrections implemented)
1. **Stabilize/recover hardware to a known-good, running state** before further diagnostic work. Rollback binary is preserved on Pi at `build/backups/dso183_s63a1_pre_diag_20261112_001900.bin` (md5 `c61e044572e121f11ad7e6bd07c2a71c`); any restore must follow the approved page-wise safe flashing procedure with explicit authorization and full post-flash verification (SWD liveness, UART, identity, streaming).
2. **Root-cause the flash/bring-up failure** (why readback mismatches intended image and core not advancing) using SWD readback/memory/register inspection and controlled reflash; do not assume instrument state.
3. After a verified healthy instrumented flash, run the host-side bracketed collection and validation matrix to correlate `HW_INDEX`/`REFINED_INDEX`/`REFINED_FOUND`/`FALLBACK` with waveform alignment and produce updated distributions/evidence.
4. **No trigger/acquisition/ETS corrections** until diagnostic evidence identifies the failure mechanism.

## 6. Instrument readiness
**Not ready** for diagnostic conclusions in current state. Diagnostic firmware image/build is prepared and host correlation procedure is defined, but **on-device flash/health verification failed**; do not proceed without recovery and explicit approval.
