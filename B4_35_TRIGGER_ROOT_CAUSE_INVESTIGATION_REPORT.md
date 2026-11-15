# B4.35 — 50 µs Rising-Edge Trigger Root-Cause Investigation Report

**Date:** 2026-11-15 (UTC, investigation session)  
**Type:** Read-only code/evidence investigation only (no physical instrument interaction, no firmware source modification, no evidence modification).  
**Workspace:** `/home/print/dso183-firmware-project`  
**Instrument state (as of report context):** verified build **14139** restored (independent 64 KiB readback SHA-256 `e2bd129fd044f44246e9c5a85547d92f2559f6b25ee72ced4d7fe0eff09949f8`, UART `BUILD_SEQ 14139`, `STATUS 1`; see B4.34 rollback evidence).

## 1. Executive summary

- B4.34 hardware validation of candidate **48462** (quiescent deterministic arming, A1) **failed the primary acceptance criterion**: 50 µs/div rising-edge (AUTO and NORM) produced **0/100** valid captures with a requested rising crossing within ±2 of index 96 (0 rising crossings found in any 50 µs rising frame; 100/100 frames showed a falling crossing near index ~286). 50 µs falling was 100/100 correctly placed at index 96; 0.1 ms and 1 ms (all tested modes/polarities) were 100/100 at index 96. All 600/600 captures had `diag_seq_mismatch` with empty per-capture diagnostics, preventing direct HW-state correlation.
- Source/evidence analysis confirms the implemented one-sided ADC analog watchdog (AWD) banding is **polarity-consistent by construction** for rising vs falling (`apply_trigger_band`: rising `[0, level+hyst]` fires on sample `> HTR`; falling `[level-hyst, 4095]` fires on sample `< LTR`; `app/acq.c`, `app/acq_handoff.h`), and no code path inverts the requested slope for 50 µs specifically. Simple AWD threshold-polarity inversion (H1) is **not supported** as the primary explanation.
- The observed 50 µs rising signature (no rising crossing present anywhere in a 320-sample window that spans ~0.5 ms = half a 1 kHz period, with a falling crossing at ~286) is **inconsistent with “correct rising-edge detection at HW level but lost in refinement”** and **consistent with the trigger/arming/capture path latching/placing the wrong event or phase for rising at 50 µs** (wrong-edge/late anchor relative to the intended rising transition), as concluded in B4.34. However, universal diagnostic incoherence means the **exact internal firing point (AWD latch timing/slope context vs index/placement artifact) cannot be proven** from available hardware evidence.
- The 48462 quiescent arm (`app/acq.c:acq_cycle`) eliminates concurrent-conversion check→enable TOCTOU by stopping TIM3 and gating `ADC1->CR2.DMA` during the boundary decision, but it introduces **per-arm sampling gaps and a TIM3 restart discontinuity** (`tim3_hw_start`: `CNT=0`, `EGR=UG`, `MMS` set after `UG`, `SR` cleared) and **resets `s_resume_pos`/restarts pacing after pre-trigger headroom was already awaited**. These timing/phase side effects are plausible contributors at 50 µs (1.5625 µs/sample; 320-sample window ≈ half 1 kHz period) but are **not proven causal** from source alone.
- **Decision gate:** `ROOT CAUSE PROBABLE — ADDITIONAL EVIDENCE REQUIRED`.

## 2. Evidence baseline and verification (Phase 1)

### 2.1 Reports and evidence locations (read/inspected)
- `B4_34_DEPLOYMENT_AND_HARDWARE_VALIDATION_REPORT.md` (SHA-256 verified in-workspace: `72f67c30355988f28d68ad971e10ddb1e1d91104a53c660216ed56012f9197fd`).
- `B4_34_TRIGGER_ARCHITECTURE_DECISION.md`, `B4_34_AUTONOMOUS_TRIGGER_ARCHITECTURE_INVESTIGATION.md`, `B4_33_TRIGGER_HANDOFF_INVESTIGATION_REPORT.md` (context; uncommitted in working tree).
- Preserved evidence: `evidence/B4_34_DEPLOY_48462_20261114T013600Z/` (preflight/deploy/validation incl. `validation/remote_collected/b434_validate_48462/`), `evidence/B4_34_ROLLBACK_14139_20261115T034700Z/`, `evidence/B4_34_HW_GATE_20261114T005319Z/`, `evidence/B4_34_RECOVERY_20261114T010000Z/`.
- Historical reference: `evidence/B4_33_PROGRAMME_20261114T035819Z/...` (12639 failure signature baseline, as cited in prior reports).

