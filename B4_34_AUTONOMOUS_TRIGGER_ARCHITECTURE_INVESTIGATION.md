# B4.34 — Autonomous Trigger Architecture Investigation

**Date:** 2026-11-14 (UTC, development session)  
**Task type:** Investigation and development only (NO deployment, NO physical instrument interaction).  
**Workspace:** `/home/print/dso183-firmware-project`

## 1. Rollback status

**UNVERIFIED (explicit).**

- Prior state (as reported, not independently re-established in this task): rollback to recovery build 14139 was initiated; `flash_cycle.py` was observed running; UART `/dev/ttyUSB0` unresponsive; independent 64 KiB readback and expected recovery identity 14139 not confirmed.
- **No physical instrument interaction was performed in this task:** no SSH for hardware operations, no UART/ST-Link access, no reset/halt/attach/interrogation, no flash/erase/program, no start/stop/interference with `flash_cycle.py`, no hardware validation. No assumptions made about rollback completion.
- A future task must explicitly authorise physical operations only after the rollback safety gate is independently satisfied (process exited, approved read-only independent 64 KiB readback SHA-256 == `e2bd129fd044f44246e9c5a85547d92f2559f6b25ee72ced4d7fe0eff09949f8`, UART responsive and identity 14139).

## 2. Source baseline and working-tree state

**Repository/Git (observed):**
- HEAD: `246a2b72220a7d7153565c13e4c25ed487ff7ffd` (`246a2b7 docs: add repo banner image to README`, branch `main`).
- Working tree: **not clean**.
  - Modified: `app/acq.c` (quiescent-arm deterministic handoff implemented in this task, replacing the 60915 boundary-check-only arm sequence; diff stat `79 insertions(+), 46 deletions(-)` vs HEAD at time of candidate build).
  - Untracked (additive): `B4_33_TRIGGER_HANDOFF_INVESTIGATION_REPORT.md`, `app/acq_handoff.h`, `tests/test_acq_handoff.c`, `B4_34_AUTONOMOUS_TRIGGER_ARCHITECTURE_INVESTIGATION.md`, `B4_34_TRIGGER_ARCHITECTURE_DECISION.md`.
  - No other repository modifications observed. No evidence of concurrent external modification during this task.

**Candidate 60915 (prior, reported):** bin `60de8ca2af9f74953c4fa62a4e554d1ddcb5057573cb9d948f922f26ccb984cc`, elf `0c6d2c1b150cf782a55b69d091cbb07194d5372ccd88fd4fb1affc6f971e67ac`, seq 60915. **Current state:** `build/` was rebuilt in this task to a new candidate (seq 48462); 60915 is therefore **not present in `build/` and not reproduced as the current build artifact**. Its identity/hashes remain recorded in `B4_33_TRIGGER_HANDOFF_INVESTIGATION_REPORT.md` and prior evidence; it can be reproduced only by restoring the corresponding 60915 source state and rebuilding (build seq is derived from checksummed app/driver/font sources per `Makefile` `DIGEST_FILES`).

**Current build/test status (offline, this task):**
- `make -s -j4 test`: **PASS (RC 0)**, all host test binaries passed (including `acq_handoff`, `trigger`, and full existing suite).
- `make -j4`: **PASS (RC 0)**. Warning: `app/acq.c:241:13: warning: 'wait_trigger_rearm' defined but not used [-Wunused-function]` (pre-existing unused wrapper, no behavioral effect).
- Current generated build seq: `48462` (`build/build_seq.h`).

## 3. Exact trigger execution and timing sequence

### 3.1 End-to-end timing path (analog input → displayed trigger position)

