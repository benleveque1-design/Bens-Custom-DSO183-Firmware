/* Waveform measurements over a uint16 sample window - pure integer math.
 *
 * Kept free of HAL and floating point so the same code runs on the host and
 * on the F1 (which has no FPU, and where a soft-float sqrt would cost both
 * flash and worst-case timing).  The values are raw 12-bit ADC counts;
 * scaling to volts is a calibration concern and stays outside this module.
 *
 * Rounding: mean and RMS round to nearest, so a symmetric square wave reads
 * back at its true midpoint instead of one count low.
 */
#ifndef MEASURE_H
#define MEASURE_H

#include <stdint.h>

struct measure_stats {
    uint16_t min;      /* smallest count in the window                 */
    uint16_t max;      /* largest count                               */
    uint16_t vpp;      /* max - min (counts)                          */
    uint16_t mean;     /* arithmetic mean, rounded to nearest          */
    uint16_t rms;      /* root mean square: mean square rounded, then
                          an integer root, so within one count          */
    uint16_t n;        /* samples actually used (0 when it failed)     */
};

/* Compute the statistics over `n` samples.  Returns 0 on success, 1 for a
 * NULL buffer, NULL out, or n == 0 (the window is never silently reused). */
uint8_t measure_compute(const uint16_t *samples, uint16_t n,
                        struct measure_stats *out);

/* Integer square root of a 32-bit value, rounded down.  Exposed because the
 * RMS path is the only user and the tests check it directly against exact
 * squares; kept here rather than in a private helper for that reason. */
uint32_t measure_isqrt(uint32_t v);

/* Estimate frequency and duty cycle from a sample window.
 * `sample_period_ns` is the time between samples (from the active timebase).
 * Uses a simple midpoint/hysteresis zero-crossing detector.
 * On success returns 0 and writes freq_hz (0 if not measurable) and
 * duty_x10 (duty in 0.1% units, 0..1000). Returns 1 on bad args. */
uint8_t measure_freq_duty(const uint16_t *samples, uint16_t n,
                          uint32_t sample_period_ns,
                          uint32_t *freq_hz_out, uint16_t *duty_x10_out);

#endif /* MEASURE_H */
