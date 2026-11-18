/* Host unit tests for extracted pure menu logic (app/menu_logic.c).
 *
 * These tests cover only deterministic transitions/stepping/clamping helpers
 * whose behaviour is directly established by the current implementation.
 * No hardware, HAL, display, storage, acquisition, timing or persistence
 * dependencies are exercised. */
#include "menu_logic.h"
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

static void test_page_transitions(void)
{
    /* Full settings-page cycle as implemented by menu_next/menu_logic_next_page. */
    CHECK(menu_logic_next_page(MENU_NONE) == MENU_TIMEBASE);
    CHECK(menu_logic_next_page(MENU_TIMEBASE) == MENU_TRIGGER_MODE);
    CHECK(menu_logic_next_page(MENU_TRIGGER_MODE) == MENU_TRIGGER_LEVEL);
    CHECK(menu_logic_next_page(MENU_TRIGGER_LEVEL) == MENU_TRIGGER_SLOPE);
    CHECK(menu_logic_next_page(MENU_TRIGGER_SLOPE) == MENU_CAL_ZERO);
    CHECK(menu_logic_next_page(MENU_CAL_ZERO) == MENU_CAL_OFFSET);
    CHECK(menu_logic_next_page(MENU_CAL_OFFSET) == MENU_CAL_GAIN);
    CHECK(menu_logic_next_page(MENU_CAL_GAIN) == MENU_WAVE_SAVE);
    CHECK(menu_logic_next_page(MENU_WAVE_SAVE) == MENU_WAVE_LOAD);
    CHECK(menu_logic_next_page(MENU_WAVE_LOAD) == MENU_MEAS_MODE);
    CHECK(menu_logic_next_page(MENU_MEAS_MODE) == MENU_CAL_RST);
    CHECK(menu_logic_next_page(MENU_CAL_RST) == MENU_DEFAULTS);
    CHECK(menu_logic_next_page(MENU_DEFAULTS) == MENU_DISPLAY);
    CHECK(menu_logic_next_page(MENU_DISPLAY) == MENU_SYS_DIAG);
    CHECK(menu_logic_next_page(MENU_SYS_DIAG) == MENU_TIMEBASE);

    /* Out-of-range/unknown state falls back to MENU_NONE (default case). */
    CHECK(menu_logic_next_page((enum menu_state)255) == MENU_NONE);
}

static void test_focus_transitions(void)
{
    CHECK(FOCUS_COUNT == 4U);
    CHECK(menu_logic_focus_next(FOCUS_TIMEBASE) == FOCUS_TRIGGER_MODE);
    CHECK(menu_logic_focus_next(FOCUS_TRIGGER_MODE) == FOCUS_TRIGGER_LEVEL);
    CHECK(menu_logic_focus_next(FOCUS_TRIGGER_LEVEL) == FOCUS_TRIGGER_SLOPE);
    CHECK(menu_logic_focus_next(FOCUS_TRIGGER_SLOPE) == FOCUS_TIMEBASE);
    /* Behaviour is defined as (focus + 1) % FOCUS_COUNT. */
    CHECK(menu_logic_focus_next(99U) == (uint8_t)((99U + 1U) % FOCUS_COUNT));
}

static void test_clamp32(void)
{
    CHECK(menu_logic_clamp32(5, 0, 10) == 5);
    CHECK(menu_logic_clamp32(-1, 0, 10) == 0);
    CHECK(menu_logic_clamp32(11, 0, 10) == 10);
    CHECK(menu_logic_clamp32(0, 0, 10) == 0);
    CHECK(menu_logic_clamp32(10, 0, 10) == 10);
    CHECK(menu_logic_clamp32(-200001, -200000, 200000) == -200000);
    CHECK(menu_logic_clamp32(200001, -200000, 200000) == 200000);
}

static void test_step_index_wrap(void)
{
    /* Timebase-like range [0, TIMEBASE_COUNT). */
    CHECK(TIMEBASE_COUNT > 1U);
    CHECK(menu_logic_step_index_wrap(0, 1, (int32_t)TIMEBASE_COUNT) == 1);
    CHECK(menu_logic_step_index_wrap(0, -1, (int32_t)TIMEBASE_COUNT)
          == (int32_t)TIMEBASE_COUNT - 1);
    CHECK(menu_logic_step_index_wrap((int32_t)TIMEBASE_COUNT - 1, 1,
                                     (int32_t)TIMEBASE_COUNT) == 0);
    CHECK(menu_logic_step_index_wrap((int32_t)TIMEBASE_COUNT - 1, -1,
                                     (int32_t)TIMEBASE_COUNT)
          == (int32_t)TIMEBASE_COUNT - 2);

    /* 2-entry range [0, 2) as used for two-slot wrap behaviour. */
    CHECK(menu_logic_step_index_wrap(0, 1, 2) == 1);
    CHECK(menu_logic_step_index_wrap(1, 1, 2) == 0);
    CHECK(menu_logic_step_index_wrap(0, -1, 2) == 1);
    CHECK(menu_logic_step_index_wrap(1, -1, 2) == 0);
}