### 2.2 Hash and integrity verification (independent recomputation in this session)
- **B4.34 validation artifacts (all `OK`):** `captures.jsonl` `ac57037d2f5aac0a855f24288ebb6d88651d7d24fe80af38d2e50df5c3f9c6b9`, `campaign.log` `73cbeb64b991f7f64410bde49efadade5a4c1bb190679bde422d7330e5b4ed78`, `functional_checks.json` `95f3889633a25793f9e34767feec9b4571f7111f797ddb96fa93e8c22742c388`, `identity.json` `b3782a78f6b9fd3f020edca863b52b04307dbc3470b9e3a82d87367dced0e540`, `signal_characterization.json` `4a1e870ce2dbb9917319af9a6d6a73244ca257064cd8377a96923ba09cc05139`, `summaries_index.json` `db8c3a4cf9743457463ec7cfa101b11bd0eadad4f920860ac42517cd25c6b8f1`, `DONE_MARKER` `46444e91bd066151fcf08ad9146c8683e1ee4055a82e276367105b683ce68175`, and all 12 `summary_*.json` files matched recorded SHA-256 values.
- **Rollback verification image:** `evidence/B4_34_ROLLBACK_14139_20261115T034700Z/post_rollback_readback_64k.bin` SHA-256 `e2bd129fd044f44246e9c5a85547d92f2559f6b25ee72ced4d7fe0eff09949f8` (matches required recovery).
- **Candidate (48462, recorded):** bin `85610ebc07382b00b1cc13737e503d61191ae3461f75be91fbb546c41fffee81` (40,172 B), elf `05b739710f0dff43e3a40460f528219d4cfbf190aecb53d7f61499453fbca906` (per B4.34 artifacts/report).

### 2.3 Summary-vs-raw-capture agreement (recomputed)
- `captures.jsonl`: 646 records total (600 `kind=capture`, 46 non-capture/meta). 12 configs × 50 captures: `tb5_auto_fall/rise`, `tb5_norm_fall/rise`, `tb6_auto_fall/rise`, `tb6_norm_fall/rise`, `tb9_auto_fall/rise`.
- For all 12 configs, recomputed per-config counts (`total`, `coherent_n`, `no_frame_n`, `diag_seq_mismatch_n`) and frame crossing presence (`frames_with_rising_crossing`, `frames_with_falling_crossing`) **match** the corresponding `summary_*.json` exactly (`SUMMARIES_AGREE_WITH_CAPTURES True`).

### 2.4 Key observed results (from preserved B4.34 evidence)
- Identity/functional (48462): `BUILD_SEQ 48462`, `STATUS 1`; stability tb5/6/9 `12/12 errs 0`; 5/5 stream toggles OK; ETS diag snapshot readable in pre-capture checks; signal Vpp ~179–181, level region ~970.
- **Primary failure (50 µs rising):** `tb5_auto_rise` and `tb5_norm_rise` each 0/50 requested rising crossing within ±2 of 96 (0%), `rise_count=0` in all frames, `fall_count=1` in all frames, falling crossing at i≈285.94–286.00. No frame errors, no no_frame.
- **50 µs falling:** 50/50 AUTO and 50/50 NORM with requested falling crossing at i=96.00 (fall-only frames).
- **0.1 ms / 1 ms:** all 8 configs 50/50 requested edge at i=96.00 (100%); both rise/fall crossings present in frames as expected for multi-period windows.
- **Diagnostics:** 600/600 captures `diag_snapshot_ok=false`, `diag_snapshot_status="seq_mismatch"`, `diag={}`, `coherent=false`.

> Note: B4.34 report text references `evidence/B4_34_HW_GATE_20251114T005319Z/` in its footer; the workspace directory is `evidence/B4_34_HW_GATE_20261114T005319Z/` (artifact present and consistent with described contents).

## 3. Complete trigger event-path diagram (Phase 2)

### 3.1 Stage-by-stage trace (source-level)

