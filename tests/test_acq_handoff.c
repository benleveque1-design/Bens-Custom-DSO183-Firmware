/* Host tests for the AWD rearm-to-enable handoff (B4.33).
 *
 * Models the temporal ordering of events at the arming boundary using the
 * pure predicates in app/acq_handoff.h.  The firmware acquisition engine
 * runs continuous ADC conversions; samples can change between the moment a
 * rearm condition is observed and the moment AWDIE is actually enabled.
 */
#include "acq_handoff.h"
#include "trigger.h"

#include <stdio.h>

#define FAIL(msg) do { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); return 1; } while (0)
#define CHECK(cond) do { if (!(cond)) { FAIL(#cond); } } while (0)

enum { HYST = ACQ_HANDOFF_HYST };

static int test_rising_rearm_safe_and_arms(void)
{
    /* Temporal: rearm side observed (low), signal still inside window at the
     * enable boundary -> safe to arm (intended upcoming crossing). */
    uint16_t level = 970U;
    uint16_t rearm_sample = 900U;   /* <= level-hyst (962) */
    uint16_t boundary     = 960U;   /* still <= level+hyst (978) */
    uint8_t  ready = acq_handoff_rearm_ready(rearm_sample, 0U, level, HYST);
    uint8_t  arm   = acq_handoff_should_arm(ready, boundary, 0U, level, HYST);

    CHECK(ready == 1U);
    CHECK(acq_handoff_arm_safe(boundary, 0U, level, HYST) == 1U);
    CHECK(arm == 1U);
    return 0;
}

static int test_rising_rearm_then_crosses_before_enable(void)
{
    /* H1: rearm observed, but signal crossed outside the safe window before
     * AWDIE enable (mid-plateau / past threshold).  Must NOT arm. */
    uint16_t level = 970U;
    uint16_t rearm_sample = 900U;   /* ready */
    uint16_t boundary     = 1050U;  /* > level+hyst (978): outside window */
    uint8_t  ready = acq_handoff_rearm_ready(rearm_sample, 0U, level, HYST);
    uint8_t  arm   = acq_handoff_should_arm(ready, boundary, 0U, level, HYST);

    CHECK(ready == 1U);
    CHECK(acq_handoff_arm_safe(boundary, 0U, level, HYST) == 0U);
    CHECK(arm == 0U);
    return 0;
}

static int test_rising_boundary_exact_limits(void)
{
    uint16_t level = 970U;
    uint16_t hi = acq_handoff_rising_hi(level, HYST); /* 978 */

    CHECK(acq_handoff_arm_safe(0U, 0U, level, HYST) == 1U);
    CHECK(acq_handoff_arm_safe(hi, 0U, level, HYST) == 1U);
    CHECK(acq_handoff_arm_safe((uint16_t)(hi + 1U), 0U, level, HYST) == 0U);
    CHECK(acq_handoff_arm_safe(4095U, 0U, level, HYST) == 0U);
    return 0;
}

static int test_falling_rearm_safe_and_arms(void)
{
    uint16_t level = 970U;
    uint16_t rearm_sample = 1100U;  /* >= level+hyst (978) */
    uint16_t boundary     = 1000U;  /* >= level-hyst (962): inside window */
    uint8_t  ready = acq_handoff_rearm_ready(rearm_sample, 1U, level, HYST);
    uint8_t  arm   = acq_handoff_should_arm(ready, boundary, 1U, level, HYST);

    CHECK(ready == 1U);
    CHECK(arm == 1U);
    return 0;
}

static int test_falling_rearm_then_crosses_before_enable(void)
{
    /* Symmetric to rising: rearm observed high, signal fell below window
     * before AWDIE enable.  Must NOT arm. */
    uint16_t level = 970U;
    uint16_t rearm_sample = 1100U;  /* ready */
    uint16_t boundary     = 850U;   /* < level-hyst (962): outside window */
    uint8_t  ready = acq_handoff_rearm_ready(rearm_sample, 1U, level, HYST);
    uint8_t  arm   = acq_handoff_should_arm(ready, boundary, 1U, level, HYST);

    CHECK(ready == 1U);
    CHECK(acq_handoff_arm_safe(boundary, 1U, level, HYST) == 0U);
    CHECK(arm == 0U);
    return 0;
}

