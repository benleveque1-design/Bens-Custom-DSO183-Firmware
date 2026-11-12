/* Host unit tests for the M5 acquisition statistics (app/acq_stats.c).
 * Build and run with:  make test
 */
#include <stdio.h>
#include <string.h>

#include "../app/acq_stats.h"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static void check_zeroed(const struct acq_stats *s, const char *tag)
{
    CHECK(s->smin == 0U);
    CHECK(s->smax == 0U);
    CHECK(s->min_index == 0U);
    CHECK(s->max_index == 0U);
    CHECK(s->mean_q8 == 0U);
    CHECK(s->clip_low == 0U);
    CHECK(s->clip_high == 0U);
    (void)tag;
}

static void test_offset_apply(void)
{
    /* no offset: identity inside the rails */
    CHECK(acq_offset_apply(0U, 0) == 0U);
    CHECK(acq_offset_apply(2048U, 0) == 2048U);
    CHECK(acq_offset_apply(4095U, 0) == 4095U);
    /* positive offset subtracts, and CLAMPS instead of wrapping */
    CHECK(acq_offset_apply(100U, 60) == 40U);
    CHECK(acq_offset_apply(10U, 60) == 0U);
    CHECK(acq_offset_apply(4095U, 1000) == 3095U);
    /* negative offset adds the same way */
    CHECK(acq_offset_apply(100U, -60) == 160U);
    CHECK(acq_offset_apply(4090U, -60) == 4095U);  /* clamped at the top rail */
}

static void test_flat_window(void)
{
    uint16_t buf[4] = { 1000U, 1000U, 1000U, 1000U };
    struct acq_stats s;

    acq_stats_compute(buf, 4U, 0, &s);
    CHECK(s.smin == 1000U);
    CHECK(s.smax == 1000U);
    CHECK(s.min_index == 0U);
    CHECK(s.max_index == 0U);
    CHECK(s.mean_q8 == (1000U << 8));   /* mean of a flat window is exact */
    CHECK(s.clip_low == 0U);
    CHECK(s.clip_high == 0U);
}

static void test_minmax_first_occurrence(void)
{
    uint16_t buf[8] = { 2000U, 100U, 3900U, 100U, 3900U, 2048U, 1U, 2U };
    struct acq_stats s;

    acq_stats_compute(buf, 8U, 0, &s);
    CHECK(s.smin == 1U);
    CHECK(s.smax == 3900U);
    CHECK(s.min_index == 6U);   /* the 1 appears only late - first index 6 */
    CHECK(s.max_index == 2U);   /* 3900 seen first at index 2, not 4 */
}

static void test_mean_q8(void)
{
    /* two samples 0 and 4095 -> Q8 mean = (0 + 4095) << 8 / 2 = 524160,
     * the .5 mean survives exactly in the Q8 fixed point */
    uint16_t buf[2] = { 0U, 4095U };
    struct acq_stats s;

    acq_stats_compute(buf, 2U, 0, &s);
    CHECK(s.mean_q8 == ((4095U << 8) / 2U));
    CHECK(s.smin == 0U);
    CHECK(s.smax == 4095U);
    CHECK(s.clip_low == 1U);
    CHECK(s.clip_high == 1U);
}

static void test_offset_changes_rails(void)
{
    /* offset subtracted past the bottom rail: everything clips low */
    uint16_t buf[2] = { 100U, 200U };
    struct acq_stats s;

    acq_stats_compute(buf, 2U, 500, &s);
    CHECK(s.smin == 0U);
    CHECK(s.smax == 0U);
    CHECK(s.clip_low == 2U);
    CHECK(s.clip_high == 0U);
    CHECK(s.mean_q8 == 0U);
    /* negative offset pushes the whole window up against the top rail */
    acq_stats_compute(buf, 2U, -6000, &s);
    CHECK(s.smin == 4095U);
    CHECK(s.smax == 4095U);
    CHECK(s.clip_high == 2U);
    CHECK(s.mean_q8 == (4095U << 8));
}

static void test_worst_case_no_overflow(void)
{
    /* every sample at full scale, maximum buffer size: the accumulation of
     * 1024 * 4095 << 8 = 1073725440 stays inside a uint32 (see header) */
    uint16_t buf[ACQ_STATS_MAX_N];
    struct acq_stats s;

    memset(buf, 0xFF, sizeof(buf));         /* every element 4095 */
    acq_stats_compute(buf, ACQ_STATS_MAX_N, 0, &s);
    CHECK(s.mean_q8 == (4095U << 8));
    CHECK(s.clip_high == ACQ_STATS_MAX_N);
    CHECK(s.smax == 4095U);
    CHECK(s.smin == 4095U);
}

static void test_degenerate_calls(void)
{
    struct acq_stats s;

    memset(&s, 0x55, sizeof(s));
    acq_stats_compute(NULL, 4U, 0, &s);
    check_zeroed(&s, "null samples");

    memset(&s, 0x55, sizeof(s));
    uint16_t buf[ACQ_STATS_MAX_N] = {0};
    acq_stats_compute(buf, 0U, 0, &s);
    check_zeroed(&s, "n == 0");

    memset(&s, 0x55, sizeof(s));
    acq_stats_compute(buf, ACQ_STATS_MAX_N + 1U, 0, &s);
    check_zeroed(&s, "n too large");

    /* the buffer must be untouched */
    for (uint32_t i = 0U; i < ACQ_STATS_MAX_N; i++) {
        CHECK(buf[i] == 0U);
    }
}

int main(void)
{
    test_offset_apply();
    test_flat_window();
    test_minmax_first_occurrence();
    test_mean_q8();
    test_offset_changes_rails();
    test_worst_case_no_overflow();
    test_degenerate_calls();
    if (failures == 0) {
        printf("acq_stats: all tests passed\n");
        return 0;
    }
    printf("acq_stats: %d failure(s)\n", failures);
    return 1;
}
