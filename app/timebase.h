/* Timebase table - pure logic with baked timer constants, host-testable.
 *
 * Range and provenance:
 *   - the vendor clamps its timebase to 0..26 (setter 0x08006270 stores
 *     PARAMS[8] after the clamp, analysis/m1_acquisition.md F-04);
 *   - the published DSO183 range is 1 us/div .. 500 s/div (JYE manual /
 *     specification), which is exactly 27 steps of the 1-2-5 sequence;
 *   - this module therefore defines the SAME range with the SAME index
 *     order: index 0 = 1 us/div ... index 26 = 500 s/div.
 *
 * Hardware limits every number below is derived from (still to be
 * bench-confirmed, open question U-02a/U-03b):
 *   - SYSCLK 64 MHz (main.c clock_config: HSI/2 x PLL16), APB1 = 32 MHz;
 *   - TIM3 is NOT clocked at APB1.  On STM32F1 the timer clock is
 *     2 x PCLK1 whenever the APB1 prescaler is not 1 (RM0008 clock tree),
 *     so TIM3 = 2 x 32 MHz = 64 MHz and the pacing tick is 15.625 ns per
 *     count.  (An earlier revision used 31.25 ns and was 2x fast.)
 *   - ADC clock = PCLK2/6 = 10.667 MHz (inside the 14 MHz F1 limitation,
 *     acq.h ACQ_ADC_PRESCALER), shortest conversion = 14 cycles
 *     (SMP=1.5 + 12.5) = 1.3125 us:
 *       * the FASTEST single-shot sample interval is 1312.5 ns
 *         (= 84 TIM3 counts at 64 MHz), i.e. ~762 kS/s;
 *       * entries whose tick is shorter need equivalent-time sampling
 *         (repetitive re-acquisition), which the acquisition engine does
 *         NOT implement yet - they are flagged single_shot == 0 and the
 *         engine must not try to pace sampling for them with TIM3.
 *       * single_shot also assumes the ADC is reprogrammed to SMP=1.5.
 *         acq.c currently uses 239.5 cycles (23.6 us/sample), which only
 *         supports single-shot from about 1 ms/div; M6 must add per-range
 *         sample-time programming before the faster single-shot entries
 *         are usable.
 *   - display window: 320 samples at 1 sample/px (320 px em dash width),
 *     10 divisions -> 32 px/div (TIMEBASE_SAMPLES_PER_DIV).  The 1024-sample
 *     acquisition buffer holds 3 full windows, which is the pre-trigger
 *     head-room the software trigger scans into.
 *
 * Sweep-time consequences (for the UI and the cycle timeout): one drawn
 * window takes window_time = div_us * 10; a full 1024-sample acquisition
 * takes 3.2 x that.  At 500 s/div a whole window is 5000 s - the caller's
 * trigger timeout handles that with the AUTO fallback, and the trigger FSM
 * keeps the display responsive (trig_fsm_take).
 */
#ifndef TIMEBASE_H
#define TIMEBASE_H

#include <stdint.h>

#define TIMEBASE_COUNT 24U  /* capped at 50 s/div (indices 0..23) */

/* First index whose step is physically single-shot capable (50 us/div).
 * Indices 0..4 need equivalent-time sampling, which the acquisition engine
 * does not implement; acq_set_timebase() rejects them rather than pacing
 * TIM3 faster than the ADC can convert. */
#define TIMEBASE_FIRST_SINGLE_SHOT 5U

/* Pacing constants (single source of truth for the table generation). */
#define TIMEBASE_APB1_HZ        32000000U              /* PCLK1 (APB1/2)    */
/* RM0008: with an APB1 prescaler != 1 the timer clock doubles. */
#define TIMEBASE_TIM3_HZ        (2U * TIMEBASE_APB1_HZ) /* 64 MHz           */
#define TIMEBASE_COUNTS_PER_US  (TIMEBASE_TIM3_HZ / 1000000U) /* 64        */
#define TIMEBASE_ADC_CLK_HZ     10666666U              /* PCLK2/6, floored  */
#define TIMEBASE_CONV_CYCLES    14U                    /* SMP 1.5 + 12.5   */
#define TIMEBASE_MIN_SS_COUNTS  84U  /* 1312.5 ns in 15.625 ns counts      */

/* ADC sample-time (SMP) policy.  RM0008's SMPR codes 0..7 select
 * 1.5/7.5/13.5/28.5/41.5/55.5/71.5/239.5 sampling cycles, and every
 * conversion also costs 12.5 cycles.  ADC clock = PCLK2/6 = 10.667 MHz and
 * TIM3 = 64 MHz, so ONE ADC clock is 6 TIM3 counts and the fit test is
 * exact in integers:
 *     3 * (smp_cycles_x2 + 25) <= TIM3 counts of one sample interval
 * (x2 because two codes need halves; 25 = 2 x 12.5; 3 = 6 / 2).
 *
 * The policy is "shortest aperture that still settles, never longer than
 * the interval allows": 7.5 cycles normally, 1.5 only on the fastest
 * single-shot range.  A long aperture (the old fixed 239.5 cycles = 22.5 us)
 * would low-pass the input: at 1 ms/div the interval is only 31.25 us. */
#define TIMEBASE_ADC_CYCLES_PER_TICK 6U   /* ADC clock = TIM3 / 6 */
#define TIMEBASE_SMP_BASELINE        1U   /* code 1 = 7.5 cycles  */

/* Half-cycles per SMP code: 3, 15, 27, 57, 83, 111, 143, 479. */
uint16_t timebase_smp_cycles_x2(uint8_t code);

/* The SMP code (0..7, ready for the SMPRx field) to use for a range.
 * Returns code 0 for REPT-only ranges and out-of-range indexes: those are
 * not paced by TIM3, so only the shortest aperture makes sense. */
uint8_t timebase_smp_code(uint8_t index);

/* Display geometry of the waveform window. */
#define TIMEBASE_PX_PER_DIV     32U
#define TIMEBASE_DIVS           10U
#define TIMEBASE_SAMPLES_PER_DIV TIMEBASE_PX_PER_DIV   /* 1 sample per px  */
#define TIMEBASE_WINDOW_SAMPLES (TIMEBASE_SAMPLES_PER_DIV * TIMEBASE_DIVS)

struct timebase_step {
    uint32_t div_us;      /* the settings value: 1 us .. 500 s, in us  */
    uint16_t tick_psc;    /* TIM3 PSC so tick = (psc+1)*(arr+1) counts */
    uint16_t tick_arr;    /* TIM3 ARR (same clockwise reference)       */
    uint8_t  single_shot; /* 1: tick fits the ADC conversion window so a
                           * paced single-shot acquisition can produce it;
                           * 0: equivalent-time / REPT regime required   */
};

/* index -> entry.  Returns NULL for an out-of-range index (and clamps are
 * the CALLER's decision - the setter semantics belong to the control flow,
 * not to the table). */
const struct timebase_step *timebase_entry(uint8_t index);

/* Time span of one drawn window (10 divisions), in us.  Saturates at
 * UINT32_MAX (the 500 s/div window is 5e9 us) - the caller that needs the
 * exact value uses 64-bit arithmetic consciously. */
uint32_t timebase_window_us(uint8_t index);

/* The pacing tick in ns, saturating (the two longest ticks exceed u32 ns). */
uint32_t timebase_tick_ns(uint8_t index);

/* Human-readable label WITHOUT the "/div" suffix, e.g. "2us", "50s". */
const char *timebase_label(uint8_t index);

#endif /* TIMEBASE_H */
