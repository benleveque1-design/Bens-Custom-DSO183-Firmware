#include "menu_logic.h"

enum menu_state menu_logic_next_page(enum menu_state s)
{
    switch (s) {
    case MENU_NONE:           return MENU_TIMEBASE;
    case MENU_TIMEBASE:       return MENU_TRIGGER_MODE;
    case MENU_TRIGGER_MODE:   return MENU_TRIGGER_LEVEL;
    case MENU_TRIGGER_LEVEL:  return MENU_TRIGGER_SLOPE;
    case MENU_TRIGGER_SLOPE:  return MENU_CAL_ZERO;
    case MENU_CAL_ZERO:       return MENU_CAL_OFFSET;
    case MENU_CAL_OFFSET:     return MENU_CAL_GAIN;
    case MENU_CAL_GAIN:       return MENU_WAVE_SAVE;
    case MENU_WAVE_SAVE:      return MENU_WAVE_LOAD;
    case MENU_WAVE_LOAD:      return MENU_MEAS_MODE;
    case MENU_MEAS_MODE:      return MENU_CAL_RST;
    case MENU_CAL_RST:        return MENU_DEFAULTS;
    case MENU_DEFAULTS:       return MENU_DISPLAY;
    case MENU_DISPLAY:        return MENU_SYS_DIAG;
    case MENU_SYS_DIAG:       return MENU_TIMEBASE;
    default:                  return MENU_NONE;
    }
}

uint8_t menu_logic_focus_next(uint8_t focus)
{
    return (uint8_t)((focus + 1U) % FOCUS_COUNT);
}

int32_t menu_logic_clamp32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int32_t menu_logic_step_index_wrap(int32_t current, int dir, int32_t count)
{
    int32_t idx = current + dir;

    if (idx < 0) {
        idx = count - 1;
    } else if (idx >= count) {
        idx = 0;
    }
    return idx;
}

int32_t menu_logic_step_mod(int32_t current, int dir, int32_t count)
{
    int32_t v = (current + dir) % count;

    if (v < 0) {
        v += count;
    }
    return v;
}

int32_t menu_logic_step_clamped(int32_t current, int dir, int32_t step,
                                int32_t lo, int32_t hi)
{
    return menu_logic_clamp32(current + (dir * step), lo, hi);
}
