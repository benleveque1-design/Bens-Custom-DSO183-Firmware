#include "glyph.h"

#include <stddef.h>

/* The 5x7 font (Adafruit GFX, MIT licence - see fonts/ADAFRUIT_GFX_LICENSE.txt).
 * Column major: font[c * 5 + column], bit 0 is the TOP row of that column. */
extern const unsigned char font[];

uint32_t glyph_cell_pixels(uint8_t scale)
{
    if (scale == 0u) {
        scale = 1u;
    }
    if (scale > GLYPH_MAX_SCALE) {
        scale = GLYPH_MAX_SCALE;
    }
    return (uint32_t)GLYPH_CELL_COLS * (uint32_t)scale *
           (uint32_t)GLYPH_CELL_ROWS * (uint32_t)scale;
}

void glyph_iter_init(struct glyph_iter *it, char c, uint8_t scale,
                     uint16_t fg, uint16_t bg)
{
    if (it == NULL) {
        return;
    }
    if (scale == 0u) {
        scale = 1u;
    }
    if (scale > GLYPH_MAX_SCALE) {
        scale = GLYPH_MAX_SCALE;
    }
    it->glyph = &font[(uint8_t)c * GLYPH_COLS];
    it->scale = scale;
    it->fg    = fg;
    it->bg    = bg;
    it->total = glyph_cell_pixels(scale);
    it->idx   = 0u;
}

uint8_t glyph_next(struct glyph_iter *it, uint16_t *px)
{
    uint32_t row_in_cell;
    uint32_t col_in_row;
    uint32_t font_row;
    uint32_t font_col;
    uint8_t  bits;

    if ((it == NULL) || (px == NULL)) {
        return 0u;
    }
    if (it->idx >= it->total) {
        return 0u;
    }

    row_in_cell = it->idx / ((uint32_t)GLYPH_CELL_COLS * it->scale);
    col_in_row  = it->idx % ((uint32_t)GLYPH_CELL_COLS * it->scale);
    font_row    = row_in_cell / it->scale;   /* 0..7, 7 = blank row    */
    font_col    = col_in_row  / it->scale;   /* 0..5, 5 = blank column */

    if ((font_col >= (uint32_t)GLYPH_COLS) ||
        (font_row >= (uint32_t)GLYPH_ROWS)) {
        *px = it->bg;                       /* the blank column/row   */
    } else {
        bits = (uint8_t)((it->glyph[font_col] >> font_row) & 1u);
        *px = bits ? it->fg : it->bg;
    }

    it->idx++;
    return 1u;
}
