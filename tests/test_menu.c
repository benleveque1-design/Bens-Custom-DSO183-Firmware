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

int main(void)
{
    test_page_transitions();
    test_focus_transitions();
    test_clamp32();
    test_step_index_wrap();
    test_step_mod();
    test_step_clamped();

    if (failures != 0) {
        printf("test_menu: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_menu: all tests passed\n");
    return 0;
}
