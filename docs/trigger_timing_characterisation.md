# Trigger Timing Characterisation (DSO183 custom firmware)

**Build context:** measurements taken on flashed **S=63A1** (seq 25505, `build/dso183.bin` md5 `c61e044572e121f11ad7e6bd07c2a71c`) over remote UART/SWD via the bench host (`<bench-host>`, e.g. a Pi on the local LAN, `/dev/ttyUSB0`, 115200 8N1). 1 kHz test signal present on input (same setup used for prior remote checks: ~174 counts Vpp observed, freq reported as 1000 Hz at 1 ms/div). No direct oscilloscope/timestamp probe was attached to the trigger path; all on-target timing values below are therefore **derived from sampled data, firmware structure, and datasheet/clock constants** unless explicitly marked *measured (indirect)*.

## 1. Scope and constraints (audit baseline)

- Inspected: `app/acq.c`, `app/acq.h`, `app/trigger.c`, `app/trigger.h`, `app/main.c` (`trig_refine`, acquisition/UI path), `app/tim3.c/.h`, `app/tim3_hw.c/.h`, `app/timebase.c/.h`, `app/stm32f1xx_it.c`.
- **Preserved:** TIM3 TRGO → ADC1 regular conversion → DMA1 Channel 1 circular buffer pipeline, single-shot acquisition flow, and unrelated functionality. No ETS/REPT implementation added.
- Existing trigger tests (`tests/test_trigger.c`) pass (`make test`/`make guard` clean on current tree).

## 2. Current trigger architecture (as-implemented)

### 2.1 Hardware detection (analog threshold crossing)
- **Mechanism:** ADC1 analog watchdog (AWD), single regular channel `ACQ_SIGNAL_CHANNEL` (ADC12_IN2 / PA2), enabled in `acq_init()` (`CR1`: `AWDCH|AWDSGL|AWDEN`).
- **Band programming (`apply_trigger_band`, `app/acq.c`):** one-sided window to give hardware edge-direction selectivity:
  - **Rising (and AUTO→rising):** `LTR=0`, `HTR = min(level + ACQ_TRIGGER_HYST, 4095)` — fires when sample leaves band above `HTR`.
  - **Falling:** `LTR = max(level - ACQ_TRIGGER_HYST, 0)`, `HTR=4095` — fires when sample leaves band below `LTR`.
- **Hysteresis:** `ACQ_TRIGGER_HYST = 8` counts (fixed). Applied in both HW band edges and SW refinement.
- **Pre-arm (`wait_trigger_rearm`):** bounded wait (budget derived from timebase, **capped at 20 ms**) requiring signal to be on the rearm side (`<= level-hyst` for rising, `>= level+hyst` for falling) before enabling AWDIE, to avoid immediate latch when already past threshold. On expiry it proceeds (may yield a less-stable phase rather than stalling UI).

### 2.2 Interrupt/latch
- **ISR:** `ADC1_2_IRQHandler` → `acq_isr_adc()` (`app/acq.c`). If AWD flag set and engine active: disables `AWDIE` (one-shot, prevents storm on standing out-of-window signal), clears SR, latches `s_trigger_index = (ACQ_SAMPLES - CNDTR) % ACQ_SAMPLES` when `s_event==0`, sets `s_event=1`, increments trigger count. NVIC priority **2** for ADC1_2/DMA1_Ch1; SysTick is priority 0 (can preempt). Comment notes latch corresponds to conversion causing event (approximately one sample after triggering sample in write-position terms).

### 2.3 Capture control (`acq_cycle`, `app/acq.c`)
- Primes ring once (bounded), ensures pre-trigger headroom from `s_resume_pos`, calls `wait_trigger_rearm()`, rearms AWD (clear flag/event, enable AWDIE), polls tightly for `s_event` (IWDG-fed per ms tick) up to `timeout_ms` (AUTO path uses 60 ms in `acq_bench_poll`; others larger/bounded). On trigger, waits for post-trigger fill (scaled by timebase, capped 8 s) then freezes (stop TIM3, gate ADC DMA requests, settle, snapshot ring). Returns `acq_gate` with `triggered`, `trigger_index`, `write_index`, `fresh`. Mode policy via `trig_fsm_action` (AUTO may fallback to newest window on timeout; NORM/SINGLE hold).

### 2.4 Software edge refinement (`trig_refine`, `app/main.c`)
- Searches a **176-sample** window centred on HW event: `start = trigger_index - 128`, length `min(176, ring, TRIG_SEARCH_LEN)` (`TRIG_REFINE_BACK=128`, `TRIG_REFINE_FWD=48`).
- Uses `trig_scan_nearest(..., centre, cfg)` (`app/trigger.c`) — **Schmitt state machine** (must rearm on far side of `level±hyst`, then cross back through `level±hyst`) selecting crossing **nearest to centre** (HW event). Slope from UI setting (AUTO aligned as rising).
- If found: `frame_build` around refined index with `BENCH_PRE=96` pre-trigger samples (320-sample window). If not found but HW triggered: builds around HW `trigger_index`. Else tail window. `s_trig_locked`/alignment reflects refinement success.

