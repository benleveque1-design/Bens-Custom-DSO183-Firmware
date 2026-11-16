#ifndef ACQ_POLICY_H
#define ACQ_POLICY_H

#include <stdint.h>

#include "acq.h"
#include "trigger.h"

/* Pure acquisition policy helpers shared by firmware and host tests.
 * They encode behaviour already established by app/acq.c without touching
 * hardware, timers, DMA, ADC registers, ISR state, or acq.c static storage.
 * Callers remain responsible for applying side effects in the existing order. */

uint16_t acq_policy_clamp_trigger_level(uint16_t level);
uint8_t acq_policy_trigger_slope_is_valid(uint8_t slope);
uint8_t acq_policy_trigger_slope_hw(uint8_t slope);

/* Resolve a logical timebase index.  Returns 1 when valid and sets outputs:
 * *hw_index is TIMEBASE_FIRST_SINGLE_SHOT for REPT (single_shot==0) entries,
 * otherwise the logical index; *is_ets is 1 for REPT entries, else 0.
 * Returns 0 for invalid indices (timebase_entry == NULL). */
uint8_t acq_policy_timebase_resolve(uint8_t logical_index, uint8_t *hw_index,
                                    uint8_t *is_ets);

uint32_t acq_policy_arm_generation_bump(uint32_t generation);

/* Apply trigger-mode bookkeeping to caller-supplied state.
 * On ACQ_OK: updates *fsm and *generation and sets *invalidate to 1.
 * On ACQ_NOT_INITED/ACQ_PARAM: leaves supplied state unchanged. */
enum acq_status acq_policy_apply_trigger_mode(uint8_t inited,
                                              uint8_t requested_mode,
                                              struct trig_fsm *fsm,
                                              uint32_t *generation,
                                              uint8_t *invalidate);

/* Apply trigger-arm bookkeeping to caller-supplied state.
 * On ACQ_OK: arms via trig_fsm_arm, bumps *generation and sets *invalidate.
 * On ACQ_NOT_INITED: leaves supplied state unchanged. */
enum acq_status acq_policy_trigger_arm(uint8_t inited, struct trig_fsm *fsm,
                                       uint32_t *generation,
                                       uint8_t *invalidate);

#endif
