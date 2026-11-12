/* Host unit tests for the timebase table (app/timebase.c).
 *
 * The table is baked data; the tests verify the properties the acquisition
 * engine and the UI will rely on, using exact integers (no floating point):
 *   - 27 ascending 1-2-5 steps from 1 us/div to 500 s/div;
 *   - the baked (PSC, ARR) reproduces each step's tick within 0.5 timer
 *     count (= 15.625 ns);
 *   - single_shot flags follow the ADC conversion-time limit (84 counts);
 *   - the saturated accessors saturate instead of wrapping.
 * The test may use double: this is a host-side check, the module itself
 * stays integer-only (policy P-07).
 */
#include "timebase.h"

#include <stdio.h>
#include <math.h>

#define FAIL(msg) do { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); return 1; } while (0)
#define CHECK(cond) do { if (!(cond)) { FAIL(#cond); } } while (0)

static int test_shape(void)
{
    /* 1-2-5 sequence: div_us must step x2, x2.5, x5 continuously. */
    static const uint32_t expect[24] = {
        1u, 2u, 5u, 10u, 20u, 50u, 100u, 200u, 500u,
        1000u, 2000u, 5000u, 10000u, 20000u, 50000u, 100000u, 200000u, 500000u,
        1000000u, 2000000u, 5000000u, 10000000u, 20000000u, 50000000u
    };

    CHECK(timebase_entry(TIMEBASE_COUNT) == NULL);
    CHECK(timebase_entry(255U) == NULL);
    for (uint8_t i = 0U; i < TIMEBASE_COUNT; i++) {
        const struct timebase_step *st = timebase_entry(i);
        CHECK(st != NULL);
        CHECK(st->div_us == expect[i]);
        CHECK(((uint32_t)st->tick_psc + 1U) * ((uint32_t)st->tick_arr + 1U)
              <= 4294967295U);   /* (psc+1)*(arr+1) keeps u32 math safe */
    }
    return 0;
}

static int test_tick_accuracy(void)
{
    for (uint8_t i = 0U; i < TIMEBASE_COUNT; i++) {
        const struct timebase_step *st = timebase_entry(i);
        /* TIM3 is 64 MHz: 15.625 ns per count (see timebase.h). */
        double target_counts = (double)st->div_us * 1000.0 /*us->ns*/
                             / 32.0                        /*samples/div*/
                             / 15.625;                     /*ns/count   */
        double got_counts = ((double)st->tick_psc + 1.0)
                          * ((double)st->tick_arr + 1.0);
        double err = fabs(got_counts - target_counts);

        CHECK(err <= 0.5);   /* one timer count = 15.625 ns of error */
        CHECK(st->single_shot == ((got_counts >= 84.0) ? 1U : 0U));
    }
    /* spot checks (the 64 MHz table doubles the old 32 MHz counts) */
    CHECK(timebase_entry(5U)->tick_arr == 99U);        /* 50 us/div */
    CHECK(timebase_entry(14U)->tick_psc == 1U &&
          timebase_entry(14U)->tick_arr == 49999U);    /* 50 ms/div */
    CHECK(timebase_entry(23U)->tick_psc == 1599U &&
          timebase_entry(23U)->tick_arr == 62499U);    /* 50 s/div (max) */
    return 0;
}

static int test_clock(void)
{
    /* Guard the F1 clock-tree assumption that the whole table depends on:
     * APB1 is 32 MHz and TIM3 doubles to 64 MHz because the APB1 prescaler
     * is not 1 (RM0008).  If main.c's clock_config changes, this must fail. */
    CHECK(TIMEBASE_APB1_HZ == 32000000U);
    CHECK(TIMEBASE_TIM3_HZ == 64000000U);
    CHECK(TIMEBASE_COUNTS_PER_US == 64U);
    CHECK(TIMEBASE_MIN_SS_COUNTS == 84U);
    return 0;
}

static uint32_t counts_of(uint8_t i)
{
    const struct timebase_step *st = timebase_entry(i);

    return ((uint32_t)st->tick_psc + 1U) * ((uint32_t)st->tick_arr + 1U);
}

static int test_smp_policy(void)
{
    /* RM0008 sample times, in half ADC clocks. */
    CHECK(timebase_smp_cycles_x2(0U) == 3U);      /* 1.5 cycles   */
    CHECK(timebase_smp_cycles_x2(1U) == 15U);     /* 7.5 cycles   */
    CHECK(timebase_smp_cycles_x2(7U) == 479U);    /* 239.5 cycles */
    CHECK(timebase_smp_cycles_x2(8U) == 0U);      /* no such code */

    /* The fastest paced range (50 us/div = 100 TIM3 counts) only fits the
     * 1.5-cycle aperture: 3*(3+25) = 84 counts. */
    CHECK(timebase_entry(5U)->single_shot == 1U);
    CHECK(timebase_smp_code(5U) == 0U);

    /* 1 ms/div has 2000 counts, so the 7.5-cycle baseline fits there. */
    CHECK(timebase_smp_code(9U) == 1U);

    for (uint8_t i = 0U; i < TIMEBASE_COUNT; i++) {
        uint8_t code = timebase_smp_code(i);
        uint32_t fits = 3U * ((uint32_t)timebase_smp_cycles_x2(code) + 25U);

        CHECK(code <= 7U);
        if (timebase_entry(i)->single_shot == 0U) {
            CHECK(code == 0U);            /* REPT: no paced conversion */
        } else {
            /* the chosen aperture must fit inside the interval ... */
            CHECK(fits <= counts_of(i));
            /* ... and the baseline is preferred whenever it fits. */
            CHECK((code == TIMEBASE_SMP_BASELINE)
                  == ((3U * (15U + 25U)) <= counts_of(i)));
        }
    }
    return 0;
}

static int test_accessors(void)
{
    CHECK(timebase_window_us(9U) == 10000U);           /* 1 ms/div x10   */
    CHECK(timebase_window_us(18U) == 10000000U);       /* 1 s/div x10    */
    CHECK(timebase_window_us(23U) == 500000000U);      /* 50 s/div x10   */
    CHECK(timebase_window_us(TIMEBASE_COUNT) == 0U);   /* out of range   */

    CHECK(timebase_tick_ns(5U) == 1562U);              /* 50 us/div tick  */
    CHECK(timebase_tick_ns(15U) == 3125000U);          /* 100 ms/div tick*/
    CHECK(timebase_tick_ns(23U) == UINT32_MAX);        /* 50 s/div tick saturates */

    CHECK(timebase_tick_ns(TIMEBASE_COUNT) == 0U);
    return 0;
}

static int test_labels(void)
{
    CHECK(timebase_label(0U)[0] == '1');
    CHECK(timebase_label(6U)[1] == '.');
    CHECK(timebase_label(15U) != NULL);
    CHECK(timebase_label(23U)[0] == '5');             /* "50s"          */
    CHECK(timebase_label(TIMEBASE_COUNT)[0] == '?');
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_shape();
    failures += test_tick_accuracy();
    failures += test_clock();
    failures += test_smp_policy();
    failures += test_accessors();
    failures += test_labels();

    if (failures == 0) {
        printf("timebase: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
