/* Host helper: render the diagnostic screen as a PPM image.
 *
 * This uses the SAME code the firmware uses to paint the panel:
 *   app/diag.c   -> the row text and the row geometry
 *   fonts/glyph.c -> the scaled 5x7 glyph cells (row-major, x fastest)
 *
 * The previous preview re-implemented the font in Python, which is how the
 * glyph scaling bug in the driver survived: the Python preview drew the
 * INTENDED picture while the driver produced garbage.  Rendering the
 * preview from the production modules removes that second implementation.
 *
 * Output: a binary PPM (P6) on the file named by argv[1] (default
 * build/panel.ppm); tools/render_diag.py turns it into a PNG.
 */
#include "diag.h"
#include "diag_fixture.h"
#include "glyph.h"
#include "ili9341.h"

#include <stdio.h>
#include <stdlib.h>

#define FG ILI9341_GREEN
#define BG ILI9341_BLACK

static uint16_t s_fb[ILI9341_WIDTH * ILI9341_HEIGHT];

static void put_pixel(int32_t x, int32_t y, uint16_t color)
{
    if ((x < 0) || (y < 0) ||
        (x >= (int32_t)ILI9341_WIDTH) || (y >= (int32_t)ILI9341_HEIGHT)) {
        return;
    }
    s_fb[(y * (int32_t)ILI9341_WIDTH) + x] = color;
}

/* Same as ili9341_draw_char, but into the framebuffer. */
static void draw_char(int32_t x, int32_t y, char c, uint16_t fg, uint16_t bg)
{
    struct glyph_iter it;
    uint16_t px;
    int32_t idx = 0;

    glyph_iter_init(&it, c, (uint8_t)DIAG_TEXT_SCALE, fg, bg);
    while (glyph_next(&it, &px) != 0u) {
        int32_t col = idx % (int32_t)(GLYPH_CELL_COLS * DIAG_TEXT_SCALE);
        int32_t row = idx / (int32_t)(GLYPH_CELL_COLS * DIAG_TEXT_SCALE);

        put_pixel(x + col, y + row, px);
        idx++;
    }
}

int main(int argc, char **argv)
{
    static char rows[DIAG_ROWS][DIAG_COLS];
    struct diag_state st;
    const char *path = (argc > 1) ? argv[1] : "build/panel.ppm";
    FILE *f;
    uint8_t *rgb;

    for (uint32_t i = 0U; i < (uint32_t)(ILI9341_WIDTH * ILI9341_HEIGHT); i++) {
        s_fb[i] = BG;
    }

    diag_fixture(&st);
    diag_rows(rows, &st);

    for (uint8_t r = 0U; r < DIAG_ROWS; r++) {
        int32_t x = (int32_t)diag_row_x(r);
        int32_t y = (int32_t)diag_row_y(r);

        for (uint8_t i = 0U; rows[r][i] != '\0'; i++) {
            draw_char(x, y, rows[r][i], FG, BG);
            x += (int32_t)(DIAG_CELL_W * DIAG_TEXT_SCALE);
        }
    }

    rgb = (uint8_t *)malloc((size_t)ILI9341_WIDTH * ILI9341_HEIGHT * 3U);
    if (rgb == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    for (uint32_t i = 0U; i < (uint32_t)(ILI9341_WIDTH * ILI9341_HEIGHT); i++) {
        uint16_t c = s_fb[i];
        uint8_t r5 = (uint8_t)((c >> 11) & 0x1FU);
        uint8_t g6 = (uint8_t)((c >> 5) & 0x3FU);
        uint8_t b5 = (uint8_t)(c & 0x1FU);

        rgb[(i * 3U) + 0U] = (uint8_t)((r5 * 255U) / 31U);
        rgb[(i * 3U) + 1U] = (uint8_t)((g6 * 255U) / 63U);
        rgb[(i * 3U) + 2U] = (uint8_t)((b5 * 255U) / 31U);
    }

    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        free(rgb);
        return 1;
    }
    fprintf(f, "P6\n%u %u\n255\n", (unsigned)ILI9341_WIDTH,
            (unsigned)ILI9341_HEIGHT);
    fwrite(rgb, 1, (size_t)ILI9341_WIDTH * ILI9341_HEIGHT * 3U, f);
    fclose(f);
    free(rgb);

    printf("wrote %s (%ux%u)\n", path, (unsigned)ILI9341_WIDTH,
           (unsigned)ILI9341_HEIGHT);
    return 0;
}
