/* ETS trigger-phase source selection and hardware-capture binding - pure logic.
 *
 * B4.27.  The acquisition engine has exactly two things it can say about the
 * position of a captured sweep inside a sample interval:
 *
 *   1. The two samples that bracket the trigger level.  When the edge is
 *      resolved by the sampling aperture, `ets_phase_estimate()` turns that
 *      bracket into a sub-sample phase (this is "B1", the production
 *      estimator).  When the edge is much faster than one sample interval the
 *      bracket is a pair of plateaus and the interpolation degenerates to a
 *      value fixed by the trigger level - no sub-sample information at all.
 *
 *   2. The TIM3 count latched in the watchdog ISR.  B4.26 showed this to be
 *      `(conversion_complete + ISR_latency) mod interval`: a latency marker
 *      that is uncorrelated with the crossing offset.  It must never be used
 *      as a phase (see B4_27 TIMING_INVESTIGATION.md section 5).  It is used
 *      here only to prove that a hardware capture belongs to the sweep being
 *      accumulated (generation binding) and to report rollover.
 *
 * So the design is deliberately two-source with a bounded fallback:
 *
 *   - resolved edge / no bracket -> reject.
 *   - edge undersampled -> trigger-quantised placement at the MIDPOINT of
 *     the sample interval (phase = interval/2).  This is unbiased and its
 *     error is bounded by +/- half an interval; it makes no claim about
 *     where inside the interval the crossing really was.
 *   - otherwise -> B1.
 *
 * "Undersampled" is decided locally from the sweep itself: both bracket
 * samples sit within ETS_SOURCE_RAIL_MARGIN counts of the window's rails and
 * the bracket step covers at least half the window's span.  That is the
 * signature of a transition that completed inside one sample interval; on a
 * slow edge (sine, triangle, or a square with a rise comparable to the sample
 * interval) at least one bracket sample is interior and B1 is kept, which is
 * where B1 has real sub-sample resolution.
 *
 * No buffer, no state, no hardware access: every rule is host-testable.
 */
#ifndef ETS_SOURCE_H
#define ETS_SOURCE_H

#include <stdint.h>

#include "ets_phase.h"

/* Rail band, in ADC counts.  A bracket sample counts as "on the rail" when
 * it is within this many counts of the window's minimum/maximum.  Four
 * counts is comfortably outside the 1.5-count ADC noise (B4.24 plateau
 * scatter) and still far below a real intermediate crossing sample. */
#define ETS_SOURCE_RAIL_MARGIN  4U

/* A bracket step covering at least half the window span is a rail-to-rail
 * transition completed inside one interval. */
#define ETS_SOURCE_FULL_STEP_NUM 1U
#define ETS_SOURCE_FULL_STEP_DEN 2U

enum ets_phase_source {
    ETS_SOURCE_REJECTED  = 0U, /* no trustworthy contribution (see rules)    */
    ETS_SOURCE_B1        = 1U, /* two-sample interpolation (ets_phase)      */
    ETS_SOURCE_QUANTISED = 2U  /* trigger-quantised midpoint placement      */
};

struct ets_source_input {
    uint16_t y_prev;             /* sample PRE-1 (y0 of the bracket)      */
    uint16_t y_curr;             /* sample PRE   (y1 of the bracket)      */
    uint16_t win_min;            /* minimum over the sweep window         */
    uint16_t win_max;            /* maximum over the sweep window         */
    uint16_t level;              /* configured trigger level              */
    enum ets_phase_edge edge;    /* effective edge direction              */
    uint32_t hw_interval_counts; /* sample interval in TIM3 counts        */
    uint8_t  hw_bound;           /* capture belongs to this sweep         */
    uint8_t  refined;            /* software refinement found an edge     */
};

struct ets_source_result {
    uint8_t  source;      /* enum ets_phase_source                          */
    uint8_t  edge_resolved; /* 1 = B1 on a resolved edge, 0 otherwise       */
    uint32_t phase_q16;   /* valid only when source != ETS_SOURCE_REJECTED  */
};

/* Decide where one accepted sweep lands in the sample interval.
 *
 * Rejected when any of the following holds (no fabricated contribution):
 *   - in == NULL, or hw_interval_counts == 0;
 *   - !refined  : no software edge to associate the sweep with;
 *   - !hw_bound : the hardware capture cannot be bound to this sweep;
 *   - the bracket does not straddle `level` in `edge` direction.
 *
 * Otherwise B1 on a resolved edge, or the interval midpoint when the edge
 * is undersampled (see the module comment).  `phase_q16` is always inside
 * [0, hw_interval_counts << 16). */
struct ets_source_result ets_source_select(const struct ets_source_input *in);

/* Binding predicate for a hardware capture.  Returns 1 only when the capture
 * was marked valid and its arm generation equals the generation of the sweep
 * being accumulated.  A rearm, timebase change, trigger change, pause or
 * resume bumps the generation, so a stale capture can never be associated
 * with a later sweep. */
uint8_t ets_source_capture_bound(uint32_t expected_generation,
                                 uint32_t capture_generation,
                                 uint8_t capture_valid);

/* 1 when the captured count is smaller than the ADC conversion-complete
 * offset, i.e. the ISR read happened after the timer wrapped at least once.
 * Diagnostic only: the latency estimate assumes exactly one wrap, so this
 * flag says "the one-wrap assumption may not describe this capture". */
uint8_t ets_source_capture_rolled(uint32_t count,
                                  uint32_t conversion_complete_counts);

#endif /* ETS_SOURCE_H */
