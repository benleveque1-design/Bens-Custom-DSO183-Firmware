/* M5 acquisition engine: ADC1 -> DMA1 channel 1 -> circular buffer, trigger
 * = ADC analog watchdog with a one-shot, ISR-latched event.
 *
 * Design inputs (provenance: ../analysis/m1_acquisition.md, ACQUISITION.md):
 *   - the vendor sample path is ADC1 -> DMA1 channel 1 into a circular
 *     sample table (1024 samples, PARAMS[16], RAM 0x20000548);
 *   - the vendor trigger is the ADC analog watchdog (F-02), with the trigger
 *     position derived from TIM4_CCR1 - the TIM4 part is M6 scope, this
 *     framework records the buffer write position instead (documented
 *     below, see acq_gate::trigger_index);
 *   - the front-panel slide switches are separate ADC channels (the ladder),
 *     so the SIGNAL channel is still UNVERIFIED (open question U-02a):
 *     ACQ_SIGNAL_CHANNEL must be confirmed on the bench before the numbers
 *     this engine reports are trusted.  Reading a wrong channel cannot harm
 *     the instrument (ADC + DMA are read-only for the board), it only makes
 *     the numbers meaningless;
 *   - the vendor's CR2-bit-21 start oddity (F-05) is NOT reproduced: this
 *     engine starts conversions from TIM3's TRGO through the regular
 *     external-trigger path (EXTTRIG=1, EXTSEL=101), which is the standard
 *     RM0008 way to pace the ADC from a timer.  (The vendor paces its own
 *     ADC from TIM2_CC2 - EXTSEL=100 - measured live on the bench, so the
 *     two cores never share a timer.)
 *
 * Policies this file implements (README "Rules this tree follows"):
 *   - no unbounded waits: every busy wait in here has an explicit
 *     HAL_GetTick() deadline (the vendored HAL's ADC enable / calibration /
 *     poll paths contain unbounded waits - this file does not use them);
 *   - no dead states: a cycle always terminates with OK, TIMEOUT or
 *     HW_ERROR and the superloop keeps iterating (the vendor's equivalent
 *     poll has no deadline);
 *   - no status byte gates, no ATSHA204A dependency, no shared "scratch
 *     slot" aliasing (F-08): the engine owns its state explicitly;
 *   - the analog-watchdog interrupt is a ONE-SHOT latch: it disables itself
 *     on the first event, so a standing out-of-window signal cannot turn
 *     into an interrupt storm.
 *
 * Not in scope of this framework (M6 work, deliberately):
 *   - timebase -> sample-rate generation (TIM3 cadence instead of the
 *     free-running ~530 kS/s conversion mode below);
 *   - the vendor's TIM4_CCR1 trigger-position mechanism;
 *   - the front-end sensitivity/coupling switching.
 *
 * Flashing note: this engine writes NO flash and touches NO option bytes,
 * NO DMA1 channel other than 1, NO ADC other than ADC1.
 */
#ifndef ACQ_H
#define ACQ_H

#include <stdint.h>

/* ---- build-time configuration (bench-check before trusting numbers) ------- */

/* Sample-buffer length: the vendor's own value (PARAMS[16] = 1024, live RAM
 * dump in ../analysis/m1_acquisition.md).  1024 x 2 B = 2 KiB of the 20 KiB
 * RAM budget. The DMA CNDTR is 16-bit, so this must stay <= 65535. */
#define ACQ_SAMPLES        1024U

/* The signal ADC channel - OPEN QUESTION U-02a, bench-verify.
 *
 * Vendor evidence (dso183_original.bin, ADC1 init at 0x08005924):
 *   CR1 = 0x00800302 -> AWDEN (bit 23) | AWDSGL (bit 9) | SCAN (bit 8)
 *                     | 12-bit (RES[1:0] = 10) | AWDCH (bits 4:0) = 2
 *   SQR3 = 2         -> the regular group's first (and only) conversion
 *                     is channel 2, i.e. PA2 / ADC12_IN2
 * The analog watchdog therefore watches exactly the channel the regular
 * group converts - the trigger input.  So the signal channel is 2, not the
 * "lowest free channel" assumption of earlier revisions.
 *
 * Still to be confirmed on the bench: that PA2 is really where the
 * instrument's BNC input lands.  The three ladder nodes are on PA5/PA6/PA7
 * (see app/ladder.h), so the signal channel does not collide with them. */
#define ACQ_SIGNAL_CHANNEL   2U    /* ADC12_IN2, PA2 */
#define ACQ_SIGNAL_PORT      GPIOA
#define ACQ_SIGNAL_PIN       GPIO_PIN_2