1. **Analog input / conditioning:** external signal enters analog front-end (board/schematic-dependent; not fully verifiable from firmware alone) and is sampled by **ADC1 channel 2 (PA2, `ADC12_IN2`)** as regular conversion input (`ACQ_SIGNAL_CHANNEL`, `app/acq.h`/`app/acq.c`).
2. **ADC conversion:** ADC1 regular group (single conversion, `SQR3=ch2`) triggered by **TIM3 TRGO** external trigger (`EXTTRIG=1`, `EXTSEL=100`/`ACQ_EXTSEL_TIM3_TRGO`, `app/acq.c:init`, `app/acq.h`). Sample time selected per hardware timebase (`timebase_smp_code`); conversion time = (SMP + 12.5) ADC clocks at PCLK2/6 (10.667 MHz). At 50 µs/div HW pacing (1.5625 µs/sample) SMP 1.5 → ~1.3125 µs conversion.
3. **DMA transfer:** **DMA1 Channel 1** circular, peripheral-to-memory, halfword, from `ADC1->DR` to `s_buf[ACQ_SAMPLES]` (`CPAR/CMAR/CNDTR`, `CCR` EN|TCIE|TEIE|CIRC|MINC|PSIZE16|MSIZE16|PL high). `CNDTR` decrements per transfer and reloads in circular mode; next write position = `(ACQ_SAMPLES - CNDTR) % ACQ_SAMPLES`.
4. **AWD condition:** ADC analog watchdog enabled single-channel on regular group (`AWDCH=2`, `AWDSGL`, `AWDEN`). One-sided band per slope (`apply_trigger_band`): rising `LTR=0, HTR=min(level+hyst,4095)`; falling `LTR=max(level-hyst,0), HTR=4095`; `hyst=8`. AWD flag set by hardware when converted sample is outside window.
5. **Interrupt request:** `ADC1_2_IRQn` enabled (prio 2). When `SR.AWD` set and `ADC_CR1_AWDIE` enabled, ADC interrupt requested.
6. **ISR execution (`acq_isr_adc`, `app/acq.c`):** if not inited or `SR.AWD` clear returns; otherwise clears `AWDIE` (one-shot), clears `SR`, captures `TIM3->CNT` and `idx = (ndtr<=ACQ_SAMPLES)?(ACQ_SAMPLES-ndtr):0` from `DMA1_CNDTR`, updates guarded HW capture (`s_hw_seq`/generation/count/index/rollover/valid), sets `s_event=1` and `s_trigger_index=idx` on first event, increments trigger count. (SysTick prio 0 can preempt; DMA ISR prio 2 handles TC/TE.)
7. **Trigger index capture semantics:** HW `trigger_index` is the DMA write position at ISR read time, lagging the actual out-of-window conversion by ADC completion/DMA/ISR latency and CNDTR progression (documented in `acq.h`/code comments); treated as a window-position anchor, refined in software.
8. **Software refinement (`trig_refine`, `app/main.c`):** on triggered capture, copies 176-sample window centred on HW index (`TRIG_REFINE_BACK=128`, `TRIG_REFINE_FWD=48`, bounded by ring/`TRIG_SEARCH_LEN=512`) and runs `trig_scan_nearest` (Schmitt rearm+cross, nearest-to-centre) with slope-aware level/hyst; on failure attempts simple threshold-crossing fallback nearest centre; if found uses refined index, else falls back to HW index (or tail for untriggered). `frame_build`/placement uses `BENCH_PRE=96`.
9. **Displayed trigger position:** frame aligned so pre-trigger target is near index 96 (`BENCH_PRE`/`TRIG_PRE_TRIGGER` semantics) around selected/refined or fallback index; ETS path accumulates separately with phase/epoch constraints.

### 3.2 Acquisition cycle handoff (current implementation, candidate 48462)

- `acq_cycle()` ensures ring primed and pre-trigger headroom, then performs **quiescent arm**: `rearm_ready = acq_wait_rearm()` (bounded, while conversions run) → `tim3_hw_stop()` + `ADC1->CR2 &= ~ADC_CR2_DMA` → bounded CNDTR-settle wait → bump/invalidate gen, disable `AWDIE`, clear `SR`, reset event/index → read stable `acq_latest_sample()` and compute `arm_safe` (inside window, independent of `rearm_ready`) → enable `AWDIE` only if safe (no conversions while quiesced) → restore `CR2_DMA`, update `s_resume_pos`, restart TIM3 (`tim3_hw_start(s_hw_timebase cfg)`) or return `ACQ_HW_ERROR` on restart failure. Unsafe boundary leaves `AWDIE` disabled (bounded timeout/mode policy). Then polls for trigger, post-fills, freezes (`tim3_hw_stop`, gate DMA req, settle, memcpy snapshot), resumes pacing.

