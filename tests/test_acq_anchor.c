/* Host unit tests for the edge-anchoring policy (app/acq_anchor.c).
 *
 * The B4.33/B4.34 hardware campaign showed the 50 us/div rising-edge failure:
 * the analog watchdog latched 129 samples after the true edge, the historic
 * 128-back refinement could not reach it, and the frame was published on the
 * mid-plateau index (requested edge off-screen, opposite transition at ~287).
 *
 * These tests build the same synthetic square wave on a 1024-sample ring and
 * prove that:
 *   - a healthy latch is anchored exactly as before (local pass, no recovery);
 *   - a late latch is recovered to the requested-slope edge;
 *   - both polarities work, including across the ring wrap;
 *   - the recovery bound is honoured and disabled when asked (ETS);
 *   - no edge means no anchor (so the caller can reject publication);
 *   - the effective span arithmetic matches acq_win_span_ok()'s contract.
 */
#include "acq_anchor.h"
#include "acq_window.h"
#include "frame.h"

#include <stdio.h>

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

#define RING 1024U
#define LOW  881U
#define HIGH 1055U
#define LEVEL 970U
#define HYST  8U

static uint16_t ring[RING];
static uint16_t dst[320];

/* Low for the first `duty` samples of each period, high afterwards: the
 * rising edge is at phase `duty`, the falling edge at phase 0. */
static void build_square(uint32_t period, uint32_t duty)
{
    uint32_t i;

    for (i = 0U; i < RING; i++) {
        ring[i] = ((i % period) < duty) ? LOW : HIGH;
    }
    for (i = 0U; i < (uint32_t)sizeof dst / sizeof dst[0]; i++) {
        dst[i] = 0U;
    }
}

static struct acq_anchor_cfg cfg_for(uint8_t slope, uint16_t recover_back)
{
    struct acq_anchor_cfg cfg;

    cfg.scan.level = LEVEL;
    cfg.scan.hyst_counts = HYST;
    cfg.scan.slope = slope;
    cfg.local_back = ACQ_ANCHOR_LOCAL_BACK;
    cfg.local_fwd = ACQ_ANCHOR_LOCAL_FWD;
    cfg.recover_back = recover_back;
    return cfg;
}

static void test_tb5_late_rising_latch(void)
{
    struct acq_anchor_cfg cfg = cfg_for(TRIG_RISING, 512U);
    struct acq_anchor a;
    struct frame f;
    uint16_t span;

    build_square(640U, 320U);          /* 50 us/div: rise at 320, fall at 0 */
    CHECK(acq_anchor_pick(ring, RING, 320U + 129U, &cfg, &a) == 1U);
    CHECK(a.found == 1U);
    CHECK(a.recovered == 1U);          /* outside the local pass */
    CHECK(a.index == 320U);
    CHECK(a.offset == -129);

    /* The recovered edge is still publishable at the measured tb5 span. */
    span = acq_anchor_effective_span(640U, a.offset);
    CHECK(span == 769U);
    CHECK(acq_win_span_ok(span, RING, 96U, 224U) == 1U);

    /* End to end: the published frame puts the rising edge on the trigger
     * column instead of showing a high plateau with a late fall. */
    CHECK(frame_build(ring, RING, a.index, 320U, 96U, dst, 320U, &f) == 0U);
    CHECK(f.win.trigger_pos == 96U);
    CHECK(dst[95] == LOW);
    CHECK(dst[96] == HIGH);
    CHECK(dst[0] == LOW);              /* before the edge: still the low half
                                        * of the previous period */

    /* The campaign's fall-at-286 frame implies a +130 latch; the same
     * recovery applies (the mechanism is not sensitive to the exact phase). */
    CHECK(acq_anchor_pick(ring, RING, 320U + 130U, &cfg, &a) == 1U);
    CHECK(a.found == 1U);
    CHECK(a.recovered == 1U);
    CHECK(a.index == 320U);
    CHECK(a.offset == -130);

    /* The single fall-at-285 frame in the B4.34 dataset implies +131. */
    CHECK(acq_anchor_pick(ring, RING, 320U + 131U, &cfg, &a) == 1U);
    CHECK(a.found == 1U);
    CHECK(a.recovered == 1U);
    CHECK(a.index == 320U);
    CHECK(a.offset == -131);
}

