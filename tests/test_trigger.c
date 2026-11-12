/* Host unit tests for the software trigger engine (app/trigger.c).
 *
 * Free of any HAL dependency; exercises the same source the firmware links
 * (see Makefile TEST_LIBS).  Property list follows the header contract.
 */
#include "trigger.h"

#include <stdio.h>

#define FAIL(msg) do { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); return 1; } while (0)
#define CHECK(cond) do { if (!(cond)) { FAIL(#cond); } } while (0)

static int test_rising_with_hysteresis(void)
{
    /* 2096 entries: long flat mid-band noise, then a clean rising edge. */
    static uint16_t s[2000];
    struct trig_cfg cfg = { .level = 2048U, .hyst_counts = 100U,
                            .slope = TRIG_RISING };
    for (uint32_t i = 0U; i < 1900U; i++) {
        s[i] = (uint16_t)(2040U + (uint16_t)(i & 7U));   /* noise below level */
    }
    s[1900] = 1900U;   /* dip: <= level - hyst = 1948 -> rearm */
    s[1901] = 2140U;   /* armed, above level, NOT >= 2148 yet  */
    s[1902] = 2160U;   /* >= level + hyst: trigger here        */
    s[1903] = 2300U;
    int32_t r = trig_scan(s, 2000U, &cfg);
    CHECK(r == 1902);
    return 0;
}

static int test_midband_noise_cannot_fire(void)
{
    /* Continuous noise around the level but never leaving the band: no
     * trigger - the analog watchdog WOULD have fired here repeatedly. */
    static uint16_t s[512];
    struct trig_cfg cfg = { .level = 2048U, .hyst_counts = 100U,
                            .slope = TRIG_RISING };
    for (uint32_t i = 0U; i < 512U; i++) {
        s[i] = (uint16_t)(1950U + (uint16_t)(i % 196U)); /* 1950..2145 */
    }
    CHECK(trig_scan(s, 512U, &cfg) == -1);
    return 0;
}

static int test_falling(void)
{
    static uint16_t s[600];
    struct trig_cfg cfg = { .level = 3000U, .hyst_counts = 50U,
                            .slope = TRIG_FALLING };
    for (uint32_t i = 0U; i < 500U; i++) {
        s[i] = 3500U;                       /* standing above the band */
    }
    s[500] = 3700U;    /* rearm: >= 3050 */
    s[501] = 3020U;    /* inside band */
    s[502] = 2900U;    /* <= 2950: trigger */
    CHECK(trig_scan(s, 600U, &cfg) == 502);
    return 0;
}

static int test_slopes_and_edges(void)
{
    uint16_t s[8] = { 500, 500, 4000, 4000, 100, 4000, 100, 100 };
    struct trig_cfg up   = { .level = 2048U, .hyst_counts = 0U,
                             .slope = TRIG_RISING };
    struct trig_cfg down = { .level = 2048U, .hyst_counts = 0U,
                             .slope = TRIG_FALLING };

    /* rising: first pair low->high is idx 2; falling: first high->low idx 4 */
    CHECK(trig_scan(s, 8U, &up) == 2);
    CHECK(trig_scan(s, 8U, &down) == 4);
    return 0;
}

static int test_fail_fast_cases(void)
{
    struct trig_cfg cfg = { .level = 2048U, .hyst_counts = 0U,
                            .slope = TRIG_RISING };
    uint16_t s[4] = { 0, 4095, 0, 4095 };

    CHECK(trig_scan(NULL, 4U, &cfg) == -1);
    CHECK(trig_scan(s, 4U, NULL) == -1);
    CHECK(trig_scan(s, 1U, &cfg) == -1);    /* need a pair */
    CHECK(trig_scan(s, 4U, &cfg) == 1);     /* clean edge at the start     */
    {
        struct trig_cfg bad = cfg;
        bad.slope = 7U;
        CHECK(trig_scan(s, 4U, &bad) == -1);
    }
    return 0;
}