### 3.3 Sources of timing uncertainty and index displacement

- **ADC sampling/conversion:** aperture and conversion latency (fixed per setting); sample quantization at `Ts`.
- **DMA/CNDTR timing:** transfer occurs on conversion completion; `CNDTR` read in ISR reflects DMA state at read time relative to transfer(s).
- **AWD detection point:** first out-of-window conversion after enable; edge position quantized to sample boundaries.
- **ISR latency/preemption:** `ADC1_2_IRQn` prio 2, `SysTick` prio 0 preemptive; variable latency shifts CNDTR-at-read relative to triggering conversion.
- **Arm handoff (addressed):** prior boundary-check-only path had residual check→enable TOCTOU with concurrent conversions; current quiescent arm removes concurrent-conversion race in arm window but introduces a controlled **sampling gap/timer-phase discontinuity** at each arm (TIM3 stop/restart resets counter/programming).
- **Refinement limits:** 176-sample search window and nearest-cross selection depend on HW anchor quality and signal periodicity/multi-crossing content; fallback may use HW index if no valid crossing found.
- **ETS-specific:** phase/timestamp handling constrained (HW count is latency marker, not sub-sample phase per existing design/docs).

## 4. Peripheral ownership and pin-routing map

| Peripheral/resource | Current use (verified in source) | Relevant constraints / notes |
|---|---|---|
| **ADC1** | Signal acquisition: regular single channel `ch2/PA2` (`ACQ_SIGNAL_CHANNEL`), external trigger from TIM3 TRGO, DMA requests enabled, AWD single-channel on regular group, calibration/enable bounded. Paused/reconfigured for ladder reads (SWSTART path) via `acq_pause/acq_resume`. | AWD internal to ADC (flag/IRQ), not directly routable as timer IC. Conversions continuous while paced; in-flight conversion can complete after trigger stop. Shared ownership with ladder via `adc_owner`. |
| **DMA1 Channel 1** | Circular P2M halfword from `ADC1->DR` to `s_buf[1024]` (2 KiB), TC/TE IRQ prio 2. Channel kept enabled across freezes (requests gated via `ADC_CR2_DMA`), disabled only in full stop paths. | CNDTR consistency critical for index; no concurrent writers to buffer except DMA. No other DMA1 channels used by acquisition. |
| **TIM3** | Sample pacing clock: programmed per timebase (`tim3_config_for`), `MMS=010` TRGO on update, started/stopped/reset by `tim3_hw_start/stop` (`CNT=0`, `EGR=UG`, `SR` cleared, MMS set after UG). No TIM3 IRQ/CC outputs used for pacing. | Stop prevents new TRGO edges; restart (current impl) resets counter/phase → timing discontinuity. Must preserve configured period for sample spacing after restart. |
| **TIM4** | **Calibration/test output generator** (`app/main.c: tim4_generator_init/TIM4_IRQHandler`): PSC 63, ARR 499, UIE IRQ prio 0 toggles GPIOA pins `PA1, PA8, PA11, PA12, PA15` at 2 kHz update → 1 kHz square (SQR/TSIG candidates). No input-capture configuration. | Actively used; repurposing for capture conflicts with calibration outputs and requires different pin/channel/IRQ configuration and signal routing. Pin remap/board routing unverified. |
| **GPIOA** | `PA2` analog input (`GPIO_MODE_ANALOG` for acquisition). `PA5/PA6/PA7` ladder inputs (analog; PA7 no longer TIM4-driven). `PA1/8/11/12/15` driven by TIM4 cal toggling. `PA9/PA10` UART (elsewhere). `PA13/14` SWD, `PA15` freed (SWJ). `PA0` analog (input init). `GPIOA->IDR/BSRR/ODR/CRL` touched in various places. | Analog mode on PA2 incompatible with timer input-capture function on same pin; any timer IC would require a different, correctly routed digital signal. |
| **GPIOB** | Buttons/ladder-related UI reads and display/data bus usage per modules (not part of trigger capture path). | No trigger timing role identified. |
| **Interrupts** | `SysTick` prio 0 (HAL tick). `ADC1_2_IRQn` prio 2 (AWD latch). `DMA1_Channel1_IRQn` prio 2 (TC/TE). `TIM4_IRQn` prio 0 (cal toggle, highest). `TIM3_IRQn` weak/default (not enabled by acquisition). | Preemption/latency affects CNDTR-at-ISR; TIM4 high prio is for cal edge quality, not capture. |
| **IWDG** | Fed at stage boundaries and during bounded waits; acquisition timeouts/budgets constrained below watchdog expectations in design. | Quiesce settle waits must remain bounded; current 2 ms bound is safe. |

