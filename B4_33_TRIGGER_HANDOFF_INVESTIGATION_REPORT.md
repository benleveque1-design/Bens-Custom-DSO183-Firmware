# B4.33 — Trigger Arm-to-Detection Handoff Investigation Report

**Date (UTC):** 2026-11-14  
**Scope:** Code investigation and candidate development only (no physical deployment or hardware validation performed/authorised in this task). Source inspected in `/home/print/dso183-firmware-project` at HEAD `246a2b72220a7d7153565c13e4c25ed487ff7ffd` with local modifications described below.  
**Physical failure under investigation:** 50 µs/div rising-edge trigger (AUTO/NORM) produced 100/100 captures with no rising crossing and a falling crossing at ~286 samples (12066 and 12639), while 50 µs/div falling and 0.1/1 ms cases passed (`evidence/B4_33_PROGRAMME_20261114T035819Z/stage_a_trigger/hw_correlation_20261114T064528Z/`, `evidence/B4_33_PROGRAMME_20261114T035819Z/B4_33_DEPLOYMENT_VALIDATION_20261114T214838Z/`).

## 0. Rollback verification status (mandatory safety gate)

**Rollback remains UNVERIFIED in this session.**

- After 12639 primary-validation failure, rollback to preserved recovery image was initiated (prior authorised session) via approved `tools/flash_cycle.py` with recovery `evidence/stage1_ets_campaign_20261113T033944Z/B4_29_CONTROLLED_DEPLOYMENT/preflash_recovery/installed_14139_flash_0x08000000_len0x10000.bin` (65,536 B, SHA-256 `e2bd129fd044f44246e9c5a85547d92f2559f6b25ee72ced4d7fe0eff09949f8`, expected identity 14139 per preserved evidence).
- Last read-only observations during this session (Pi `benabus@192.168.1.201`, `/dev/ttyUSB0`, ST-LINK context as previously used): `pgrep -af flash_cycle.py` indicated a `flash_cycle.py` process still present/running at checked times; UART queries (`GETV_BUILD_SEQ`/status via `tools/remote_proto.py`/`remote_client.SerialLink`) returned unresponsive/timeout (`SerialError('reply timeout for seq 0 op 9')`).
- **Not completed in this session:** confirmation that `flash_cycle.py` has exited; independent approved read-only readback of complete 64 KiB flash and SHA-256 comparison to `e2bd129fd044f44246e9c5a85547d92f2559f6b25ee72ced4d7fe0eff09949f8`; confirmation of UART responsiveness and identity `14139`.
- **No instrument interaction was performed in this session beyond read-only process/UART status checks.** No reset, halt, flash, erase, programming, or disruptive attach performed. Before any further instrument use, the mandatory safety gate must be satisfied (process exited, independent 64 KiB readback hash match, UART responsive with identity 14139) or STOP.

## 1. Exact source-level execution sequence (rearm-to-detection handoff)

### 1.1 Relevant components

- `app/acq.c`: `acq_cycle()` (arm/wait/post-fill/freeze/snapshot), `acq_wait_rearm()`, `acq_latest_sample()`, `apply_trigger_band()`, `acq_isr_adc()`.
- `app/acq.h`: one-sided AWD band with `ACQ_TRIGGER_HYST=8`, trigger index semantics (`ACQ_SAMPLES - CNDTR` at ISR), timing/buffer constants.
- `app/acq_window.h`: ring/window placement (`acq_ring_advance`, pre-trigger placement helpers).
- `app/tim3_hw.c`: TIM3 pacing (`TRGO` update, `MMS=010`, started/stopped around acquisition/freeze).
- `app/main.c`: `trig_refine()` (`TRIG_REFINE_BACK=128`, `TRIG_REFINE_FWD=48`, `trig_scan_nearest` + simple-threshold fallback, `BENCH_PRE=96`), frame placement.
- `app/trigger.c`, `app/trigger.h`: Schmitt rearm+cross scan (`trig_scan_nearest`), slope config, FSM/mode policy.
- `docs/trigger_timing_characterisation.md`, `docs/trigger_50us_validation.md`: timing/bench characterization and prior validation.