static void test_tb5_late_falling_latch(void)
{
    struct acq_anchor_cfg cfg = cfg_for(TRIG_FALLING, 512U);
    struct acq_anchor a;
    struct frame f;

    build_square(640U, 320U);          /* falling edge at phase 0 */
    CHECK(acq_anchor_pick(ring, RING, 129U, &cfg, &a) == 1U);
    CHECK(a.found == 1U);
    CHECK(a.recovered == 1U);
    CHECK(a.index == 0U);
    CHECK(a.offset == -129);

    CHECK(frame_build(ring, RING, a.index, 320U, 96U, dst, 320U, &f) == 0U);
    CHECK(dst[95] == HIGH);
    CHECK(dst[96] == LOW);
}

static void test_healthy_latch_is_unchanged(void)
{
    struct acq_anchor_cfg cfg = cfg_for(TRIG_RISING, 512U);
    struct acq_anchor a;

    build_square(640U, 320U);
    /* One sample after the crossing: the exact index the old refinement
     * produced. */
    CHECK(acq_anchor_pick(ring, RING, 320U + 1U, &cfg, &a) == 1U);
    CHECK(a.found == 1U);
    CHECK(a.recovered == 0U);
    CHECK(a.index == 320U);
    CHECK(a.offset == -1);

    /* Latch on the crossing itself. */
    CHECK(acq_anchor_pick(ring, RING, 320U, &cfg, &a) == 1U);
    CHECK(a.recovered == 0U);
    CHECK(a.index == 320U);
    CHECK(a.offset == 0);
}

static void test_recovery_bound_and_disable(void)
{
    struct acq_anchor_cfg cfg;
    struct acq_anchor a;

    build_square(640U, 320U);

    /* Too short to reach the edge 129 samples back: refuse, do not guess. */
    cfg = cfg_for(TRIG_RISING, 64U);
    CHECK(acq_anchor_pick(ring, RING, 449U, &cfg, &a) == 0U);
    CHECK(a.found == 0U);

    /* Recovery disabled (ETS: recover_back <= local_back). */
    cfg = cfg_for(TRIG_RISING, ACQ_ANCHOR_LOCAL_BACK);
    CHECK(acq_anchor_pick(ring, RING, 449U, &cfg, &a) == 0U);
    CHECK(a.found == 0U);
    cfg.recover_back = 0U;
    CHECK(acq_anchor_pick(ring, RING, 449U, &cfg, &a) == 0U);

    /* Just enough: 130 back reaches the edge at -129. */
    cfg = cfg_for(TRIG_RISING, 130U);
    CHECK(acq_anchor_pick(ring, RING, 449U, &cfg, &a) == 1U);
    CHECK(a.recovered == 1U);
    CHECK(a.index == 320U);
    CHECK(a.offset == -129);
}

static void test_no_edge_is_rejected(void)
{
    struct acq_anchor_cfg cfg = cfg_for(TRIG_RISING, 512U);
    struct acq_anchor a;
    uint32_t i;

    /* DC: no crossing of either slope. */
    for (i = 0U; i < RING; i++) {
        ring[i] = 1000U;
    }
    CHECK(acq_anchor_pick(ring, RING, 100U, &cfg, &a) == 0U);
    CHECK(a.found == 0U);

    /* Signal below the threshold: never crosses upwards. */
    for (i = 0U; i < RING; i++) {
        ring[i] = (uint16_t)(((i % 32U) < 16U) ? 400U : 600U);
    }
    cfg.scan.level = 900U;
    CHECK(acq_anchor_pick(ring, RING, 100U, &cfg, &a) == 0U);
    CHECK(a.found == 0U);
}