### 2.5 Supporting logic
- AUTO re-centres trigger level to signal mid when timeout or level outside captured `[min,max]` (`acq_bench_poll`). Adjustable level (0–4095) and slope (rising/falling/auto→rising) via acq/menu/link APIs. Fixed `ACQ_TRIGGER_HYST=8`.

## 3. Audit findings (reliability & timing)

| Area | Assessment | Notes |
|---|---|---|
| Rising/falling detection | **Good (by design)** | HW one-sided AWD enforces direction; SW `trig_scan*` enforces rearm+cross hysteresis. Host tests cover rising/falling/hysteresis/noise rejection/nearest. |
| Threshold & hysteresis | **Adequate, fixed** | Level adjustable 0–4095. Hysteresis fixed at 8 counts (~0.2% FS of 12-bit) — small but applied consistently HW+SW; may be marginal for very noisy/small signals (no user adjustment). |
| HW trigger index accuracy | **Quantized, ISR-latency sensitive** | Index derived from DMA `CNDTR` at ISR time. `CNDTR` is hardware-updated by DMA; delayed ISR entry can read a later position than the sample that caused AWD, introducing variable offset in samples. SW refinement (±128) corrects when true edge is within window. |
| Consistent trig→acq timing | **Good at moderate rates; limited by quantization** | Sampling paced by TIM3 TRGO (hardware). Edge position in buffer quantized to sample period. Refinement places edge near pre-trigger index 96 in output frame when successful. |
| False triggers / noise | **Mitigated** | One-shot AWD latch, pre-arm wait, HW one-sided band, SW Schmitt scan. Residual risk: arming after 20 ms rearm timeout, level outside signal (AUTO recenters), or noise larger than hysteresis. |
| Missed cycles | **Possible (structural)** | Gaps during post-fill/freeze/snapshot/restart and between cycles; at some rates/events edges can be missed (acceptable for scope-style capture, but relevant to ETS). |
| 50 µs/div behavior (S=63A1) | **Needs bench follow-up** | Indirect UART frame inspection at 50 µs/div, 1 kHz (0.5 ms window) did not consistently show a rising crossing near pre-trigger index 96 in sampled captures (see §4.3). With 0.5 ms window on a 1 ms signal, captured segment may not include the targeted edge depending on alignment; this warrants on-bench verification of 50 µs/div trigger alignment after SMP fix. |

## 4. Timing characterisation

### 4.1 Theoretical / design-derived (from code & clocks)

**Clocking/sample timing (from `timebase.c/.h`, `acq.h`):**
- `TIMEBASE_TIM3_HZ = 64 MHz` (15.625 ns/count). Sample period `Ts = (PSC+1)*(ARR+1) / 64 MHz`.
- ADC clock 10.667 MHz (PCLK2/6). Conversion = `(SMP_cycles + 12.5)` ADC cycles. Per-timebase SMP applied (`timebase_smp_code`): 50 µs/div → SMP 1.5 (14 cycles, ~1.3125 µs conv, fits 1.5625 µs interval); ≥0.1 ms/div → baseline SMP 7.5 (20 cycles, 1.875 µs) where it fits.

| Timebase | `Ts` (sample period) | 320-sample window | Quantization (±0.5 sample) | Notes |
|---|---:|---:|---:|---|
| 50 µs/div (idx 5) | 1.5625 µs (1562.5 ns) | 0.5 ms | ±781.25 ns | Fastest single-shot; SMP 1.5 |
| 0.1 ms/div (idx 6) | 3.125 µs (3125 ns) | 1.0 ms | ±1562.5 ns | |
| 1 ms/div (idx 9) | 31.25 µs (31250 ns) | 10.0 ms | ±15625 ns | |

**Trigger path latency contributors (theoretical):**
- **Detection quantization:** edge crossing is observed at a discrete conversion boundary → inherent uncertainty on order of **±0.5·Ts** relative to true analog crossing (dominant at slow sample rates).
- **ADC conversion/aperture alignment:** conversion result corresponds to sampled input; aperture (1.5–7.5 cycles selected) and pipeline add a fixed offset small relative to `Ts` but relevant at 50 µs/div.
- **AWD→ISR latency:** Cortex-M3 exception entry (stacking) typically ~12 cycles plus effects at 64 MHz (order of ~0.2 µs nominal) **plus** preemption/jitter: SysTick (prio 0) can preempt ADC IRQ (prio 2). Jitter bounded in normal operation but non-zero; because latch uses DMA `CNDTR` (hardware progress), late ISR can record a later write position (up to preemption duration / `Ts` in samples).
- **Software refinement:** `trig_scan_nearest` over copied buffer corrects position to nearest valid Schmitt crossing in 176-sample window, but cannot exceed buffer time resolution; effective edge placement remains quantized to sample indices.