| Stage | Functions / locations | Registers / flags / variables | Behavior (as implemented) |
|---|---|---|---|
| 1. User-selected trigger slope | `op_trig_slope` / `s_trig_slope` (`app/main.c`), `acq_set_trigger_slope` (`app/acq.c`), `trig_cfg.slope` (`app/trigger.h/.c`) | `s_trig_slope` (0 rise,1 fall,2 auto), `s_trig_slope_hw` (0/1), `ADC1->LTR/HTR` (via band) | UI/link set `s_trig_slope`; main loop syncs to HW via `acq_set_trigger_slope`. AUTO mapped to rising for HW (`s_trig_slope_hw=0`). Software refinement uses rising for AUTO. |
| 2. Trigger threshold setup | `op_trigger`/`acq_set_trigger_level`, `acq_trigger_level_get` (`app/acq.c`) | `s_trig_level`, `ADC1->LTR`, `ADC1->HTR` | User level stored in `s_trig_level`; one-sided AWD band written by `apply_trigger_band()`. |
| 3. ADC analogue watchdog configuration | `acq_init`, `apply_trigger_band` (`app/acq.c`) | `ADC1->CR1`: `AWDCH=ACQ_SIGNAL_CHANNEL(2/PA2)`, `AWDSGL`, `AWDEN` set; `AWDIE` armed per-cycle only. `ADC1->HTR/LTR` slope-dependent. `ADC1->SR` cleared in init/arm paths. | Single-channel AWD on regular group, out-of-window behavior. Continuous conversions paced externally (not ADC continuous mode). |
| 4. AWD threshold/hysteresis handling | `apply_trigger_band`, `acq_handoff_*` (`app/acq_handoff.h`) | `ACQ_TRIGGER_HYST=8` (`ACQ_HANDOFF_HYST`), `s_trig_level` | Rising: `LTR=0, HTR=min(level+hyst,4095)` (fires when sample > HTR). Falling: `LTR=max(level-hyst,0), HTR=4095` (fires when sample < LTR). Rearm/arm-safe predicates mirror same window semantics. |
| 5. Trigger rearm logic | `acq_wait_rearm`, `acq_handoff_rearm_ready/should_arm`, `acq_cycle` (`app/acq.c`) | `s_trig_slope_hw`, `s_trig_level`, latest DMA sample | Bounded wait for far-side sample while conversions run (`rearm_ready`); boundary safety decided on quiesced stable sample (`arm_safe`). AWDIE enabled only if `arm_safe` (48462). |
| 6. Acquisition timer start/stop | `tim3_hw_start/stop` (`app/tim3_hw.c`), `acq_set_timebase`, `acq_cycle`, `pacing_resume`, `acq_pause/resume` (`app/acq.c`) | `TIM3->CR1/CR2/SMCR/DIER/CCER/PSC/ARR/CNT/EGR/SR`, `s_timebase`, `s_hw_timebase` | Start programs PSC/ARR/CNT=0, issues `UG`, clears `SR`, sets `MMS=010` (TRGO on update) then `CEN`. Stop clears `CEN` and `CR2`. Quiescent arm stops/restarts TIM3 around arm decision. |
| 7. ADC conversion scheduling | `acq_init`, `timebase_smp_code` (`app/timebase.c`), `adc_channel_sample_time` (`app/acq.c`) | `ADC1->CR2`: `EXTSEL=TIM3_TRGO (100)`, `EXTTRIG`, `DMA`; `ADC1->SQR1/SQR3`; `ADC1->SMPRx` | 1 regular conversion on ch2 per TIM3 TRGO. Sample time per HW timebase (tb5/tb6/tb9 use SMP 1.5, code 0). |
| 8. DMA transfer & circular buffer position | `dma_start`, `acq_latest_sample`, ISR/cycle reads (`app/acq.c`) | `DMA1_Channel1->CCR/CPAR/CMAR/CNDTR`, `ADC1->DR`, `s_buf[1024]`, `s_snap[1024]`, `s_snap_pos`, `s_resume_pos` | Circular P2M halfword ADC1->DR→`s_buf`. Write position derived as `(ACQ_SAMPLES - CNDTR) % ACQ_SAMPLES`; last sample at `pos-1`. Channel kept enabled; requests gated via `ADC1->CR2.DMA` in pause/freeze/quiescent arm. |
| 9. AWD status flag handling & interrupt entry | `acq_isr_adc`, NVIC setup (`app/acq.c`, `stm32f1xx_it.c`) | `ADC1->SR.AWD`, `ADC1->CR1.AWDIE`, `ADC1_2_IRQn` (prio 2) | On AWD with `AWDIE`: ISR clears `AWDIE` (one-shot), clears `ADC1->SR`, captures `TIM3->CNT` and CNDTR-derived index, records HW capture, sets `s_event/s_trigger_index` on first event. |
| 10. Hardware trigger index capture | `acq_isr_adc`, `acq_gate` (`app/acq.c`, `app/acq.h`) | `s_trigger_index`, `s_hw_capture.*`, `s_hw_seq`, `s_arm_generation`, `DMA1_Channel1->CNDTR` | `idx = (ndtr<=ACQ_SAMPLES)?(ACQ_SAMPLES-ndtr):0` at ISR read time (lags triggering conversion by completion/DMA/ISR latency). |
| 11. Trigger event generation & state transitions | `acq_cycle`, `trig_fsm_action` (`app/acq.c`, `app/trigger.c`) | `s_event` (0/1/2), `acq_gate.triggered/fresh`, `s_trig` (mode/armed) | `s_event==1` on AWD; timeout sets `s_event==2`. Post-fill wait for triggered event. Mode policy decides CAPTURE/FALLBACK/HOLD (AUTO fallback on timeout/unusable, NORM/SINGLE hold without usable trigger). |
| 12. Software refinement | `trig_refine` (`app/main.c`), `trig_scan_nearest` (`app/trigger.c`) | `TRIG_REFINE_BACK=128`, `TRIG_REFINE_FWD=48`, `BENCH_PRE=96`, `s_last_trig_idx` | Searches 176-sample window centered on HW index for nearest Schmitt crossing (slope-aware, AUTO→rising); fallback simple threshold crossing nearest center; else `found=0` (caller uses HW index for triggered, or tail if untriggered). |
| 13. Final frame positioning & publication | `frame_build`/`frame_build_tail` (`app/frame.c`, `app/acq_window.*`), `acq_bench_poll` (`app/main.c`) | `BENCH_WINDOW=320`, `BENCH_PRE=96`, `s_frame`, `s_diag_*`, `s_diag_seq` | Triggered+refined or triggered+HW-anchor placed with `pre=96` so trigger near index 96; untriggered uses tail. Diag fields snapshotted and `s_diag_seq++` after recorded acquisition handling. |

### 3.2 Data-flow: requested slope and detected event