/* ADC clock: PCLK2 (64 MHz on this image) / 6 = 10.67 MHz, inside the
 * 14 MHz ADC-clock limit (RM0008).  The sampling time is per range:
 * timebase_smp_code() picks 7.5 cycles normally (0.70 us aperture, 20 cycles
 * per conversion = 1.875 us, so free-running conversions reach ~530 kS/s) and
 * 1.5 cycles on the fastest paced range (14 cycles = 1.3125 us).  The old
 * fixed 239.5 cycles (252 cycles = 23.6 us per conversion, ~42 kS/s) only
 * left room for single-shot from about 1 ms/div, which is why it is no longer
 * the default. */
#define ACQ_ADC_PRESCALER    RCC_ADCPCLK2_DIV6
#define ACQ_SAMPLE_TIME      ADC_SAMPLETIME_7CYCLES_5

/* Trigger: the watchdog is out-of-window on the F1, so it flags samples
 * ABOVE HighThreshold or BELOW LowThreshold.  A two-sided band fires on
 * whichever band edge the signal reaches first, which has no slope
 * selectivity: with the band around the level, a signal resting below the
 * band latches the watchdog on the very first conversion after arming and
 * the displayed window lands at a random phase ("the wave keeps rebuilding").
 *
 * The band is therefore ONE-SIDED and follows the selected slope:
 *   rising  -> LTR = 0,             HTR = level + hyst  (fires on a rise)
 *   falling -> LTR = level - hyst,  HTR = 4095          (fires on a fall)
 * acq_set_trigger_slope() rewrites it.  The caller still has to arm while
 * the input is on the rearm side of the threshold (acq_cycle() waits for
 * that, bounded) or the first conversion after arming can still latch. */
#define ACQ_TRIGGER_LEVEL    2048U
/* Half-width of the analog-watchdog hysteresis around the level.  The
 * software edge refinement uses the same value, so the hardware event and
 * the displayed crossing agree. */
#define ACQ_TRIGGER_HYST     8U

/* Every wait budget in this module (each one checked against HAL_GetTick).
 * Generous on purpose: these bound a register handshake, not a wait for a
 * signal, so they only need to exceed the peripheral's own latency.  Note
 * that SysTick does NOT feed the IWDG (deliberately: a hung main loop must
 * reset), so nothing here may wait longer than the watchdog timeout. */
#define ACQ_CAL_TIMEOUT_MS   20U    /* ADC RSTCAL/CAL self-clear bits   */
#define ACQ_DMA_STOP_MS      20U    /* DMA EN ack after writing 0       */
#define ACQ_ADC_STOP_MS      20U    /* ADON ack after writing 0         */
/* Mirror of main.c MAIN_IWDG_TIMEOUT_MS.  acq_cycle() polls with HAL_Delay()
 * and does NOT feed the watchdog, so a caller-supplied timeout longer than
 * this would be cut short by a reset instead of returning ACQ_TIMEOUT. */
/* RM0008 Table 79 (regular external trigger, ADC1/ADC2), EXTSEL[2:0]:
 *   000 TIM1_CC1  001 TIM1_CC2  010 TIM1_CC3  011 TIM2_CC2
 *   100 TIM3_TRGO 101 TIM4_CC4  110 EXTI11    111 SWSTART
 * The vendor image runs EXTSEL=100 (read live), i.e. TIM3_TRGO - the same
 * timer this driver paces from.  Two earlier revisions of this constant were
 * wrong (110 = TIM4_CC4, then 101 = TIM4_CC4 again): with either, the ADC
 * waits forever on a timer nobody starts and the DMA count never moves. */
#define ACQ_EXTSEL_TIM3_TRGO  4U
/* EXTSEL=111 selects SWSTART as the regular trigger source.  With EXTTRIG=1
 * the F1 takes the source from EXTSEL and ignores a bare SWSTART bit; this
 * is the combination the ladder reader needs (measured on the board:
 * EXTSEL=111 + SWSTART sets EOC immediately and DR returns a live count). */
#define ACQ_EXTSEL_SWSTART    7U

/* Timebase used until the caller picks one (index 9 = 1 ms/div). */
#define ACQ_DEFAULT_TIMEBASE  9U

/* Bound on "did conversions actually start": wait for the DMA counter to
 * leave its initial value. */
#define ACQ_START_MS         20U

#define ACQ_IWDG_TIMEOUT_MS  10000U
#define ACQ_MAX_WAIT_MS      5000U  /* clamp: must stay below the IWDG timeout */
/* Bound on the post-trigger fill stage.  At the slowest timebases one
 * conversion can take milliseconds, so this is deliberately longer than a
 * frame-at-speed; it only matters when the pacing timer stalls, in which
 * case the gate degrades to the untriggered (tail) path. */
#define ACQ_POST_FILL_MS     1000U

/* ---- public API ------------------------------------------------------------ */

