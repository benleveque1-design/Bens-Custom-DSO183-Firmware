/* Host unit tests for waveform geometry (app/waveform.c). */
#include "waveform.h"

#include <stdio.h>

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static uint16_t mins[64];
static uint16_t maxs[64];

static void test_decimate_even(void)
{
    uint16_t s[100];
    uint16_t i, got;

    for (i = 0; i < 100U; i++) {
        s[i] = (uint16_t)(100U + i);
    }
    got = wave_decimate(s, 100U, 10U, mins, maxs, 64U);
    CHECK(got == 10U);
    CHECK(mins[0] == 100U && maxs[0] == 109U);
    CHECK(mins[9] == 190U && maxs[9] == 199U);

    /* Short window: fewer samples than columns repeats, never reads past. */
    got = wave_decimate(s, 4U, 8U, mins, maxs, 64U);
    CHECK(got == 8U);
    for (i = 0; i < 8U; i++) {
        CHECK(mins[i] == maxs[i]);
        CHECK(mins[i] >= 100U && mins[i] <= 103U);
    }

    /* cap truncates instead of overflowing the caller's arrays. */
    got = wave_decimate(s, 100U, 40U, mins, maxs, 5U);
    CHECK(got == 5U);
}

static void test_decimate_spike(void)
{
    uint16_t s[16];
    uint16_t i, got;

    for (i = 0; i < 16U; i++) {
        s[i] = 200U;
    }
    s[7] = 4000U;                       /* one-sample spike */
    got = wave_decimate(s, 16U, 4U, mins, maxs, 64U);
    CHECK(got == 4U);
    /* The spike sits in column 1 (indices 4..7) and must survive as a max. */
    CHECK(maxs[1] == 4000U);
    CHECK(mins[1] == 200U);
    CHECK(maxs[0] == 200U && maxs[3] == 200U);
}

static void test_decimate_rejects(void)
{
    CHECK(wave_decimate(NULL, 10U, 2U, mins, maxs, 64U) == 0U);
    CHECK(wave_decimate(mins, 10U, 2U, NULL, maxs, 64U) == 0U);
    CHECK(wave_decimate(mins, 10U, 2U, mins, NULL, 64U) == 0U);
    CHECK(wave_decimate(mins, 0U, 2U, mins, maxs, 64U) == 0U);
    CHECK(wave_decimate(mins, 10U, 0U, mins, maxs, 64U) == 0U);
    CHECK(wave_decimate(mins, 10U, 2U, mins, maxs, 0U) == 0U);
}

static void test_y_mapping(void)
{
    const struct wave_geom g = { 16U, 184U };
    const struct wave_range r = { 0U, 4095U };

    CHECK(wave_y(0U, &r, &g) == 199U);            /* bottom row 16+183    */
    CHECK(wave_y(4095U, &r, &g) == 16U);          /* top row              */
    CHECK(wave_y(2048U, &r, &g) > 16U);
    CHECK(wave_y(2048U, &r, &g) < 199U);
    /* Mid-point must land within one pixel of the centre (107). */
    CHECK(wave_y(2047U, &r, &g) >= 106U);
    CHECK(wave_y(2047U, &r, &g) <= 108U);

    /* Clamp instead of wrapping outside the range. */
    CHECK(wave_y(5000U, &r, &g) == 16U);
    CHECK(wave_y(0U, &r, &g) == 199U);

    /* Empty/inverted range centres the trace, never divides by zero. */
    {
        const struct wave_range flat = { 800U, 800U };
        CHECK(wave_y(800U, &flat, &g) == 108U);
        CHECK(wave_y(0U, &flat, &g) == 108U);
    }

    /* Zero range window and a 1-pixel-high geometry must not fault. */
    CHECK(wave_y(123U, NULL, &g) == 108U);
    {
        const struct wave_geom tiny = { 40U, 1U };
        CHECK(wave_y(123U, &r, &tiny) == 40U);
    }
    CHECK(wave_y(123U, &r, NULL) == 0U);
}

int main(void)
{
    test_decimate_even();
    test_decimate_spike();
    test_decimate_rejects();
    test_y_mapping();

    {
        /* AC coupling: a 3000-mean ramp recentres on 2048 and keeps shape. */
        uint16_t buf[5] = { 2900U, 2950U, 3000U, 3050U, 3100U };
        uint16_t mean = wave_remove_mean(buf, 5U);

        CHECK(mean == 3000U);
        CHECK(buf[0] == 1948U);
        CHECK(buf[2] == 2048U);
        CHECK(buf[4] == 2148U);
        CHECK(wave_remove_mean(NULL, 5U) == 0U);
        CHECK(wave_remove_mean(buf, 0U) == 0U);
    }

    {
        uint16_t buf[8] = { 100u, 200u, 300u, 400u, 500u, 600u, 700u, 800u };
        uint16_t out[4] = { 0u, 0u, 0u, 0u };

        CHECK(wave_column_means(buf, 8u, 4u, out, 4u) == 4u);
        CHECK(out[0] == 150u && out[1] == 350u
              && out[2] == 550u && out[3] == 750u);
        CHECK(wave_column_means(buf, 8u, 4u, out, 2u) == 2u);   /* cap clamps */
        CHECK(wave_column_means(NULL, 8u, 4u, out, 4u) == 0u);
        CHECK(wave_column_means(buf, 0u, 4u, out, 4u) == 0u);
        CHECK(wave_column_means(buf, 8u, 0u, out, 4u) == 0u);
    }

    {
        /* Plot-width bound: cols/cap match UI_PLOT_W (300); output never
         * exceeds requested count and indices stay within cap. */
        uint16_t buf[256];
        uint16_t out[300];
        uint16_t i, got;
        for (i = 0u; i < 256u; i++) { buf[i] = (uint16_t)(1000u + i); }
        for (i = 0u; i < 300u; i++) { out[i] = 0xBEEFu; }
        got = wave_column_means(buf, 256u, 300u, out, 300u);
        CHECK(got == 300u);
        for (i = 0u; i < 300u; i++) {
            CHECK(out[i] >= 1000u && out[i] <= 1255u);
        }
    }

    if (failures == 0) {
        printf("waveform: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