```text
User/UI or link
  -> s_trig_slope (0 rise,1 fall,2 auto) [main.c]
     -> acq_set_trigger_slope(): s_trig_slope_hw = (fall?1:0)  [HW: AUTO mapped to rising]
        -> apply_trigger_band(): ADC1->LTR/HTR one-sided window for s_trig_slope_hw
           -> (quiescent arm, 48462) arm_safe = inside_window(stable boundary sample, s_trig_slope_hw)
              -> ADC1->CR1.AWDIE enabled only if arm_safe (while quiesced)
                 -> ADC conversion outside [LTR,HTR] sets ADC1->SR.AWD
                    -> ADC1_2 IRQ: latch CNDTR-derived trigger_index + HW capture, set s_event=1 (first)
                       -> acq_cycle marks triggered (if post-fill ok) -> gate.triggered, gate.trigger_index
                          -> trig_refine(): searches around HW index using cfg.slope = (s_trig_slope==FALL?FALL:RISING)
                             -> refined index (found) OR HW index (triggered, found=0) OR tail (untriggered)
                                -> frame_build(..., used_idx, BENCH_PRE=96): places requested/selected crossing near window idx 96
                                   -> s_frame published; s_diag_seq++ and s_diag_{gate,hw,refined,fallback,...} updated
```

**Key representations:** requested direction is split into (a) HW arming/band via `s_trig_slope_hw` (AUTO→rising) and (b) SW refinement slope via `s_trig_slope` (AUTO→rising). `gate.triggered` reflects AWD event occurrence (any out-of-window crossing for the configured band), not an explicit “edge polarity matched request” flag beyond band design.

## 4. ADC analogue watchdog register and polarity analysis (Phase 3)

### 4.1 Registers/bits/flags traced (source-observed)
- **Configuration (`app/acq.c:acq_init`, `apply_trigger_band`):** `ADC1->CR1 |= (AWDCH & ACQ_SIGNAL_CHANNEL) | AWDSGL | AWDEN` (channel 2, single-channel, regular group enabled). `AWDIE` toggled dynamically (armed in `acq_cycle`, cleared in `acq_isr_adc` as one-shot and during arm sequencing). `ADC1->LTR/HTR` set per slope/level/hyst. `ADC1->SR` cleared (`= 0U`) at init/arm/ISR points as implemented.
- **Interrupt/NVIC:** `ADC1_2_IRQn` enabled prio 2 (`acq_init`). `acq_isr_adc` returns immediately if `s_inited==0` or `SR.AWD` clear; otherwise clears `AWDIE`, clears `SR`, captures state, sets event.
- **No JAWD path observed:** `JAWDEN`/injected watchdog not used in acquisition path.

### 4.2 Implemented edge-detection mechanism (STM32F1 out-of-window AWD)
- **Rising request (`s_trig_slope_hw==0`):** `LTR=0`, `HTR=min(level+hyst,4095)` (`apply_trigger_band`). AWD fires when converted sample is **outside** `[LTR,HTR]` → for normal in-range levels this means sample `> HTR` (crossing upward past upper threshold). Correct for detecting a rising transition through the band.
- **Falling request (`s_trig_slope_hw==1`):** `LTR=max(level-hyst,0)`, `HTR=4095`. AWD fires when sample `< LTR` (crossing downward). Correct for detecting a falling transition.
- **Hysteresis:** fixed `ACQ_TRIGGER_HYST=8` used in band and in SW refinement; rearm predicates require far-side (`<= level-hyst` for rising, `>= level+hyst` for falling). Arm safety requires boundary sample inside window.
- **Re-arming:** `acq_wait_rearm()` waits bounded for far-side sample (while running), then 48462 quiesces and enables `AWDIE` only if stable boundary sample inside window; ISR disables `AWDIE` on first AWD (one-shot). Mode policy (AUTO/NORM/SINGLE) controls post-cycle behavior, not AWD hardware polarity.

### 4.3 Symmetry assessment
- **By design/code:** rising and falling paths are symmetric in band construction, rearm/arm predicates (`acq_handoff.h`), and refinement slope selection, differing only in threshold side/direction. No 50 µs-specific branch in AWD/band/arm code was identified.
- **Runtime caveat (not provable from source alone):** symmetry depends on sampled signal phase at arm boundary, conversion timing, and ISR/DMA timing. With 48462’s quiescent stop/restart, effective sampling phase relative to the analog signal is perturbed per arm; asymmetry could emerge from timing/phase interaction despite symmetric register logic.

### 4.4 Conclusions
- Register-level polarity/banding as coded is **plausible and consistent** with requested-edge detection; there is **no direct source evidence** of inverted AWD thresholds for rising. H1 (simple AWD polarity/config error) is **weak** given 50 µs falling and 0.1/1 ms rising pass with the same mechanism.

## 5. Timebase comparison and calculated timing (Phase 4)

### 5.1 Configured values (from `app/timebase.c/.h`, `app/acq.h/.c`, `app/tim3_hw.c`)
- Clocks (as used by table): `TIMEBASE_APB1_HZ=32 MHz`, `TIMEBASE_TIM3_HZ=64 MHz` (APB1 prescaler != 1 ⇒ timer clock doubled), tick 15.625 ns/count. ADC clock `PCLK2/6 = 10.666… MHz`, conversion = (SMP + 12.5) ADC clocks; 1 ADC clock = 6 TIM3 counts. `TIMEBASE_MIN_SS_COUNTS=84` (1.3125 µs) at SMP 1.5. `BENCH_WINDOW=320`, `BENCH_PRE=96`.

