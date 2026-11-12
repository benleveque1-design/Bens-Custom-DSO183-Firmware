/* Host unit tests for the calibration maths (app/cal.c). */
#include "cal.h"

#include <stdio.h>

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static void test_identity(void)
{
    CHECK(cal_identity.offset_uv == 0);
    CHECK(cal_identity.gain_q16 == 65536L);

    /* 1 count = 1 uV: the mapping is exact and saturating bounds unreachable. */
    CHECK(cal_raw_to_uv(0U, &cal_identity) == 0);
    CHECK(cal_raw_to_uv(1U, &cal_identity) == 1);
    CHECK(cal_raw_to_uv(2048U, &cal_identity) == 2048);
    CHECK(cal_raw_to_uv(4095U, &cal_identity) == 4095);

    /* A NULL calibration degrades to raw counts, never to a bogus voltage. */
    CHECK(cal_raw_to_uv(4095U, NULL) == 4095);
}

static void test_fit(void)
{
    struct cal c = { 123, 456 };

    /* Round trip through two points on a 1 uV/count line. */
    CHECK(cal_fit(0U, 0, 4095U, 4095, &c) == 0U);
    CHECK(c.gain_q16 == 65536L);
    CHECK(c.offset_uv == 0);
    CHECK(cal_raw_to_uv(1000U, &c) == 1000);

    /* 10 uV per count with an offset that makes raw 100 read as 0 V. */
    CHECK(cal_fit(100U, 0, 1100U, 10000, &c) == 0U);
    CHECK(c.gain_q16 == 655360L);
    CHECK(c.offset_uv == -1000);
    CHECK(cal_raw_to_uv(100U, &c) == 0);
    CHECK(cal_raw_to_uv(1100U, &c) == 10000);
    CHECK(cal_raw_to_uv(600U, &c) == 5000);

    /* An inverting input is legitimate (negative slope, positive offset). */
    CHECK(cal_fit(0U, 1000, 1000U, 0, &c) == 0U);
    CHECK(c.gain_q16 == -65536L);
    CHECK(cal_raw_to_uv(0U, &c) == 1000);
    CHECK(cal_raw_to_uv(1000U, &c) == 0);
    CHECK(cal_raw_to_uv(500U, &c) == 500);

    /* The fit must follow the points in either argument order. */
    CHECK(cal_fit(1000U, 0, 0U, 1000, &c) == 0U);
    CHECK(cal_raw_to_uv(0U, &c) == 1000);
    CHECK(cal_raw_to_uv(1000U, &c) == 0);
}

static void test_fit_rejections(void)
{
    struct cal c = { 7, 8 };

    CHECK(cal_fit(0U, 0, 0U, 1000, &c) == 1U);        /* zero span        */
    CHECK(cal_fit(0U, 0, 4096U, 1000, &c) == 1U);     /* raw out of range */
    CHECK(cal_fit(5000U, 0, 10U, 1000, &c) == 1U);
    CHECK(cal_fit(0U, 0, 100U, 1000, NULL) == 1U);
    CHECK(c.offset_uv == 7 && c.gain_q16 == 8);       /* untouched        */

    /* A slope so extreme it cannot fit a Q16 int32 is refused, not stored. */
    CHECK(cal_fit(0U, 0, 1U, 2000000, &c) == 1U);
    CHECK(c.offset_uv == 7 && c.gain_q16 == 8);
}

static void test_saturation(void)
{
    struct cal big = { 0, 65536L * 1000L };     /* 1 mV per count   */
    struct cal neg = { 0, -65536L * 1000L };    /* -1 mV per count  */

    CHECK(cal_raw_to_uv(4095U, &big) == 2000000L);    /* clamped, not wrapped */
    CHECK(cal_raw_to_uv(4095U, &neg) == -2000000L);

    /* Just inside the clamp the value passes through unchanged. */
    CHECK(cal_raw_to_uv(1999U, &big) == 1999000L);
    CHECK(cal_raw_to_uv(1999U, &neg) == -1999000L);
}

static void test_uv_to_mv(void)
{
    CHECK(cal_uv_to_mv(0) == 0);
    CHECK(cal_uv_to_mv(999) == 0);
    CHECK(cal_uv_to_mv(1000) == 1);
    CHECK(cal_uv_to_mv(1999) == 1);
    CHECK(cal_uv_to_mv(-999) == 0);
    CHECK(cal_uv_to_mv(-1000) == -1);
    CHECK(cal_uv_to_mv(-1999) == -1);
    CHECK(cal_uv_to_mv(2000000L) == 2000);
}

int main(void)
{
    test_identity();
    test_fit();
    test_fit_rejections();
    test_saturation();
    test_uv_to_mv();

    if (failures == 0) {
        printf("cal: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