### 1.2 `acq_cycle()` sequence (steady-state capture path)

1. **Entry/guards:** validates init/params; clamps timeout to `ACQ_MAX_WAIT_MS`; feeds IWDG at stage boundaries. Sets output defaults; if SINGLE already disarmed returns `ACQ_OK` without touching sample path.
2. **Ring prime (once):** waits bounded for circular DMA buffer to wrap so all samples valid.
3. **Pre-trigger headroom:** waits bounded until `acq_ring_advance(s_resume_pos, write_pos, ACQ_SAMPLES) >= TRIG_PRE_TRIGGER` (samples since last resume/freeze point).
4. **Handoff/arm sequence (critical):**
   - `rearm_ready = acq_wait_rearm()` (bounded wait for rearm-side sample; may return 0 on timeout).
   - **Boundary sample read (post-fix, current candidate):** `s_now = acq_latest_sample()` read after state reset preparation; `arm_safe = acq_handoff_should_arm(rearm_ready, s_now, slope, level, hyst)` i.e. safe iff `s_now` inside current AWD window **regardless of `rearm_ready`**.
     - Rising (`s_trig_slope_hw==0`): band `[0, level+hyst]`, safe when `s_now <= level+hyst`.
     - Falling: band `[level-hyst, 4095]`, safe when `s_now >= level-hyst`.
   - `arm_generation_bump(); hw_capture_invalidate();`
   - `ADC1->CR1 &= ~ADC_CR1_AWDIE;` (disarm watchdog interrupt)
   - `ADC1->SR = 0U;` (clear ADC status flags, including AWD, by software)
   - `s_event = 0; s_trigger_index = 0;`
   - If `arm_safe`: `ADC1->CR1 |= ADC_CR1_AWDIE;` else leave disabled (cycle proceeds to timeout/mode policy without spurious mid-plateau latch).
   - *(Pre-fix behavior, 12639): when `rearm_ready==1`, `arm_safe` was forced true without boundary recheck; only timeout path checked current sample.)*
5. **Trigger wait:** polls `s_event` (set by `acq_isr_adc`) until trigger, HW error, or timeout; feeds IWDG per ms. On timeout sets `s_event=2`.
6. **Post-trigger fill:** if triggered, waits bounded (timebase-scaled) for `acq_ring_advance(base=s_trigger_index, pos, ACQ_SAMPLES) >= post_samples`.
7. **Mode policy:** `trig_fsm_action` decides CAPTURE/FALLBACK/HOLD (AUTO fallback on timeout, NORM/SINGLE hold without new capture as applicable).
8. **Freeze/snapshot:** `tim3_hw_stop()`, gate ADC DMA requests (`ADC1->CR2 &= ~ADC_CR2_DMA`), settle, `memcpy` ring to snapshot, record `s_snap_pos`.
9. **Resume:** `pacing_resume()` restarts pacing/DMA-request path for next cycle; returns triggered index or timeout.

### 1.3 `acq_wait_rearm()` and sampled values

- Polls `acq_latest_sample()` in a bounded loop (derived from `timebase_tick_ns(s_hw_timebase)`, capped 20 ms; feeds IWDG per ms tick). `acq_latest_sample()` reads last completed DMA-written sample: `ndtr = DMA1_CNDTR`, `pos = (ACQ_SAMPLES-ndtr)%ACQ_SAMPLES`, `idx = (pos-1)%ACQ_SAMPLES`, returns `s_buf[idx]`.
- **Rearm condition (far side):** Rising waits until `s <= level-hyst` (`acq_handoff_rearm_ready`); Falling waits until `s >= level+hyst`.
- Returns 1 when observed, 0 on timeout. Return means "a qualifying sample was observed at some poll time", not that current/boundary sample remains safe.

### 1.4 AWD configuration and enable point