| TB idx | Label | div (µs) | PSC | ARR | Counts/ sample | Sample interval (ns / µs) | Window (samples / time) | SMP code (fits baseline) | Conversion (µs, SMP 1.5) | Periods of 1 kHz in window (approx) |
|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 5 | 50 µs | 50 | 0 | 99 | 100 | 1562.5 ns / **1.5625 µs** | 320 / **500 µs** | 0 (1.5 cyc) | 1.3125 µs | **0.5** (half period) |
| 6 | 0.1 ms | 100 | 0 | 199 | 200 | 3125 ns / **3.125 µs** | 320 / **1000 µs** | 0 | 1.3125 µs | **1.0** (2 edges/cycle typical) |
| 9 | 1 ms | 1000 | 0 | 1999 | 2000 | 31250 ns / **31.25 µs** | 320 / **10,000 µs** | 0 | 1.3125 µs | **10.0** |

**Assumptions:** clock tree as coded (64 MHz SYSCLK, APB1 32 MHz, TIM3 64 MHz, ADC PCLK2/6) is correct for this target; actual silicon/board clocking not re-measured in this read-only session. 1 kHz test signal assumed from campaign context/signal characterization (not directly proven by firmware).

### 5.2 Timing/behavior differences relevant to 50 µs
- **Samples per signal period (1 kHz):** ~640 at tb5, ~320 at tb6, ~32 at tb9. **Window vs period:** tb5 window is ~half-period (320 samples) so a correctly placed single-edge capture may contain only that edge type; tb6/tb9 contain multiple periods/edges.
- **Conversion headroom:** conversion 1.3125 µs vs interval 1.5625 µs at tb5 (tightest among tested single-shot ranges) leaves ~250 ns margin; at tb6/9 margin is ample. This makes tb5 most sensitive to pacing/restart/jitter effects, though no overrun evidence is present.
- **TRGO/TIM3 restart (48462):** `tim3_hw_start` resets `CNT=0`, generates `UG` then clears `SR`, sets `MMS=TRGO on update` after `UG` (to avoid unintended TRGO from init) and enables counter. Restart thus re-phases the sampling grid and introduces a gap (stop + settle + restart latency) before next TRGO/conversion. At 1.5625 µs/sample, even small gaps correspond to multiple missed samples and an arbitrary post-restart phase relative to a continuous analog input.
- **Quiescent-arm ordering interaction:** `acq_cycle` waits for pre-trigger headroom (`>= TRIG_PRE_TRIGGER` since `s_resume_pos`) **before** quiescent arm; quiescent arm then stops/restarts TIM3 and sets `s_resume_pos` to the quiesced write position. Old ring contents remain, but sampling continuity/headroom assumptions around the arm boundary are perturbed (timing discontinuity explicitly acknowledged in B4.34).
- **AWD/ISR/CNDTR:** `trigger_index = ACQ_SAMPLES - CNDTR` at ISR read (latched after conversion+DMA+IRQ latency). Refinement window is 176 samples centered on HW index; a sufficiently displaced anchor cannot be recovered.

### 5.3 50 µs-specific explanatory value
- tb5’s half-period window means that if the capture is anchored to the **wrong phase** (e.g. near a falling transition region instead of the intended rising transition), the window can contain **only a falling crossing at ~286 and no rising crossing at all** — matching observations exactly. At tb6/tb9 the presence of multiple edges in-window makes “requested edge at 96” achievable even with some phase variation (nearest requested crossing can be placed), consistent with 100% placement there. This supports a **phase/anchor selection error specific in manifestation at 50 µs**, but does not by itself prove the internal cause (AWD latch vs restart-induced phase vs placement logic).

## 6. Diagnostic coherence failure analysis (Phase 5)

### 6.1 Observed failure (48462 validation)
- 600/600 captures: `diag_seq_pre`/`diag_seq_post` bracketed reads around frame never produced a stable coherent snapshot (`diag_snapshot_ok=false`, `diag_snapshot_status="seq_mismatch"` in all cases per validation script logic; occasional `read_error` possible by design but not observed in aggregates shown), `diag={}`.

### 6.2 Firmware-side implementation (traced)
- **Storage/update (`app/main.c`):** volatile diag fields (`s_diag_seq`, `s_diag_gate_triggered`, `s_diag_hw_index`, `s_diag_hw_count`, `s_diag_refined_index`, `s_diag_refined_found`, `s_diag_fallback`, `s_diag_timebase`, `s_diag_trig_slope_snap`, `s_diag_trig_level_snap`, `s_diag_trig_mode_snap`) updated in `acq_bench_poll` after a recorded acquisition/frame decision path: fields assigned then **`s_diag_seq++`** (e.g. around the post-refinement/publish logic; seq increment observed immediately after setting diag fields for that acquisition record).
- **Readout:** `GETV_DIAG_SEQ` and `GETV_DIAG_*` handled in `op_get_value`/link command path as individual 16-bit value reads per request/response (no firmware-side atomic multi-field snapshot or seqlock; `DIAG_SEQ` is a plain counter read).

