#include "ui.h"
#include <stddef.h>

uint16_t ui_grid_x(uint8_t div)
{
    if (div >= UI_GRID_DIVS_X) div = UI_GRID_DIVS_X - 1;
    return (uint16_t)(UI_GRID_MARGIN_LEFT
                      + (uint16_t)div * (UI_GRID_W / UI_GRID_DIVS_X));
}

uint16_t ui_grid_y(uint8_t div)
{
    if (div >= UI_GRID_DIVS_Y) div = UI_GRID_DIVS_Y - 1;
    return (uint16_t)(UI_GRID_MARGIN_TOP
                      + (uint16_t)div * (UI_GRID_H / UI_GRID_DIVS_Y));
}

uint8_t ui_in_grid(uint16_t x, uint16_t y)
{
    int32_t gx = (int32_t)x - (int32_t)UI_GRID_MARGIN_LEFT;
    int32_t gy = (int32_t)y - (int32_t)UI_GRID_MARGIN_TOP;

    if (gx < 0 || gx >= (int32_t)UI_GRID_W) return 0;
    if (gy < 0 || gy >= (int32_t)UI_GRID_H) return 0;
    return 1;
}

int32_t ui_counts_to_mv_scale(int32_t counts, uint16_t span, uint16_t mv_div)
{
    int64_t mv;
    uint32_t s = span;
    uint32_t d = mv_div;

    if (s == 0U) { s = 4095U; }
    if (d == 0U) { d = 1U; }
    mv = ((int64_t)counts * (int64_t)d * (int64_t)UI_PLOT_DIV_Y) / (int64_t)s;
    if (mv > 2147483647LL) { mv = 2147483647LL; }
    if (mv < -2147483647LL - 1) { mv = -2147483647LL - 1; }
    return (int32_t)mv;
}

uint16_t ui_display_ref(uint16_t mean, uint16_t stored_zero, uint8_t use_zero,
                        uint8_t ac_coupled)
{
    if (ac_coupled != 0U) {
        return mean;
    }
    if (use_zero != 0U) {
        return stored_zero;
    }
    return mean;
}

void ui_av_mv(const uint16_t *samples, uint16_t n, uint16_t ref,
              uint16_t span, uint16_t mv_div, uint8_t force_zero,
              int32_t *avp_mv, int32_t *avn_mv)
{
    int64_t sum_p = 0, sum_n = 0;
    uint32_t cnt_p = 0U, cnt_n = 0U;
    uint16_t i;

    if (avp_mv != NULL) { *avp_mv = 0; }
    if (avn_mv != NULL) { *avn_mv = 0; }
    if ((samples == NULL) || (n == 0U) || (force_zero != 0U)) {
        return;
    }
    for (i = 0U; i < n; i++) {
        int32_t d = (int32_t)samples[i] - (int32_t)ref;
        if (d > 0) { sum_p += d; cnt_p++; }
        else if (d < 0) { sum_n += -d; cnt_n++; }
    }
    if (avp_mv != NULL) {
        *avp_mv = (cnt_p != 0U)
                ? ui_counts_to_mv_scale((int32_t)(sum_p / (int64_t)cnt_p), span, mv_div)
                : 0;
    }
    if (avn_mv != NULL) {
        *avn_mv = (cnt_n != 0U)
                ? -ui_counts_to_mv_scale((int32_t)(sum_n / (int64_t)cnt_n), span, mv_div)
                : 0;
    }
}
