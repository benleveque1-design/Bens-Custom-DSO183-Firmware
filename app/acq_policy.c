#include "acq_policy.h"

#include <stddef.h>

#include "timebase.h"

uint16_t acq_policy_clamp_trigger_level(uint16_t level)
{
    return (level > 4095U) ? 4095U : level;
}

uint8_t acq_policy_trigger_slope_is_valid(uint8_t slope)
{
    return (slope <= 2U) ? 1U : 0U;
}

uint8_t acq_policy_trigger_slope_hw(uint8_t slope)
{
    /* AUTO ("either edge") maps to rising for repeatable alignment. */
    return (slope == 1U) ? 1U : 0U;
}

uint8_t acq_policy_timebase_resolve(uint8_t logical_index, uint8_t *hw_index,
                                    uint8_t *is_ets)
{
    const struct timebase_step *step = timebase_entry(logical_index);

    if (step == NULL) {
        return 0U;
    }
    if (step->single_shot == 0U) {
        if (hw_index != NULL) {
            *hw_index = (uint8_t)TIMEBASE_FIRST_SINGLE_SHOT;
        }
        if (is_ets != NULL) {
            *is_ets = 1U;
        }
    } else {
        if (hw_index != NULL) {
            *hw_index = logical_index;
        }
        if (is_ets != NULL) {
            *is_ets = 0U;
        }
    }
    return 1U;
}

uint32_t acq_policy_arm_generation_bump(uint32_t generation)
{
    generation++;
    if (generation == 0U) {
        generation = 1U;      /* 0 is reserved for "never armed" */
    }
    return generation;
}

enum acq_status acq_policy_apply_trigger_mode(uint8_t inited,
                                              uint8_t requested_mode,
                                              struct trig_fsm *fsm,
                                              uint32_t *generation,
                                              uint8_t *invalidate)
{
    if (inited == 0U) {
        if (invalidate != NULL) {
            *invalidate = 0U;
        }
        return ACQ_NOT_INITED;
    }
    if (requested_mode > (uint8_t)TRIG_MODE_SINGLE) {
        if (invalidate != NULL) {
            *invalidate = 0U;
        }
        return ACQ_PARAM;
    }
    if (fsm != NULL) {
        fsm->mode = requested_mode;
        if (requested_mode == (uint8_t)TRIG_MODE_SINGLE) {
            fsm->armed = 1U;      /* entering SINGLE means "capture one" */
        }
    }
    if (generation != NULL) {
        *generation = acq_policy_arm_generation_bump(*generation);
    }
    if (invalidate != NULL) {
        *invalidate = 1U;
    }
    return ACQ_OK;
}

enum acq_status acq_policy_trigger_arm(uint8_t inited, struct trig_fsm *fsm,
                                       uint32_t *generation,
                                       uint8_t *invalidate)
{
    if (inited == 0U) {
        if (invalidate != NULL) {
            *invalidate = 0U;
        }
        return ACQ_NOT_INITED;
    }
    if (fsm != NULL) {
        trig_fsm_arm(fsm);
    }
    if (generation != NULL) {
        *generation = acq_policy_arm_generation_bump(*generation);
    }
    if (invalidate != NULL) {
        *invalidate = 1U;
    }
    return ACQ_OK;
}