- `apply_trigger_band()`: Rising `LTR=0, HTR=min(level+hyst,4095)`; Falling `LTR=max(level-hyst,0), HTR=4095` (`ACQ_TRIGGER_HYST=8`). Band kept consistent with slope/level.
- ADC configured for TIM3 TRGO-triggered conversions (`EXTSEL` TIM3_TRGO, `EXTTRIG`, `DMA`), regular channel `ACQ_SIGNAL_CHANNEL` (ch2/PA2), AWD single-channel on regular group (`AWDCH|AWDSGL|AWDEN`), NVIC for `ADC1_2_IRQn`/`DMA1_Channel1_IRQn` prio 2.
- `ADC_CR1_AWDIE` is the enable point for watchdog interrupt generation in this path; it is set conditionally after `ADC_CR1_AWDIE` clear, `ADC_SR=0`, and boundary safety check (post-fix ordering).

### 1.5 First conversion capable of generating AWD interrupt / ISR / CNDTR latch

- TIM3 paces conversions continuously; DMA writes ring continuously. A conversion completing after AWDIE is enabled and with converted sample outside `[LTR,HTR]` sets AWD; with AWDIE set this generates `ADC1_2_IRQHandler` -> `acq_isr_adc()`.
- ISR (if `s_inited` and `SR.AWD`): disables `ADC_CR1_AWDIE` (one-shot), clears `SR`, captures `cnt=TIM3->CNT`, `ndtr=DMA1_CNDTR`, computes `idx = (ndtr<=ACQ_SAMPLES)?(ACQ_SAMPLES-ndtr):0` as `trigger_index` (latched HW position; lags triggering sample by ADC/DMA/ISR timing per code comments), updates guarded HW capture record, sets `s_event=1` and `s_trigger_index=idx` on first event for the arm, increments trigger count.
- Refinement/fallback in `trig_refine()` operates on frozen ring snapshot around HW `trigger_index` (±128/48), selecting nearest Schmitt crossing or simple-threshold fallback to HW index.

### 1.6 Conversions and threshold crossing during handoff

- **Conversions continue throughout** the rearm wait and arm sequence (TIM3/DMA/ADC not stopped). `acq_latest_sample()` observes latest completed DMA sample but is not atomic with enable.
- The signal **can cross the threshold between operations**: between a successful rearm observation and boundary check/`SR` clear/`AWDIE` enable, or between boundary check and `AWDIE` enable, one or more conversions may complete with values transitioning across `level±hyst`.
- **Physical possibility:** yes. At 50 µs/div sample period 1.5625 µs (1 kHz square, ~640 samples/cycle, ~320 high/320 low), a transition from rearm side to outside-window can occur within a few samples; CPU/register operations and any latency provide ample time for intervening conversions.

## 2. Timing/order diagram (rearm-to-AWDIE handoff)

### 2.1 Intended/observed order (post-fix, current candidate)

```
Time  CPU / firmware                          ADC+DMA (continuous)              AWD state
----  --------------------------------------   --------------------------------  -----------------------------
 t0   acq_wait_rearm() polls latest sample     conversions N,N+1,... ongoing      AWDIE off (previously disarmed)
      sees rearm-side sample (e.g. <= lo)      DMA writes ring
 t1   rearm_ready=1, returns                   conversion(s) may cross band      (still disarmed)
 t2   arm_generation_bump/invalidate           ...
 t3   ADC1->CR1 &= ~AWDIE                      ...
 t4   ADC1->SR = 0 (clear stale flags)         out-of-window conv here -> AWD set
 t5   reset s_event/s_trigger_index            but SR cleared at/after? (race)   depends on completion timing
 t6   s_now = acq_latest_sample()  <--- boundary safety sample
 t7   arm_safe = inside_window(s_now)?         conversion may complete out-window
      if safe: ADC1->CR1 |= AWDIE  <--- enable point
 t8                                            next/outstanding out-window conv  AWD sets; IRQ if AWDIE set
 t9                                            ADC1_2 IRQ -> acq_isr_adc()       latches CNDTR idx, disarms AWDIE
```

