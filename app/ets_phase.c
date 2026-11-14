#include "ets_phase.h"

#include <stddef.h>

struct ets_phase_result ets_phase_estimate(uint16_t y0, uint16_t y1,
                                           uint16_t level,
                                           enum ets_phase_edge edge,
                                           uint32_t hw_interval_counts)
{
    struct ets_phase_result r = {0U, 0U};
    uint32_t denom;
    uint32_t numer; /* distance from y1 to crossing in ADC counts along bracket: (y1-level) or (level-y1) */
    uint64_t phase;

    if ((hw_interval_counts == 0U)
        || ((edge != ETS_PHASE_RISING) && (edge != ETS_PHASE_FALLING))) {
        return r;
    }

    if (edge == ETS_PHASE_RISING) {
        /* Accept only a demonstrable bracket of configured level in rising direction. */
        if (!((y0 < level) && (y1 >= level))) {
            return r;
        }
        denom = (uint32_t)y1 - (uint32_t)y0;
        numer = (uint32_t)y1 - (uint32_t)level; /* 0 when y1==level => phase 0 */
    } else {
        if (!((y0 > level) && (y1 <= level))) {
            return r;
        }
        denom = (uint32_t)y0 - (uint32_t)y1;
        numer = (uint32_t)level - (uint32_t)y1; /* 0 when y1==level => phase 0 */
    }

    if (denom == 0U) {
        return r;
    }

    /* phase_q16 = numer/denom * hw_interval_counts * 65536, rounded to nearest.
     * numer <= denom is guaranteed by valid bracket conditions above, so
     * phase <= hw_interval_counts<<16. */
    phase = (((uint64_t)numer * (uint64_t)hw_interval_counts) << 16)
            + ((uint64_t)denom / 2U);
    phase /= (uint64_t)denom;

    /* Normalize: phase == hw_interval_counts<<16 is outside [0, limit); map to 0
     * only for that exact boundary, otherwise reject. */
    {
        uint64_t limit = ((uint64_t)hw_interval_counts << 16);
        if (phase >= limit) {
            if (phase == limit) {
                phase = 0U;
            } else {
                return r; /* should not occur for a valid bracket */
            }
        }
    }

    r.valid = 1U;
    r.phase_q16 = (uint32_t)phase;
    return r;
}

int64_t ets_phase_floor_div(int64_t n, int64_t d)
{
    if (d <= 0) {
        return 0;
    }
    if (n >= 0) {
        return n / d;
    }
    /* Mathematical floor for negative numerator: -ceil(|n|/d). */
    return -(((-n) + d - 1) / d);
}

uint8_t ets_phase_bin_index(uint16_t i, uint16_t bench_pre, uint16_t bench_window,
                            uint32_t hw_interval_counts,
                            uint32_t ets_interval_counts,
                            uint32_t phase_q16,
                            int32_t *out_bin)
{
    int64_t hw_q16;
    int64_t ets_q16;
    int64_t t_q16;
    int64_t bin;

    if ((out_bin == NULL) || (ets_interval_counts == 0U) || (bench_window == 0U)) {
        return 0U;
    }

    hw_q16 = ((int64_t)hw_interval_counts << 16);
    ets_q16 = ((int64_t)ets_interval_counts << 16);
    t_q16 = ((int64_t)i - (int64_t)bench_pre) * hw_q16 + (int64_t)phase_q16;
    bin = (int64_t)bench_pre + ets_phase_floor_div(t_q16, ets_q16);

    if ((bin < 0) || (bin >= (int64_t)bench_window)) {
        return 0U;
    }

    *out_bin = (int32_t)bin;
    return 1U;
}
