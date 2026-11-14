/* ETS sub-sample trigger phase estimator and exact timer-count mapping helpers.
 *
 * Stage B1/B3: host-testable interpolation and ETS bin-mapping arithmetic.
 * Live ETS integration (Stage B3) uses these helpers; this module does not
 * own acquisition/trigger state and introduces no buffers/storage/lifetimes.
 *
 * Representation (exact, no 15.625 ns -> 1562 ns rounding):
 *   - Hardware/sample intervals are expressed in TIM3 timer counts:
 *       hw_interval_counts = (PSC+1)*(ARR+1), 64 MHz TIM3 (15.625 ns/count).
 *   - Phase is returned as fixed-point timer counts in Q16:
 *       phase_q16 = phase_counts * 65536, normalized to [0, hw_interval_counts<<16).
 *     i.e. one timer count = 65536 Q16 units. Fractional precision preserved
 *     until the final bin decision.
 *
 * Crossing model (linear interpolation between adjacent samples y0=k-1, y1=k):
 *   f = (level - y0) / (y1 - y0) is the fractional position from y0 to y1.
 *   Crossing time relative to sample y1 is (f-1)*hw_interval_counts.
 *   Phase relative to current sample y1 (crossing earlier than y1 when >0) is:
 *       phase_counts = (1-f) * hw_interval_counts
 *   so sample y1 occurs at +phase_counts relative to the interpolated crossing.
 *   ETS mapping (crossing-aligned): t = (i-BENCH_PRE)*hw + phase.
 *
 * Validation (no fabricated phase): accepts only a pair that demonstrably
 * brackets the configured trigger level in the requested direction:
 *   Rising : y0 < level && y1 >= level
 *   Falling: y0 > level && y1 <= level
 * Rejects equal samples/invalid denominator, wrong-direction/non-bracketing
 * pairs, invalid/out-of-range inputs, and results outside the valid interval.
 * Boundary equality is explicit: y1 == level => phase_q16 == 0 (crossing at y1).
 * A pair with y0 == level is not accepted as a bracket for this sample.
 *
 * ETS bin mapping (exact timer counts, Q16, crossing-aligned):
 *   t_q16  = (i - BENCH_PRE) * (hw_interval_counts << 16) + phase_q16
 *   bin    = BENCH_PRE + floor_div(t_q16, ets_interval_counts << 16)
 * Signed division for negative offsets uses mathematical floor (toward -inf),
 * never C truncation-toward-zero.
 */
#ifndef ETS_PHASE_H
#define ETS_PHASE_H

#include <stdint.h>

enum ets_phase_edge {
    ETS_PHASE_RISING  = 0U,
    ETS_PHASE_FALLING = 1U
};

struct ets_phase_result {
    uint8_t  valid;      /* 1 = accepted estimate, 0 = rejected (phase_q16 undefined/0) */
    uint32_t phase_q16;  /* normalized phase in timer counts * 65536, [0, hw_interval_counts<<16) */
};

/* Estimate normalized fractional phase offset (Q16 timer counts) of a threshold
 * crossing between adjacent samples y0 (k-1) and y1 (k). Returns valid=0 and
 * phase_q16=0 when the pair does not pass explicit validation. */
struct ets_phase_result ets_phase_estimate(uint16_t y0, uint16_t y1,
                                           uint16_t level,
                                           enum ets_phase_edge edge,
                                           uint32_t hw_interval_counts);

/* Mathematical floor division for signed numerator and positive denominator.
 * Returns floor(n / d). d must be > 0. */
int64_t ets_phase_floor_div(int64_t n, int64_t d);

/* Compute ETS bin index for sample i with exact timer-count/Q16 mapping.
 * hw_interval_counts and ets_interval_counts are (PSC+1)*(ARR+1) timer counts.
 * phase_q16 is in [0, hw_interval_counts<<16).
 * Returns 1 and writes *out_bin when in [0, bench_window), else returns 0. */
uint8_t ets_phase_bin_index(uint16_t i, uint16_t bench_pre, uint16_t bench_window,
                            uint32_t hw_interval_counts,
                            uint32_t ets_interval_counts,
                            uint32_t phase_q16,
                            int32_t *out_bin);

#endif /* ETS_PHASE_H */
