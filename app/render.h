/* Waveform renderer seam - pure logic, host-testable, no HAL.
 *
 * The drawing primitive stays in the display driver; this module only decides
 * *what* to draw: one vertical bar per screen column, from the column's
 * maximum count down to its minimum count.  Separating it means the whole
 * pipeline (samples -> columns -> pixels) can be checked on the host with a
 * recording sink instead of by looking at the panel.
 */
#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>

#include "waveform.h"

/* Draw a filled vertical run at column `x`, rows `y_top`..`y_bottom`
 * inclusive (both already clamped inside the grid by this module). */
typedef void (*render_vline_fn)(void *ctx, uint16_t x,
                                uint16_t y_top, uint16_t y_bottom);

/* Render `cols` columns of `samples` into the grid described by `geom`,
 * starting at screen column `x0`.  `mins`/`maxs` are caller-supplied scratch
 * of at least `cols` entries (the caller owns the RAM budget).  Returns the
 * number of columns drawn, or 0 on a parameter error - including a NULL draw
 * callback, so a missing hook is a visible no-op instead of a crash. */
uint16_t render_columns(const uint16_t *samples, uint16_t n,
                        const struct wave_range *range,
                        const struct wave_geom *geom,
                        uint16_t x0, uint16_t cols,
                        uint16_t *mins, uint16_t *maxs,
                        render_vline_fn draw, void *ctx);

#endif /* RENDER_H */
