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

static void test_display_ref(void)
{
    CHECK(ui_display_ref(2000U, 1800U, 1U, 0U) == 1800U);
    CHECK(ui_display_ref(2000U, 1800U, 0U, 0U) == 2000U);
    CHECK(ui_display_ref(2000U, 1800U, 1U, 1U) == 2000U); /* AC uses mean */
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
    test_display_ref();
    test_av_mv();
    if (failures == 0) {
        printf("ui_display_math: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
