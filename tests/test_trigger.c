/* Host unit tests for the software trigger engine (app/trigger.c).
 *
 * Free of any HAL dependency; exercises the same source the firmware links
 * (see Makefile TEST_LIBS).  Property list follows the header contract.
 */
#include "trigger.h"
#include "frame.h"
#include "acq_window.h"

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

/* Regression characterization for the 50 us/div rising-edge misalignment
 * mechanism (B4.33 Stage A).  At 1.5625 us/sample a 1 kHz 50% duty signal
 * has 640 samples/cycle.  If a triggered capture is anchored to a mid-
 * plateau index (e.g. AWD latched while the signal was already above the
 * rising window), refinement cannot find the rising edge (it lies outside
 * the 176-sample search window) and placing the frame at that index
 * produces a high-plateau window with a falling crossing near index 287
 * and no rising crossing - matching the reported hardware symptom.
 * Refinement and frame placement must still behave correctly when given the
 * correct edge index. */
static int test_rising_50us_rearm_sensitive_scenario(void)
{
    enum { RING = 1024, WIN = 320, PRE = 96, PERIOD = 640, FALL = 320,
           BACK = 128, FWD = 48, HYST = 8 };
    static uint16_t ring[RING];
    static uint16_t dst[WIN];
    struct trig_cfg cfg = { .level = 970U, .hyst_counts = HYST,
                            .slope = TRIG_RISING };
    uint16_t search[BACK + FWD];
    uint32_t i, start, centre, rise_idx, hw_correct, hw_late;
    int32_t edge;
    struct frame f;
    uint8_t rc;

    for (i = 0U; i < RING; i++) {
        ring[i] = ((i % PERIOD) < (uint32_t)FALL) ? 881U : 1055U;
    }
    rise_idx = 320U; /* first sample at/after rising edge */
    hw_correct = rise_idx + 1U;      /* one past triggering sample */
    hw_late = rise_idx + 129U;       /* mid-plateau (spurious latch) */

    /* Correct HW anchor: refinement finds the rising edge. */
    start = (hw_correct + RING - BACK) % RING;
    for (i = 0U; i < (uint32_t)(BACK + FWD); i++) {
        search[i] = ring[(start + i) % RING];
    }
    centre = BACK;
    edge = trig_scan_nearest(search, (uint32_t)(BACK + FWD), centre, &cfg);
    CHECK(edge >= 0);
    CHECK(((start + (uint32_t)edge) % RING) == rise_idx);

    /* Mid-plateau anchor: no rising crossing in window -> refinement fails. */
    start = (hw_late + RING - BACK) % RING;
    for (i = 0U; i < (uint32_t)(BACK + FWD); i++) {
        search[i] = ring[(start + i) % RING];
    }
    edge = trig_scan_nearest(search, (uint32_t)(BACK + FWD), centre, &cfg);
    CHECK(edge == -1);
    {
        /* Simple rising-threshold fallback (as in trig_refine) also fails. */
        uint32_t best = 0U, best_dist = 0xFFFFFFFFUL;
        int32_t found_simple = -1;

        for (i = 1U; i < (uint32_t)(BACK + FWD); i++) {
            uint16_t p = search[i - 1U], c = search[i];

            if ((p < cfg.level) && (c >= cfg.level)) {
                uint32_t dist = (i >= centre) ? (i - centre) : (centre - i);

                if (dist < best_dist) {
                    best_dist = dist;
                    best = i;
                    found_simple = (int32_t)i;
                }
            }
        }
        (void)best;
        CHECK(found_simple == -1);
    }

    /* Falling back to the mid-plateau HW index reproduces the symptom:
     * no rising crossing, falling crossing at index 287. */
    rc = frame_build(ring, RING, hw_late, WIN, PRE, dst, WIN, &f);
    CHECK(rc == 0U);
    CHECK(f.win.trigger_pos == PRE);
    {
        uint8_t nr = 0U, nf = 0U;
        uint16_t nf_idx = 0U;

        for (i = 1U; i < WIN; i++) {
            if ((dst[i - 1U] < cfg.level) && (dst[i] >= cfg.level)) { nr++; }
            if ((dst[i - 1U] > cfg.level) && (dst[i] <= cfg.level)) {
                nf++;
                nf_idx = (uint16_t)i;
            }
        }
        CHECK(nr == 0U);
        CHECK(nf == 1U);
        CHECK(nf_idx == 287U);
        CHECK(dst[0] == 1055U);
        CHECK(dst[286] == 1055U);
        CHECK(dst[287] == 881U);
    }

    /* The campaign frame shows the same mechanism one sample later in phase
     * (fall at 286, implying a +130 latch): the local window still starts
     * after the true edge. */
    {
        static uint16_t dst2[WIN];
        struct frame f2;
        uint8_t nr2 = 0U, nf2 = 0U;
        uint16_t nf2_idx = 0U;

        rc = frame_build(ring, RING, rise_idx + 130U, WIN, PRE, dst2, WIN, &f2);
        CHECK(rc == 0U);
        for (i = 1U; i < WIN; i++) {
            if ((dst2[i - 1U] < cfg.level) && (dst2[i] >= cfg.level)) { nr2++; }
            if ((dst2[i - 1U] > cfg.level) && (dst2[i] <= cfg.level)) {
                nf2++;
                nf2_idx = (uint16_t)i;
            }
        }
        CHECK(nr2 == 0U);
        CHECK(nf2 == 1U);
        CHECK(nf2_idx == 286U);
    }

    /* A correctly rearmed rising edge remains detectable in a valid window. */
    start = (hw_correct + RING - BACK) % RING;
    for (i = 0U; i < (uint32_t)(BACK + FWD); i++) {
        search[i] = ring[(start + i) % RING];
    }
    edge = trig_scan_nearest(search, (uint32_t)(BACK + FWD), centre, &cfg);
    CHECK(edge == 127);
    return 0;
}