**Pin/timer routing note:** Exact DSO183 board routing/alternate-function remaps (beyond firmware configuration) cannot be fully proven without validated schematic/bench tracing; statements here reflect firmware-observed configuration and STM32F103 constraints.

## 5. STM32F103 hardware findings (datasheet/manual-grounded, applied)

*References (STM32F103): RM0008 (reference manual, ADC/DMA/TIM/EXTI/NVIC/AFIO), device datasheet (pin functions/timers). Conclusions below are applied to this design; register-level claims are consistent with implemented code patterns.*

- **ADC external trigger (regular, TIM3 TRGO):** conversions start on selected external trigger edge while `EXTTRIG` enabled and ADC operating; stopping the trigger source prevents new starts, but a conversion already started completes (fixed conversion time). EOC/AWD flags set on completion/condition; clearing `SR` by software removes flags but conditions can reassert on subsequent conversions.
- **ADC DMA requests:** with `CR2.DMA` set, DMA requests issued for regular conversions; gating `CR2.DMA` stops requests (while ADC behavior otherwise per configuration). Continuous sampling in this design is paced by TIM3 (not ADC continuous mode).
- **DMA1 circular mode:** `CNDTR` decrements per transfer and reloads at end of buffer; channel remains enabled. If transfers stop, `CNDTR` remains stable (subject to no new requests).
- **AWD:** compares selected channel conversion against `LTR/HTR`; generates flag/interrupt per enable bits. AWD is an ADC-internal event (no direct timer input-capture routing from AWD on STM32F103).
- **TIM3 as TRGO (update):** `MMS=010` outputs TRGO on update event; counter enable/disable controls counting and update generation. Reprogramming with `UG` loads PSC/ARR; generating TRGO from `UG` depends on configuration/order—current `tim3_hw_start` sets MMS after `UG` and clears `SR` to avoid an unintended trigger from initialization.
- **Timer input capture:** requires a valid digital input mapped to timer channel (pin AF/remap or internal trigger where supported). Analog GPIO configuration and ADC input use are incompatible with digital timer IC on the same signal pin; no built-in analog comparator on STM32F103 to convert analog threshold crossing to timer IC edge without external hardware.

## 6. Stock TIM4_CCR1 investigation and confidence

- **In-repo claims:** `app/acq.h`, `app/acq_window.h`, `docs/FEATURES.md`, and prior investigation reports state that vendor/stock trigger position was derived from `TIM4_CCR1` (while using AWD for detection), contrasting with current AWD ISR + CNDTR approach.
- **Available evidence reviewed:** source comments/docs, `ghidra-mcp/dso183.gpr` and `ghidra-mcp/dso183.rep` present in repo (binary reverse-engineering artifacts). No extracted disassembly/decompilation text or definitive vendor-source proof of `TIM4_CCR1` usage/semantics was produced in this session.
- **Verification status:** **Not independently verified from actual vendor source, binary disassembly/decompilation output, or vendor hardware documentation in this session.** The mechanism (which signal TIM4 captured, how it related to analog trigger/ADC timing, and board routing) remains **unconfirmed**.
- **Confidence:** **Low (unverified claim).** Even if accurate, it does not by itself establish that a timer IC path is available/reproducible on this hardware with the current analog signal routing and firmware peripheral allocation.