**Expected jitter (theoretical, order-of-magnitude):** dominated by **sample quantization** for this architecture: ~**±0.78 µs** at 50 µs/div, ~**±1.56 µs** at 0.1 ms/div, ~**±15.6 µs** at 1 ms/div, assuming refinement selects the same true edge each cycle and ISR-latency-induced CNDTR skew remains < 1 sample (plausible at ≥1.5625 µs/sample with light ISRs, but not directly instrumented here).

### 4.2 Measured results (indirect, on hardware)

**Method (remote, non-invasive):** stopped freeze monitor to avoid UART contention; via `tools/remote_client.py` set `TIMEBASE`, `TRIG=AUTO`, `SLOPE=rising`, `SPAN=424`, streamed frames (320 samples each) and, per frame, located the rising crossing nearest sample index 96 (display pre-trigger target) using (a) `GETV_TRIG_LEVEL` and (b) frame midpoint. Reported sample-index variation converted to time using nominal `Ts`. 1 kHz input, S=63A1.

| Timebase | Frames | `GETV_TRIG_LEVEL` | Freq (reported) | Vpp (counts) | Edge index (lvl, near 96): n/min/max/mean/p2p/stdev (samples) | **Measured p2p / stdev (time)** | Edge index (mid) |
|---|---:|---:|---:|---:|---|---:|---|
| 0.1 ms/div | 30 | 972 | 0* | 175 | 30 / 96 / 96 / 96 / 0 / 0 | **0 ns / 0 ns** (0 samples) | same (0/0) |
| 1 ms/div | 25 | 972 | 1000 Hz | 173 | 25 / 96 / 96 / 96 / 0 / 0 | **0 ns / 0 ns** (0 samples) | same (0/0) |
| 50 µs/div | 30 | 885 (AUTO-recentered during run)** | 0* | 174 | 28 / 290 / 319 / 301.07 / 29 / 9.346 | **45.31 µs / 14.60 µs** (29 / 9.346 samples)*** | no mid crossing detected near 96 in frames |

\* At 0.1 ms and 50 µs/div the 10-div window (1 ms / 0.5 ms) contains ≤~1 cycle of 1 kHz, so firmware frequency measurement may report 0 / be unreliable — expected.  
\** Separate 50 µs inspection showed `GETV_TRIG_LEVEL=972`, `MIN=883`, `MAX=1056`, Vpp 174 with frames high across ~0–290 and a transition near the right edge (no rising crossing through 970/mid near index 96 in those captures). The 50 µs statistics above reflect crossings relative to the auto level (885) found late in buffers, **not stable alignment at the intended pre-trigger edge**.  
\*** Interpreted as **alignment/edge-selection inconsistency under that test condition**, not a direct sub-sample ISR jitter measurement.

**Interpretation (measured):**
- At **0.1 ms/div and 1 ms/div**, refined edge placement was **repeatable to 0 sample indices** across sampled frames (30/25) under a clean ~1 kHz input — consistent with correct HW+SW trigger/refinement operation within **< 1 sample period** (sub-sample jitter not resolvable by this method; bounded above by one `Ts` in this observation, i.e. <3.125 µs and <31.25 µs respectively, with theoretical quantization ±0.5·Ts).
- At **50 µs/div**, measured frame-edge positions did not demonstrate stable pre-trigger rising-edge alignment in this remote test; cause not conclusively isolated (possible interaction of 0.5 ms window on 1 kHz, trigger level/arming/refinement selection, or fast-timebase capture details). **Bench validation required** for 50 µs/div trigger reliability after the recent SMP fix.

### 4.3 Additional observations (indirect)
- `wait_trigger_rearm` budget capped at 20 ms favors UI responsiveness but can arm without confirmed rearm on difficult/slow conditions (documented tradeoff).
- Software refinement depends on a valid Schmitt crossing existing within the 176-sample search window; with nearest-to-centre selection, multiple equivalent edges in-window can select different cycles if phase varies (known display-level stability concern; not a HW timing jitter issue per se).

## 5. Improvements made / considered

