/* Host unit tests for the renderer seam (app/render.c) using a recording sink. */
#include "render.h"

#include <stdio.h>

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

#define MAX_BARS 32

struct sink {
    uint16_t n;
    uint16_t x[MAX_BARS];
    uint16_t y0[MAX_BARS];
    uint16_t y1[MAX_BARS];
};

static void rec(void *ctx, uint16_t x, uint16_t y_top, uint16_t y_bottom)
{
    struct sink *s = (struct sink *)ctx;

    if (s->n < MAX_BARS) {
        s->x[s->n]  = x;
        s->y0[s->n] = y_top;
        s->y1[s->n] = y_bottom;
    }
    s->n++;
}

int main(void)
{
    uint16_t samples[16];
    uint16_t mins[16];
    uint16_t maxs[16];
    struct wave_geom geom = { 16U, 184U };
    struct wave_range range = { 1000U, 1015U };
    struct sink s;
    uint16_t i;
    uint16_t got;

    for (i = 0U; i < 16U; i++) {
        samples[i] = (uint16_t)(1000U + i);
    }

    /* Four columns starting at x = 10: one bar per column, ordered. */
    s.n = 0U;
    got = render_columns(samples, 16U, &range, &geom, 10U, 4U,
                         mins, maxs, rec, &s);
    CHECK(got == 4U);
    CHECK(s.n == 4U);
    for (i = 0U; i < s.n; i++) {
        CHECK(s.x[i] == (uint16_t)(10U + i));
        CHECK(s.y0[i] <= s.y1[i]);              /* never upside down  */
        CHECK(s.y0[i] >= geom.y_top);           /* inside the grid    */
        CHECK(s.y1[i] <= (uint16_t)(geom.y_top + geom.height - 1U));
    }
    /* Column 0 holds counts 1000..1003 -> its bar ends on the bottom row and
     * starts part-way up; column 3 holds 1012..1015 -> starts at the top. */
    CHECK(s.y1[0] == 199U);
    CHECK(s.y0[3] == 16U);
    CHECK(s.y0[0] > s.y0[3]);

    /* More columns than samples: every column still gets a bar. */
    s.n = 0U;
    got = render_columns(samples, 2U, &range, &geom, 0U, 8U,
                         mins, maxs, rec, &s);
    CHECK(got == 8U);
    CHECK(s.n == 8U);

    /* NULL range: all bars collapse onto the grid centre, never a divide. */
    s.n = 0U;
    got = render_columns(samples, 16U, NULL, &geom, 0U, 4U,
                         mins, maxs, rec, &s);
    CHECK(got == 4U);
    for (i = 0U; i < s.n; i++) {
        CHECK(s.y0[i] == 108U);
        CHECK(s.y1[i] == 108U);
    }

    /* Parameter errors: nothing is drawn. */
    s.n = 0U;
    CHECK(render_columns(samples, 16U, &range, &geom, 0U, 4U,
                         mins, maxs, NULL, &s) == 0U);
    CHECK(render_columns(NULL, 16U, &range, &geom, 0U, 4U,
                         mins, maxs, rec, &s) == 0U);
    CHECK(render_columns(samples, 0U, &range, &geom, 0U, 4U,
                         mins, maxs, rec, &s) == 0U);
    CHECK(render_columns(samples, 16U, &range, &geom, 0U, 0U,
                         mins, maxs, rec, &s) == 0U);
    CHECK(render_columns(samples, 16U, &range, NULL, 0U, 4U,
                         mins, maxs, rec, &s) == 0U);
    CHECK(render_columns(samples, 16U, &range, &geom, 0U, 4U,
                         NULL, maxs, rec, &s) == 0U);
    CHECK(s.n == 0U);

    if (failures == 0) {
        printf("render: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
