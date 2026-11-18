# Test procedure (DSO183 firmware)

This document describes the repeatable procedure for verifying DSO183 firmware on
host and on hardware, with the goal of making the steps easy to automate in a
future test harness. It reflects the B4.49-era host/remote test infrastructure
plus the UART starvation fix (#16) validated in B4.51 on branch
`fix/uart-link-starvation`.

**Validated candidate (B4.51, specific — not a permanent expected build
identity):** build sequence **13856 (`0x3620`)**, binary **41,316 bytes**,
binary SHA-256
`05b01f605fcac49b630c22a4928f8b030070ed15088d85e56845e5468351e9f7`. Hardware
validation evidence: `evidence/B4_51_HW_VALIDATION_20261116T023134Z/`
(gitignored, local). The earlier B4.49 dev reference (build seq **25691 /
`0x645B`**) is retained below only as a clearly labelled historical example.

**Scope:** host unit/integration tests, build identity capture, safe deployment,
and remote/hardware validation (including faster timebases and remotely
adjustable settings). It is **not** a substitute for bench-only validation
(acquisition/trigger/ETS timing, DMA/ADC/TIM3/ISR behaviour, display/visual
checks, or full HW equivalence claims).

---

## 1. Preconditions and safety

- **Toolchain:** `arm-none-eabi-gcc/objcopy/size/nm`, GNU Make, Python 3. Host
  tests and guards are pure host-side; no hardware/peripheral/HAL mocks are
  required.
- **Hardware access (remote validation):** instrument reachable over UART
  (e.g. `/dev/ttyUSB0`, 115200 8N1, 3.3 V) and a debug probe (ST-Link/pyocd)
  for verified flashing/identity/reset. In a validated setup this can run on a
  remote bench host (e.g. `user@bench-host`) with the copied artefacts/tools.
- **Deployment safety (mandatory):**
  - Flash/deploy **only** when explicitly authorised.
  - Use **only** `tools/flash_cycle.py` for full-image deployment (page-wise
    erase/program, **page 0 / vector table written last**, per-page and
    whole-image read-back verification, ELF/flash/RAM identity checks, reset
    release).
  - **Never** use `st-flash write` on STM32F1 (can mass-erase calibration/
    settings and the vendor record).
  - **Never** write the protected settings/calibration page at `0x0800FC00`.
  - Do not perform calibration writes, flash-store writes, or destructive
    operations as part of routine regression testing unless explicitly required
    and authorised.
- **UART/link behaviour (B4.51-validated candidate):** UART is shared with console
  output; remote clients scan for protocol frames. Protocol servicing now occurs
  cooperatively during acquisition waits. `NOP`, `GET_STATUS` and `GET_VALUE`
  (read-only) are serviced immediately during an active acquisition cycle, while
  mutating commands are deferred until that cycle completes (no reentrant
  acquisition/DMA/TIM3 mutation). The deferred queue is **single-slot**; the
  current host client uses sequential request/reply operation. Multiple pipelined
  mutations can overwrite an earlier deferred command and were **not** tested by
  the sequential host client (known limitation). A deferred reply can arrive
  later than a normal host timeout, particularly at slow timebases — distinguish
  a **late reply** (command executed/replied after delay) from a **lost command**
  or a genuinely **unresponsive link**. Do **not** assume every command completes
  within four seconds. The B4.51 campaign validated the original #16 reproduction
  and relevant regression cases on the tested instrument (see §5.2, §6); do not
  generalise beyond the tested instrument and candidate. For genuine transport or
  core failures, SWD reset remains an appropriate recovery mechanism with identity
  verification afterward (see §5.3). Treat other transient transport issues as
  environmental until reproduced with a clean sequence.

## 2. Host verification (required before HW)

Run from the repo root. Capture exit codes and full logs for the test record.

| Step | Command | Pass criteria |
|------|---------|---------------|
| Unit/integration suites | `make test` | exit 0; all suites pass (currently 38, including `test_menu`, `test_acq_policy`). |
| Static/code guards | `make guard` | exit 0; `guard: 0 issue(s)`. |
| Size/budget | `make size` (or `sh tools/size_report.sh build/dso183.elf`) | `OK: within the flash and RAM budget` (flash ≤ 61440 B app area, RAM ≤ 20480 B). |
| Docs/consistency (handover baseline) | `make check` | exit 0 for a release/handover-consistent tree. **Note:** dev builds that change size/build-seq without updating `FLASH_HANDOVER.md` may fail this check intentionally; record the reason if not applicable. |

A harness should treat `make test` + `make guard` as the fast gate and record
`make check` separately when validating a tagged/handover build.

## 3. Build and identity capture (record for every HW run)

1. Build the image: `make` (produces `build/dso183.bin`, `build/dso183.elf`,
   `build/build_seq.h`).
2. **Ensure the binary is complete.** The flash image must include initialised
   `.data` (LMA/init image) so runtime state (e.g. `status_build_seq`,
   `status_build_magic 0x4453424D`) matches the ELF. Regenerate explicitly when
   in doubt:
   ```bash
   arm-none-eabi-objcopy -O binary build/dso183.elf build/dso183.bin
   ```
   (A truncated/malformed bin missing `.data` will flash "successfully" by page
   verification but boot with wrong runtime identity/initialised data.)
3. Capture identity/artefacts (store under `evidence/<run>/`):
   - `sha256sum build/dso183.bin build/dso183.elf`
   - `wc -c build/dso183.bin`
   - `cat build/build_seq.h` (expected `STATUS_BUILD_SEQ`)
   - `sh tools/size_report.sh build/dso183.elf` and/or `arm-none-eabi-size`
   - `git status -sb` / `git rev-parse --short HEAD` (baseline + local deltas)

Example (B4.51 validated candidate, for this procedure): seq **13856
(`0x3620`)**, magic `0x4453424D`, bin **41,316 B** SHA-256
`05b01f605fcac49b630c22a4928f8b030070ed15088d85e56845e5468351e9f7`.

Historical example (B4.49 corrected dev image, retained for reference only): seq
**25691 (`0x645B`)**, magic `0x4453424D`, bin **41132 B** SHA-256
`4e54e509d28634cf38ba11da536abc871476fb2f04fd07cc8fb91bda176af020`.

## 4. Safe flashing and deployment verification

Run `flash_cycle.py` against the built image/ELF on the host attached to the
probe (or on the remote bench host with the copied artefacts/tools):

```bash
python3 tools/flash_cycle.py --image build/dso183.bin --elf build/dso183.elf
```

Pass criteria (all required, abort on any failure):
- Every programmed page written and read-back verified; **page 0 written last**.
- Whole-image byte-for-byte SWD read-back matches the image.
- Queued system reset released so the core actually resets.
- Running RAM identity matches the ELF: `status_build_magic == 0x4453424D` and
  `status_build_seq` equals the ELF/build-seq value.
- Liveness/measurement status printed without error.

Post-flash cross-checks (recommended for harness evidence): UART `GETV_BUILD_SEQ`
and SWD read of `status_build_seq`/magic agree with the expected build; record
the `flash_cycle` log.

## 5. Remote/hardware validation

Use the PC-link tools over the console UART. Auto-detects `/dev/ttyUSB*` etc.
unless `--port` is given.

### 5.1 Live protocol/control/stream suite

```bash
python3 tools/remote_run.py --live [--port /dev/ttyUSB0]
```

- Runs offline codec/control-policy suites plus live transport/control/stream
  suites. Exit 0 = all executed suites passed.
- **Interpretation:** compare failures against *current* firmware expectations,
  not stale assumptions (see §6). A harness should pin expected values to the
  build/protocol under test (e.g. `TIMEBASE_COUNT`, REPT acceptance, NOP value
  semantics).

### 5.2 Targeted remote adjustments (regression/coverage)

Exercise and read back settings that are adjustable remotely (repeatable script
or harness cases). Record set/get status and values:

- **Run/stop & status:** `OP_RUN`, `OP_STOP`, `GETV_STATUS`.
- **Trigger mode:** 0/1/2 cycle and read back (`GETV_TRIG_MODE`).
- **Trigger slope:** 0/1/2 set/read back (`GETV_TRIG_SLOPE`).
- **Trigger level:** representative values including boundaries/clamping
  (e.g. 0, mid, 4095; out-of-range behaviour is protocol-defined — see §6) and
  read back (`GETV_TRIG_LEVEL`).
- **Timebase sweep (including faster ranges):** set/read back every valid index
  **0..(TIMEBASE_COUNT-1)** (currently 0–23, which includes fast/repetitive
  REPT 0–4) and confirm an out-of-range index (e.g. 24) is rejected.
- **Span:** representative set/read backs (`GETV_SPAN`) noting observed
  acceptance/normalisation.
- **Streaming:** toggle on/off; where stable, capture ≥1 valid frame and sanity
  range-check samples.
- **Calibration (remote):** offset/gain set/get, `CAL_SAVE`, and `CAL_RESET` read-backs
  with correct reply status. `CAL_SAVE` and `CAL_RESET` perform immediate calibration
  persistence attempts by design (flash write on success; on failure the firmware
  preserves retry/pending state and must not report successful persistence). Offset/
  gain edits may schedule debounced calibration persistence; treat power-cycle
  persistence claims as requiring explicit pre/post evidence (see #12).
- **Averaging:** set/read back supported values.
- **Wave save/recall:** save slots, verify saved flags, recall while stopped.
- **UART starvation regression (#16):** `RUN`; two paced `SET_TIMEBASE` +
  `GET_VALUE` sweeps over indices 0–23; repeat with rapid/no-delay pacing.
  Exercise repeated `GET_VALUE` and `GET_STATUS` queries at slow timebases
  18–23 during acquisition waits. Verify correct replies/readbacks and
  **distinguish late replies from host timeouts or lost commands** (a deferred
  mutating reply may arrive after a normal client timeout but still complete
  correctly; read-only queries are serviced immediately but can still be delayed
  under sustained slow-stage blocking). Record timeliness separately from
  correctness.

A harness should treat each case as: send command → assert reply status/value →
read back getter → assert expected/effective value, with host timeouts and reply
latency recorded distinctly where relevant.

### 5.3 Recovery and sequencing

- SWD reset remains an appropriate recovery mechanism for **genuine transport or
  core failures** (not the expected recovery for the original #16 starvation
  failure on the B4.51-validated candidate). Release the core so reset fires
  rather than using unsafe flash operations, and **re-verify build identity
  after recovery** before continuing.
- Slow-timebase acquisition can produce long response latency. Host timeout
  settings must account for deferred mutations and acquisition-cycle duration;
  do not treat a late reply as a lost command or an unresponsive link without
  corroborating evidence.
- Allow brief settle delays around timebase changes and after stream toggles to
  reduce sequencing artefacts. Prefer clean, ordered sequences over rapid
  interleaved commands when characterising behaviour, and use paced request/reply
  clients when measuring timeliness.

### 5.4 Evidence to retain

Per run: host logs (`make test/guard/size`), build identity (hashes, seq,
size), `flash_cycle` log, `remote_run --live` log, targeted-adjustment
results (structured JSON/log), and any SWD recovery/identity notes. Store under
`evidence/<run>/` (gitignored in this repo).

## 6. Known expectation mismatches, B4.51 observations, and interpretation

### Known expectation mismatches (current protocol/behaviour)

These are **not** confirmed regressions of the B4.46/B4.49 refactors when
observed against firmware where `TIMEBASE_COUNT == 24` and REPT 0–4 are valid:

- `SET_TIMEBASE 26` → rejected (BAD_ARG) is correct for 0–23.
- `SET_TIMEBASE 4` → accepted (ST_OK) is correct (REPT/fast range is valid).
- `NOP` argument ignored; returned value may reflect build/implementation detail
  (treat as informational unless the protocol contract specifies otherwise).
- `SET_TRIGGER` out-of-range (>4095): `link_cmd_arg_valid` may reject the
  command while `acq_set_trigger_level` clamps internally — a BAD_ARG with level
  read-back still at the prior/clamped value is consistent with this split, not
  by itself a new regression (confirm against intended protocol semantics before
  filing).
- A stream "frames_captured == 0" inside a mixed suite run, when frames are
  captured after a clean reset/sequence, indicates sequencing/transport
  sensitivity rather than a confirmed acquisition regression.

### B4.51 observed results (validated candidate 13856 / `0x3620`, tested instrument only)

The following were observed on the B4.51 validated candidate with
extended-timeout measurement and late-vs-timely classification (do **not**
generalise beyond the tested instrument and candidate):

- **#16 paced sweep (2× `SET_TIMEBASE` + `GET_VALUE` over 0–23, ~50 ms pacing,
  after `RUN`):** 98/98 correct replies/readbacks.
- **Rapid/no-delay sweep:** 98/98 correct replies/readbacks.
- **`python3 tools/remote_run.py --live`:** 62/62 passed (codec/control/serial/
  stream suites as executed).
- **Timeouts:** zero observed timeouts or lost replies in the extended-timeout
  validation runs; no link unresponsiveness reproduced for the #16 scenario.
- **Latency:** some responses exceeded four seconds (late but correct), with a
  maximum observed query latency of approximately **12.5 seconds** (consistent
  with deferred mutating replies and/or sustained slow-stage acquisition waits).
- **Known limitation:** single-slot deferred-mutation overwrite behaviour was
  **not** tested by the sequential host client and remains a known limitation.

**Bug filing:** only add GitHub issues for **confirmed, reproducible** bugs with
evidence, expected vs actual behaviour, and an isolated reproduction (build seq,
exact command sequence, recovery). Do not file issues for the expectation
mismatches above or for late-but-correct replies within the documented behaviour.

## 7. Building a future test harness

Recommended structure (incremental, host-first, HW-optional):

1. **Harness core:** a runner that executes host gate (`make test`, `make guard`,
   optional `make check`), collects JUnit/JSON-like results, and enforces exit
   codes.
2. **Build/identity module:** build, regenerate/verify bin-vs-ELF `.data`
   completeness, compute hashes/seq/size, and emit a run manifest.
3. **Deploy module (authorised HW only):** wrap `tools/flash_cycle.py`, require
   explicit opt-in, parse/record pass/fail of each safety/verification stage.
4. **Remote module:** wrap `tools/remote_client.py`/`remote_proto.py` with
   - expectation tables parameterised by firmware/protocol version
     (`TIMEBASE_COUNT`, valid REPT range, trigger/span semantics),
   - ordered scenarios: identity → control → per-setting sweeps/read-backs →
     stream capture → restore defaults,
   - bounded retries, settle delays, and SWD-reset recovery hooks (bench-only,
     authorised).
5. **Artifacts:** normalise logs/results/JSON per run under `evidence/` or a CI
   artifact dir; flag transport anomalies separately from functional failures.
6. **Keep HW-coupled logic out of host scope:** do not mock DMA/ADC/TIM3/ISR
   timing or claim HW coverage from host tests; treat remote functional checks as
   sampled/integration evidence per validated build.

This procedure, plus the existing `make test` suites and `tools/remote_*` /
`tools/flash_cycle.py` scripts, provides the baseline a harness can orchestrate
without changing firmware behaviour.