static void test_step_mod(void)
{
    /* 3-way wraps used for trigger mode and trigger slope. */
    CHECK(menu_logic_step_mod(0, 1, 3) == 1);
    CHECK(menu_logic_step_mod(1, 1, 3) == 2);
    CHECK(menu_logic_step_mod(2, 1, 3) == 0);
    CHECK(menu_logic_step_mod(0, -1, 3) == 2);
    CHECK(menu_logic_step_mod(1, -1, 3) == 0);
    CHECK(menu_logic_step_mod(2, -1, 3) == 1);
}

static void test_step_clamped(void)
{
    /* Trigger level: step 64, [0, 4095]. */
    CHECK(menu_logic_step_clamped(2048, 1, 64, 0, 4095) == 2112);
    CHECK(menu_logic_step_clamped(2048, -1, 64, 0, 4095) == 1984);
    CHECK(menu_logic_step_clamped(0, -1, 64, 0, 4095) == 0);
    CHECK(menu_logic_step_clamped(10, -1, 64, 0, 4095) == 0);
    CHECK(menu_logic_step_clamped(4095, 1, 64, 0, 4095) == 4095);
    CHECK(menu_logic_step_clamped(4080, 1, 64, 0, 4095) == 4095);

    /* CAL offset: step 8, [-200000, 200000]. */
    CHECK(menu_logic_step_clamped(0, 1, 8, -200000, 200000) == 8);
    CHECK(menu_logic_step_clamped(0, -1, 8, -200000, 200000) == -8);
    CHECK(menu_logic_step_clamped(-200000, -1, 8, -200000, 200000) == -200000);
    CHECK(menu_logic_step_clamped(200000, 1, 8, -200000, 200000) == 200000);

    /* CAL gain: step 1024, [16384, 262144]. */
    CHECK(menu_logic_step_clamped(65536, 1, 1024, 16384, 262144) == 66560);
    CHECK(menu_logic_step_clamped(65536, -1, 1024, 16384, 262144) == 64512);
    CHECK(menu_logic_step_clamped(16384, -1, 1024, 16384, 262144) == 16384);
    CHECK(menu_logic_step_clamped(262144, 1, 1024, 16384, 262144) == 262144);
}

static void test_persist_retry_timing_and_backoff(void)
{
    CHECK(MENU_PERSIST_BASE_MS == 800U);
    CHECK(MENU_PERSIST_BACKOFF_MAX_MS == 30000U);
    CHECK(menu_logic_persist_retry_delay_ms(0U) == 800U);
    CHECK(menu_logic_persist_retry_delay_ms(1U) == 800U);
    CHECK(menu_logic_persist_retry_delay_ms(2U) == 1600U);
    CHECK(menu_logic_persist_retry_delay_ms(3U) == 3200U);
    CHECK(menu_logic_persist_retry_delay_ms(4U) == 6400U);
    CHECK(menu_logic_persist_retry_delay_ms(5U) == 12800U);
    CHECK(menu_logic_persist_retry_delay_ms(6U) == 25600U);
    CHECK(menu_logic_persist_retry_delay_ms(7U) == 30000U);
    CHECK(menu_logic_persist_retry_delay_ms(20U) == 30000U);
    CHECK(menu_logic_persist_retry_delay_ms(255U) == 30000U);
}

static void test_persist_notification_throttling(void)
{
    CHECK(MENU_PERSIST_NOTE_THROTTLE_MS == 10000U);
    CHECK(menu_logic_persist_should_note_failure(0U, 0U, 0U) == 0U);
    /* First failure in episode: immediate feedback. */
    CHECK(menu_logic_persist_should_note_failure(1000U, 1U, 0U) == 1U);
    /* Subsequent failures: throttled. */
    CHECK(menu_logic_persist_should_note_failure(5000U, 2U, 1000U) == 0U);
    CHECK(menu_logic_persist_should_note_failure(10999U, 2U, 1000U) == 0U);
    CHECK(menu_logic_persist_should_note_failure(11000U, 2U, 1000U) == 1U);
    CHECK(menu_logic_persist_should_note_failure(25000U, 5U, 15000U) == 1U);
}

struct pdom {
    uint8_t  dirty;
    uint8_t  fail_count;
    uint8_t  failing;
    uint32_t due_ms;
    uint32_t note_ms;
    uint8_t  notes;
    uint8_t  recovery_notes;
};

static void pdom_mark_dirty(struct pdom *d, uint32_t now)
{
    d->dirty = 1U;
    d->fail_count = 0U;
    d->failing = 0U;
    d->note_ms = 0U;
    d->due_ms = now + MENU_PERSIST_BASE_MS;
}

