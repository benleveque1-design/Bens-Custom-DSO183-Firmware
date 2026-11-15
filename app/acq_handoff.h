/* Pure, host-testable helpers for the AWD arming handoff (B4.33).
 *
 * These predicates model the one-sided analog-watchdog window used by the
 * acquisition engine.  They have no hardware or HAL dependencies so host
 * tests can exercise the same decision logic the firmware applies at the
 * rearm-to-AWDIE boundary.
 */
#ifndef ACQ_HANDOFF_H
#define ACQ_HANDOFF_H

#include <stdint.h>

/* Hysteresis in ADC counts (mirrors ACQ_TRIGGER_HYST in acq.h). */
#define ACQ_HANDOFF_HYST 8U

/* Rising band is [0, level + hyst] (inclusive of boundary values that keep
 * the sample inside the watchdog window).  A sample strictly above the upper
 * bound is outside the window and must not be armed against. */
static inline uint16_t acq_handoff_rising_hi(uint16_t level, uint16_t hyst)
{
    uint32_t hi = (uint32_t)level + (uint32_t)hyst;
    return (hi > 4095U) ? 4095U : (uint16_t)hi;
}

/* Falling band is [level - hyst, 4095]. */
static inline uint16_t acq_handoff_falling_lo(uint16_t level, uint16_t hyst)
{
    return (level > hyst) ? (uint16_t)(level - hyst) : 0U;
}

/* Rearm condition for rising: sample on the far/low side (<= level-hyst). */
static inline uint8_t acq_handoff_rearm_ready(uint16_t sample, uint8_t falling,
                                              uint16_t level, uint16_t hyst)
{
    if (falling != 0U) {
        uint16_t hi = acq_handoff_rising_hi(level, hyst);
        return (sample >= hi) ? 1U : 0U;
    }
    return (sample <= acq_handoff_falling_lo(level, hyst)) ? 1U : 0U;
}

/* Safe-to-arm condition: sample currently inside the AWD window so enabling
 * AWDIE cannot immediately latch on this sample's side of the threshold. */
static inline uint8_t acq_handoff_arm_safe(uint16_t sample, uint8_t falling,
                                           uint16_t level, uint16_t hyst)
{
    if (falling != 0U) {
        return (sample >= acq_handoff_falling_lo(level, hyst)) ? 1U : 0U;
    }
    return (sample <= acq_handoff_rising_hi(level, hyst)) ? 1U : 0U;
}

/* Modeled arming decision: given whether a rearm-side sample was observed and
 * the sample present at the actual AWDIE-enable boundary, return 1 if AWDIE
 * may be enabled safely at that boundary, 0 if arming must be skipped.
 * Rearm readiness is necessary to seek a crossing but never sufficient for
 * safety when the boundary sample has left the window (TOCTOU). */
static inline uint8_t acq_handoff_should_arm(uint8_t rearm_ready,
                                              uint16_t boundary_sample,
                                              uint8_t falling,
                                              uint16_t level, uint16_t hyst)
{
    (void)rearm_ready;
    return acq_handoff_arm_safe(boundary_sample, falling, level, hyst);
}

#endif /* ACQ_HANDOFF_H */
