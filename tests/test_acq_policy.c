/* Host unit tests for extracted acquisition policy helpers (app/acq_policy.c).
 *
 * Covers only deterministic policy/validation/mapping/bookkeeping behaviour
 * established by the current implementation.  No ADC/DMA/TIM3/ISR/HAL,
 * no hardware acquisition/timing simulation, and no acq.c static-state hooks. */
#include "acq_policy.h"
#include "timebase.h"
#include "trigger.h"

#include <stdio.h>

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static void test_trigger_level_clamp(void)
{
    CHECK(acq_policy_clamp_trigger_level(0U) == 0U);
    CHECK(acq_policy_clamp_trigger_level(1U) == 1U);
    CHECK(acq_policy_clamp_trigger_level(2048U) == 2048U);
    CHECK(acq_policy_clamp_trigger_level(4094U) == 4094U);
    CHECK(acq_policy_clamp_trigger_level(4095U) == 4095U);
    CHECK(acq_policy_clamp_trigger_level(4096U) == 4095U);
    CHECK(acq_policy_clamp_trigger_level(5000U) == 4095U);
    CHECK(acq_policy_clamp_trigger_level(0xFFFFU) == 4095U);
}

static void test_trigger_slope_validation_and_normalization(void)
{
    /* Valid parameter range is 0..2 (AUTO/NORM-style slope selector in acq). */
    CHECK(acq_policy_trigger_slope_is_valid(0U) == 1U);
    CHECK(acq_policy_trigger_slope_is_valid(1U) == 1U);
    CHECK(acq_policy_trigger_slope_is_valid(2U) == 1U);
    CHECK(acq_policy_trigger_slope_is_valid(3U) == 0U);
    CHECK(acq_policy_trigger_slope_is_valid(255U) == 0U);

    /* AUTO (2) maps to rising (0) for repeatable alignment; falling stays 1. */
    CHECK(acq_policy_trigger_slope_hw(0U) == 0U);
    CHECK(acq_policy_trigger_slope_hw(1U) == 1U);
    CHECK(acq_policy_trigger_slope_hw(2U) == 0U);
}

static void test_timebase_resolve_all_entries_and_invalid(void)
{
    uint8_t hw;
    uint8_t ets;
    uint8_t i;

    for (i = 0U; i < TIMEBASE_COUNT; i++) {
        const struct timebase_step *st = timebase_entry(i);

        hw = 0xAAU;
        ets = 0xAAU;
        CHECK(st != NULL);
        CHECK(acq_policy_timebase_resolve(i, &hw, &ets) == 1U);

        if (st->single_shot == 0U) {
            /* REPT/ETS ranges pace hardware at the fastest single-shot rate. */
            CHECK(hw == (uint8_t)TIMEBASE_FIRST_SINGLE_SHOT);
            CHECK(ets == 1U);
        } else {
            CHECK(hw == i);
            CHECK(ets == 0U);
        }
    }

    /* Boundary/invalid indices. */
    CHECK(acq_policy_timebase_resolve(TIMEBASE_COUNT, &hw, &ets) == 0U);
    CHECK(acq_policy_timebase_resolve(255U, &hw, &ets) == 0U);
}

static void test_arm_generation_bump(void)
{
    CHECK(acq_policy_arm_generation_bump(1U) == 2U);
    CHECK(acq_policy_arm_generation_bump(41U) == 42U);
    /* 0 is reserved for "never armed": wrap from max to 1. */
    CHECK(acq_policy_arm_generation_bump(0xFFFFFFFFU) == 1U);
}

