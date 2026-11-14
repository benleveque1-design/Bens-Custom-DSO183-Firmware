#include "ets_source.h"

#include <stddef.h>

/* 1 when the bracket {y_prev, y_curr} is a rail-to-rail transition that
 * completed inside one sample interval.  Requires both samples on the
 * window rails AND a step covering at least half the window span, so a
 * slow edge (sine/triangle/wide square) keeps its B1 interpolation. */
static uint8_t edge_undersampled(const struct ets_source_input *in)
{
    uint32_t span;
    uint32_t lo;
    uint32_t hi;
    uint32_t step;

    if (in->win_max <= in->win_min) {
        return 0U;                 /* flat window: nothing to resolve    */
    }
    span = (uint32_t)in->win_max - (uint32_t)in->win_min;
    if (span <= (2U * ETS_SOURCE_RAIL_MARGIN)) {
        return 0U;                 /* too narrow to call anything a rail */
    }

    lo = (in->y_prev < in->y_curr) ? in->y_prev : in->y_curr;
    hi = (in->y_prev > in->y_curr) ? in->y_prev : in->y_curr;

    /* Both bracket samples must be within the rail band, otherwise one of
     * them is an interior point and carries genuine sub-sample shape. */
    if ((lo > ((uint32_t)in->win_min + ETS_SOURCE_RAIL_MARGIN))
        || (hi < ((uint32_t)in->win_max - ETS_SOURCE_RAIL_MARGIN))) {
        return 0U;
    }

    step = hi - lo;
    return ((step * ETS_SOURCE_FULL_STEP_DEN)
            >= (span * ETS_SOURCE_FULL_STEP_NUM)) ? 1U : 0U;
}

struct ets_source_result ets_source_select(const struct ets_source_input *in)
{
    struct ets_source_result r = { ETS_SOURCE_REJECTED, 0U, 0U };
    struct ets_phase_result b1;

    if ((in == NULL) || (in->hw_interval_counts == 0U)) {
        return r;
    }
    /* Association first: a sweep without a refined edge, or whose hardware
     * capture cannot be bound to it, has no trustworthy placement at all. */
    if ((in->refined == 0U) || (in->hw_bound == 0U)) {
        return r;
    }
    /* No bracket means the level was not crossed between these samples in
     * the requested direction: the sweep is not aligned to an edge. */
    b1 = ets_phase_estimate(in->y_prev, in->y_curr, in->level, in->edge,
                            in->hw_interval_counts);
    if (b1.valid == 0U) {
        return r;
    }

    if (edge_undersampled(in) == 0U) {
        r.source        = ETS_SOURCE_B1;
        r.edge_resolved = 1U;
        r.phase_q16     = b1.phase_q16;
        return r;
    }

    /* Undersampled edge: place the sweep at the middle of the sample
     * interval.  Unbiased, bounded by +/- half an interval, and explicitly
     * not claimed to be the crossing offset.  The shift is applied before
     * the multiply so the result cannot overflow 32 bits. */
    r.source        = ETS_SOURCE_QUANTISED;
    r.edge_resolved = 0U;
    r.phase_q16     = (in->hw_interval_counts / 2U) << 16;
    return r;
}

uint8_t ets_source_capture_bound(uint32_t expected_generation,
                                 uint32_t capture_generation,
                                 uint8_t capture_valid)
{
    if (capture_valid == 0U) {
        return 0U;
    }
    return (expected_generation == capture_generation) ? 1U : 0U;
}

uint8_t ets_source_capture_rolled(uint32_t count,
                                  uint32_t conversion_complete_counts)
{
    if (conversion_complete_counts == 0U) {
        return 0U;
    }
    return (count < conversion_complete_counts) ? 1U : 0U;
}
