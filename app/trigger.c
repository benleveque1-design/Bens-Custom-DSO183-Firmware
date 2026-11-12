#include "trigger.h"

#include <stddef.h>

static uint16_t trig_cross(uint16_t level, uint16_t hyst)
{
    uint32_t hi = (uint32_t)level + (uint32_t)hyst;

    return (hi > TRIG_FULL_SCALE) ? (uint16_t)TRIG_FULL_SCALE : (uint16_t)hi;
}

static uint16_t trig_rearm(uint16_t level, uint16_t hyst)
{
    uint32_t lo = (uint32_t)level - (uint32_t)hyst;

    return (level < hyst) ? 0U : (uint16_t)lo;
}

/* One bounded pass, two states:
 *   0 = WAIT_REARM - looking for a sample on the far side of the band
 *                    (at or below level - hyst for rising).
 *   1 = ARMED      - the next sample that crosses back through the band
 *                    (at or beyond level + hyst for rising) fires.
 * The rearm history lives in the state machine, so oscillating mid-band
 * noise can neither fire the trigger nor re-arm it: exactly the property
 * the analog watchdog lacks and the reason this scan exists. */
int32_t trig_scan(const uint16_t *samples, uint32_t n,
                  const struct trig_cfg *cfg)
{
    uint16_t cross_lvl, rearm_lvl;
    uint8_t armed = 0U;
    uint32_t i;

    if ((samples == NULL) || (cfg == NULL) || (n < 2U)) {
        return -1;
    }
    if (cfg->slope > TRIG_FALLING) {
        return -1;                          /* caller bug: loud, not silent */
    }

    if (cfg->slope == TRIG_RISING) {
        cross_lvl = trig_cross(cfg->level, cfg->hyst_counts);
        rearm_lvl = trig_rearm(cfg->level, cfg->hyst_counts);
    } else {
        /* Falling edge: the same band with the roles mirrored. */
        cross_lvl = trig_rearm(cfg->level, cfg->hyst_counts);
        rearm_lvl = trig_cross(cfg->level, cfg->hyst_counts);
    }

    for (i = 0U; i < n; i++) {
        uint16_t cur = samples[i];

        if (armed != 0U) {
            if (cfg->slope == TRIG_RISING) {
                if (cur >= cross_lvl) {
                    return (int32_t)i;
                }
            } else {
                if (cur <= cross_lvl) {
                    return (int32_t)i;
                }
            }
        } else {
            if (cfg->slope == TRIG_RISING) {
                if (cur <= rearm_lvl) {
                    armed = 1U;
                }
            } else {
                if (cur >= rearm_lvl) {
                    armed = 1U;
                }
            }
        }
    }
    return -1;
}

/* Same hysteresis state machine as trig_scan(), but every crossing in the
 * buffer is a candidate and the one closest to `center` wins.  The display
 * uses this to snap the software edge onto the hardware watchdog event: a
 * 176-sample window can hold several periods of a fast signal, and the
 * first crossing in the window can be many periods away from the event the
 * hardware actually latched.  Ties keep the earlier crossing (the "<="
 * keeps the first of two equidistant candidates). */
int32_t trig_scan_nearest(const uint16_t *samples, uint32_t n,
                          uint32_t center, const struct trig_cfg *cfg)
{
    uint16_t cross_lvl, rearm_lvl;
    uint8_t armed = 0U;
    uint32_t i;
    uint32_t best = 0U;
    uint32_t best_dist = 0xFFFFFFFFUL;
    int32_t  found = -1;

    if ((samples == NULL) || (cfg == NULL) || (n < 2U)) {
        return -1;
    }
    if (cfg->slope > TRIG_FALLING) {
        return -1;                          /* caller bug: loud, not silent */
    }

    if (cfg->slope == TRIG_RISING) {
        cross_lvl = trig_cross(cfg->level, cfg->hyst_counts);
        rearm_lvl = trig_rearm(cfg->level, cfg->hyst_counts);
    } else {
        cross_lvl = trig_rearm(cfg->level, cfg->hyst_counts);
        rearm_lvl = trig_cross(cfg->level, cfg->hyst_counts);
    }

    for (i = 0U; i < n; i++) {
        uint16_t cur = samples[i];
        uint8_t  fired;

        if (armed != 0U) {
            fired = (cfg->slope == TRIG_RISING) ? ((cur >= cross_lvl) ? 1U : 0U)
                                                : ((cur <= cross_lvl) ? 1U : 0U);
        } else {
            fired = 0U;
            if (cfg->slope == TRIG_RISING) {
                if (cur <= rearm_lvl) {
                    armed = 1U;
                }
            } else {
                if (cur >= rearm_lvl) {
                    armed = 1U;
                }
            }
        }
        if (fired != 0U) {
            uint32_t dist = (i >= center) ? (i - center) : (center - i);

            if (dist < best_dist) {
                best_dist = dist;
                best = i;
                found = (int32_t)i;
            }
            /* A trigger consumes the arm state; the next crossing needs a
             * fresh rearm sample, exactly as in trig_scan(). */
            armed = 0U;
        }
    }
    return (found < 0) ? -1 : (int32_t)best;
}

void trig_fsm_init(struct trig_fsm *fsm, uint8_t mode)
{
    if (fsm == NULL) {
        return;
    }
    fsm->mode  = (mode <= TRIG_MODE_SINGLE) ? mode : (uint8_t)TRIG_MODE_AUTO;
    fsm->armed = 1U;
}

void trig_fsm_arm(struct trig_fsm *fsm)
{
    if (fsm == NULL) {
        return;
    }
    fsm->armed = 1U;
}

enum trig_action trig_fsm_action(struct trig_fsm *fsm, uint8_t found,
                                 uint8_t timed_out)
{
    if (fsm == NULL) {
        return TRIG_ACTION_HOLD;
    }
    switch (fsm->mode) {
    case TRIG_MODE_NORM:
        return (found != 0U) ? TRIG_ACTION_CAPTURE : TRIG_ACTION_HOLD;
    case TRIG_MODE_SINGLE:
        if ((found != 0U) && (fsm->armed != 0U)) {
            fsm->armed = 0U;                /* the ONE capture happened */
            return TRIG_ACTION_CAPTURE;
        }
        return TRIG_ACTION_HOLD;
    case TRIG_MODE_AUTO:
    default:
        if (found != 0U) {
            return TRIG_ACTION_CAPTURE;
        }
        return (timed_out != 0U) ? TRIG_ACTION_FALLBACK : TRIG_ACTION_HOLD;
    }
}

uint8_t trig_fsm_take(struct trig_fsm *fsm, uint8_t found)
{
    enum trig_action action = trig_fsm_action(fsm, found,
                                              (found != 0U) ? 0U : 1U);

    return (action == TRIG_ACTION_HOLD) ? 0U : 1U;
}

uint32_t trig_window_start(int32_t trig_index, uint32_t window_len,
                            uint32_t buffer_len)
{
    uint32_t pre = TRIG_PRE_TRIGGER;
    int32_t  start;

    if ((trig_index < 0) || (window_len == 0U) ||
        (buffer_len < window_len) || (pre >= window_len)) {
        return 0U;      /* no trigger or non-sensical window: start at 0 */
    }
    start = trig_index - (int32_t)pre;
    if (start < 0) {
        return 0U;
    }
    /* Never hand back a window that would run past the buffer end. */
    if ((uint32_t)start > (buffer_len - window_len)) {
        start = (int32_t)(buffer_len - window_len);
    }
    return (uint32_t)start;
}
