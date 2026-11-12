/* Host unit tests for the bench report formatting (app/bench.c). */
#include "bench.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static void test_stats(void)
{
    struct frame f;
    char line[96];

    memset(&f, 0, sizeof f);
    f.length = 4U;
    f.stats.n = 4U;
    f.stats.min = 0U;
    f.stats.max = 4095U;
    f.stats.vpp = 4095U;
    f.stats.mean = 2048U;
    f.stats.rms = 2895U;

    CHECK(bench_format_stats(&f, 7U, 2U, 1U, line, sizeof line) == 0U);
    CHECK(strcmp(line,
                 "acq ok 7 to 2 err 1 | n 4 min 0 max 4095 vpp 4095"
                 " mean 2048 rms 2895\r\n") == 0);

    /* Big counters still print (no %d narrowing). */
    CHECK(bench_format_stats(&f, 4000000000UL, 0U, 12U, line, sizeof line) == 0U);
    CHECK(strstr(line, "acq ok 4000000000") == line);

    /* Rejections and truncation: never leave an unterminated buffer. */
    CHECK(bench_format_stats(&f, 0U, 0U, 0U, NULL, sizeof line) == 1U);
    CHECK(bench_format_stats(NULL, 0U, 0U, 0U, line, sizeof line) == 1U);
    CHECK(line[0] == '\0');
    CHECK(bench_format_stats(&f, 0U, 0U, 0U, line, 0U) == 1U);

    memset(line, 'X', sizeof line);
    CHECK(bench_format_stats(&f, 1U, 0U, 0U, line, 20U) == 1U);
    CHECK(memchr(line, '\0', sizeof line) != NULL);
}

static void test_samples(void)
{
    static const uint16_t four[4] = { 100U, 200U, 4095U, 0U };
    static const uint16_t one[1] = { 4095U };
    char line[64];

    CHECK(bench_format_samples(four, 4U, line, sizeof line) == 0U);
    CHECK(strcmp(line, "smp 100 200 4095 0\r\n") == 0);

    CHECK(bench_format_samples(one, 1U, line, sizeof line) == 0U);
    CHECK(strcmp(line, "smp 4095\r\n") == 0);

    /* A buffer too small for all numbers keeps only whole numbers. */
    CHECK(bench_format_samples(four, 4U, line, 12U) == 1U);
    CHECK(strncmp(line, "smp 100 200", 11) == 0);
    CHECK(strlen(line) < 12U);

    CHECK(bench_format_samples(NULL, 4U, line, sizeof line) == 1U);
    CHECK(bench_format_samples(four, 0U, line, sizeof line) == 1U);
    CHECK(bench_format_samples(four, 4U, NULL, sizeof line) == 1U);
    CHECK(bench_format_samples(four, 4U, line, 0U) == 1U);
}

int main(void)
{
    test_stats();
    test_samples();

    if (failures == 0) {
        printf("bench: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