### 6.3 Host/validation protocol (traced)
- `validate_48462.py: coherent_diag_snapshot()` reads `pre = GETV(DIAG_SEQ=18)`, reads diag fields (19–27,45) individually, reads `post = GETV(DIAG_SEQ)`, accepts only if `pre==post` (retries up to 8), else returns `seq_mismatch` with empty diag. Capture flow reads `diag_seq_pre`, enables streaming and reads a frame, takes snapshot, disables streaming.

### 6.4 Why incoherence occurs (analysis)
- **Concurrent mutation:** `acq_bench_poll` runs in the main loop and increments `s_diag_seq` on each recorded acquisition; streaming/acquisition continues across the host’s multi-request snapshot window, so seq can change between `pre` and `post` (or between field reads) → persistent mismatch under active acquisition.
- **Non-atomic multi-field reads:** fields are fetched as separate GETV transactions without a stable firmware-side snapshot boundary; even if seq were stable, cross-field consistency is not guaranteed by protocol design (seq is used as a heuristic guard).
- **Semantic coupling to acquisition cadence:** during 50 µs captures (frequent cycles/frames), likelihood of interleaving is high, explaining universal mismatch in this campaign.
- **Suitability:** current mechanism is **not reliable for per-capture correlation during active acquisition/streaming** (consistent with prior notes and observed 600/600 mismatch).

### 6.5 Smallest defensible way to obtain coherent diagnostics in a future test (no implementation change now)
- **Quiesce-then-snapshot:** stop/hold acquisition and streaming deterministically (e.g. `RUN/STOP` and `STREAM 0` as appropriate), allow in-flight activity to settle, then perform bracketed read (`seq_pre` → all `GETV_DIAG_*`/related fields → `seq_post`) and accept only on equality; associate snapshot with the **last completed recorded acquisition** (not a concurrently streamed frame unless frame identity/seq binding is added).
- **Single-transaction snapshot (future, protocol-level):** add a firmware command that latches/copies a consistent diag set (and sequence/identity) under a brief critical section and returns it as one atomic response/blob — smallest coherent mechanism if per-capture correlation during/adjacent to capture is required. (Design choice; not implemented here.)
- **Host tooling:** record frame identity/sequence if available and bind snapshots explicitly; treat `seq_mismatch` as unusable for correlation (already done).

## 7. Hypothesis comparison (Phase 6)

| Hypothesis | Supporting evidence | Contradicting / limiting evidence | Source locations | Confidence | Discriminating observation |
|---|---|---|---|---|---|
| **H1. Rising AWD configuration or threshold polarity is incorrect** | 50 µs rising shows no rising crossings and wrong-phase capture in all 100 frames. | AWD band is explicitly one-sided and symmetric in code (`apply_trigger_band`, `acq_handoff.h`); 50 µs falling and 0.1/1 ms rising pass 100%. No 50 µs-specific polarity branch found. | `app/acq.c` (apply_trigger_band, init), `app/acq.h`, `app/acq_handoff.h` | **Low** | Coherent per-capture diag showing AWD latch/slope context and sample values around fire; or direct register/signal trace proving opposite-threshold fire for rising. |
| **H2. Rearm logic enables AWD at an unsafe signal phase** | 48462 still fails physically at 50 µs rising; quiescent arm changes timing/phase (stop/restart, `s_resume_pos` reset after pre-arm headroom wait) even with stable boundary check. Rising needs boundary inside `[0,HTR]`; if effective post-restart sampling phase aligns badly, latch could occur on wrong transition. | Boundary sample checked while quiesced and `AWDIE` enabled only if inside window; 50 µs falling and other TBs pass. No direct evidence of unsafe enable. | `app/acq.c:acq_cycle` (quiescent arm), `acq_wait_rearm`, `app/acq_handoff.h`, `app/tim3_hw.c` | **Medium** | Coherent snapshot of boundary sample/arm decision vs subsequent latched edge (requires quiesced coherent diag) and/or on-target instrumentation of post-restart phase relative to signal. |
| **H3. AWD interrupt/status handling loses or misclassifies the requested event** | Persistent wrong capture for rising requests; AWD event is “out-of-window” (band-selected) but no explicit slope-tagged event record beyond configured band. | ISR is simple one-shot latch (`SR.AWD` gated, clears `AWDIE/SR`, records first event); symmetric config; other cases pass. | `app/acq.c:acq_isr_adc` | **Low–Medium** | Correlated HW capture/diag showing which transition produced `s_event` and resulting `trigger_index` for rising 50 µs. |
| **H4. DMA/CNDTR timing causes a wrong hardware trigger index** | HW index is latency-sensitive (CNDTR at ISR read). 50 µs has highest sample rate among tested. | Falling 50 µs placed correctly at 96; 0.1/1 ms correct; refinement window tolerates moderate latency but observed signature implies large phase/edge mismatch, not small jitter. | `app/acq.c` (ISR, cycle), `app/main.c:trig_refine` | **Low** | Coherent HW index vs true edge position over many captures; compare with known-good cases at same TB. |
| **H5. Timer restart or conversion gating creates a 50 µs-specific phase discontinuity** | 48462 stops TIM3/gates DMA, settles, then restarts TIM3 (`CNT=0`, `UG`, MMS after UG) and resumes after pre-arm headroom logic; tb5 interval 1.5625 µs makes gap/phase reset most impactful and window is half-period (wrong phase can remove rising edge from window). Explains manifestation specificity. | Intended to be deterministic; no direct measurement of gap/phase. Other TBs unaffected in observed placement. | `app/acq.c:acq_cycle`, `app/tim3_hw.c`, `app/timebase.c` | **Medium** | On-target measurement of stop duration, first-conversion timing/phase after restart, and resulting capture content at 50 µs rising with coherent diag. |
| **H6. Trigger refinement or frame placement is responsible** | Misplacement could in principle come from refinement/placement. | Refinement selects nearest crossing in window around HW index or falls back to HW index; for 50 µs rising **no rising crossing exists in captured frame** at all (fall-only at ~286), which refinement cannot fabricate/misplace from a correct rising-anchored capture. Frame placement uses fixed `BENCH_PRE=96`. 0.1/1 ms and 50 µs falling correct. | `app/main.c:trig_refine`, `app/trigger.c`, `app/frame.c`, `app/acq_window.*` | **Very low** | Demonstrate a frame containing a correctly positioned rising crossing that refinement/placement moves/removes (contradicted by current frame content). |
| **H7. Interaction between multiple mechanisms** | Combines quiescent-arm timing discontinuity (H5/H2) with half-period window sensitivity at tb5 and anchor/refinement dependence; AWD banding itself appears correct (against H1). Fits “rising-only at 50 µs fails, others pass” and wrong-phase frame content. | Precise interaction unproven without coherent HW diagnostics/timing measurements. | All above | **Medium–High (as class of explanation)** | Coherent per-capture correlation: arm boundary state, post-restart sampling/first conversion, AWD fire event/index, refinement input/result, and final frame crossings across tb5 rising vs falling/other TBs. |

