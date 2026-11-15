# B4.34 — Trigger Architecture Decision

**Date:** 2026-11-14 (UTC)  
**Status:** Candidate-only (no deployment, no physical validation)  
**Related:** `B4_34_AUTONOMOUS_TRIGGER_ARCHITECTURE_INVESTIGATION.md`

## What is actually broken

- **Observed symptom (12066, 12639):** 50 µs/div rising-edge trigger (AUTO/NORM) fails 100/100 under prior validated conditions: no rising crossing in captured frame and falling crossing near ~286 (displaced from pre-trigger target 96), while 50 µs falling and 0.1/1 ms cases pass.
- **Confirmed software defect (H1):** pre-60915 arming could enable `ADC_CR1_AWDIE` after `acq_wait_rearm()` observed readiness **without** revalidating that the boundary sample was inside the AWD window at enable time, allowing a rearm-observed→crossed-during-handoff unsafe arm (TOCTOU) consistent with mid-plateau/late latch and refinement miss.
- **Residual issue in 60915:** boundary check was added but conversions continued between check and enable, leaving a **residual check→enable TOCTOU**; physical failure remained on 12639-class evidence and 60915 was not physically proven.

## Strongest evidence

- Source-level trace of `acq_cycle`/`acq_wait_rearm`/AWD ISR/refinement (`app/acq.c`, `app/main.c`, `app/trigger.c`) and continuous TIM3→ADC→DMA sampling during handoff.
- Physical campaign evidence: deterministic 100/100 rising failure signature at 50 µs (indices ~286–287 class) vs aligned falling/regressions (B4.33 artifacts/reports).
- Refinement-window geometry (`TRIG_REFINE_BACK=128/FWD=48`, `BENCH_PRE=96`) explains inability to recover intended edge from a sufficiently displaced HW anchor (consistent with `test_rising_50us_rearm_sensitive_scenario` characterization).
- Stock `TIM4_CCR1` hardware-capture claim exists in-repo but is **unverified** from vendor source/binary/schematic evidence in this session; STM32F103/AWD/analog-input constraints limit direct timer-capture feasibility without confirmed routed digital signal/external conditioning.

## Selected architecture

**Software (existing hardware): Quiescent deterministic arming (A1).**  
Before enabling `AWDIE`, stop TIM3 pacing and gate ADC DMA requests, wait bounded for in-flight conversion/transfer settle (CNDTR stable), clear AWD/event state, evaluate boundary safety on a **stable** `acq_latest_sample()`, enable `AWDIE` only if inside window while conversions are quiesced, then restore DMA requests and restart TIM3. Unsafe boundary skips arming for that cycle (bounded timeout/mode policy).

## Why alternatives were rejected

- **Boundary-check-only (60915):** did not eliminate concurrent-conversion check→enable TOCTOU; insufficiently deterministic.
- **EOC-only / ordering tweaks without quiescence:** cannot prevent new conversions/flag races in the arm window.
- **Hardware timer capture (TIM4_CCR1-style) on current known paths:** **not defensible now** — AWD is ADC-internal, PA2 analog input not a timer IC edge source, no on-chip comparator, TIM4 allocated to calibration outputs, and no verified board-routed digital trigger signal; stock claim unverified. Hardware changes/confirmed routing would be required.
- **Refinement-only enlargement/heuristics:** dependent on anchor quality and risks wrong-edge selection in multi-period/noisy cases; not a correctness fix for unsafe/mis-timed arming.

## What has been implemented

- `app/acq.c`: quiescent arm sequence replacing residual-TOCTOU arm path; bounded settle, deterministic boundary decision, safe resume/error handling, documented timing discontinuity.
- `app/acq_handoff.h` + `tests/test_acq_handoff.c`: pure handoff predicates and temporal/ordering host tests (plus existing trigger regressions retained).
- **Candidate (not deployed):** build seq **48462**, bin SHA-256 `85610ebc07382b00b1cc13737e503d61191ae3461f75be91fbb546c41fffee81`, elf SHA-256 `05b739710f0dff43e3a40460f528219d4cfbf190aecb53d7f61499453fbca906`, map SHA-256 `6ee46bb379e463c46834b83e29bdc12b8143a7ea5eccb88cccf402ca743b1f04`, flash 40,168/61,440 B (65%), RAM 18,648/20,480 B (91%). Full host tests and firmware build PASS (offline).

## What remains unproven

- **Physical correctness:** no instrument interaction/validation performed; host tests cannot prove STM32F103 peripheral timing/concurrency behavior or fix the observed hardware symptom.
- **Runtime effects of quiescent arm:** per-arm sampling gap/TIM3 restart phase reset and settle heuristic adequacy across timebases/modes/edges (to be confirmed with correlated on-target diagnostics).
- **Residual non-handoff factors:** ISR/CNDTR latency and refinement/fallback behavior under real anchors; stock/hardware capture feasibility remains unverified.

## Readiness for separate deployment-readiness review

**Conditionally ready for review, NOT ready for deployment/claim of physical fix.**  
Rollback to 14139 is **UNVERIFIED** and must satisfy the mandatory safety gate before any physical operation. Any future deployment/validation requires explicit authorisation, approved `tools/flash_cycle.py` procedure, independent readback/identity verification, correlated diagnostics, and adherence to B2/B-REC/ETS and evidence-preservation constraints.

## Final decision

`ROBUST SOFTWARE FIX IMPLEMENTED — CANDIDATE ONLY`
