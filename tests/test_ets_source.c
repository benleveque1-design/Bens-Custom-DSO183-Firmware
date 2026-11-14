/* Host tests for the B4.27 ETS trigger-phase source selector.
 *
 * These pin the rules that decide where one accepted sweep lands inside a
 * sample interval:
 *   - B1 (two-sample interpolation) when the aperture resolved the edge;
 *   - trigger-quantised midpoint when the edge is rail-to-rail inside one
 *     interval (no sub-sample information);
 *   - rejection when the sweep cannot be associated (no refinement, unbound
 *     capture) or the level is not bracketed.
 * They also pin the capture-binding and rollover predicates.  No hardware.
 */
#include "ets_source.h"
#include "ets_phase.h"

#include <stdio.h>
#include <stdint.h>

#define FAIL(msg) do { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); return 1; } while (0)
#define CHECK(cond) do { if (!(cond)) { FAIL(#cond); } } while (0)
#define CHECK_EQ(a, b) do { \
    unsigned long _a = (unsigned long)(a), _b = (unsigned long)(b); \
    if (_a != _b) { printf("FAIL %s:%d: %s != %s (%lu != %lu)\n", \
        __FILE__, __LINE__, #a, #b, _a, _b); return 1; } } while (0)

#define HW 100U

/* A resolved rising edge: the bracket is interior, the neighbour samples
 * move, and half the transition happens inside the aperture. */
static void resolved_rising(struct ets_source_input *in)
{
    in->y_prev  = 860U;
    in->y_curr  = 980U;
    in->win_min = 820U;
    in->win_max = 1020U;
    in->level   = 900U;
    in->edge    = ETS_PHASE_RISING;
    in->hw_interval_counts = HW;
    in->hw_bound = 1U;
    in->refined  = 1U;
}

/* A fast rising edge: both bracket samples on the rails, full step. */
static void undersampled_rising(struct ets_source_input *in)
{
    resolved_rising(in);
    in->y_prev  = 812U;   /* low rail  */
    in->y_curr  = 1016U;  /* high rail */
    in->win_min = 810U;
    in->win_max = 1017U;
}

static void undersampled_falling(struct ets_source_input *in)
{
    in->y_prev  = 1016U;
    in->y_curr  = 812U;
    in->win_min = 810U;
    in->win_max = 1017U;
    in->level   = 900U;
    in->edge    = ETS_PHASE_FALLING;
    in->hw_interval_counts = HW;
    in->hw_bound = 1U;
    in->refined  = 1U;
}

static int test_resolved_edge_uses_b1(void)
{
    struct ets_source_input in;
    struct ets_source_result r;
    struct ets_phase_result b1;

    resolved_rising(&in);
    r = ets_source_select(&in);
    /* The cross-check against the production estimator is the no-regression
     * contract: on a resolved edge the new selector must return exactly the
     * B1 value. */
    b1 = ets_phase_estimate(in.y_prev, in.y_curr, in.level, in.edge,
                            in.hw_interval_counts);
    CHECK(b1.valid == 1U);
    CHECK_EQ(r.source, ETS_SOURCE_B1);
    CHECK_EQ(r.edge_resolved, 1U);
    CHECK_EQ(r.phase_q16, b1.phase_q16);
    return 0;
}

static int test_resolved_falling_edge_uses_b1(void)
{
    struct ets_source_input in;
    struct ets_source_result r;

    resolved_rising(&in);
    in.edge = ETS_PHASE_FALLING;
    /* Falling bracket: prev above, curr below. */
    in.y_prev = 980U;
    in.y_curr = 860U;
    r = ets_source_select(&in);
    CHECK_EQ(r.source, ETS_SOURCE_B1);
    CHECK_EQ(r.edge_resolved, 1U);
    return 0;
}

static int test_undersampled_rising_is_quantised_midpoint(void)
{
    struct ets_source_input in;
    struct ets_source_result r;

    undersampled_rising(&in);
    r = ets_source_select(&in);
    CHECK_EQ(r.source, ETS_SOURCE_QUANTISED);
    CHECK_EQ(r.edge_resolved, 0U);
    /* Midpoint of [0, HW): exactly HW/2 counts in Q16. */
    CHECK_EQ(r.phase_q16, (HW / 2U) << 16);
    return 0;
}

static int test_undersampled_falling_is_quantised_midpoint(void)
{
    struct ets_source_input in;
    struct ets_source_result r;

    undersampled_falling(&in);
    r = ets_source_select(&in);
    CHECK_EQ(r.source, ETS_SOURCE_QUANTISED);
    CHECK_EQ(r.phase_q16, (HW / 2U) << 16);
    return 0;
}

static int test_quantised_phase_is_inside_interval(void)
{
    struct ets_source_input in;
    struct ets_source_result r;

    /* Odd interval: the midpoint is floored and must stay in range. */
    undersampled_rising(&in);
    in.hw_interval_counts = 101U;
    r = ets_source_select(&in);
    CHECK_EQ(r.source, ETS_SOURCE_QUANTISED);
    CHECK_EQ(r.phase_q16, (101U / 2U) << 16);
    CHECK(r.phase_q16 < (in.hw_interval_counts << 16));
    return 0;
}

static int test_unbound_capture_rejected(void)
{
    struct ets_source_input in;
    struct ets_source_result r;

    resolved_rising(&in);
    in.hw_bound = 0U;
    r = ets_source_select(&in);
    CHECK_EQ(r.source, ETS_SOURCE_REJECTED);
    return 0;
}

static int test_unrefined_sweep_rejected(void)
{
    struct ets_source_input in;
    struct ets_source_result r;

    resolved_rising(&in);
    in.refined = 0U;
    r = ets_source_select(&in);
    CHECK_EQ(r.source, ETS_SOURCE_REJECTED);
    return 0;
}

static int test_no_bracket_rejected(void)
{
    struct ets_source_input in;
    struct ets_source_result r;

    /* Level above both bracket samples: no rising crossing here. */
    resolved_rising(&in);
    in.level = 1100U;
    r = ets_source_select(&in);
    CHECK_EQ(r.source, ETS_SOURCE_REJECTED);

    /* Wrong direction: a rising selector must not accept a falling pair. */
    resolved_rising(&in);
    in.y_prev = 980U;
    in.y_curr = 860U;
    r = ets_source_select(&in);
    CHECK_EQ(r.source, ETS_SOURCE_REJECTED);
    return 0;
}

static int test_degenerate_inputs_rejected(void)
{
    struct ets_source_input in;
    struct ets_source_result r;

    r = ets_source_select(NULL);
    CHECK_EQ(r.source, ETS_SOURCE_REJECTED);

    resolved_rising(&in);
    in.hw_interval_counts = 0U;
    r = ets_source_select(&in);
    CHECK_EQ(r.source, ETS_SOURCE_REJECTED);
    return 0;
}

static int test_one_interior_bracket_keeps_b1(void)
{
    struct ets_source_input in;
    struct ets_source_result r;

    /* y_prev is on the low rail, y_curr is 50 counts below the high rail:
     * an interior crossing sample, so the aperture did resolve the edge. */
    undersampled_rising(&in);
    in.y_curr = 967U;      /* win_max - 50 */
    r = ets_source_select(&in);
    CHECK_EQ(r.source, ETS_SOURCE_B1);
    CHECK_EQ(r.edge_resolved, 1U);
    return 0;
}

static int test_slow_square_keeps_b1(void)
{
    struct ets_source_input in;
    struct ets_source_result r;

    /* Rise comparable to one sample interval: the bracket is interior even
     * though the window spans the full amplitude. */
    in.y_prev  = 905U;
    in.y_curr  = 1100U;
    in.win_min = 880U;
    in.win_max = 1120U;
    in.level   = 1000U;
    in.edge    = ETS_PHASE_RISING;
    in.hw_interval_counts = HW;
    in.hw_bound = 1U;
    in.refined  = 1U;
    r = ets_source_select(&in);
    CHECK_EQ(r.source, ETS_SOURCE_B1);
    return 0;
}

static int test_half_span_step_is_undersampled(void)
{
    struct ets_source_input in;
    struct ets_source_result r;

    /* The step condition only binds on a very small span (for a normal
     * span the rail test already implies a rail-to-rail step).  Use a
     * 12-count window so both branches are reachable and pin the >=
     * comparison: step 6 == span/2 takes the quantised path, step 4 does
     * not. */
    in.y_prev  = 803U;
    in.y_curr  = 809U;     /* step 6 == span/2, both on the 800..812 rails */
    in.win_min = 800U;
    in.win_max = 812U;
    in.level   = 806U;
    in.edge    = ETS_PHASE_RISING;
    in.hw_interval_counts = HW;
    in.hw_bound = 1U;
    in.refined  = 1U;

    r = ets_source_select(&in);
    CHECK_EQ(r.source, ETS_SOURCE_QUANTISED);

    in.y_prev = 804U;      /* step 4 < 6, still rail-to-rail bounds */
    in.level  = 806U;
    r = ets_source_select(&in);
    CHECK_EQ(r.source, ETS_SOURCE_B1);
    return 0;
}

static int test_rail_margin_boundary(void)
{
    struct ets_source_input in;
    struct ets_source_result r;

    /* Exactly ETS_SOURCE_RAIL_MARGIN counts above the minimum is still on
     * the rail; one count more is interior. */
    in.y_prev  = 800U + ETS_SOURCE_RAIL_MARGIN;
    in.y_curr  = 1196U;
    in.win_min = 800U;
    in.win_max = 1200U;
    in.level   = 900U;
    in.edge    = ETS_PHASE_RISING;
    in.hw_interval_counts = HW;
    in.hw_bound = 1U;
    in.refined  = 1U;

    r = ets_source_select(&in);
    CHECK_EQ(r.source, ETS_SOURCE_QUANTISED);

    in.y_prev = 800U + ETS_SOURCE_RAIL_MARGIN + 1U;
    r = ets_source_select(&in);
    CHECK_EQ(r.source, ETS_SOURCE_B1);
    return 0;
}

static int test_narrow_window_is_not_undersampled(void)
{
    struct ets_source_input in;
    struct ets_source_result r;

    /* A window whose span is within the rail band is noise-level: it must
     * not be treated as a completed edge, even with both samples on the
     * extremes. */
    in.y_prev  = 900U;
    in.y_curr  = 906U;
    in.win_min = 900U;
    in.win_max = 906U;     /* span 6 <= 2 * RAIL_MARGIN */
    in.level   = 903U;
    in.edge    = ETS_PHASE_RISING;
    in.hw_interval_counts = HW;
    in.hw_bound = 1U;
    in.refined  = 1U;

    r = ets_source_select(&in);
    CHECK_EQ(r.source, ETS_SOURCE_B1);
    return 0;
}

static int test_capture_binding(void)
{
    /* Matching generation and a valid capture binds. */
    CHECK_EQ(ets_source_capture_bound(7U, 7U, 1U), 1U);
    /* A generation mismatch (rearm, pause, timebase/trigger change) never
     * binds, even when the capture itself is marked valid. */
    CHECK_EQ(ets_source_capture_bound(8U, 7U, 1U), 0U);
    CHECK_EQ(ets_source_capture_bound(7U, 8U, 1U), 0U);
    /* An invalid capture never binds. */
    CHECK_EQ(ets_source_capture_bound(7U, 7U, 0U), 0U);
    return 0;
}

static int test_capture_rollover(void)
{
    /* Count below the conversion-complete offset: at least one wrap. */
    CHECK_EQ(ets_source_capture_rolled(0U, 84U), 1U);
    CHECK_EQ(ets_source_capture_rolled(83U, 84U), 1U);
    CHECK_EQ(ets_source_capture_rolled(84U, 84U), 0U);
    CHECK_EQ(ets_source_capture_rolled(99U, 84U), 0U);
    /* No configured offset: the predicate cannot claim a wrap. */
    CHECK_EQ(ets_source_capture_rolled(0U, 0U), 0U);
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_resolved_edge_uses_b1();
    failures += test_resolved_falling_edge_uses_b1();
    failures += test_undersampled_rising_is_quantised_midpoint();
    failures += test_undersampled_falling_is_quantised_midpoint();
    failures += test_quantised_phase_is_inside_interval();
    failures += test_unbound_capture_rejected();
    failures += test_unrefined_sweep_rejected();
    failures += test_no_bracket_rejected();
    failures += test_degenerate_inputs_rejected();
    failures += test_one_interior_bracket_keeps_b1();
    failures += test_slow_square_keeps_b1();
    failures += test_half_span_step_is_undersampled();
    failures += test_rail_margin_boundary();
    failures += test_narrow_window_is_not_undersampled();
    failures += test_capture_binding();
    failures += test_capture_rollover();

    if (failures == 0) {
        printf("ets_source: all tests passed\n");
    }
    return failures;
}
