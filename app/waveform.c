#include "waveform.h"

#include <stddef.h>

uint16_t wave_decimate(const uint16_t *samples, uint16_t n, uint16_t cols,
                       uint16_t *mins, uint16_t *maxs, uint16_t cap)
{
    uint16_t out = 0U;
    uint16_t i;

    if ((samples == NULL) || (mins == NULL) || (maxs == NULL)) {
        return 0U;
    }
    if ((n == 0U) || (cols == 0U) || (cap == 0U)) {
        return 0U;
    }
    if (cols > cap) {
        cols = cap;
    }

    for (i = 0U; i < cols; i++) {
        uint32_t start = ((uint32_t)i * n) / cols;
        uint32_t end   = ((uint32_t)(i + 1U) * n) / cols;
        uint16_t lo, hi;

        /* n < cols leaves some slices empty (start == end): clamp to the
         * nearest existing sample so the column repeats instead of reading
         * past the window. */
        if (end <= start) {
            end = start + 1U;
        }
        if (start >= n) {
            start = (uint32_t)n - 1U;
        }
        if (end > n) {
            end = n;
        }

        lo = samples[start];
        hi = samples[start];
        {
            uint32_t k;

            for (k = start + 1U; k < end; k++) {
                if (samples[k] < lo) {
                    lo = samples[k];
                }
                if (samples[k] > hi) {
                    hi = samples[k];
                }
            }
        }
        mins[out] = lo;
        maxs[out] = hi;
        out++;
    }
    return out;
}

uint16_t wave_y(uint16_t count, const struct wave_range *range,
                const struct wave_geom *geom)
{
    uint32_t span;
    uint32_t value;
    uint32_t pixels;
    uint32_t y;

    if (geom == NULL) {
        return 0U;
    }
    if (geom->height < 2U) {
        return geom->y_top;
    }
    if ((range == NULL) || (range->v_max <= range->v_min)) {
        return (uint16_t)(geom->y_top + (geom->height / 2U));
    }

    pixels = (uint32_t)geom->height - 1U;
    span   = (uint32_t)range->v_max - (uint32_t)range->v_min;

    if (count <= range->v_min) {
        return (uint16_t)(geom->y_top + pixels);          /* bottom row     */
    }
    if (count >= range->v_max) {
        return geom->y_top;                               /* top row        */
    }

    value = (uint32_t)count - (uint32_t)range->v_min;
    /* Higher count -> smaller y (the panel's y axis grows downward). */
    y = geom->y_top + pixels - (value * pixels) / span;
    return (uint16_t)y;
}

uint16_t wave_remove_mean(uint16_t *samples, uint16_t n)
{
    uint32_t sum = 0U;
    uint16_t mean;
    uint16_t i;

    if ((samples == NULL) || (n == 0U)) {
        return 0U;
    }
    for (i = 0U; i < n; i++) {
        sum += samples[i];
    }
    mean = (uint16_t)((sum + (uint32_t)(n / 2U)) / (uint32_t)n);

    for (i = 0U; i < n; i++) {
        int32_t v = 2048 + (int32_t)samples[i] - (int32_t)mean;
        if (v < 0) {
            v = 0;
        } else if (v > 4095) {
            v = 4095;
        }
        samples[i] = (uint16_t)v;
    }
    return mean;
}

uint16_t wave_column_means(const uint16_t *samples, uint16_t n, uint16_t cols,
                           uint16_t *out, uint16_t cap)
{
    uint16_t i;

    if ((samples == NULL) || (out == NULL) || (n == 0U) || (cols == 0U)
        || (cap == 0U)) {
        return 0U;
    }
    if (cols > cap) {
        cols = cap;
    }
    for (i = 0U; i < cols; i++) {
        uint32_t start = ((uint32_t)i * n) / cols;
        uint32_t end   = ((uint32_t)(i + 1U) * n) / cols;
        uint32_t sum   = 0U;
        uint32_t count;

        if (end <= start) {
            end = start + 1U;              /* short window: repeat a sample */
        }
        if (end > n) {
            end = n;
        }
        count = end - start;
        while (start < end) {
            sum += samples[start++];
        }
        out[i] = (uint16_t)((sum + count / 2U) / count);
    }
    return cols;
}
