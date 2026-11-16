/* Host tests for pure UI display-measurement helpers (app/ui.c). */
#include "ui.h"

#include <stdio.h>

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static void test_counts_to_mv_scale(void)
{
    /* 8 divisions over 4095 counts at 100 mV/div => ~0.195 mV/count. */
    CHECK(ui_counts_to_mv_scale(0, 4095U, 100U) == 0);
    CHECK(ui_counts_to_mv_scale(4095, 4095U, 100U) == 800);
    CHECK(ui_counts_to_mv_scale(-4095, 4095U, 100U) == -800);
    /* 1 V/div, 8 divs full scale */
    CHECK(ui_counts_to_mv_scale(2048, 4096U, 1000U) == 4000);
    /* invalid span/div fall back safely (0 span -> 4095, 0 mv/div -> 1 mV/div) */
    CHECK(ui_counts_to_mv_scale(4095, 0U, 100U) == 800);
    CHECK(ui_counts_to_mv_scale(8, 4095U, 0U) == 0); /* 1 mV/div, tiny count */
}

static int32_t nominal_mv(int32_t counts, uint16_t span, uint16_t mv_div)
{
    int64_t mv = ((int64_t)counts * (int64_t)mv_div * 8) / (int64_t)span;
    return (int32_t)mv;
}

static void test_nominal_all_combos(void)
{
    /* Nominal production path: s_span=424 (SCOPE_SPAN_COUNTS), 8 divisions,
     * mv/div per ladder combo = k_range_mv[r]*k_gain_x[g] (r*3+g). */
    static const uint16_t mv_divs[9] = {
        5000U, 2000U, 1000U, 500U, 200U, 100U, 50U, 20U, 10U
    };
    static const uint16_t span = 424U;
    uint8_t i;

    for (i = 0U; i < 9U; i++) {
        uint16_t mv = mv_divs[i];
        int32_t full = nominal_mv((int32_t)span, span, mv); /* 8 divs */
        int32_t one_div = nominal_mv((int32_t)(span / 8U), span, mv);
        int32_t neg_full = nominal_mv(-(int32_t)span, span, mv);

        CHECK(full == (int32_t)mv * 8);
        CHECK(one_div == (int32_t)mv);
        CHECK(neg_full == -((int32_t)mv * 8));
        CHECK(ui_counts_to_mv_scale((int32_t)span, span, mv) == full);
        CHECK(ui_counts_to_mv_scale((int32_t)(span / 8U), span, mv) == one_div);
        CHECK(ui_counts_to_mv_scale(-(int32_t)span, span, mv) == neg_full);
        CHECK(ui_counts_to_mv_scale(0, span, mv) == 0);

        /* Rounding/truncation boundaries around 1 mV (C trunc toward zero). */
        {
            int32_t c1 = (int32_t)((span + (uint16_t)(mv * 8U) - 1U) / (uint16_t)(mv * 8U)); /* ceil(span/(mv*8)) */
            int32_t c0 = c1 - 1;
            CHECK(ui_counts_to_mv_scale(c0, span, mv) == nominal_mv(c0, span, mv));
            CHECK(ui_counts_to_mv_scale(c1, span, mv) == nominal_mv(c1, span, mv));
            CHECK(ui_counts_to_mv_scale(-c1, span, mv) == nominal_mv(-c1, span, mv));
            CHECK(ui_counts_to_mv_scale(c0, span, mv) == 0 || ui_counts_to_mv_scale(c0, span, mv) == 1);
        }
    }
}

static void test_display_ref(void)
{
    /* DC/GND path (ac_coupled==0): use stored zero when present, else mean. */
    CHECK(ui_display_ref(2000U, 1800U, 1U, 0U) == 1800U);
    CHECK(ui_display_ref(2000U, 1800U, 0U, 0U) == 2000U);
    /* Explicitly stored zero value 0 must be used when present. */
    CHECK(ui_display_ref(2000U, 0U, 1U, 0U) == 0U);
    CHECK(ui_display_ref(2000U, 0U, 0U, 0U) == 2000U);
    /* AC uses mean regardless of stored zero/present flag. */
    CHECK(ui_display_ref(2000U, 1800U, 1U, 1U) == 2000U);
    CHECK(ui_display_ref(2000U, 0U, 1U, 1U) == 2000U);
    CHECK(ui_display_ref(2000U, 1800U, 0U, 1U) == 2000U);
}

static void test_av_mv(void)
{
    uint16_t s1[4] = { 100U, 100U, 300U, 300U };
    uint16_t flat[4] = { 200U, 200U, 200U, 200U };
    int32_t ap, an;

    ui_av_mv(s1, 4U, 200U, 4095U, 100U, 0U, &ap, &an);
    CHECK(ap > 0);
    CHECK(an < 0);
    CHECK(ap == -an); /* symmetric */

    ui_av_mv(flat, 4U, 200U, 4095U, 100U, 0U, &ap, &an);
    CHECK(ap == 0 && an == 0);

    ui_av_mv(s1, 4U, 200U, 4095U, 100U, 1U, &ap, &an); /* forced zero (GND) */
    CHECK(ap == 0 && an == 0);

    ui_av_mv(NULL, 4U, 200U, 4095U, 100U, 0U, &ap, &an);
    CHECK(ap == 0 && an == 0);
}

int main(void)
{
    test_counts_to_mv_scale();
    test_nominal_all_combos();
    test_display_ref();
    test_av_mv();
    if (failures == 0) {
        printf("ui_display_math: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
