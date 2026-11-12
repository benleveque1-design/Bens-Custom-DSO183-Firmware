/* Host unit tests for the glyph rasteriser (fonts/glyph.c).
 *
 * The pixel stream is what the panel receives, so these tests pin down
 * the shape rules the display driver depends on:
 *   - a cell is exactly 6*scale x 8*scale pixels (window and pixel count
 *     must agree);
 *   - scale is a SQUARE scaling: every pixel of the scaled cell is the
 *     scale x scale block of the scale-1 cell (the previous driver
 *     scaled only horizontally - that bug would fail this test);
 *   - the 6th column and the 8th row of the cell are always background;
 *   - the vendored font table is the one the driver expects.
 */
#include "glyph.h"

#include <stdio.h>

#define FAIL(msg) do { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); return 1; } while (0)
#define CHECK(cond) do { if (!(cond)) { FAIL(#cond); } } while (0)

#define CELL_MAX (GLYPH_CELL_COLS * GLYPH_MAX_SCALE * GLYPH_CELL_ROWS * GLYPH_MAX_SCALE)

/* Render one cell into out[], return the number of pixels produced. */
static uint32_t render(char c, uint8_t scale, uint16_t fg, uint16_t bg,
                       uint16_t *out)
{
    struct glyph_iter it;
    uint16_t px;
    uint32_t n = 0u;

    glyph_iter_init(&it, c, scale, fg, bg);
    while (glyph_next(&it, &px) != 0u) {
        if (n < CELL_MAX) {
            out[n] = px;
        }
        n++;
    }
    return n;
}

static int test_cell_size(void)
{
    uint16_t buf[CELL_MAX];

    for (uint8_t s = 1u; s <= GLYPH_MAX_SCALE; s++) {
        uint32_t want = (uint32_t)GLYPH_CELL_COLS * s *
                        (uint32_t)GLYPH_CELL_ROWS * s;
        CHECK(glyph_cell_pixels(s) == want);
        CHECK(render('A', s, 0xFFFFu, 0x0000u, buf) == want);
    }
    /* scale 0 is treated as 1, anything above the maximum is clamped */
    CHECK(glyph_cell_pixels(0u) == 48u);
    CHECK(glyph_cell_pixels(200u) == glyph_cell_pixels(GLYPH_MAX_SCALE));
    return 0;
}

static int test_scale_is_square(void)
{
    uint16_t one[GLYPH_CELL_COLS * GLYPH_CELL_ROWS];
    uint16_t two[GLYPH_CELL_COLS * 2u * GLYPH_CELL_ROWS * 2u];
    uint16_t four[GLYPH_CELL_COLS * 4u * GLYPH_CELL_ROWS * 4u];

    CHECK(render('B', 1u, 0xFFFFu, 0x0000u, one) == 48u);
    CHECK(render('B', 2u, 0xFFFFu, 0x0000u, two) == 192u);
    CHECK(render('B', 4u, 0xFFFFu, 0x0000u, four) == 768u);

    /* every pixel of the 2x cell must be the 2x2 block of the 1x cell */
    for (uint32_t y = 0u; y < 16u; y++) {
        for (uint32_t x = 0u; x < 12u; x++) {
            CHECK(two[(y * 12u) + x] == one[((y / 2u) * 6u) + (x / 2u)]);
        }
    }
    /* ... and the same for 4x */
    for (uint32_t y = 0u; y < 32u; y++) {
        for (uint32_t x = 0u; x < 24u; x++) {
            CHECK(four[(y * 24u) + x] == one[((y / 4u) * 6u) + (x / 4u)]);
        }
    }
    return 0;
}

static int test_blank_column_and_row(void)
{
    uint16_t buf[GLYPH_CELL_COLS * 2u * GLYPH_CELL_ROWS * 2u];

    CHECK(render('A', 2u, 0xFFFFu, 0x1234u, buf) == 192u);

    /* column 5 of the font is blank -> pixels 10 and 11 of every row */
    for (uint32_t y = 0u; y < 16u; y++) {
        CHECK(buf[(y * 12u) + 10u] == 0x1234u);
        CHECK(buf[(y * 12u) + 11u] == 0x1234u);
    }
    /* row 7 of the font is blank -> the last two rows of the cell */
    for (uint32_t x = 0u; x < 12u; x++) {
        CHECK(buf[(14u * 12u) + x] == 0x1234u);
        CHECK(buf[(15u * 12u) + x] == 0x1234u);
    }
    return 0;
}

static int test_space_is_blank(void)
{
    uint16_t buf[CELL_MAX];
    uint32_t n = render(' ', 2u, 0xFFFFu, 0x0000u, buf);

    CHECK(n == 192u);
    for (uint32_t i = 0u; i < n; i++) {
        CHECK(buf[i] == 0x0000u);
    }
    return 0;
}

static int test_known_glyph(void)
{
    /* 'A' in the vendored 5x7 table is 0x7C,0x12,0x11,0x12,0x7C
     * (column major, bit 0 = top row).  Its top row is therefore
     * background, background, foreground, background, background + gap. */
    uint16_t buf[GLYPH_CELL_COLS * GLYPH_CELL_ROWS];

    CHECK(render('A', 1u, 0xFFFFu, 0x0000u, buf) == 48u);
    CHECK(buf[0] == 0x0000u);
    CHECK(buf[1] == 0x0000u);
    CHECK(buf[2] == 0xFFFFu);
    CHECK(buf[3] == 0x0000u);
    CHECK(buf[4] == 0x0000u);
    CHECK(buf[5] == 0x0000u);   /* the blank column */
    return 0;
}

static int test_fail_fast(void)
{
    struct glyph_iter it;
    uint16_t px = 0xFFFFu;

    glyph_iter_init(NULL, 'A', 1u, 0u, 0u);       /* must not crash */
    CHECK(glyph_next(NULL, &px) == 0u);
    glyph_iter_init(&it, 'A', 1u, 0u, 0u);
    CHECK(glyph_next(&it, NULL) == 0u);
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_cell_size();
    failures += test_scale_is_square();
    failures += test_blank_column_and_row();
    failures += test_space_is_blank();
    failures += test_known_glyph();
    failures += test_fail_fast();

    if (failures == 0) {
        printf("glyph: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
