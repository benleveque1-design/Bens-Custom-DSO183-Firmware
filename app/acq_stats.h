/* Sample statistics for the acquisition buffer - pure logic, host-testable.
 *
 * This module has no hardware knowledge at all: it operates on the 12-bit
 * counts that the DMA has already put into the sample buffer, which is what
 * the statistics code in the vendor firmware does as well (0x080038b9 reads
 * the sample table at 0x20000548).
 *
 * The zero offset is applied the same way the vendor applies its vertical
 * offset before the statistics: subtracted, then clamped to 0..4095 so that
 * clip events stay visible rather than wrapping around.  (See
 * ../analysis/m1_calibration.md F-01: the per-sensitivity offset is applied
 * as the vertical offset PARAMS[4], clamped to +/-500.)
 */
#ifndef ACQ_STATS_H
#define ACQ_STATS_H

#include <stdint.h>

#define ACQ_STATS_FULL_SCALE 4095U   /* 12-bit ADC full-scale count */
#define ACQ_STATS_MAX_N      1024U   /* guards the mean accumulation */

struct acq_stats {
    uint16_t smin;       /* lowest offset-corrected sample (0..4095) */
    uint16_t smax;       /* highest offset-corrected sample (0..4095) */
    uint32_t min_index;  /* index of the FIRST occurrence of smin */
    uint32_t max_index;  /* index of the FIRST occurrence of smax */
    uint32_t mean_q8;    /* mean << 8, i.e. mean = mean_q8 >> 8 */
    uint32_t clip_low;   /* how many samples hit the lower rail */
    uint32_t clip_high;  /* how many samples hit the upper rail */
};

/* Apply the zero offset with clamping at both rails.
 * A positive zero_offset shifts the sample down (like the vendor's
 * calibration offset subtraction); the clamp keeps clipped samples at the
 * rail instead of wrapping negative. */
uint16_t acq_offset_apply(uint16_t raw, int16_t zero_offset);

/* Compute the window statistics of `n` samples.
 *
 * Contract (host-tested in tests/test_acq_stats.c):
 *   - samples == NULL or n == 0 or n > ACQ_STATS_MAX_N: `out` is zeroed;
 *   - otherwise smin/smax/min_index/max_index/mean_q8/clip_* are filled;
 *   - min_index/max_index point at the FIRST occurrence.
 * `out` must not be NULL (a NULL out is a caller bug - fail loudly in
 * development by returning without touching anything). */
void acq_stats_compute(const uint16_t *samples, uint32_t n,
                       int16_t zero_offset, struct acq_stats *out);

#endif /* ACQ_STATS_H */
