/* Glyph rasteriser - pure logic, host-testable, no hardware knowledge.
 *
 * The panel is written as a stream of RGB565 pixels in row-major order
 * (x fastest), so a character is a fixed-size CELL of pixels:
 *
 *   the font is 5 columns x 7 rows (Adafruit GFX 'classic' 5x7, column
 *   major, bit 0 = top row - see fonts/glcdfont.c), the CELL is 6x8 so
 *   that there is one blank column on the right and one blank row at the
 *   bottom, and a SCALE factor repeats every font pixel in both
 *   directions:
 *
 *       cell width  = 6 * scale
 *       cell height = 8 * scale
 *       pixels      = 48 * scale * scale
 *
 * The module hands the pixels out one at a time through an iterator, so
 * the driver needs no buffer at all (RAM on this instrument is tight) and
 * the host test can check the exact pixel stream.
 *
 * Why an iterator and not a "draw into a buffer" helper: the previous
 * draw_char() in the driver scaled the pixels only HORIZONTALLY (the
 * vertical repeat was missing) and never emitted the blank column, so
 * the number of pixels it produced did not match the window it opened.
 * The screen would have shown stretched, drifting garbage instead of
 * text.  The shape rules are pinned down by tests/test_glyph.c.
 */
#ifndef GLYPH_H
#define GLYPH_H

#include <stdint.h>

#define GLYPH_COLS      5U   /* font pixels across                */
#define GLYPH_ROWS      7U   /* font pixels down                  */
#define GLYPH_CELL_COLS 6U   /* font columns + 1 blank column     */
#define GLYPH_CELL_ROWS 8U   /* font rows    + 1 blank row       */
#define GLYPH_MAX_SCALE 4U   /* the driver's documented maximum   */

/* Pixels in one scaled cell (48 * scale * scale). */
uint32_t glyph_cell_pixels(uint8_t scale);

struct glyph_iter {
    const unsigned char *glyph;  /* 5 font bytes of the character   */
    uint8_t  scale;
    uint16_t fg;
    uint16_t bg;
    uint32_t total;              /* pixels in the whole cell        */
    uint32_t idx;                /* next pixel to produce           */
};

/* Prepare an iterator for one character.  scale 0 is treated as 1 and
 * anything above GLYPH_MAX_SCALE is clamped. */
void glyph_iter_init(struct glyph_iter *it, char c, uint8_t scale,
                     uint16_t fg, uint16_t bg);

/* Next pixel of the cell, row-major (x fastest).  Returns 1 and stores
 * the RGB565 colour, or 0 when the cell is exhausted. */
uint8_t glyph_next(struct glyph_iter *it, uint16_t *px);

#endif /* GLYPH_H */