static int test_falling_boundary_exact_limits(void)
{
    uint16_t level = 970U;
    uint16_t lo = acq_handoff_falling_lo(level, HYST); /* 962 */

    CHECK(acq_handoff_arm_safe(4095U, 1U, level, HYST) == 1U);
    CHECK(acq_handoff_arm_safe(lo, 1U, level, HYST) == 1U);
    CHECK(acq_handoff_arm_safe((uint16_t)(lo - 1U), 1U, level, HYST) == 0U);
    CHECK(acq_handoff_arm_safe(0U, 1U, level, HYST) == 0U);
    return 0;
}

static int test_rearm_timeout_path_safety(void)
{
    /* Rearm wait timed out (no ready observation).  Arming is still permitted
     * only when the boundary sample is inside the window; outside-window must
     * be rejected (bounded safe outcome: untriggered/timeout per mode). */
    uint16_t level = 970U;

    CHECK(acq_handoff_should_arm(0U, 970U, 0U, level, HYST) == 1U);
    CHECK(acq_handoff_should_arm(0U, 1050U, 0U, level, HYST) == 0U);
    CHECK(acq_handoff_should_arm(0U, 970U, 1U, level, HYST) == 1U);
    CHECK(acq_handoff_should_arm(0U, 850U, 1U, level, HYST) == 0U);
    return 0;
}

static int test_temporal_sequence_changes_during_handoff(void)
{
    /* Models ordered samples/conversion states seen over time:
     * t0: rearm observed (safe/low side)
     * t1: still safe inside window
     * t2: crossed outside window before enable
     * Decision at each possible enable boundary follows boundary sample, not
     * the stale t0 observation. */
    uint16_t level = 970U;
    uint16_t seq[] = { 900U, 960U, 990U, 1050U };
    uint8_t  ready = acq_handoff_rearm_ready(seq[0], 0U, level, HYST);

    CHECK(ready == 1U);
    CHECK(acq_handoff_should_arm(ready, seq[0], 0U, level, HYST) == 1U);
    CHECK(acq_handoff_should_arm(ready, seq[1], 0U, level, HYST) == 1U);
    /* 990 > 978: outside window even though rearm was seen earlier. */
    CHECK(acq_handoff_should_arm(ready, seq[2], 0U, level, HYST) == 0U);
    CHECK(acq_handoff_should_arm(ready, seq[3], 0U, level, HYST) == 0U);
    return 0;
}

static int test_enable_sequence_residual_toctou_model(void)
{
    /* Models status-clear/enable sequence stages:
     *  - check sample safe (decision input)
     *  - SR cleared, AWDIE about to be enabled
     *  - conversion completes after check but before/around enable
     * The predicate-based decision uses the checked boundary sample; if the
     * true sample changes to outside-window in that narrow gap, a residual
     * TOCTOU remains (documented limitation of continuous-conversion arming).
     * This test asserts the modeled ordering distinction explicitly. */
    uint16_t level = 970U;
    uint16_t checked  = 970U;  /* inside window at decision point */
    uint16_t at_enable = 1050U; /* changed during SR-clear/enable window */

    CHECK(acq_handoff_should_arm(1U, checked, 0U, level, HYST) == 1U);
    /* If implementation could observe at_enable, it must refuse to arm. */
    CHECK(acq_handoff_should_arm(1U, at_enable, 0U, level, HYST) == 0U);
    /* Residual gap exists when checked!=at_enable across the sequence. */
    CHECK(acq_handoff_arm_safe(checked, 0U, level, HYST) !=
          acq_handoff_arm_safe(at_enable, 0U, level, HYST));
    return 0;
}

static int test_rising_falling_symmetry(void)
{
    uint16_t level = 970U;
    uint16_t hyst = HYST;

    /* Symmetric mid-level safe for both directions. */
    CHECK(acq_handoff_arm_safe(level, 0U, level, hyst) == 1U);
    CHECK(acq_handoff_arm_safe(level, 1U, level, hyst) == 1U);
    /* Opposite extremes unsafe for the opposite direction's window edge. */
    CHECK(acq_handoff_arm_safe(4095U, 0U, level, hyst) == 0U);
    CHECK(acq_handoff_arm_safe(0U, 1U, level, hyst) == 0U);
    /* Rearm sides are opposite. */
    CHECK(acq_handoff_rearm_ready(0U, 0U, level, hyst) == 1U);
    CHECK(acq_handoff_rearm_ready(4095U, 1U, level, hyst) == 1U);
    CHECK(acq_handoff_rearm_ready(4095U, 0U, level, hyst) == 0U);
    CHECK(acq_handoff_rearm_ready(0U, 1U, level, hyst) == 0U);
    return 0;
}