## 7. ADC gating feasibility analysis (Solution A)

### 7.1 Feasibility and implemented strategy

**Feasible and implemented (candidate 48462): deterministic quiescent arming.**

- **Mechanism:** stop TIM3 (`tim3_hw_stop`) to prevent new TRGO-triggered conversions; gate ADC DMA requests (`ADC1->CR2 &= ~ADC_CR2_DMA`); wait bounded (≤2 ms, CNDTR-stability heuristic) for in-flight conversion/transfer to settle; with no conversions possible, clear AWD/event state, read stable boundary sample, decide `arm_safe`, enable `AWDIE` only if safe; restore `CR2_DMA`, update `s_resume_pos`, restart TIM3 (`tim3_hw_start` with HW timebase cfg). On restart failure return `ACQ_HW_ERROR`.
- **Why it helps:** removes concurrent conversion completion in the check→enable window (the residual H1/H2 TOCTOU of 60915). Boundary sample and enable decision occur in a quiesced state.

### 7.2 Strategy comparison

| Strategy | Description | Eliminates | Leaves / risks | DMA/timing consequences | Verdict |
|---|---|---|---|---|---|
| **A1 (implemented): TIM3 stop + DMA gate + settle + arm + restart** | Quiesce conversions, deterministic boundary, resume pacing | Concurrent-conversion TOCTOU between boundary check and `AWDIE` enable; prevents mid-handoff sample change | Sampling gap/timer-phase discontinuity at arm; restart reinitializes TIM3 (`CNT` reset); if settle insufficient in pathological case (bounded) residual risk small; restart-failure path must be handled | DMA channel EN/CNDTR/ring preserved; no writes while gated/stopped; `s_resume_pos` updated; pre-trigger history remains in ring | **Selected (best software fit)** |
| **A2: conversion-complete/EOC boundary without stopping triggers** | Use EOC/state to find boundary while triggers continue | Partial ordering insight only | Does not prevent new conversions before enable; TOCTOU remains | Minimal, but ineffective for atomicity | Rejected |
| **A3: full acquisition state machine (rearm/quiesce/arm/resume separated)** | Formalize states and transitions | Same class as A1 if quiescence enforced | Higher complexity/state surface; same fundamental restart-gap tradeoff | Similar to A1 | Viable refinement; A1 achieves core property with smaller change |
| **A4: stop TIM3 but do not gate CR2_DMA / different settle** | Variants of quiescence | Similar if truly no conversions | Subtle differences in request/flag behavior; less explicit | Similar | Less explicit than A1 |

### 7.3 Race conditions, consequences, failure behavior

- **Eliminated:** sample change due to conversions completing between stable boundary read and `AWDIE` enable (primary residual race targeted).
- **Remaining/other:** analog signal continues to evolve during quiesce but is **not sampled** (deterministic w.r.t. ADC samples); after restart, first new conversion occurs per timer behavior—edge may occur at/after resume (handled as normal trigger wait). Timer restart introduces a **timing discontinuity** (gap, phase reset) per arm cycle; acceptable for trigger re-acquisition but a continuity/phase consideration (ETS/precise phase implications unchanged in contract). If TIM3 restart fails, function returns `ACQ_HW_ERROR` (caller already handles HW errors). Bounded settle (2 ms) avoids unbounded wait and IWDG starvation.
- **DMA consequences:** channel remains enabled; requests gated and pacing stopped ⇒ no transfers, `CNDTR` stable. On resume, requests and pacing restored; no reconfiguration of circular buffer base/length.
- **Execution-time/memory cost:** small bounded wait per cycle + few register ops; negligible RAM increase.