## 8. Host/simulation test results and limitations (Phase 7)

### 8.1 Existing tests (not modified; results from prior recorded runs)
- **48462 candidate (B4.34 context):** `make -s -j4 test` **PASS (RC 0)**, `make -j4` **PASS (RC 0)** (per `B4_34_AUTONOMOUS_TRIGGER_ARCHITECTURE_INVESTIGATION.md`; not re-run as part of hardware interaction). Relevant host tests include `tests/test_acq_handoff.c` (boundary/temporal predicate modeling, mode-policy regressions) and `tests/test_trigger.c` (incl. refinement/edge-scan characterizations such as rearm-sensitive scenario geometry).

### 8.2 Additional production-firmware modifications
- **None.** No production firmware source modified during B4.35 (investigation-only). No new tests/scripts added in this session.

### 8.3 Coverage/limitations
- Host tests validate **pure logic** (handoff predicates, trigger scan/refinement/FSM, frame/measurement math) and modeled ordering; they **do not** execute or faithfully model STM32F103 ADC1 AWD behavior, TIM3 TRGO pacing/restart sequencing, DMA1 circular/CNDTR progression, NVIC preemption/ISR latency, analog signal continuity across quiesce gaps, or register-level concurrency.
- `acq_cycle` hardware control flow (quiesce, settle, TIM3 restart, DMA gating, live ring/ISR interaction) is **not host-simulatable** with the existing test harness.
- Existing B4.34 hardware evidence provides frame-level outcomes but **no coherent per-capture HW diagnostics** (`seq_mismatch`), limiting causal attribution.
- **Simulation vs proven hardware:** any inferred timing/phase mechanism above is **analytical from source/config and observed outcomes**, not a proven on-target measurement.

## 9. Most likely root cause, with confidence and evidence

**Most likely (probable, not confirmed):** a **50 µs-specific trigger arming/phase interaction introduced or exposed by 48462’s quiescent stop–gate–settle–restart sequence** causes the post-arm acquisition to latch/place captures at the wrong signal phase for **rising-edge requests** (yielding frames containing only a falling transition near ~286, no rising crossing), while falling-edge requests and slower timebases remain correctly placed. Contributing factors consistent with evidence: (1) TIM3 stop/restart (`CNT` reset, `UG`/TRGO sequencing) and DMA request gating create a sampling discontinuity and re-phase the sample grid relative to a continuous analog input; (2) `acq_cycle` performs this quiesce/restart after pre-trigger headroom was awaited and resets `s_resume_pos` at the quiesced point; (3) at tb5 (1.5625 µs/sample, 320-sample half-period window for 1 kHz) wrong phase manifests as absence of the requested rising edge in-window and presence of the opposite edge. The AWD banding logic itself appears polarity-correct in source.

**Confidence:** **Medium** (probable class of cause).  
**Why not confirmed:** 600/600 per-capture diagnostics were incoherent (`seq_mismatch`), so the actual AWD firing event, boundary/arm state, HW index, and refinement behavior for the failing rising captures cannot be directly correlated on hardware; alternative or interacting causes (e.g. subtle arm-boundary/phase acceptance specific to rising under restart, or an unmodeled peripheral timing effect) cannot be excluded.

## 10. Recommended next engineering step

