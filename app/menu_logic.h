#ifndef MENU_LOGIC_H
#define MENU_LOGIC_H

#include <stdint.h>
#include "menu.h"

/* Pure menu transition/stepping helpers extracted for deterministic host
 * testing.  These functions encode only behaviour already established by
 * app/menu.c; callers remain responsible for side effects (acq/settings
 * application, persistence, UI state, timing). */
enum menu_state menu_logic_next_page(enum menu_state s);
uint8_t menu_logic_focus_next(uint8_t focus);
int32_t menu_logic_clamp32(int32_t v, int32_t lo, int32_t hi);
/* Wrap a stepped index in [0, count): -1 -> count-1, count -> 0. */
int32_t menu_logic_step_index_wrap(int32_t current, int dir, int32_t count);
/* Wrap a stepped value in [0, count) using modulo arithmetic. */
int32_t menu_logic_step_mod(int32_t current, int dir, int32_t count);
/* Clamp (current + dir*step) to [lo, hi]. */
int32_t menu_logic_step_clamped(int32_t current, int dir, int32_t step,
                                int32_t lo, int32_t hi);

/* Debounced persistence retry/notification policy (pure, deterministic).
 * fail_count is the number of consecutive failed save attempts in the
 * current episode (1-based for the first failure). */
#define MENU_PERSIST_BASE_MS      800U
#define MENU_PERSIST_BACKOFF_MAX_MS 30000U
#define MENU_PERSIST_NOTE_THROTTLE_MS 10000U

uint32_t menu_logic_persist_retry_delay_ms(uint8_t fail_count);
uint8_t menu_logic_persist_should_note_failure(uint32_t now, uint8_t fail_count,
                                               uint32_t last_note_ms);

#endif