## 8. Hardware capture feasibility analysis (Solution B)

### 8.1 Stock `TIM4_CCR1` claim

Unverified (Section 6). Even assuming stock used `TIM4_CCR1`, the captured signal’s electrical nature, pin routing, and relationship to the analog ADC input/AWD event are unknown from accessible in-repo evidence.

### 8.2 STM32F103 / board feasibility

- **Timer IC prerequisites:** supported digital input to timer channel (GPIO AF/remap or internal trigger as applicable). **ADC AWD is internal** to ADC (no direct timer IC capture of AWD). **Analog input PA2** is configured as analog ADC input; cannot simultaneously serve as digital timer input capture for the analog waveform edge.
- **No on-chip analog comparator (STM32F103):** cannot generate a digital threshold-crossing edge for timer capture without external circuitry.
- **Current TIM4 allocation:** used for 1 kHz calibration/test GPIO toggling via update IRQ (`app/main.c`), not configured for input capture; outputs occupy pins (e.g. PA1/PA8/PA11/PA12/PA15). Reuse would conflict with calibration functionality and require reconfiguration/routing.
- **Pin mapping/remap/routing:** specific DSO183 schematic routing for any trigger/comparator-to-timer path not verifiable from firmware alone; user-provided vendor PDFs exist but were not machine-extracted here. No evidence of an existing, firmware-accessible digital trigger signal mapped to a free timer capture channel.
- **Timestamp-to-DMA index mapping:** would require synchronized timebase and known conversion/DMA alignment; non-trivial but secondary to physical signal-path feasibility.

### 8.3 Conclusion

**Not feasible with confirmed existing hardware/firmware signal paths as understood from repository evidence.** A hardware-timed capture (TIM4 or other timer) would likely require external comparator/trigger-conditioning and verified board routing (and/or different pin/timer allocation conflicting with current calibration use). **No fabricated hardware path is assumed.** Hardware modification or confirmed routed digital trigger signal would be prerequisites.

## 9. Additional alternatives considered

| Alternative | Assessment | Correctness vs symptom-hiding |
|---|---|---|
| **60915 boundary-check-only (no quiesce)** | Reduced unsafe arm on stale-ready case but left residual check→enable TOCTOU; did not fix physical failure per evidence. | Partial improvement, insufficiently deterministic. |
| **AWD threshold/programming/status tweaks** | Bands/hysteresis already slope-aware; no evidence threshold semantics alone cause deterministic +~190-sample displacement. | Unlikely correctness fix alone. |
| **Interrupt priority/latency tuning** | May reduce jitter of CNDTR-at-ISR but cannot remove arming race or guarantee anchor; AWD must fire correctly first. | Symptom/quality improvement, not root handoff correctness. |
| **DMA index snapshot strategies** | Useful for correlation/consistency; does not make unsafe arming safe. | Diagnostic/supporting, not fix. |
| **Refinement window enlargement / anchor heuristics** | Can recover some displaced anchors but risks wrong edge selection on multi-period/noisy signals and cannot “find” an edge not representable from a fundamentally bad latch context; must be evidence-based. | Risk of masking/symptom-chasing if over-broad; keep conservative (see §11). |
| **Replace unsafe fallback with explicitly invalid/untriggered result** | Avoids presenting a wrong aligned frame in some cases but does not deliver correct trigger capture; mode/policy implications needed. | Safer presentation, not correctness of detection/alignment. |
| **Hybrid AWD detect + timer timestamp** | Attractive if a timestampable digital trigger event exists; depends on hardware path (see §8). | Genuine if feasible; currently unproven on this hardware. |
| **Better index reconstruction from known timing** | Bounded by uncertainty in conversion/IRQ latency and by whether AWD latched intended crossing. | Incremental; cannot fix unsafe arming. |

## 10. Architecture comparison and recommendation

