/* Waveform geometry - column decimation and count-to-pixel mapping.
 *
 * Pure integer logic, host-testable, no HAL.  The renderer (M7) owns the
 * actual drawing; this file decides which pixels a window occupies:
 *
 *   - 1000+ samples never map onto 320 columns one-for-one, so each column
 *     takes the min and max of its slice of the window and draws a vertical
 *     bar.  That is what preserves a narrow spike: taking a single sample per
 *     column (or averaging) hides it entirely.
 *   - the mapping is top-down: higher ADC counts are higher on the screen,
 *     because that is how a scope is read.
 *
 * The y mapping works in ADC counts, not volts: volt scaling is calibration
 * (M8a) and must not be baked into the geometry.
 */
#ifndef WAVEFORM_H
#define WAVEFORM_H

#include <stdint.h>

/* Pixel rectangle the trace may occupy (the grid interior). */
struct wave_geom {
    uint16_t y_top;      /* first pixel row of the grid                    */
    uint16_t height;     /* grid height in pixels (>= 2)                   */
};

/* Vertical value range mapped onto the grid.  v_max > v_min. */
struct wave_range {
    uint16_t v_min;      /* count mapped to the bottom row                 */
    uint16_t v_max;      /* count mapped to the top row                    */
};

/* Bucket `n` samples into `cols` columns, writing the min and max of each
 * slice to mins/maxs.  Returns the number of columns written (0 on a
 * parameter error).  When n < cols the extra columns repeat the nearest
 * sample position rather than being left blank, so a short window still
 * spans the full width; when n is not a multiple of cols the slice
 * boundaries are `i*n/cols` .. `(i+1)*n/cols` and every slice is non-empty.
 * `cap` bounds the writes; the return value is min(cols, cap). */
uint16_t wave_decimate(const uint16_t *samples, uint16_t n, uint16_t cols,
                       uint16_t *mins, uint16_t *maxs, uint16_t cap);

/* Count -> pixel row.  Values outside [v_min, v_max] clamp to the grid edge,
 * and `v_max <= v_min` centres the trace instead of dividing by zero. */
uint16_t wave_y(uint16_t count, const struct wave_range *range,
                const struct wave_geom *geom);


/* Remove the frame mean (AC coupling).  Each sample becomes
 * 2048 + (sample - mean), clamped to 0..4095, so a DC offset no longer
 * pushes the trace off-screen.  Returns the mean used, or 0 for a NULL
 * pointer / empty window.  Mutates the caller's buffer on purpose: the
 * frame is already a private copy in main(). */
uint16_t wave_remove_mean(uint16_t *samples, uint16_t n);


/* Per-column mean of the window (display smoothing).  Each column becomes
 * the average of its slice instead of its min/max band, which keeps the shape
 * but removes the noise "comb" at very small spans.  Returns the number of
 * columns written; 0 on NULL/empty input. */
uint16_t wave_column_means(const uint16_t *samples, uint16_t n, uint16_t cols,
                           uint16_t *out, uint16_t cap);

#endif /* WAVEFORM_H */