static void test_ring_wrap(void)
{
    struct acq_anchor_cfg cfg = cfg_for(TRIG_RISING, 512U);
    struct acq_anchor a;

    build_square(640U, 320U);          /* rising edges at 320 and 960 */
    /* Latch 129 samples after the edge at 960, i.e. across the wrap. */
    CHECK(acq_anchor_pick(ring, RING, (uint16_t)((960U + 129U) % RING),
                          &cfg, &a) == 1U);
    CHECK(a.found == 1U);
    CHECK(a.recovered == 1U);
    CHECK(a.index == 960U);
    CHECK(a.offset == -129);
}

static void test_other_timebases(void)
{
    struct acq_anchor_cfg cfg = cfg_for(TRIG_RISING, 512U);
    struct acq_anchor a;

    /* tb6: 320 samples/period, rise at 160.  A 100-sample-late latch is
     * still inside the local pass, so nothing is recovered. */
    build_square(320U, 160U);
    CHECK(acq_anchor_pick(ring, RING, 160U + 100U, &cfg, &a) == 1U);
    CHECK(a.index == 160U);
    CHECK(a.offset == -100);
    CHECK(a.recovered == 0U);

    /* tb9: 32 samples/period, rise at 16.  Several periods sit in the local
     * window; the crossing nearest the latch wins. */
    build_square(32U, 16U);
    CHECK(acq_anchor_pick(ring, RING, 16U + 5U, &cfg, &a) == 1U);
    CHECK(a.index == 16U);
    CHECK(a.offset == -5);
    CHECK(a.recovered == 0U);
}

static void test_effective_span(void)
{
    CHECK(acq_anchor_effective_span(640U, 0) == 640U);
    CHECK(acq_anchor_effective_span(640U, 1) == 639U);
    CHECK(acq_anchor_effective_span(640U, -129) == 769U);
    CHECK(acq_anchor_effective_span(0U, -5) == 5U);
    CHECK(acq_anchor_effective_span(5U, 10) == 0U);       /* clamped low  */
    CHECK(acq_anchor_effective_span(65535U, 0) == 65535U);
    CHECK(acq_anchor_effective_span(65535U, -1) == 65535U); /* clamped high */
}

static void test_invalid_arguments(void)
{
    struct acq_anchor_cfg cfg = cfg_for(TRIG_RISING, 512U);
    struct acq_anchor a;

    a.index = 0x1234U;
    a.offset = 0x5678;
    a.found = 0xAAU;
    a.recovered = 0xBBU;

    CHECK(acq_anchor_pick(NULL, RING, 0U, &cfg, &a) == 0U);
    CHECK(acq_anchor_pick(ring, RING, 0U, NULL, &a) == 0U);
    CHECK(acq_anchor_pick(ring, RING, 0U, &cfg, NULL) == 0U);
    CHECK(acq_anchor_pick(ring, 0U, 0U, &cfg, &a) == 0U);
    CHECK(acq_anchor_pick(ring, 1U, 0U, &cfg, &a) == 0U);
    CHECK(acq_anchor_pick(ring, RING, (uint16_t)RING, &cfg, &a) == 0U);
    CHECK(a.index == 0x1234U);         /* untouched on invalid input */
    CHECK(a.offset == 0x5678);
    CHECK(a.found == 0xAAU);
    CHECK(a.recovered == 0xBBU);
}

int main(void)
{
    test_tb5_late_rising_latch();
    test_tb5_late_falling_latch();
    test_healthy_latch_is_unchanged();
    test_recovery_bound_and_disable();
    test_no_edge_is_rejected();
    test_ring_wrap();
    test_other_timebases();
    test_effective_span();
    test_invalid_arguments();

    if (failures == 0) {
        printf("acq_anchor: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
