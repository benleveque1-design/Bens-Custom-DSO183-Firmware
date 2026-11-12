#include "render.h"

#include <stddef.h>

uint16_t render_columns(const uint16_t *samples, uint16_t n,
                        const struct wave_range *range,
                        const struct wave_geom *geom,
                        uint16_t x0, uint16_t cols,
                        uint16_t *mins, uint16_t *maxs,
                        render_vline_fn draw, void *ctx)
{
    uint16_t drawn;
    uint16_t i;

    if ((samples == NULL) || (mins == NULL) || (maxs == NULL)
        || (draw == NULL) || (geom == NULL) || (n == 0U) || (cols == 0U)) {
        return 0U;
    }

    /* decimate() also enforces the scratch capacity contract. */
    drawn = wave_decimate(samples, n, cols, mins, maxs, cols);
    if (drawn == 0U) {
        return 0U;
    }

    for (i = 0U; i < drawn; i++) {
        uint16_t y_top = wave_y(maxs[i], range, geom);   /* highest count  */
        uint16_t y_bot = wave_y(mins[i], range, geom);   /* lowest count   */

        /* Defensive: wave_y() already orders these, but a future range
         * change must not turn a bar upside down and scribble outside the
         * grid.  Swap instead of trusting the invariant. */
        if (y_top > y_bot) {
            uint16_t t = y_top;
            y_top = y_bot;
            y_bot = t;
        }
        draw(ctx, (uint16_t)(x0 + i), y_top, y_bot);
    }
    return drawn;
}
