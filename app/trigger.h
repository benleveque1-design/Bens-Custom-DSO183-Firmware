/* Software trigger engine - pure logic, host-testable.
 *
 * The hardware trigger in acq.c is the ADC analog watchdog: it fires when a
 * sample leaves the [LTR, HTR] window, is one-shot latched in the ISR, and
 * therefore has NO slope selectivity, NO hysteresis and NO hold-off.  The
 * published DSO183 behaviour (manual: trigger modes auto / normal / single,
 * rising or falling slope) needs all three, so the edge refinement happens
 * here, in software, on the sample buffer that acq.c collects.
 *
 * The scan implements the classic edge-trigger state machine used by the
 * sibling DSO-150 project and by the DSO138 reference flow:
 *
 *   1. rearm      - wait for a sample on the "far" side of the hysteresis
 *                   band (below level - hyst for a rising edge, above
 *                   level + hyst for a falling edge);
 *   2. trigger    - fire on the first sample that crosses back through the
 *                   band (at or beyond level + hyst / level - hyst).  The
 *                   reported index is the triggering (post-edge) sample.
 *
 * A rearm sample may be immediately followed by a trigger sample; the pair
 * of conditions is evaluated per sample step, so a fast clean edge of 2
 * samples is found, while slow noise around the level is skipped until a
 * full rearm -> cross sequence happens (that is the hysteresis behaviour
 * the analog watchdog cannot do).
 *
 * Policies (README "Rules this tree follows"): no floats, no allocation,
 * no unbounded loops - both functions run a single bounded pass over the
 * buffer handed to them.
 */
#ifndef TRIGGER_H
#define TRIGGER_H

#include <stdint.h>

/* The acquisition buffer is 12-bit ADC counts (right-aligned, 0..4095). */
#define TRIG_FULL_SCALE 4095U

enum trig_slope {
    TRIG_RISING  = 0U,      /* fire when the level crosses UP through level   */
    TRIG_FALLING = 1U       /* fire when the level crosses DOWN through level */
};

enum trig_mode {
    /* Same three modes as the vendor firmware (PARAMS[9], 0..2) and its
     * manual: AUTO (always take a window, retrigger on the next event),
     * NORM (only take a window after a trigger), SINGLE (take exactly one
     * window after an explicit re-arm). */
    TRIG_MODE_AUTO   = 0U,
    TRIG_MODE_NORM   = 1U,
    TRIG_MODE_SINGLE = 2U
};

struct trig_cfg {
    uint16_t level;        /* crossing threshold in ADC counts        */
    uint16_t hyst_counts;  /* hysteresis margin required past level    */
    uint8_t  slope;        /* enum trig_slope                          */
};

/* Scan for the FIRST valid edge crossing.
 *
 * Returns the index (0..n-1) of the triggering sample, or -1 when no
 * crossing is present.  Evaluated on pairs: a trigger at index i needs
 * samples[i-1] on the far side of the band and samples[i] across it, so a
 * single sample buffer always defines both edges. */
int32_t trig_scan(const uint16_t *samples, uint32_t n,
                  const struct trig_cfg *cfg);

/* Scan for the crossing CLOSEST to `center` (in samples).
 *
 * Same hysteresis state machine as trig_scan(), but every valid crossing is
 * considered and the one with the smallest distance to `center` is
 * returned.  This is what the display uses to snap the software edge to the
 * hardware watchdog event: the watchdog index is within a sample or two of
 * the true crossing, so the nearest crossing is the right one even when the
 * search window contains several periods of a fast signal.  Returns -1 when
 * the buffer contains no crossing. */
int32_t trig_scan_nearest(const uint16_t *samples, uint32_t n,
                          uint32_t center, const struct trig_cfg *cfg);

/* Trigger mode FSM.  SINGLE needs an explicit re-arm; AUTO and NORM are
 * stateless except for the SINGLE armed flag. */
struct trig_fsm {
    uint8_t mode;    /* enum trig_mode */
    uint8_t armed;   /* SINGLE: 1 = waiting for its one capture */
};

void trig_fsm_init(struct trig_fsm *fsm, uint8_t mode);
void trig_fsm_arm(struct trig_fsm *fsm);

/* What the acquisition loop should DO with the window it just captured.
 * This is the mode policy in one place so acq.c does not re-implement it:
 *
 *   AUTO:   found -> CAPTURE; otherwise FALLBACK (show the newest samples);
 *   NORM:   found -> CAPTURE; otherwise HOLD (keep the previous display);
 *   SINGLE: armed AND found -> CAPTURE (and disarm); otherwise HOLD.
 *
 * `timed_out` is 1 when the trigger wait ended without a usable capture
 * (timeout or an incomplete post-trigger fill).  It only affects AUTO;
 * NORM and SINGLE hold until a real trigger arrives.
 */
enum trig_action {
    TRIG_ACTION_CAPTURE  = 0U,   /* take this triggered window          */
    TRIG_ACTION_FALLBACK = 1U,   /* AUTO: take an untriggered window    */
    TRIG_ACTION_HOLD     = 2U    /* keep the previous displayed window  */
};
enum trig_action trig_fsm_action(struct trig_fsm *fsm, uint8_t found,
                                 uint8_t timed_out);

/* Decide whether the acquisition loop TAKES the captured window.
 *   AUTO:   always take (this is the AUTO fallback, timeout or not);
 *   NORM:   take only when a trigger was found;
 *   SINGLE: take only when armed AND a trigger was found; a taken window
 *           disarms the FSM until the next trig_fsm_arm().
 * found: 1 when the window contains a trigger (hardware latch or the
 * software scan), 0 for an untriggered freewheeling window. */
uint8_t trig_fsm_take(struct trig_fsm *fsm, uint8_t found);

/* Display-window placement: the horizontal position of the trigger inside
 * the drawn trace.  The window starts TRIG_PRE_TRIGGER samples before the
 * trigger (clamped at 0), i.e. about 30% pre-trigger on the 320-sample
 * display window - matching the sibling DSO-150 project's default.
 * A no-trigger window (trig_index < 0) starts at 0.
 * The start is ALSO clamped to buffer_len - window_len so the caller can
 * never be handed a window that reads past the end of the buffer. */
#define TRIG_PRE_TRIGGER 96U
uint32_t trig_window_start(int32_t trig_index, uint32_t window_len,
                            uint32_t buffer_len);

#endif /* TRIGGER_H */