enum acq_status {
    ACQ_OK        = 0U,  /* a complete, valid window is in the buffer */
    ACQ_TIMEOUT   = 1U,  /* no trigger within the caller's budget (the
                           AUTO-mode fallback: window still valid) */
    ACQ_PARAM     = 2U,  /* caller error (NULL out, timeout == 0) */
    ACQ_HW_ERROR  = 3U,  /* peripheral did not behave (calibration/enable
                           failed); safe to retry via another acq_init() */
    ACQ_NOT_INITED = 4U  /* acq_init() has not succeeded yet */
};

struct acq_gate {
    uint8_t  triggered;      /* 1 = watchdog fired, 0 = timeout fallback */
    uint8_t  fresh;          /* 1 = the snapshot was replaced; 0 = the
                              * caller must keep displaying the previous
                              * frame (NORM/SINGLE hold).  A held gate still
                              * reports the previous write position. */
    uint32_t trigger_index;  /* buffer write position latched at ISR entry.
                              * It lags the true crossing by the ADC-to-ISR
                              * latency: at 1.875 us/conversion that is a few
                              * samples, and the exact figure is open (U-04b).
                              * Treat it as "window position at trigger", and
                              * measure the offset on the bench against a
                              * known square before trusting sub-sample
                              * placement (M5/M6). */
    uint32_t write_index;    /* buffer write position when the snapshot was
                              * frozen (reduced mod ACQ_SAMPLES).  Valid on
                              * both paths and used for the untriggered
                              * AUTO tail window. */
    uint32_t duration_ms;    /* ms from arm to latch/timeout */
    uint32_t arm_generation; /* arming generation this cycle belongs to.
                              * A hardware trigger-phase capture is only
                              * bindable to the window when its generation
                              * matches this value (B4.27). */
};

/* Coherent copy of the ADC-watchdog trigger-phase capture (B4.27).
 *
 * `count` is TIM3->CNT as read at the watchdog ISR.  It is NOT a sub-sample
 * phase: it measures the ADC conversion completion plus ISR entry latency,
 * `(conversion_complete + latency) mod interval` (B4.27 timing
 * investigation).  It exists so a capture can be bound to a sweep and so a
 * timer wrap can be reported; never feed it to the ETS bin mapper.
 *
 * `bound` is 1 only when the capture was marked valid and its generation
 * matches the generation passed to acq_trigger_hw_read(). */
struct acq_trigger_hw {
    uint32_t trigger_index;  /* ring position latched with the count        */
    uint32_t count;          /* TIM3->CNT at ISR entry                      */
    uint32_t generation;     /* arming generation the capture belongs to    */
    uint8_t  valid;          /* 1 = a capture was written since the last arm */
    uint8_t  rollover;       /* 1 = count < conversion-complete offset      */
    uint8_t  bound;          /* 1 = valid and generation-matched            */
    uint8_t  reserved;
};

/* Initialise/enable ADC1 + DMA1_Ch1 and START the continuous sample path.
 * On failure it is safe to call acq_init() again (it quiesces first).
 * Never touches flash, option bytes, or any other DMA channel. */
enum acq_status acq_init(void);

/* Select the TIM3 pacing interval (timebase index 0..26).  Restarts the
 * timer with the new PSC/ARR; conversions pause for the duration of the two
 * register writes, so call it between frames, not inside one.
 * ACQ_PARAM for an out-of-range index, ACQ_NOT_INITED before acq_init(). */
enum acq_status acq_set_timebase(uint8_t index);

/* Move the analog-watchdog trigger level to `level`.  The band written is
 * ONE-SIDED and follows the slope selected by acq_set_trigger_slope():
 *   rising  -> HTR = level + hyst, LTR = 0
 *   falling -> HTR = 4095,         LTR = level - hyst
 * so the hardware event is always a crossing in the requested direction.
 * Safe to call while acquisition runs; ACQ_NOT_INITED before acq_init(). */
enum acq_status acq_set_trigger_level(uint16_t level);

/* Select the hardware trigger direction: 0 = rising, 1 = falling, 2 = auto
 * (mapped to rising, so AUTO still produces a repeatable alignment instead
 * of alternating edge directions).  Rewrites the watchdog band immediately;
 * safe while running.  ACQ_PARAM for slope > 2, ACQ_NOT_INITED before
 * acq_init(). */
enum acq_status acq_set_trigger_slope(uint8_t slope);

/* Select the trigger-mode policy (enum trig_mode: AUTO / NORM / SINGLE).
 * The watchdog remains the hardware event source; this only controls what
 * acq_cycle() does with a cycle that produced no usable trigger:
 *   AUTO   - fall back to the newest samples and keep refreshing;
 *   NORM   - hold the previous display until a trigger arrives;
 *   SINGLE - take exactly one triggered window, then hold.
 * Selecting SINGLE arms it.  ACQ_PARAM for an out-of-range mode;
 * ACQ_NOT_INITED before acq_init(). */
