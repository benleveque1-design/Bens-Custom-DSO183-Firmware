/* Host unit tests for the waveform measurements (app/measure.c). */
#include "measure.h"

#include <math.h>
#include <stdio.h>

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static void test_isqrt_exact(void)
{
    /* Exact squares up to the 12-bit full scale, plus the boundaries. */
    CHECK(measure_isqrt(0U) == 0U);
    CHECK(measure_isqrt(1U) == 1U);
    CHECK(measure_isqrt(2U) == 1U);
    CHECK(measure_isqrt(3U) == 1U);
    CHECK(measure_isqrt(4U) == 2U);
    CHECK(measure_isqrt(15U) == 3U);
    CHECK(measure_isqrt(16U) == 4U);
    CHECK(measure_isqrt(4095U * 4095U) == 4095U);
    CHECK(measure_isqrt(4095U * 4095U - 1U) == 4094U);
    /* 32-bit top end: isqrt(0xFFFFFFFF) == 65535 */
    CHECK(measure_isqrt(0xFFFFFFFFUL) == 65535U);
}

static void test_flat(void)
{
    uint16_t s[8];
    struct measure_stats st;
    uint8_t i;

    for (i = 0U; i < 8U; i++) {
        s[i] = 1000U;
    }
    CHECK(measure_compute(s, 8U, &st) == 0U);
    CHECK(st.n == 8U);
    CHECK(st.min == 1000U);
    CHECK(st.max == 1000U);
    CHECK(st.vpp == 0U);
    CHECK(st.mean == 1000U);
    CHECK(st.rms == 1000U);
}

static void test_square(void)
{
    /* Symmetric square wave about 2048, full 12-bit swing. */
    uint16_t s[4] = {0U, 4095U, 0U, 4095U};
    struct measure_stats st;

    CHECK(measure_compute(s, 4U, &st) == 0U);
    CHECK(st.min == 0U);
    CHECK(st.max == 4095U);
    CHECK(st.vpp == 4095U);
    CHECK(st.mean == 2048U);            /* (8190 + 2) / 4, rounded */
    /* Exact RMS of {0,4095,0,4095} is 4095/sqrt(2) = 2895.6 -> 2895. */
    CHECK(st.rms == 2895U);
}

static void test_against_double(void)
{
    /* Pseudo-random but reproducible window; compare against a double
     * reference with a one-count tolerance (the integer path rounds the
     * mean square and then floors the root). */
    uint16_t s[256];
    struct measure_stats st;
    double sum = 0.0, sum_sq = 0.0;
    uint32_t x = 12345U;
    uint16_t i;

    for (i = 0U; i < 256U; i++) {
        x = x * 1103515245UL + 12345UL;
        s[i] = (uint16_t)((x >> 16) & 0x0FFFU);
        sum += s[i];
        sum_sq += (double)s[i] * (double)s[i];
    }
    CHECK(measure_compute(s, 256U, &st) == 0U);

    {
        double mean_ref = sum / 256.0;
        double rms_ref  = sqrt(sum_sq / 256.0);

        CHECK(fabs((double)st.mean - mean_ref) <= 1.0);
        CHECK(fabs((double)st.rms - rms_ref) <= 1.0);
    }
}

static void test_single_sample(void)
{
    uint16_t s[1] = {1234U};
    struct measure_stats st;

    CHECK(measure_compute(s, 1U, &st) == 0U);
    CHECK(st.mean == 1234U);
    CHECK(st.rms == 1234U);
    CHECK(st.vpp == 0U);
    CHECK(st.n == 1U);
}

static void test_rejections(void)
{
    uint16_t s[4] = {1U, 2U, 3U, 4U};
    struct measure_stats st;

    st.n = 99U;
    CHECK(measure_compute(NULL, 4U, &st) == 1U);
    CHECK(st.n == 0U);                  /* cleared, not left stale */
    CHECK(measure_compute(s, 0U, &st) == 1U);
    CHECK(st.n == 0U);
    CHECK(measure_compute(s, 4U, NULL) == 1U);
}


static void test_freq_duty_square(void)
{
    /* 8-sample period square wave, 50% duty, 1 ms/sample => 125 Hz. */
    uint16_t s[64];
    uint32_t f = 0;
    uint16_t d = 0;
    uint16_t i;
    for (i = 0U; i < 64U; i++) { s[i] = ((i % 8U) < 4U) ? 4000U : 100U; }
    CHECK(measure_freq_duty(s, 64U, 1000000U, &f, &d) == 0U);
    CHECK(f > 120U && f < 130U);
    CHECK(d > 450U && d < 550U);
}

static void test_freq_duty_flat_invalid(void)
{
    uint16_t s[32];
    uint32_t f = 99;
    uint16_t d = 99;
    uint16_t i;
    for (i = 0U; i < 32U; i++) { s[i] = 2000U; }
    CHECK(measure_freq_duty(s, 32U, 1000000U, &f, &d) == 0U);
    CHECK(f == 0U);
}

static void test_freq_duty_bad_args(void)
{
    uint16_t s[8] = {0,1,0,1,0,1,0,1};
    uint32_t f; uint16_t d;
    CHECK(measure_freq_duty(NULL, 8U, 1000U, &f, &d) == 1U);
    CHECK(measure_freq_duty(s, 8U, 1000U, NULL, &d) == 1U);
    CHECK(measure_freq_duty(s, 8U, 1000U, &f, NULL) == 1U);
}
int main(void)
{
    test_isqrt_exact();
    test_flat();
    test_square();
    test_against_double();
    test_single_sample();
    test_rejections();
    test_freq_duty_square();
    test_freq_duty_flat_invalid();
    test_freq_duty_bad_args();

    if (failures == 0) {
        printf("measure: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