static int test_hysteresis_saturation(void)
{
    /* Level near rails: bounds saturate without wrap. */
    CHECK(acq_handoff_rising_hi(4090U, HYST) == 4095U);
    CHECK(acq_handoff_rising_hi(4095U, HYST) == 4095U);
    CHECK(acq_handoff_falling_lo(5U, HYST) == 0U);
    CHECK(acq_handoff_falling_lo(0U, HYST) == 0U);
    CHECK(acq_handoff_arm_safe(4095U, 0U, 4090U, HYST) == 1U);
    CHECK(acq_handoff_arm_safe(0U, 1U, 5U, HYST) == 1U);
    return 0;
}

static int test_retry_abort_behavior_bounded_model(void)
{
    /* Models a bounded handoff policy over a finite sequence of boundary
     * samples (as acq_cycle would observe across attempts/cycles): each
     * attempt makes one arm/no-arm decision from its boundary sample and the
     * outcome is decided without unbounded looping.  Unsafe samples yield
     * no-arm; a safe sample yields arm. */
    uint16_t level = 970U;
    uint16_t samples[] = { 1050U, 1040U, 990U, 970U, 1055U };
    uint8_t  expected[] = { 0U, 0U, 0U, 1U, 0U };
    uint32_t attempts = 0U;
    uint32_t armed_at = 0xFFFFFFFFUL;

    for (uint32_t i = 0U; i < (uint32_t)(sizeof(samples)/sizeof(samples[0])); i++) {
        uint8_t arm = acq_handoff_should_arm(1U, samples[i], 0U, level, HYST);
        attempts++;
        CHECK(arm == expected[i]);
        if ((arm != 0U) && (armed_at == 0xFFFFFFFFUL)) {
            armed_at = i;
        }
    }
    CHECK(attempts == 5U); /* bounded: exactly one decision per sample */
    CHECK(armed_at == 3U);
    return 0;
}

static int test_mode_policy_regressions(void)
{
    /* AUTO/NORM/SINGLE behavior remains correct (trig_fsm, existing contract). */
    struct trig_fsm fsm;

    trig_fsm_init(&fsm, TRIG_MODE_AUTO);
    CHECK(trig_fsm_action(&fsm, 1U, 0U) == TRIG_ACTION_CAPTURE);
    CHECK(trig_fsm_action(&fsm, 0U, 1U) == TRIG_ACTION_FALLBACK);
    CHECK(trig_fsm_action(&fsm, 0U, 0U) == TRIG_ACTION_HOLD);

    trig_fsm_init(&fsm, TRIG_MODE_NORM);
    CHECK(trig_fsm_action(&fsm, 1U, 0U) == TRIG_ACTION_CAPTURE);
    CHECK(trig_fsm_action(&fsm, 0U, 1U) == TRIG_ACTION_HOLD);
    CHECK(trig_fsm_action(&fsm, 0U, 0U) == TRIG_ACTION_HOLD);

    trig_fsm_init(&fsm, TRIG_MODE_SINGLE);
    CHECK(fsm.armed == 1U);
    CHECK(trig_fsm_action(&fsm, 0U, 1U) == TRIG_ACTION_HOLD);
    CHECK(fsm.armed == 1U);
    CHECK(trig_fsm_action(&fsm, 1U, 0U) == TRIG_ACTION_CAPTURE);
    CHECK(fsm.armed == 0U);
    CHECK(trig_fsm_action(&fsm, 1U, 0U) == TRIG_ACTION_HOLD);
    trig_fsm_arm(&fsm);
    CHECK(fsm.armed == 1U);
    CHECK(trig_fsm_action(&fsm, 1U, 0U) == TRIG_ACTION_CAPTURE);
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_rising_rearm_safe_and_arms();
    failures += test_rising_rearm_then_crosses_before_enable();
    failures += test_rising_boundary_exact_limits();
    failures += test_falling_rearm_safe_and_arms();
    failures += test_falling_rearm_then_crosses_before_enable();
    failures += test_falling_boundary_exact_limits();
    failures += test_rearm_timeout_path_safety();
    failures += test_temporal_sequence_changes_during_handoff();
    failures += test_enable_sequence_residual_toctou_model();
    failures += test_rising_falling_symmetry();
    failures += test_hysteresis_saturation();
    failures += test_retry_abort_behavior_bounded_model();
    failures += test_mode_policy_regressions();

    if (failures == 0) {
        printf("acq_handoff: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