| Approach | Race eliminated? | DMA safety | Timing accuracy | Hardware changes | Complexity | Recommendation |
|---|---|---|---|---|---|---|
| **A1 Quiescent arming (TIM3 stop/gate/settle/arm/restart)** | **Yes for concurrent-conversion check→enable TOCTOU** (deterministic boundary while quiesced) | **Good** (DMA EN/CNDTR/ring preserved; no transfers while quiesced) | **Improved determinism of arm**; introduces per-arm sampling gap/phase reset; HW anchor still CNDTR/ISR-derived | **None** | **Low–moderate** | **Selected / implemented** |
| **60915 boundary-check-only** | **No** (residual TOCTOU) | Good | Unchanged | None | Low | Superseded |
| **Timer input capture (TIM4/other) timestamp** | Can improve timestamp quality if correctly wired | Depends on integration | **Potentially high** (sub-sample timing) if feasible | **Likely required** (signal/comparator/routing, timer allocation) | High | **Not feasible now; investigate only with confirmed HW path** |
| **Hybrid AWD + HW timestamp** | Depends | Depends | Potentially high | Likely required | High | Future/conditional |
| **Refinement-only changes** | No | N/A | Limited by anchor | None | Low–moderate | Not a substitute for handoff correctness |

**Ranking (by stated criteria):** 1) A1 quiescent arming, 2) conservative refinement/diagnostic improvements (supporting), 3) hardware capture/hybrid (contingent on hardware feasibility evidence).  
**Recommendation:** adopt **A1 quiescent arming** as the smallest robust software solution on existing hardware; pursue hardware-timed capture only if/when a real signal path is verified.

## 11. Trigger refinement reassessment (independent)

- **Current parameters/behavior:** `TRIG_REFINE_BACK=128`, `TRIG_REFINE_FWD=48`, `BENCH_PRE=96`, `TRIG_SEARCH_LEN=512`; `trig_scan_nearest` (Schmitt, nearest-to-centre) + simple threshold-crossing fallback nearest centre; fallback to HW index if no crossing found. Window centered on HW `trigger_index`.
- **Anchor dependence:** with a badly displaced HW anchor (e.g. mid-plateau, true edge outside 176-sample window), refinement cannot recover the intended edge (by design/data absence). Quiescent arming aims to provide a valid, timely AWD latch and thus a reasonable anchor.
- **Blind enlargement risk:** increasing back/fwd range can include additional periods/crossings in periodic waveforms, increasing wrong-edge selection risk despite nearest-to-centre; also interacts with buffer bounds/wrap and compute. Any change must be justified by observed anchor/error distributions and tested against multi-crossing cases.
- **Assessment:** no refinement parameter/algorithm change implemented in this candidate; current design is acceptable **given a correct anchor**. Refinement remains a dependent stage, not the primary correctness fix. Further adjustment should be evidence-driven after correlated on-target diagnostics.

## 12. Implemented changes (candidate 48462)

- **`app/acq.c`:** replaced boundary-check-only arm sequence with **quiescent arming** (TIM3 stop, `ADC_CR2_DMA` gate, bounded CNDTR settle, stable boundary sample check, `AWDIE` enable only while quiesced, restore/resume with HW timebase; `ACQ_HW_ERROR` on restart failure). Retained/factored pure handoff predicates via `acq_handoff.h`. Documented sequencing, gap/discontinuity, and deterministic boundary rationale in comments. (Unused `wait_trigger_rearm` wrapper remains.)
- **`app/acq_handoff.h`:** pure, host-testable predicates for rearm/inside-window/arm decision (from prior work; unchanged semantics, used by implementation/tests).
- **`tests/test_acq_handoff.c`:** host temporal/ordering model tests for handoff predicates and mode-policy regressions (from prior work; still valid for boundary decision correctness).
- **Contracts preserved:** no changes to B2/B-REC, ETS readiness/rejection semantics, 80-bin metric/coverage classifications, diagnostic meanings, approved flashing safeguards, or recovery/historical evidence. Trigger modes, buffer ownership, and bounded behavior preserved.

## 13. Test results and coverage limitations