**Critical property:** `s_now` at `t6` is the basis for enabling AWDIE at `t7`; conversions between `t6` and `t7` (and boundary effects around `t4-t5`) are not observed by that check.

### 2.2 Pre-fix (12639) defect path (H1)

```
t0  acq_wait_rearm() observes rearm-side sample -> returns rearm_ready=1
t1  ... conversions continue; signal crosses to outside window (e.g. high plateau)
t2  code sets arm_safe = rearm_ready (NO boundary sample check)
t3  SR cleared, AWDIE enabled  ==> immediate/early AWD latch at arbitrary phase
t4  HW trigger_index displaced (can be mid-plateau, e.g. +129 class from true edge)
t5  trig_refine misses true edge (outside 176-sample window / no valid rearm+cross)
t6  frame placed at HW/fallback index => no rising crossing, late falling ~286/287
```

This is physically possible and matches the observed signature.

## 3. Hypothesis findings (H1–H4)

### H1 — Rearm-to-enable TOCTOU race (rearm valid, boundary no longer safe)
- **Source-level evidence (confirmed):** In pre-fix 12639 logic (`app/acq.c`), when `acq_wait_rearm()` returned ready, `arm_safe` was set to `rearm_ready` and AWDIE could be enabled without rechecking `acq_latest_sample()` against the safe AWD window at the arming boundary; only the timeout path performed a current-sample safety check. `acq_wait_rearm()` returns on observing a past/polled rearm-side sample while conversions continue.
- **Hardware timing/peripheral behavior:** TIM3-triggered ADC conversions and DMA circular writes continue during handoff. AWD flag is set by hardware on an out-of-window conversion for the watched channel; enabling AWDIE when condition is/will become true around enable can produce immediate/prompt latch. Boundary state can change within 1.5625 µs/sample at 50 µs/div.
- **Explains observed physical signature:** Yes (highly consistent). A latch occurring ~129 samples after the true rising edge places that edge outside the 128-sample-back refinement search (`trig_refine`), causing refinement/fallback failure and a frame with no rising crossing and falling crossing near 287 — reproducing 12066/12639 physical results and the characterized `test_rising_50us_rearm_sensitive_scenario` geometry. 12639 changed only timeout-unsafe handling, so an unchanged 100% failure is expected if rearm-observed-then-unsafe was the active case.
- **Confidence:** **High (code defect confirmed). Medium-high (likely contributor/cause of physical failure).** Physical causation not directly proven without correlated HW/refined telemetry on hardware.
- **Discriminators:** Correlated per-capture `DIAG_SEQ`/`HW_INDEX`/`REFINED_INDEX`/`FOUND`/`FALLBACK`/frame showing HW anchor displaced mid-plateau at arm events where rearm was observed; or observing boundary sample outside window at would-be enable.

