/* End-to-end pipeline test: ring -> window -> stats -> calibration -> bars.
 *
 * The unit tests prove each stage in isolation.  This one proves the stages
 * AGREE: a signal with analytically known statistics is pushed through the
 * whole pure chain and the results are compared against closed-form values,
 * not against what the code happens to produce.  It is the closest thing to
 * a bench measurement that can run without hardware.
 *
 * Signal: a 50 % square wave with a 4-sample period (two low, two high), so
 * that every 8-sample display column contains BOTH rails - the case
 * decimation exists for.  Low = 1000 counts, high = 3000 counts.
 *   min  = 1000
 *   max  = 3000
 *   vpp  = 2000
 *   mean = 2000
 *   rms  = sqrt((1000^2 + 3000^2) / 2) = sqrt(5 000 000) = 2236.07 -> 2236
 * Calibrated with a two-point fit at (1000 -> 0 uV) and (3000 -> 2000 uV),
 * i.e. 1 uV/count and a -1000 uV offset.
 */
#include "acq_window.h"
#include "cal.h"
#include "frame.h"
#include "render.h"
#include "waveform.h"

#include <stdio.h>

#define RING 64U
#define GRID_TOP 16U
#define GRID_HEIGHT 184U

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static void fill_square(uint16_t *ring, uint16_t n, uint16_t period)
{
    uint16_t i;

    for (i = 0U; i < n; i++) {
        ring[i] = ((i % period) < (period / 2U)) ? 1000U : 3000U;
    }
}

/* Recording sink: the bars the display driver would have drawn. */
struct bar {
    uint16_t x;
    uint16_t y_top;
    uint16_t y_bottom;
};

struct sink {
    struct bar bars[32];
    uint16_t count;
};

static void record(void *ctx, uint16_t x, uint16_t y_top, uint16_t y_bottom)
{
    struct sink *s = (struct sink *)ctx;

    if (s->count < 32U) {
        s->bars[s->count].x = x;
        s->bars[s->count].y_top = y_top;
        s->bars[s->count].y_bottom = y_bottom;
    }
    s->count++;
}

int main(void)
{
    static uint16_t ring[RING];
    static uint16_t window[RING];
    static uint16_t mins[32];
    static uint16_t maxs[32];
    struct frame f;
    struct wave_range range = {1000U, 3000U};
    struct wave_geom geom = {GRID_TOP, GRID_HEIGHT};
    struct cal scale;
    struct sink sink;
    uint16_t cols;

    fill_square(ring, RING, 4U);

    /* --- frame: whole ring, trigger at the rising edge ------------------ */
    CHECK(frame_build(ring, RING, 0U, RING, 0U, window, RING, &f) == 0U);
    CHECK(f.length == RING);
    CHECK(f.stats.n == RING);
    CHECK(f.stats.min == 1000U);
    CHECK(f.stats.max == 3000U);
    CHECK(f.stats.vpp == 2000U);
    CHECK(f.stats.mean == 2000U);
    CHECK(f.stats.rms == 2236U);

    /* --- decimation must preserve both rails in every column ----------- */
    cols = wave_decimate(window, f.length, 8U, mins, maxs, 8U);
    CHECK(cols == 8U);
    for (uint16_t i = 0U; i < cols; i++) {
        CHECK(mins[i] == 1000U);
        CHECK(maxs[i] == 3000U);
    }

    /* --- bars: constant height, in order, inside the grid -------------- */
    sink.count = 0U;
    CHECK(render_columns(window, f.length, &range, &geom, 0U, 8U,
                         mins, maxs, record, &sink) == 8U);
    CHECK(sink.count == 8U);
    for (uint16_t i = 0U; i < sink.count; i++) {
        CHECK(sink.bars[i].x == i);
        CHECK(sink.bars[i].y_top == GRID_TOP);          /* 3000 -> top row */
        CHECK(sink.bars[i].y_bottom == GRID_TOP + GRID_HEIGHT - 1U); /* 1000 */
        CHECK(sink.bars[i].y_top < sink.bars[i].y_bottom);
    }

    /* --- calibration: two-point fit over the same values -------------- */
    CHECK(cal_fit(1000U, 0, 3000U, 2000, &scale) == 0U);
    CHECK(cal_raw_to_uv(1000U, &scale) == 0);
    CHECK(cal_raw_to_uv(3000U, &scale) == 2000);
    CHECK(cal_raw_to_uv(2000U, &scale) == 1000);
    CHECK(cal_uv_to_mv(cal_raw_to_uv(3000U, &scale)) == 2);
    CHECK(cal_uv_to_mv(cal_raw_to_uv(0U, &scale)) == -1);   /* -1000 uV */

    /* --- a window that wraps must give the same answer as a fresh one -- */
    {
        static uint16_t wrapped[RING];
        struct frame f2;

        /* Four whole laps later the DMA index is different but the samples
         * at those positions are the same: same window, same statistics. */
        CHECK(frame_build(ring, RING, 0U + 4U * RING, RING, 0U,
                          wrapped, RING, &f2) == 0U);
        CHECK(f2.win.start == f.win.start);
        CHECK(f2.stats.min == f.stats.min);
        CHECK(f2.stats.max == f.stats.max);
        CHECK(f2.stats.mean == f.stats.mean);
        CHECK(f2.stats.rms == f.stats.rms);
        for (uint16_t i = 0U; i < RING; i++) {
            CHECK(wrapped[i] == window[i]);
        }
    }

    /* --- determinism: same input, same output -------------------------- */
    {
        struct frame a;
        struct frame b;
        static uint16_t w2[RING];

        CHECK(frame_build(ring, RING, 7U, 32U, 16U, window, RING, &a) == 0U);
        CHECK(frame_build(ring, RING, 7U, 32U, 16U, w2, RING, &b) == 0U);
        CHECK(a.length == b.length);
        CHECK(a.stats.mean == b.stats.mean);
        CHECK(a.stats.rms == b.stats.rms);
        for (uint16_t i = 0U; i < a.length; i++) {
            CHECK(window[i] == w2[i]);
        }
    }

    if (failures == 0) {
        printf("pipeline: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
