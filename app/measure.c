#include "measure.h"

#include <stddef.h>

uint32_t measure_isqrt(uint32_t v)
{
    uint32_t res = 0U;
    uint32_t bit = 1UL << 30;

    while (bit > v) {
        bit >>= 2;
    }
    while (bit != 0U) {
        if (v >= res + bit) {
            v -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

uint8_t measure_compute(const uint16_t *samples, uint16_t n,
                        struct measure_stats *out)
{
    uint64_t sum = 0U;
    uint64_t sum_sq = 0U;
    uint16_t min = 0xFFFFU;
    uint16_t max = 0U;
    uint32_t mean_sq;
    uint16_t i;

    if ((samples == NULL) || (out == NULL) || (n == 0U)) {
        if (out != NULL) {
            out->n = 0U;
        }
        return 1U;
    }

    for (i = 0U; i < n; i++) {
        uint32_t v = samples[i];

        sum += v;
        sum_sq += (uint64_t)v * v;
        if (samples[i] < min) {
            min = samples[i];
        }
        if (samples[i] > max) {
            max = samples[i];
        }
    }

    out->min  = min;
    out->max  = max;
    out->vpp  = (uint16_t)(max - min);
    out->n    = n;
    out->mean = (uint16_t)((sum + (uint64_t)(n / 2U)) / n);

    mean_sq = (uint32_t)((sum_sq + (uint64_t)(n / 2U)) / n);
    out->rms = (uint16_t)measure_isqrt(mean_sq);
    return 0U;
}

uint8_t measure_freq_duty(const uint16_t *samples, uint16_t n,
                          uint32_t sample_period_ns,
                          uint32_t *freq_hz_out, uint16_t *duty_x10_out)
{
    uint16_t minv = 0xFFFFU, maxv = 0U;
    uint16_t i;
    uint16_t mid, hyst, lo_th, hi_th;
    uint8_t state;
    uint32_t last_rise = 0U, rise_count = 0U;
    uint32_t high_samples = 0U, total_in_periods = 0U;
    uint32_t rise_pos = 0U;
    uint8_t in_measured = 0U;

    if ((samples == NULL) || (freq_hz_out == NULL) || (duty_x10_out == NULL)) {
        return 1U;
    }
    *freq_hz_out = 0U;
    *duty_x10_out = 0U;
    if ((n < 4U) || (sample_period_ns == 0U)) {
        return 0U;
    }
    for (i = 0U; i < n; i++) {
        if (samples[i] < minv) minv = samples[i];
        if (samples[i] > maxv) maxv = samples[i];
    }
    if (maxv <= minv + 2U) {
        return 0U;
    }
    mid = (uint16_t)(((uint32_t)minv + (uint32_t)maxv) / 2U);
    hyst = (uint16_t)(((uint32_t)maxv - (uint32_t)minv) / 8U);
    if (hyst < 1U) hyst = 1U;
    lo_th = (mid > hyst) ? (uint16_t)(mid - hyst) : minv;
    hi_th = (uint16_t)((uint32_t)mid + hyst);
    if (hi_th > maxv) hi_th = maxv;

    state = (samples[0] >= mid) ? 1U : 0U;
    rise_pos = 0U;
    for (i = 1U; i < n; i++) {
        uint16_t v = samples[i];
        if ((state == 0U) && (v >= hi_th)) {
            state = 1U;
            rise_pos = i;
            if (rise_count >= 1U) {
                total_in_periods += (i - last_rise);
                in_measured = 1U;
            }
            last_rise = i;
            rise_count++;
        } else if ((state != 0U) && (v <= lo_th)) {
            state = 0U;
            if (in_measured != 0U) {
                high_samples += (i > rise_pos) ? (i - rise_pos) : 0U;
            }
        }
    }
    if ((state != 0U) && (in_measured != 0U) && (n > rise_pos)) {
        high_samples += (n - 1U - rise_pos);
    }

    if ((rise_count >= 2U) && (total_in_periods > 0U)) {
        uint64_t period_ns = (uint64_t)total_in_periods * (uint64_t)sample_period_ns
                           / (uint64_t)(rise_count - 1U);
        if (period_ns > 0ULL) {
            *freq_hz_out = (uint32_t)(1000000000ULL / period_ns);
        }
        {
            uint32_t duty = (high_samples * 1000U) / total_in_periods;
            if (duty > 1000U) duty = 1000U;
            *duty_x10_out = (uint16_t)duty;
        }
    }
    return 0U;
}