/* Ring-direct nearest scan: the anchoring module uses it so a search longer
 * than the old 512-entry scratch buffer (and one that crosses the ring wrap)
 * stays possible without extra RAM. */
static int test_ring_scan_nearest(void)
{
    enum { RING = 32 };
    static uint16_t ring[RING];
    struct trig_cfg rise = { .level = 500U, .hyst_counts = 20U,
                             .slope = TRIG_RISING };
    struct trig_cfg fall = { .level = 500U, .hyst_counts = 20U,
                             .slope = TRIG_FALLING };
    uint32_t i;
    int32_t e;

    /* Low for i%8 < 4, high otherwise: rising crossings at 4,12,20,28 and
     * falling crossings at 0,8,16,24. */
    for (i = 0U; i < RING; i++) {
        ring[i] = ((i % 8U) < 4U) ? 400U : 600U;
    }

    /* start=22, n=16 wraps: rising crossings at local 6 (ring 28) and
     * local 14 (ring 4); the centre decides which one wins. */
    e = trig_scan_ring_nearest(ring, RING, 22U, 16U, 8U, &rise);
    CHECK(e == 6);
    e = trig_scan_ring_nearest(ring, RING, 22U, 16U, 13U, &rise);
    CHECK(e == 14);

    /* Falling crossings in the same wrapped segment: ring 0 at local 10. */
    e = trig_scan_ring_nearest(ring, RING, 22U, 16U, 8U, &fall);
    CHECK(e == 10);

    /* start >= ring_size is reduced, not rejected. */
    e = trig_scan_ring_nearest(ring, RING, (uint16_t)(22U + RING), 16U, 8U,
                               &rise);
    CHECK(e == 6);

    /* No crossing at all. */
    for (i = 0U; i < RING; i++) {
        ring[i] = 600U;
    }
    CHECK(trig_scan_ring_nearest(ring, RING, 0U, RING, 10U, &rise) == -1);
    CHECK(trig_scan_ring_nearest(ring, RING, 0U, RING, 10U, &fall) == -1);

    /* Fail fast on bad arguments. */
    CHECK(trig_scan_ring_nearest(NULL, RING, 0U, 8U, 0U, &rise) == -1);
    CHECK(trig_scan_ring_nearest(ring, RING, 0U, 8U, 0U, NULL) == -1);
    CHECK(trig_scan_ring_nearest(ring, 0U, 0U, 8U, 0U, &rise) == -1);
    CHECK(trig_scan_ring_nearest(ring, RING, 0U, 1U, 0U, &rise) == -1);
    CHECK(trig_scan_ring_nearest(ring, RING, 0U, 8U, 0U,
                                 &(struct trig_cfg){500U, 20U, 3U}) == -1);
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
    failures += test_rising_50us_rearm_sensitive_scenario();
    failures += test_ring_scan_nearest();

    if (failures == 0) {
        printf("trigger: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