1. **Enable coherent, per-capture-correlated diagnostics (defensible, minimal risk to contracts):** implement a firmware-side atomic diagnostic snapshot (or quiesce-safe snapshot command) and host capture flow that binds `DIAG_SEQ`/HW index/refined/found/fallback/trigger config/timebase/gate and (if feasible) key arm-state markers to the **same** acquisition/frame; validate snapshot coherence before any further hardware fix attempt. (Do not weaken existing evidence/acceptance criteria.)
2. **Instrument the quiescent-arm/restart path (read-only/diagnostic additions, separately planned):** record timestamps/counters or state around `tim3_hw_stop/start`, settle duration, `s_resume_pos`, first post-restart conversion/index progression, and boundary sample/arm decision for tb5 rising vs tb5 falling (and a control TB) under controlled conditions.
3. **Hardware A/B characterization (only with explicit authorization and safety gates):** compare captured frame content and correlated diags for (a) current quiescent arm, (b) arm sequencing that preserves required determinism without unnecessary phase reset where feasible, and/or (c) controlled re-evaluation of pre-arm headroom relative to restart — to isolate H5/H2/H7. Any flashing must follow approved `tools/flash_cycle.py`, protected-page/page-0-last, and independent verification.
4. **Defer code fixes until correlation identifies the failing transition:** avoid speculative polarity/refinement changes given passing 0.1/1 ms and 50 µs falling behavior.

## 11. Explicit list of unresolved questions

- What exact event (sample/value/edge) produced `s_event`/AWD fire for 50 µs rising captures in 48462 (rising crossing, falling crossing, or other out-of-window condition) and at what HW index?
- What were `rearm_ready`, quiesced boundary sample, `arm_safe`, and post-restart timing/phase for each failing 50 µs rising arm?
- How large is the actual TIM3 stop-to-first-valid-conversion gap at tb5 on hardware, and how does it align with the 1 kHz signal phase?
- Does resetting `s_resume_pos` and restarting TIM3 after pre-arm headroom wait invalidate pre-trigger continuity in a way that biases anchor/placement at tb5?
- Is the 48462 behavior reproducible with coherent diagnostics and identical signal/level/methodology once snapshot coherence is fixed?
- Would a quiescent-arm variant that avoids `CNT`/phase reset (if safe/feasible) preserve determinism and restore 50 µs rising placement?
- Are there board/analog-front-end or stock-firmware trigger-path behaviors (e.g. TIM4_CCR1/timing capture) relevant to 50 µs rising that are not represented in current firmware (stock mechanism remains unverified in-repo)?

## 12. Short proposed B4.36 implementation plan (not implemented)

**B4.36 (proposed, contingent on separate authorization and B4.35 findings): Diagnostic coherence + targeted arm-timing characterization/fix**

1. **Diagnostics (prerequisite):**
   - Add a coherent diag snapshot mechanism (atomic firmware latch/return of `DIAG_SEQ` + all `GETV_DIAG_*`/HW-capture/arm markers needed) and update host tooling to bind snapshot to acquisition/frame identity; verify 0 `seq_mismatch` in a dry-run capture protocol (or explicit quiesce-snapshot protocol) before relying on data.
2. **Targeted instrumentation/experiment (minimal, guarded):**
   - Add read-only counters/state exposure for quiescent-arm phases (stop/settle duration, restart, boundary sample, arm_safe/armed, resume_pos delta) behind existing diagnostic channels; no behavior change to trigger decision.
   - Run authorized on-hardware correlated captures at tb5 rising/falling and tb6 control to test H2/H5/H7 (compare anchor/edge vs arm timing).
3. **Candidate fix (only if evidence supports):**
   - If H5/H2 confirmed: adjust quiescent-arm sequencing to maintain deterministic boundary safety while minimizing phase discontinuity (e.g. preserve/rebase timing continuity or reorder headroom/restart as evidence dictates), keeping contracts (B2/B-REC/ETS/diag meanings) intact and RAM/flash within budgets.
   - If evidence instead implicates another mechanism: address that mechanism specifically with regression coverage for all 12 configs.
4. **Validation/rollback readiness:**
   - Full host tests + build; authorized deployment via `tools/flash_cycle.py` with safety gate, independent readback/identity, complete 12×50 matrix with coherent diagnostics, and immediate safeguarded rollback path to verified 14139 on failure.

*No production firmware or diagnostic implementation performed in B4.35.*

## 13. Final decision gate

**`ROOT CAUSE PROBABLE — ADDITIONAL EVIDENCE REQUIRED`**

**Rationale:** B4.34 provides definitive frame-level evidence that 48462 fails 50 µs rising (wrong/absent rising edge, falling edge at ~286) while other tested cases pass, and source analysis identifies a plausible 50 µs-sensitive phase/timing interaction in the quiescent arm/restart path (with AWD polarity/banding appearing correct). However, universal per-capture diagnostic incoherence (`seq_mismatch`) prevents direct correlation of AWD firing, arm boundary state, HW index, and refinement for the failing cases; therefore the root cause cannot be confirmed from available evidence and any fix would be speculative. Coherent on-target diagnostics and targeted timing characterization are required to distinguish H2/H5/H7 conclusively.