enum acq_status acq_set_trigger_mode(uint8_t mode);

/* Re-arm a completed SINGLE capture (also re-arms the watchdog on the next
 * acq_cycle()).  Harmless in AUTO/NORM. */
enum acq_status acq_trigger_arm(void);

/* Cycle = re-arm the watchdog, wait for a trigger (or the caller's timeout),
 * then FREEZE the sample path and copy the ring into an immutable snapshot
 * before restarting it.  `post_samples` is how many conversions must be
 * captured AFTER the trigger before freezing: pass the post-trigger share of
 * the display window (window - pre) so the window handed to frame_build is
 * entirely real, causal data.  Bounded by timeout_ms (clamped to
 * ACQ_MAX_WAIT_MS, which is below the IWDG timeout - this function does not
 * feed the watchdog); the post-fill stage has its own ACQ_POST_FILL_MS
 * bound.  If the post-fill cannot complete, the gate is reported as
 * untriggered and the untriggered tail window is still valid.
 *
 * After return, acq_samples() points at the frozen copy: the DMA is already
 * running again, writing the next capture, but it can no longer change what
 * the caller reads.  Safe to call repeatedly without any special teardown.
 * Before the first successful cycle acq_samples() still returns the live
 * buffer (there is no snapshot yet).
 *
 * With NORM/SINGLE a cycle without a usable trigger does NOT replace the
 * snapshot: gate.fresh is 0 and the caller keeps the previous frame.
 * AUTO always replaces it (headline `ACQ_TIMEOUT`, `triggered = 0`). */
enum acq_status acq_cycle(uint32_t timeout_ms, uint16_t post_samples,
                          struct acq_gate *out);

/* Pause/resume for a short, bounded use of ADC1's regular group (the
 * front-panel ladder).  The injected group is unusable on this board
 * (JSWSTART raises JEOC but JDR1..3 stay at zero), so the switches are read
 * the only way that is proven to work here: stop the pacing timer, park the
 * DMA channel and release the ADC to the ladder reader, then restore SQR3,
 * re-arm DMA and restart TIM3.  The pause is a few conversions long. */
enum acq_status acq_pause(void);
enum acq_status acq_resume(void);

/* Read back the values last applied, for the UI (scale label and trigger
 * marker).  acq_timebase_get() returns the index actually pacing TIM3;
 * acq_trigger_level_get() returns the user level, not the hysteresis band;
 * acq_trigger_mode_get() returns the enum trig_mode in effect. */
uint8_t  acq_timebase_get(void);
uint16_t acq_trigger_level_get(void);
uint8_t  acq_trigger_mode_get(void);

/* ETS (equivalent-time) support for REPT timebases (indices 0..4).
 * When a REPT timebase is selected, hardware runs at a fast base rate and
 * multiple triggered sweeps are interleaved.  acq_ets_active() reports
 * whether the current logical timebase uses ETS.  acq_trigger_phase_ns()
 * returns the raw TIM3 count of the last trigger event expressed in
 * nanoseconds - a latency marker, not a phase (see acq_trigger_hw_read). */
uint8_t  acq_ets_active(void);
uint8_t  acq_base_timebase_get(void);
uint32_t acq_trigger_phase_ns(void);

/* Read a coherent, sequence-guarded copy of the last trigger-phase capture.
 * Returns 1 and fills *out when the capture is valid AND its arm generation
 * equals expected_generation; otherwise returns 0 with out->bound = 0 (the
 * other fields still describe the last capture, for diagnostics).  The
 * expected generation is struct acq_gate::arm_generation. */
uint8_t acq_trigger_hw_read(struct acq_trigger_hw *out,
                            uint32_t expected_generation);

/* Raw TIM3 count of the most recent hardware capture (diagnostic only).
 *
 * This is the same latency marker as acq_trigger_hw_read()->count, exposed as
 * a single aligned 16-bit read so the diagnostic snapshot can publish it
 * without a seqlock.  It is NOT a sub-sample phase; the hardware campaign uses
 * it to test the timing model (B4.27 prediction P1) - never feed it to the
 * ETS bin mapper. */
uint16_t acq_trigger_hw_count(void);


/* The sample buffer (ACQ_SAMPLES 12-bit counts, big-endian-in-16? - no:
 * plain little-endian uint16 right-aligned 0..4095) and its length. */
const uint16_t *acq_samples(void);
uint32_t        acq_sample_count(void);

/* Interrupt service glue - called from stm32f1xx_it.c.  Both keep the
 * IWDG untouched (SysTick owns the feed), never block, and keep ISR work
 * minimal: clear flags, latch the event, count for the liveness monitor. */
void acq_isr_dma(void);
void acq_isr_adc(void);

#endif /* ACQ_H */
