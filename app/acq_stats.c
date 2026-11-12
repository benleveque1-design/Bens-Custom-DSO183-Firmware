#include <stddef.h>

#include "acq_stats.h"

uint16_t acq_offset_apply(uint16_t raw, int16_t zero_offset)
{
    int32_t v = (int32_t)raw - (int32_t)zero_offset;

    if (v < 0) {
        return 0U;
    }
    if (v > (int32_t)ACQ_STATS_FULL_SCALE) {
        return (uint16_t)ACQ_STATS_FULL_SCALE;
    }
    return (uint16_t)v;
}

void acq_stats_compute(const uint16_t *samples, uint32_t n,
                       int16_t zero_offset, struct acq_stats *out)
{
    uint32_t sum = 0U;
    uint32_t lo  = ACQ_STATS_FULL_SCALE;
    uint32_t hi  = 0U;
    uint32_t i;

    if (out == NULL) {
        return;                        /* caller bug: see header contract */
    }
    out->min_index = 0U;
    out->max_index = 0U;
    out->mean_q8   = 0U;
    out->clip_low   = 0U;
    out->clip_high  = 0U;
    out->smin       = 0U;
    out->smax       = 0U;

    if ((samples == NULL) || (n == 0U) || (n > ACQ_STATS_MAX_N)) {
        return;
    }

    for (i = 0U; i < n; i++) {
        uint16_t v = acq_offset_apply(samples[i], zero_offset);

        sum += v;
        if (v < lo) {
            lo = v;
            out->min_index = i;        /* FIRST occurrence of the minimum */
        }
        if (v > hi) {
            hi = v;
            out->max_index = i;        /* FIRST occurrence of the maximum */
        }
        if (v == 0U) {
            out->clip_low++;
        }
        if (v == ACQ_STATS_FULL_SCALE) {
            out->clip_high++;
        }
    }

    out->smin = (uint16_t)lo;
    out->smax = (uint16_t)hi;
    /* Maximum discriminant sum: ACQ_STATS_MAX_N * FULL_SCALE << 8 =
     * 1024 * 4095 << 8 = 2^30 - see the header for the contract; the shift
     * cannot overflow a uint32 with n <= ACQ_STATS_MAX_N. */
    out->mean_q8 = (sum << 8) / n;
}