static int test_level_clamped_at_rails(void)
{
    /* level near the rail with hysteresis must not wrap around. */
    static uint16_t s[100];
    struct trig_cfg cfg = { .level = 1U, .hyst_counts = 100U,
                            .slope = TRIG_RISING };
    for (uint32_t i = 0U; i < 50U; i++) {
        s[i] = 0U;                          /* rearm instantaneously */
    }
    s[50] = 50U;                            /* > level (1+100 satur=101? no:
                                             * cross = level+hyst = 101,
                                             * 50 < 101 so NOT yet) */
    s[60] = 150U;                           /* >= 101: fire */
    CHECK(trig_scan(s, 100U, &cfg) == 60);
    return 0;
}

static int test_nearest_picks_centre_crossing(void)
{
    /* Three rising edges (idx 10, 110, 210).  The first-crossing scan always
     * answers 10; the anchored scan must answer the one nearest centre. */
    static uint16_t s[300];
    struct trig_cfg cfg = { .level = 2048U, .hyst_counts = 50U,
                            .slope = TRIG_RISING };
    uint32_t i;

    for (i = 0U; i < 300U; i++) {
        s[i] = 1000U;
    }
    s[10] = 3000U;  s[11] = 3000U;  s[12] = 1000U;
    s[110] = 3000U; s[111] = 3000U; s[112] = 1000U;
    s[210] = 3000U; s[211] = 3000U; s[212] = 1000U;

    CHECK(trig_scan(s, 300U, &cfg) == 10);
    CHECK(trig_scan_nearest(s, 300U, 115U, &cfg) == 110);
    CHECK(trig_scan_nearest(s, 300U, 5U, &cfg) == 10);
    CHECK(trig_scan_nearest(s, 300U, 260U, &cfg) == 210);
    /* Equidistant between 10 and 110 (centre 60): first one wins. */
    CHECK(trig_scan_nearest(s, 300U, 60U, &cfg) == 10);
    return 0;
}

static int test_nearest_falling_and_fail_fast(void)
{
    static uint16_t s[200];
    struct trig_cfg cfg = { .level = 2000U, .hyst_counts = 40U,
                            .slope = TRIG_FALLING };
    uint32_t i;

    for (i = 0U; i < 200U; i++) {
        s[i] = 3500U;
    }
    s[40] = 1000U;  s[41] = 1000U;  s[42] = 3500U;
    s[140] = 1000U; s[141] = 1000U; s[142] = 3500U;

    CHECK(trig_scan_nearest(s, 200U, 138U, &cfg) == 140);
    CHECK(trig_scan_nearest(s, 200U, 10U, &cfg) == 40);

    CHECK(trig_scan_nearest(NULL, 200U, 0U, &cfg) == -1);
    CHECK(trig_scan_nearest(s, 200U, 0U, NULL) == -1);
    CHECK(trig_scan_nearest(s, 1U, 0U, &cfg) == -1);
    {
        struct trig_cfg bad = cfg;
        bad.slope = 9U;
        CHECK(trig_scan_nearest(s, 200U, 0U, &bad) == -1);
    }
    /* No crossing at all: -1 even though the buffer is non-empty. */
    for (i = 0U; i < 200U; i++) {
        s[i] = 2000U;
    }
    CHECK(trig_scan_nearest(s, 200U, 100U, &cfg) == -1);
    return 0;
}

static int test_fsm_modes(void)
{
    struct trig_fsm fsm;

    trig_fsm_init(&fsm, TRIG_MODE_AUTO);
    CHECK(trig_fsm_take(&fsm, 0U) == 1U);   /* AUTO: freewheeling ok  */
    CHECK(trig_fsm_take(&fsm, 1U) == 1U);

    trig_fsm_init(&fsm, TRIG_MODE_NORM);
    CHECK(trig_fsm_take(&fsm, 0U) == 0U);
    CHECK(trig_fsm_take(&fsm, 1U) == 1U);
    CHECK(trig_fsm_take(&fsm, 1U) == 1U);   /* NORM keeps accepting   */

    trig_fsm_init(&fsm, TRIG_MODE_SINGLE);
    CHECK(trig_fsm_take(&fsm, 1U) == 1U);   /* armed AND triggered    */
    CHECK(fsm.armed == 0U);
    CHECK(trig_fsm_take(&fsm, 1U) == 0U);   /* no longer armed        */
    CHECK(trig_fsm_take(&fsm, 0U) == 0U);
    trig_fsm_arm(&fsm);
    CHECK(fsm.armed == 1U);
    CHECK(trig_fsm_take(&fsm, 1U) == 1U);   /* second armed capture   */

    trig_fsm_init(&fsm, 99U);               /* out-of-range mode -> AUTO */
    CHECK(fsm.mode == TRIG_MODE_AUTO);
    return 0;
}