- **Host tests:** `make -s -j4 test` **PASS (RC 0)**. Full suite passed (all existing binaries plus `acq_handoff` and `trigger`).
- **Firmware build:** `make -j4` **PASS (RC 0)**.
- **Mutation tests / static checks:** no dedicated mutation-test target or additional static-analysis target identified in `Makefile`; not run beyond normal compilation warnings (`-Wall -Wextra`, observed unused-wrapper warning only).
- **Coverage limitations (important):**
  - Host tests do **not** model real STM32F103 ADC/DMA/TIM peripheral concurrency, in-flight conversion completion timing, NVIC preemption, or register-level quiescence semantics; `acq_cycle` hardware path is not host-executable.
  - `test_acq_handoff.c` validates **pure boundary predicates and modeled ordering decisions**, not actual `tim3_hw_stop/start`, `CR2_DMA` gating, or CNDTR settle behavior.
  - Refinement tests are waveform-logic level, not coupled to live ISR/CNDTR timing.
  - **No physical validation performed** (prohibited). A host-tested candidate **must not** be claimed to fix the physical failure.
  - Correlated on-target HW/refined diagnostics remain necessary to confirm physical behavior.

## 14. Candidate identity, hashes and memory usage (candidate only, not deployed)

- **Build sequence:** `48462`
- **Hashes:**
  - `build/dso183.bin` — 40,168 B, SHA-256 `85610ebc07382b00b1cc13737e503d61191ae3461f75be91fbb546c41fffee81`
  - `build/dso183.elf` — SHA-256 `05b739710f0dff43e3a40460f528219d4cfbf190aecb53d7f61499453fbca906`
  - `build/dso183.map` — SHA-256 `6ee46bb379e463c46834b83e29bdc12b8143a7ea5eccb88cccf402ca743b1f04`
- **Memory/size:** `text 40032`, `data 136`, `bss 18512`, `dec 58680`. Flash 40,168 B / 61,440 B (65%). RAM 18,648 B / 20,480 B (91%, ~1,832 B headroom, within existing constraint).
- **Deployment status:** **not flashed/deployed; no instrument communication.**

## 15. Remaining risks and exact next steps before any physical validation

### Remaining risks
- Rollback (14139) completion/identity **unverified**.
- Quiescent arm introduces per-cycle **sampling gap/timer restart discontinuity**; behavior under all timebases/modes and interaction with pre-trigger assumptions requires on-target confirmation.
- CNDTR-settle heuristic is bounded and practical but not a formal hardware completion proof; TIM3 restart semantics depend on implementation/MCU behavior as used.
- HW anchor remains CNDTR/ISR-derived (latency/quantization) though arming race is addressed; refinement fallback-to-HW-index can still misplace if anchor poor (now less expected).
- Stock `TIM4_CCR1` mechanism and any hardware capture path **unverified/unavailable** without confirmed routing/hardware changes.
- Host coverage cannot prove physical correctness.

### Exact next steps (gated)
1. **Satisfy rollback safety gate (separately authorised, read-only verification):** confirm `flash_cycle.py` exited; approved independent readback of full 64 KiB and SHA-256 == `e2bd129fd044f44246e9c5a85547d92f2559f6b25ee72ced4d7fe0eff09949f8`; UART responsive and identity 14139. If any inconclusive/failed, STOP.
2. **Deployment-readiness review (separate):** review candidate 48462 diff/contracts, plan correlated diagnostics capture (HW index/refined/found/fallback/seq bound to frames), and confirm approved `tools/flash_cycle.py` procedure and recovery path.
3. **Only with explicit authorisation:** deploy 48462, independently verify flash readback/UART identity, then run physical validation (50 µs rising/falling AUTO/NORM + 0.1/1 ms regressions, same signal/methodology, correlated diagnostics).
4. **If still failing:** use correlated evidence to distinguish anchor latency/refinement issues vs residual arm/timing issues; evaluate refinement adjustments (evidence-based) or hardware-signal-path feasibility for timestamp capture.