static void test_apply_trigger_mode_guards_and_bookkeeping(void)
{
    struct trig_fsm fsm;
    uint32_t gen;
    uint8_t inv;
    enum acq_status st;

    /* Not initialised: no state changes. */
    fsm.mode = (uint8_t)TRIG_MODE_AUTO;
    fsm.armed = 0U;
    gen = 10U;
    inv = 1U;
    st = acq_policy_apply_trigger_mode(0U, (uint8_t)TRIG_MODE_NORM, &fsm, &gen, &inv);
    CHECK(st == ACQ_NOT_INITED);
    CHECK(fsm.mode == (uint8_t)TRIG_MODE_AUTO);
    CHECK(fsm.armed == 0U);
    CHECK(gen == 10U);
    CHECK(inv == 0U);

    /* Invalid mode: no state changes. */
    fsm.mode = (uint8_t)TRIG_MODE_AUTO;
    fsm.armed = 1U;
    gen = 10U;
    inv = 1U;
    st = acq_policy_apply_trigger_mode(1U, 3U, &fsm, &gen, &inv);
    CHECK(st == ACQ_PARAM);
    CHECK(fsm.mode == (uint8_t)TRIG_MODE_AUTO);
    CHECK(fsm.armed == 1U);
    CHECK(gen == 10U);
    CHECK(inv == 0U);

    /* AUTO: accepted, mode set, armed preserved, generation bumped, invalidated. */
    fsm.mode = (uint8_t)TRIG_MODE_SINGLE;
    fsm.armed = 0U;
    gen = 10U;
    inv = 0U;
    st = acq_policy_apply_trigger_mode(1U, (uint8_t)TRIG_MODE_AUTO, &fsm, &gen, &inv);
    CHECK(st == ACQ_OK);
    CHECK(fsm.mode == (uint8_t)TRIG_MODE_AUTO);
    CHECK(fsm.armed == 0U); /* not modified for AUTO */
    CHECK(gen == 11U);
    CHECK(inv == 1U);

    /* NORM: accepted, armed preserved. */
    fsm.mode = (uint8_t)TRIG_MODE_AUTO;
    fsm.armed = 1U;
    gen = 11U;
    inv = 0U;
    st = acq_policy_apply_trigger_mode(1U, (uint8_t)TRIG_MODE_NORM, &fsm, &gen, &inv);
    CHECK(st == ACQ_OK);
    CHECK(fsm.mode == (uint8_t)TRIG_MODE_NORM);
    CHECK(fsm.armed == 1U); /* not modified for NORM */
    CHECK(gen == 12U);
    CHECK(inv == 1U);

    /* SINGLE: accepted, entering SINGLE arms capture. */
    fsm.mode = (uint8_t)TRIG_MODE_AUTO;
    fsm.armed = 0U;
    gen = 12U;
    inv = 0U;
    st = acq_policy_apply_trigger_mode(1U, (uint8_t)TRIG_MODE_SINGLE, &fsm, &gen, &inv);
    CHECK(st == ACQ_OK);
    CHECK(fsm.mode == (uint8_t)TRIG_MODE_SINGLE);
    CHECK(fsm.armed == 1U);
    CHECK(gen == 13U);
    CHECK(inv == 1U);

    /* Generation wrap on accepted call. */
    fsm.mode = (uint8_t)TRIG_MODE_AUTO;
    fsm.armed = 0U;
    gen = 0xFFFFFFFFU;
    inv = 0U;
    st = acq_policy_apply_trigger_mode(1U, (uint8_t)TRIG_MODE_NORM, &fsm, &gen, &inv);
    CHECK(st == ACQ_OK);
    CHECK(gen == 1U);
    CHECK(inv == 1U);
}

static void test_trigger_arm_bookkeeping(void)
{
    struct trig_fsm fsm;
    uint32_t gen;
    uint8_t inv;
    enum acq_status st;

    /* Not initialised: no state changes. */
    fsm.mode = (uint8_t)TRIG_MODE_SINGLE;
    fsm.armed = 0U;
    gen = 5U;
    inv = 1U;
    st = acq_policy_trigger_arm(0U, &fsm, &gen, &inv);
    CHECK(st == ACQ_NOT_INITED);
    CHECK(fsm.armed == 0U);
    CHECK(gen == 5U);
    CHECK(inv == 0U);

    /* SINGLE: trig_fsm_arm arms; generation bumped and invalidated. */
    fsm.mode = (uint8_t)TRIG_MODE_SINGLE;
    fsm.armed = 0U;
    gen = 5U;
    inv = 0U;
    st = acq_policy_trigger_arm(1U, &fsm, &gen, &inv);
    CHECK(st == ACQ_OK);
    CHECK(fsm.armed == 1U);
    CHECK(gen == 6U);
    CHECK(inv == 1U);

    /* AUTO/NORM: trig_fsm_arm sets armed=1 (harmless per trig_fsm contract). */
    fsm.mode = (uint8_t)TRIG_MODE_AUTO;
    fsm.armed = 0U;
    gen = 6U;
    inv = 0U;
    st = acq_policy_trigger_arm(1U, &fsm, &gen, &inv);
    CHECK(st == ACQ_OK);
    CHECK(fsm.armed == 1U);
    CHECK(gen == 7U);
    CHECK(inv == 1U);

    fsm.mode = (uint8_t)TRIG_MODE_NORM;
    fsm.armed = 0U;
    gen = 7U;
    inv = 0U;
    st = acq_policy_trigger_arm(1U, &fsm, &gen, &inv);
    CHECK(st == ACQ_OK);
    CHECK(fsm.armed == 1U);
    CHECK(gen == 8U);
    CHECK(inv == 1U);
}

int main(void)
{
    test_trigger_level_clamp();
    test_trigger_slope_validation_and_normalization();
    test_timebase_resolve_all_entries_and_invalid();
    test_arm_generation_bump();
    test_apply_trigger_mode_guards_and_bookkeeping();
    test_trigger_arm_bookkeeping();

    if (failures != 0) {
        printf("test_acq_policy: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_acq_policy: all tests passed\n");
    return 0;
}