static int test_fsm_action(void)
{
    struct trig_fsm fsm;

    /* AUTO: a trigger captures, otherwise fall back only once the wait has
     * expired - never hold, because the screen must keep updating. */
    trig_fsm_init(&fsm, TRIG_MODE_AUTO);
    CHECK(trig_fsm_action(&fsm, 1U, 0U) == TRIG_ACTION_CAPTURE);
    CHECK(trig_fsm_action(&fsm, 0U, 1U) == TRIG_ACTION_FALLBACK);
    CHECK(trig_fsm_action(&fsm, 0U, 0U) == TRIG_ACTION_HOLD);

    /* NORM: capture on trigger, otherwise hold the last frame regardless of
     * how much time passed. */
    trig_fsm_init(&fsm, TRIG_MODE_NORM);
    CHECK(trig_fsm_action(&fsm, 1U, 0U) == TRIG_ACTION_CAPTURE);
    CHECK(trig_fsm_action(&fsm, 0U, 0U) == TRIG_ACTION_HOLD);
    CHECK(trig_fsm_action(&fsm, 0U, 1U) == TRIG_ACTION_HOLD);
    CHECK(fsm.armed == 1U);                 /* NORM never disarms */

    /* SINGLE: one capture, then hold until re-armed. */
    trig_fsm_init(&fsm, TRIG_MODE_SINGLE);
    CHECK(trig_fsm_action(&fsm, 0U, 1U) == TRIG_ACTION_HOLD);
    CHECK(fsm.armed == 1U);                 /* a miss keeps it armed */
    CHECK(trig_fsm_action(&fsm, 1U, 0U) == TRIG_ACTION_CAPTURE);
    CHECK(fsm.armed == 0U);
    CHECK(trig_fsm_action(&fsm, 1U, 0U) == TRIG_ACTION_HOLD);
    trig_fsm_arm(&fsm);
    CHECK(trig_fsm_action(&fsm, 1U, 0U) == TRIG_ACTION_CAPTURE);

    CHECK(trig_fsm_action(NULL, 1U, 0U) == TRIG_ACTION_HOLD);
    return 0;
}

static int test_window_start(void)
{
    CHECK(trig_window_start(-1, 320U, 1024U) == 0U);    /* no trigger    */
    CHECK(trig_window_start(0, 320U, 1024U) == 0U);     /* clamp low     */
    CHECK(trig_window_start(200, 320U, 1024U) == 104U); /* 200 - 96      */
    CHECK(trig_window_start(999, 320U, 1024U) == 704U); /* upper clamp:
                                                            1024 - 320     */
    CHECK(trig_window_start(200, 96U, 1024U) == 0U);    /* window smaller
                                                            than the
                                                            pre-trigger   */
    CHECK(trig_window_start(200, 0U, 1024U) == 0U);
    CHECK(trig_window_start(200, 320U, 200U) == 0U);    /* buffer too
                                                            small         */
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_rising_with_hysteresis();
    failures += test_midband_noise_cannot_fire();
    failures += test_falling();
    failures += test_slopes_and_edges();
    failures += test_fail_fast_cases();
    failures += test_level_clamped_at_rails();
    failures += test_nearest_picks_centre_crossing();
    failures += test_nearest_falling_and_fail_fast();
    failures += test_fsm_modes();
    failures += test_fsm_action();
    failures += test_window_start();

    if (failures == 0) {
        printf("trigger: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