- **Made (prior to this milestone in current line, preserved):** per-timebase ADC sample time (`acq_set_timebase`/init/resume apply `timebase_smp_code`) so fast ranges (50 µs/div) meet conversion-time budget; one-sided AWD slope band; bounded pre-arm wait; SW hysteresis refinement around HW event. No new destabilizing trigger-path changes introduced during this audit.
- **Considered (not implemented, to avoid unrelated risk):** adjustable hysteresis (UI/protocol/storage impact), longer/adaptive rearm budget, exposing HW-vs-refined trigger delta for instrumentation, and phase-continuous edge selection (previously attempted and reverted due to regression). Any change here should be validated on bench across timebases/modes.

## 6. Hardware timer/compare-based triggering — investigation (future ETS)

**Goal for ETS (1–20 µs/div):** acquire samples at precisely known offsets relative to a repeatable trigger across many repetitions and interleave them (equivalent-time). Requires (a) stable trigger event, (b) **deterministic, finely programmable trigger-to-sample delay/phase** (sub-`Ts` effective resolution), (c) accumulation logic.

**Current hardware/timer usage:**
- **TIM3:** sample clock source (`TRGO` update → ADC external trigger), reprogrammed per timebase — must be preserved.
- **TIM4:** used for calibration/test square-wave outputs (IRQ-driven toggling in `app/main.c`).
- **Signal input:** analog on PA2/ADC_IN2 (not a digital timer input-capture pin for the waveform edge). STM32F103 has **no built-in analog comparator** to convert threshold crossings into a timer IC event.

**Feasibility assessment:**
- **ADC AWD + ISR timestamping:** can detect analog crossings but timestamp/latency has jitter (IRQ latency/preemption) and only sample-quantized alignment; reading a free-running timer (e.g. TIM2 CNT if coordinated) in ISR could provide a coarse phase estimate, but coordinating with the existing TIM3-paced ADC/DMA without disturbing it is non-trivial.
- **Timer input capture (IC):** not directly applicable to the analog ADC input without external comparator hardware to generate a digital edge at the trigger threshold.
- **Programmable delay after trigger:** ETS typically needs to shift the ADC sampling instant by controlled increments each repetition (e.g. timer compare/output-trigger schemes or delayed start relative to trigger). Doing so while keeping **TIM3 TRGO → ADC → DMA** continuous pipeline intact would require careful redesign (gating/retriggering TIM3 or using another timer/ADC trigger arrangement) and is **not a drop-in change**.
- **Conclusion:** on this F103/board, **ETS at 1–20 µs/div is plausible in principle** for repetitive signals but **not straightforward** with the current architecture: main blockers are lack of analog-comparator-to-timer capture path, need for precise programmable trigger-relative sampling phase, and preserving the existing acquisition pipeline. Significant engine/timing work plus bench characterization would be required; single-shot real-time remains limited by ADC conversion time (~1.3125 µs minimum).

## 7. Limitations and recommendations

1. **Measured jitter evidence is indirect and sample-quantized:** 0-sample p2p observed at 0.1/1 ms/div indicates good repeatability but does not prove sub-sample (ns) jitter is zero; theoretical bound is ±0.5·Ts plus any residual ISR/CNDTR skew. For ns-grade characterization, instrument DWT cycle counter / timer capture at AWD event or use an external timing reference on bench.
2. **Validate 50 µs/div on bench:** confirm stable rising/falling alignment and edge placement near pre-trigger at 50 µs/div with known signals after SMP fix; investigate any misalignment seen in remote 1 kHz/0.5 ms captures.
3. **Consider exposing debug telemetry (optional, future):** raw HW `trigger_index`, refined index, and event timing counters over link/SWD for deeper characterization without disturbing capture timing.
4. **ETS path:** plan around a dedicated repetitive-capture mode (interleaved offsets, accumulation, stable-trigger gating) rather than trying to force ETS onto the current single-shot continuous-DMA flow.

## 8. Summary

- **Implementation:** existing trigger combines HW AWD (one-sided, direction-aware) with bounded pre-arm and SW Schmitt refinement (`trig_scan_nearest`) — a solid baseline for rising/falling detection and hysteresis-based noise rejection; fixed 8-count hysteresis and 20 ms rearm cap are pragmatic but not tunable.
- **Measured (indirect):** 0-sample edge-index variation at 0.1 ms/div and 1 ms/div (30/25 frames, 1 kHz) → repeatability within one sample period; 50 µs/div results inconclusive for stable pre-trigger alignment in this test and need bench follow-up.
- **Theoretical:** timing dominated by sample quantization (±0.781 µs @50 µs/div, ±1.5625 µs @0.1 ms/div, ±15.625 µs @1 ms/div) plus small ISR-latency/CNDTR effects; TIM3 pacing provides consistent sample intervals.
- **ETS suitability:** same hardware can in principle support ETS, but current analog-only trigger path and preserved TIM3→ADC→DMA pipeline make precise trigger-relative programmable sampling non-trivial; **not ready without significant design/implementation work**.
