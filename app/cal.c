#include "cal.h"

#include <stddef.h>

#define CAL_UV_MAX  2000000L    /* +/-2 V: far above any real reading, */
#define CAL_UV_MIN  (-2000000L) /* far below INT32_MIN so saturate is  */

const struct cal cal_identity = { 0, 65536L };

uint8_t cal_fit(uint16_t raw_lo, int32_t uv_lo,
                uint16_t raw_hi, int32_t uv_hi,
                struct cal *out)
{
    int32_t span;
    int64_t gain;
    int64_t offset;

    if ((out == NULL) || (raw_lo == raw_hi) || (raw_lo > 4095U) || (raw_hi > 4095U)) {
        return 1U;
    }

    span = (int32_t)raw_hi - (int32_t)raw_lo;
    gain = (((int64_t)uv_hi - (int64_t)uv_lo) << 16) / (int64_t)span;
    offset = (int64_t)uv_lo - (((int64_t)raw_lo * gain) >> 16);

    /* A gain this far out means the caller passed nonsense (e.g. two raws
     * one count apart with volts between them); refuse rather than storing
     * a value that would saturate every reading. */
    if ((gain > 0x7FFFFFFFLL) || (gain < -0x7FFFFFFFLL)
        || (offset > 0x7FFFFFFFLL) || (offset < -0x7FFFFFFFLL)) {
        return 1U;
    }

    out->offset_uv = (int32_t)offset;
    out->gain_q16  = (int32_t)gain;
    return 0U;
}

int32_t cal_raw_to_uv(uint16_t raw, const struct cal *c)
{
    int64_t uv;

    if (c == NULL) {
        return (int32_t)raw;
    }
    uv = (int64_t)c->offset_uv
       + ((((int64_t)raw * (int64_t)c->gain_q16)) >> 16);

    if (uv > (int64_t)CAL_UV_MAX) {
        return CAL_UV_MAX;
    }
    if (uv < (int64_t)CAL_UV_MIN) {
        return CAL_UV_MIN;
    }
    return (int32_t)uv;
}

int32_t cal_uv_to_mv(int32_t uv)
{
    return uv / 1000L;
}

/* Live (mutable) calibration.  Defaults to the identity pair; the boot
 * path loads the persisted pair and the calibration menu swaps values. */
static struct cal s_cal_live = { 0, 65536L };

const struct cal *cal_active(void)
{
    return &s_cal_live;
}

void cal_active_set(const struct cal *c)
{
    if (c != NULL) {
        s_cal_live = *c;
    }
}