### H2 — ADC status/enable ordering and insufficient atomicity
- **Source-level evidence:** Sequence disarms AWDIE, clears `SR`, resets event/index, then conditionally enables AWDIE — correct to avoid latching an old crossing, but not atomic with ongoing conversions. A conversion completing out-of-window between `SR` clear and enable, or between boundary check and enable (residual in post-fix), can set AWD and fire promptly once enabled.
- **Hardware behavior:** STM32F1 ADC AWD/SR semantics make flag timing relative to conversion completion and software clear relevant; without halting conversions there is an unavoidable observation-to-enable gap.
- **Explains observed signature:** Yes, as part of the same handoff window (immediate/early latch). Distinct from H1 only in that even a correct "boundary check" can race with subsequent conversion.
- **Confidence:** **High** that residual atomicity/TOCTOU exists; **Medium** that this (beyond H1's missing check) alone explains unchanged 12639 result.
- **Discriminators:** Telemetry/instrumentation of samples/state around clear/enable; experiments with conversion gating/atomic sequencing (architectural) vs check-only fix.

### H3 — AWD interrupt position latency (CNDTR-derived index displaced)
- **Source-level evidence:** `trigger_index` captured from `ACQ_SAMPLES - CNDTR` in ISR; subject to conversion-to-ISR latency and CNDTR progression.
- **Hardware behavior:** IRQ latency (NVIC prio 2, SysTick prio 0 can preempt), DMA update timing; at 1.5625 µs/sample small latencies shift index by samples, but refinement window (128 back/48 fwd) tolerates moderate error. A +129-sample-class displacement is larger than typical ISR latency alone and more consistent with late/incorrect arming (H1/H2).
- **Explains observed signature:** Partially possible for small errors, but **unlikely as primary cause** of deterministic +~190-sample misplacement relative to pre-trigger target given passing falling/other ranges and refinement margin.
- **Confidence:** **Low-medium** as primary; **Medium** as secondary contributor.
- **Discriminators:** Compare HW index vs true edge with correlated diagnostics; compare against hardware-timed capture (stock `TIM4_CCR1`-style) if feasible.

### H4 — Refinement failure (reasonable HW anchor, SW refinement misses edge)
- **Source-level evidence:** `trig_refine` uses Schmitt nearest-cross search in bounded window then simple-threshold fallback else HW index; rising/falling configured by slope. Existing logic finds edge when present in window (host characterization and passing cases).
- **Explains observed signature:** As a **consequence** of displaced HW anchor (edge absent from searchable/rearmable segment), yes. As an independent primary cause for rising-only at 50 µs with correct anchor, evidence is weak (falling passes, signal periodic).
- **Confidence:** **Low** as primary cause; **High** as failure mode given bad anchor.
- **Discriminators:** Correlated HW/refined/found/fallback telemetry: if HW anchor already mid-plateau and refined not found, refinement is downstream.

## 4. Confirmed root cause / status

- **Confirmed (source-level):** **H1 code defect** — when `acq_wait_rearm()` observed readiness, arming logic treated readiness as safety (`arm_safe = rearm_ready`) and could enable `ADC_CR1_AWDIE` without verifying the current sample was inside the AWD window at the actual enable boundary, despite continuous conversions. This is a real rearm-to-enable TOCTOU/unsafe-arm condition and is sufficient in principle to produce the observed mid-plateau-latch/refinement-miss signature.
- **Not proven:** that this specific defect was the sole physical cause of the 12639 field failure (no correlated HW telemetry, no post-fix hardware validation). A **residual TOCTOU** remains between boundary sample check and `AWDIE` enable because conversions continue; therefore the handoff is improved but not guaranteed race-free under current peripheral architecture.

## 5. Fix description and rationale (implemented, candidate only)

### 5.1 Changes

- **`app/acq.c`:** In `acq_cycle()` arm sequence, always evaluate arm safety from `acq_latest_sample()` at the arming boundary using `acq_handoff_should_arm(rearm_ready, s_now, slope, level, hyst)`; never derive safety from `rearm_ready` alone. Reordered sequence to disarm/clear/reset state first and sample immediately before conditional `ADC_CR1_AWDIE` enable to minimise TOCTOU. `acq_wait_rearm()` rearm predicate factored to shared pure helper. Comments document residual TOCTOU and non-guaranteed race freedom.
- **`app/acq_handoff.h` (new, pure/host-testable):** `acq_handoff_rising_hi`, `acq_handoff_falling_lo`, `acq_handoff_rearm_ready`, `acq_handoff_arm_safe`, `acq_handoff_should_arm` (boundary sample governs safety; rearm readiness not sufficient).
- **`tests/test_acq_handoff.c` (new):** Host tests modeling temporal handoff ordering and boundary decisions (see §6).

### 5.2 Rationale and how it addresses the identified race

- Closes the **confirmed H1 defect**: a sample observed as rearm-ready earlier can no longer authorize enabling AWDIE if the boundary sample is outside the watchdog window; unsafe boundary (including rearm-then-crossed-during-handoff, or timeout-still-unsafe) results in no arm for that cycle and bounded timeout/mode-policy handling (no spurious mid-plateau latch from this path).
- Preserves trigger semantics (AWD bands, modes/policies, refinement, diagnostics) and avoids unbounded waits/retry loops or IWDG-starving busy loops.

### 5.3 What is not closed

- **Residual TOCTOU:** a conversion may complete outside the window after the boundary check but before/around `AWDIE` enable (narrow window). With continuous conversions and no conversion gating/atomic arm primitive, this cannot be fully eliminated in software alone.
- **Minimum architectural options if residual race proves relevant:** (a) briefly gate/stop conversions or ADC DMA triggering around the arm commit and resume safely; (b) arm-disable-recheck protocol acknowledging that a "legitimate immediate crossing" vs "stale/late" case must be distinguished; (c) hardware timestamp/capture-based trigger position/phase (e.g. timer input-capture / stock `TIM4_CCR1`-class approach, subject to pin/timer/schematic feasibility and TIM4 cal-output conflict) for robust positioning and ETS; plus correlated per-capture diagnostics to prove behavior.

## 6. Tests added and results

### 6.1 Added/updated

- **`tests/test_acq_handoff.c` (new, 13 cases, host/pure):** models temporal ordering (not static-only):
  - rising rearm-safe then arm; rising rearm-then-crosses-before-enable (must not arm);
  - rising boundary limits (inside/edge/outside, saturation behavior);
  - falling rearm-safe then arm; falling rearm-then-crosses-before-enable (symmetric);
  - falling boundary limits;
  - rearm-timeout path safety (arm only if boundary inside window);
  - temporal sequence across handoff (`rearm seen` then samples move inside→outside over time; decision follows boundary, not stale readiness);
  - status-clear/enable residual TOCTOU model (checked-safe vs changed-at-enable distinction documented/asserted);
  - rising/falling symmetry; hysteresis saturation/rails;
  - bounded retry/abort decision model over finite sample sequence (one decision per attempt, no unbounded loop);
  - AUTO/NORM/SINGLE mode policy regressions via `trig_fsm_action`.
- **Existing regression preserved:** `tests/test_trigger.c::test_rising_50us_rearm_sensitive_scenario()` and all prior trigger tests unchanged and passing.

### 6.2 Results (host)

- **Full host test suite:** `make -s -j4 test` **PASS (RC 0)**, all test binaries passed including `acq_handoff: all tests passed` and `trigger: all tests passed` (34 binaries total in current suite run: `acq_stats acq_window adc_owner bench cal cal_table continuity crc16 diag ets_diag ets_epoch ets_integration ets_phase ets_source flash_layout frame glyph ladder link link_cmd link_e2e link_host measure pipeline render runstate settings tim3 timebase trigger ui ui_display_math waveform wave_store` plus new `acq_handoff`).
- **Relevant mutation tests:** no dedicated mutation-test target identified in `Makefile` beyond the standard guard/test suite; none run separately. **Gap noted.**

### 6.3 Simulation/coverage gaps

- No host execution of HAL/ADC/DMA/ISR/timing behavior (`acq_cycle` hardware path not unit-simulatable in host tests); tests cover pure predicates, modeled ordering, and trigger/refinement/FSM logic, **not physical register timing or true concurrent conversion/IRQ interleaving**.
- No correlated on-target HW/refined telemetry validation in this task (and current protocol/continuous acquisition produced 600/600 `seq_mismatch` in prior campaigns).
- Therefore passing simulation/tests **do not prove** the physical 50 µs/div rising-trigger handoff is fixed.

## 7. Candidate build identity and hashes (not deployed)

- **Build sequence:** `60915` (generated `build/build_seq.h`: `#define STATUS_BUILD_SEQ 60915`).
- **Artifacts (`build/`):**
  - `build/dso183.bin`: 40,044 bytes, SHA-256 `60de8ca2af9f74953c4fa62a4e554d1ddcb5057573cb9d948f922f26ccb984cc`
  - `build/dso183.elf`: 1,027,444 bytes, SHA-256 `0c6d2c1b150cf782a55b69d091cbb07194d5372ccd88fd4fb1affc6f971e67ac`
  - `build/dso183.map`: 454,563 bytes, SHA-256 `a04a02e2b64c9073872f2d99549cb1dd711547c9c52d58597b41e32c54cab64e`
- **Source revision / working-tree state:** base `246a2b72220a7d7153565c13e4c25ed487ff7ffd` (`246a2b7 docs: add repo banner image to README`); working tree **not clean** (intentional, uncommitted): modified `app/acq.c`; untracked `app/acq_handoff.h`, `tests/test_acq_handoff.c`; `build/` rebuilt to 60915. Diff stat: `app/acq.c | 41 insertions(+), 49 deletions(-)`.
- **Build:** `make -j4` RC 0. Note: pre-existing-style warning `app/acq.c:241:13: warning: 'wait_trigger_rearm' defined but not used [-Wunused-function]` (unused wrapper retained; no behavioral impact).

## 8. Memory usage

- **From `arm-none-eabi-size` / build summary (`build/dso183.elf`):** `text 39904`, `data 136`, `bss 18512`, `dec 58552`.
- **Flash:** 40,040 B / 61,440 B (65%) `[text 39904 + data 136]`.
- **RAM:** 18,648 B / 20,480 B (91%) `[data 136 + bss 18512]`, ~**1,832 bytes headroom** (within existing ~1,840 B constraint).

## 9. Remaining risks and recommended next investigation

- **Rollback unverified:** instrument recovery state not confirmed in this session (see §0); must satisfy safety gate before any instrument interaction.
- **Residual TOCTOU / not race-free:** boundary check is immediately before enable but conversions continue; a conversion completing out-of-window in the check→enable gap can still latch. Physical sufficiency of fix for 50 µs rising is **unproven** without authorised deployment and correlated diagnostics.
- **Diagnostic correlation gap:** prior campaigns lacked coherent per-capture HW/refined/fallback correlation (`seq_mismatch`); without it H1/H2/H3 cannot be distinguished on hardware.
- **Architectural gap vs stock:** in-repo evidence indicates stock/vendor trigger position derived from `TIM4_CCR1` (hardware-timed) while firmware uses AWD ISR+CNDTR+SW refinement; TIM4 currently used for cal/test output in `app/main.c`. Robust alignment and ETS phase quality may require hardware timing/capture feasibility review against provided vendor docs (schematic/circuit/user/assembly PDFs) and possible redesign.
- **Next steps (when authorised):** (1) complete/verify rollback safety gate; (2) deploy 60915 candidate only with explicit authorisation via approved `flash_cycle.py`; (3) capture correlated diagnostics and re-run 50 µs rising/falling AUTO/NORM + regressions (same signal/method, ≥50/cfg) requiring rising crossing within ±2 of 96 in ≥95% valid captures/config with no recurrence; (4) if residual race implicated, evaluate conversion-gating or hardware timestamp/capture options.

## 10. Final decision

**`ROOT CAUSE CONFIRMED — FIX NOT YET SAFE`**

**Justification:** A definite source-level handoff defect is confirmed (H1): pre-fix code could enable `ADC_CR1_AWDIE` after observing rearm readiness without revalidating that the current sample was inside the AWD window at the actual arming boundary, allowing a TOCTOU unsafe arm consistent with the observed mid-plateau latch/refinement-miss signature. A minimal boundary-safety fix (with factored pure predicates and temporal-order host tests) is implemented in candidate 60915, but a residual check-to-enable TOCTOU remains under continuous conversions and no physical deployment/validation was authorised or performed; therefore the fix is **not yet proven safe/sufficient** for the hardware failure. Rollback verification remains unverified (see §0).
