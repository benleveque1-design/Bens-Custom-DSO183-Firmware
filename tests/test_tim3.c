/* Host unit tests for the TIM3 pacing configuration (app/tim3.c). */
#include "tim3.h"
#include "timebase.h"

#include <stdio.h>

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static uint32_t table_counts(uint8_t i)
{
    const struct timebase_step *st = timebase_entry(i);
    return ((uint32_t)st->tick_psc + 1U) * ((uint32_t)st->tick_arr + 1U);
}

static void test_config_values(void)
{
    struct tim3_config cfg = {0xFFFFu, 0xFFFFu};

    CHECK(tim3_config_for(0U, &cfg) == 0U);
    CHECK(cfg.psc == 0U && cfg.arr == 1U);
    CHECK(tim3_counts(&cfg) == 2U);          /* 1 us/div, REPT */

    CHECK(tim3_config_for(5U, &cfg) == 0U);
    CHECK(cfg.psc == 0U && cfg.arr == 99U);  /* 50 us/div, 100 counts */
    CHECK(tim3_counts(&cfg) == 100U);

    CHECK(tim3_config_for(14U, &cfg) == 0U);
    CHECK(cfg.psc == 1U && cfg.arr == 49999U);

    CHECK(tim3_config_for(23U, &cfg) == 0U);
    CHECK(cfg.psc == 1599U && cfg.arr == 62499U);   /* 50 s/div (max) */
    CHECK(tim3_config_for(24U, &cfg) == 1U);
    CHECK(cfg.psc == 1599U && cfg.arr == 62499U);   /* untouched on reject */
}

static void test_rejects_bad_input(void)
{
    struct tim3_config cfg = {0x1234u, 0x5678u};

    CHECK(tim3_config_for(TIMEBASE_COUNT, &cfg) == 1U);
    CHECK(cfg.psc == 0x1234u && cfg.arr == 0x5678u);   /* untouched */
    CHECK(tim3_config_for(255U, &cfg) == 1U);
    CHECK(tim3_config_for(0U, NULL) == 1U);
    CHECK(tim3_counts(NULL) == 0U);
}

static void test_matches_table(void)
{
    uint8_t i;

    for (i = 0U; i < TIMEBASE_COUNT; i++) {
        struct tim3_config cfg;

        CHECK(tim3_config_for(i, &cfg) == 0U);
        CHECK(tim3_counts(&cfg) == table_counts(i));
        /* Register width contract: both values must fit the 16-bit PSC/ARR. */
        CHECK(cfg.psc <= 0xFFFFu);
        CHECK(cfg.arr <= 0xFFFFu);
    }
}

int main(void)
{
    test_config_values();
    test_rejects_bad_input();
    test_matches_table();

    if (failures == 0) {
        printf("tim3: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
