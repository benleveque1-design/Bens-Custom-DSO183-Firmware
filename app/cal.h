/* Calibration maths - integer only, host-testable.
 *
 * The ADC reports 12-bit counts; what the user reads is a voltage.  The
 * mapping is a line, `uV = offset_uv + raw * gain_q16 / 65536`, and the pair
 * (offset, gain) is per input range and per instrument - it is what the
 * calibration store at 0x0800F800 exists to remember (CAL-02 / M8a).
 *
 * Q16 for the gain keeps sub-microvolt-per-count resolution in a 32-bit
 * word (the probe's front end is a divider, so the slope is not an integer).
 * Everything is int64 intermediate: 4095 counts * a large gain overflows
 * int32 long before the result does.
 *
 * No HAL, no floating point: the F1 has no FPU and this runs per frame.
 */
#ifndef CAL_H
#define CAL_H

#include <stdint.h>

struct cal {
    int32_t offset_uv;   /* output when raw == 0                       */
    int32_t gain_q16;    /* microvolts per count, scaled by 65536      */
};

/* A freshly programmed instrument with no stored calibration: 1 count =
 * 1 microvolt, no offset.  Use until the real pair is measured. */
extern const struct cal cal_identity;

/* Fit through two measured points (raw counts -> microvolts).  Both raws
 * must be inside 0..4095 and differ; `uv_hi` may be lower than `uv_lo`
 * (an inverting input is legitimate).  Returns 0 on success, 1 on a
 * parameter error, in which case *out is left untouched. */
uint8_t cal_fit(uint16_t raw_lo, int32_t uv_lo,
                uint16_t raw_hi, int32_t uv_hi,
                struct cal *out);

/* Apply the calibration.  Saturates instead of wrapping: a value that large
 * is a fault, not a reading, and a wrapped voltage would look plausible. */
int32_t cal_raw_to_uv(uint16_t raw, const struct cal *c);

/* Microvolts -> millivolts for display, rounding toward zero (the readout
 * is never used for a further calculation). */
int32_t cal_uv_to_mv(int32_t uv);

/* Live (mutable) calibration used for display and adjustment.  Defaults to
 * the identity calibration; loaded from the calibration store at boot and
 * updated by the calibration menu. */
const struct cal *cal_active(void);
void cal_active_set(const struct cal *c);

#endif /* CAL_H */