static void pdom_attempt(struct pdom *d, uint32_t now, int ok)
{
    if ((d->dirty == 0U) || ((int32_t)(now - d->due_ms) < 0)) {
        return;
    }
    if (ok != 0) {
        if (d->failing != 0U) {
            d->recovery_notes++;
        }
        d->dirty = 0U;
        d->fail_count = 0U;
        d->failing = 0U;
        d->note_ms = 0U;
    } else {
        if (d->fail_count < 255U) {
            d->fail_count++;
        }
        if (menu_logic_persist_should_note_failure(now, d->fail_count, d->note_ms) != 0U) {
            d->notes++;
            d->note_ms = now;
        }
        d->failing = 1U;
        d->due_ms = now + menu_logic_persist_retry_delay_ms(d->fail_count);
    }
}

static void test_persist_recovery_and_dirty_preservation(void)
{
    struct pdom d = {0};

    /* New change -> debounce 800ms, silent on normal success. */
    pdom_mark_dirty(&d, 1000U);
    CHECK(d.dirty == 1U);
    CHECK(d.due_ms == 1800U);
    pdom_attempt(&d, 1799U, 1);
    CHECK(d.dirty == 1U);
    pdom_attempt(&d, 1800U, 1);
    CHECK(d.dirty == 0U);
    CHECK(d.failing == 0U);
    CHECK(d.fail_count == 0U);
    CHECK(d.notes == 0U);
    CHECK(d.recovery_notes == 0U);

    /* Repeated failures: dirty preserved, bounded backoff, throttled notes. */
    pdom_mark_dirty(&d, 2000U);
    pdom_attempt(&d, 2800U, 0); /* fail #1 */
    CHECK(d.dirty == 1U);
    CHECK(d.failing == 1U);
    CHECK(d.fail_count == 1U);
    CHECK(d.notes == 1U);
    CHECK(d.due_ms == 3600U);
    pdom_attempt(&d, 3600U, 0); /* fail #2 */
    CHECK(d.fail_count == 2U);
    CHECK(d.due_ms == 5200U); /* +1600 */
    CHECK(d.notes == 1U);      /* throttled */
    pdom_attempt(&d, 15200U, 0); /* simulate later attempt at throttled time */
    /* fail #3 at 15200: note allowed (>=10000 since 2800) */
    CHECK(d.fail_count == 3U);
    CHECK(d.notes == 2U);
    CHECK(d.due_ms == 18400U); /* +3200 */

    /* Recovery after failures: success clears state and reports once. */
    pdom_attempt(&d, 18400U, 1);
    CHECK(d.dirty == 0U);
    CHECK(d.failing == 0U);
    CHECK(d.fail_count == 0U);
    CHECK(d.recovery_notes == 1U);

    /* Max backoff cap reached. */
    pdom_mark_dirty(&d, 30000U);
    {
        uint32_t t = 30800U;
        int i;
        for (i = 0; i < 8; i++) {
            pdom_attempt(&d, t, 0);
            t = d.due_ms;
        }
        CHECK(d.fail_count == 8U);
        CHECK(menu_logic_persist_retry_delay_ms(d.fail_count) == 30000U);
        CHECK(d.due_ms == t);
        /* next interval remains capped */
        pdom_attempt(&d, t, 0);
        CHECK(d.fail_count == 9U);
        CHECK((d.due_ms - t) == 30000U);
    }
}

static void test_persist_domains_independent(void)
{
    struct pdom settings = {0};
    struct pdom cal = {0};

    pdom_mark_dirty(&settings, 1000U);
    pdom_mark_dirty(&cal, 1200U);
    /* Cal fails repeatedly, settings succeeds independently. */
    pdom_attempt(&cal, 2000U, 0);       /* cal fail #1 */
    pdom_attempt(&settings, 1800U, 1);  /* settings ok */
    CHECK(settings.dirty == 0U);
    CHECK(settings.failing == 0U);
    CHECK(cal.dirty == 1U);
    CHECK(cal.failing == 1U);
    CHECK(cal.fail_count == 1U);
    CHECK(cal.due_ms == 2800U);         /* 2000 + 800, unaffected by settings */
    /* Settings changed again later: independent fresh episode. */
    pdom_mark_dirty(&settings, 2500U);
    CHECK(settings.due_ms == 3300U);
    CHECK(cal.due_ms == 2800U);
    CHECK(cal.fail_count == 1U);
    pdom_attempt(&cal, 2800U, 0);       /* cal fail #2 backs off */
    CHECK(cal.fail_count == 2U);
    CHECK(cal.due_ms == 4400U);
    CHECK(settings.dirty == 1U);        /* still pending independently */
}

int main(void)
{
    test_page_transitions();
    test_focus_transitions();
    test_clamp32();
    test_step_index_wrap();
    test_step_mod();
    test_step_clamped();
    test_persist_retry_timing_and_backoff();
    test_persist_notification_throttling();
    test_persist_recovery_and_dirty_preservation();
    test_persist_domains_independent();

    if (failures != 0) {
        printf("test_menu: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_menu: all tests passed\n");
    return 0;
}
