/* UI framework - DSO183 screen layout
 * Grid + trace + status bars, pure geometry, host-testable
 */
#ifndef UI_FRAME_H
#define UI_FRAME_H

#include <stdint.h>

#define UI_W 320
#define UI_H_PIX 240

/* Grid (legacy geometry kept for the pure-geometry tests). */
#define UI_GRID_DIVS_X 10
#define UI_GRID_DIVS_Y 8
#define UI_GRID_MARGIN_TOP 16
#define UI_GRID_MARGIN_BOTTOM 40
#define UI_GRID_MARGIN_LEFT 0
#define UI_GRID_MARGIN_RIGHT 0

#define UI_GRID_W (UI_W - UI_GRID_MARGIN_LEFT - UI_GRID_MARGIN_RIGHT)
#define UI_GRID_H (UI_H_PIX - UI_GRID_MARGIN_TOP - UI_GRID_MARGIN_BOTTOM)

/* Top bar: timebase/trigger info */
#define UI_TOP_H 16
/* Bottom bar: volts/div, mode, etc */
#define UI_BOT_H 40

/* ---- DSO183-UI-SPEC-001 geometry ------------------------------------
 * Header 0..16 (separator at 17), graticule 10..309 x 18..209
 * (300 x 192, 10 x 8 divisions), footer separator at 215 and two
 * 8-pixel text rows at 218 / 228. */
#define UI_HDR_H          17
#define UI_PLOT_X         10
#define UI_PLOT_Y         18
#define UI_PLOT_W         300
#define UI_PLOT_H         192
#define UI_PLOT_DIV_X     10
#define UI_PLOT_DIV_Y     8
#define UI_PLOT_PX_DIV_X  (UI_PLOT_W / UI_PLOT_DIV_X)   /* 30 */
#define UI_PLOT_PX_DIV_Y  (UI_PLOT_H / UI_PLOT_DIV_Y)   /* 24 */
#define UI_FOOT_SEP_Y     215
#define UI_FOOT_R1_Y      218
#define UI_FOOT_R2_Y      228
#define UI_SCREEN_END_Y   240

/* ---- DSO183-UI-SPEC-001 palette -------------------------------------
 * Plain RGB565 so this header stays host-testable (no display driver
 * include).  The panel runs standard RGB565 (MADCTL BGR=0). */
#define UI_RGB565(r, g, b)                                         \
    ((uint16_t)((((uint16_t)(r) & 0xF8u) << 8) |                   \
                (((uint16_t)(g) & 0xFCu) << 3) |                   \
                 (((uint16_t)(b) & 0xF8u) >> 3)))

#define UI_C_BG        UI_RGB565(6, 10, 16)
#define UI_C_HDR_BG    UI_RGB565(11, 19, 29)
#define UI_C_FTR_BG    UI_RGB565(11, 19, 29)
#define UI_C_CARD      UI_RGB565(14, 25, 38)
#define UI_C_GRID      UI_RGB565(21, 38, 58)
#define UI_C_GRID_HI   UI_RGB565(32, 60, 92)
#define UI_C_BORDER    UI_RGB565(43, 78, 115)
#define UI_C_TRACE     UI_RGB565(0, 229, 255)
#define UI_C_TEXT      UI_RGB565(226, 237, 248)
#define UI_C_MUTED     UI_RGB565(130, 154, 177)
#define UI_C_RUN_BG    UI_RGB565(0, 34, 61)
#define UI_C_HOLD_BG   UI_RGB565(61, 27, 0)
#define UI_C_HOLD_FG   UI_RGB565(255, 130, 0)
#define UI_C_FOCUS     UI_RGB565(255, 196, 0)
#define UI_C_TRIG      UI_RGB565(255, 184, 0)
#define UI_C_GREEN     UI_RGB565(0, 255, 100)

struct ui_rect {
    uint16_t x,y,w,h;
};

uint16_t ui_grid_x(uint8_t div);
uint16_t ui_grid_y(uint8_t div);
uint8_t ui_in_grid(uint16_t x, uint16_t y);

/* ---- pure display-measurement helpers (host-testable) ----------------
 * These mirror the counts-to-millivolt scaling used by the production UI.
 * They take explicit parameters so host tests can validate the math without
 * HAL/display state.  `span` is ADC counts across UI_PLOT_DIV_Y (8) divisions;
 * `mv_div` is millivolts per division.  Returns 0 for invalid span/mv_div. */
int32_t  ui_counts_to_mv_scale(int32_t counts, uint16_t span, uint16_t mv_div);

/* 0 V reference selection for display: use `stored_zero` when `use_zero!=0`
 * (a calibrated per-combo zero is present and AC coupling is not forcing the
 * mean), otherwise `mean`.  Mirrors production `ui_frame_ref`/AC handling. */
uint16_t ui_display_ref(uint16_t mean, uint16_t stored_zero, uint8_t use_zero,
                        uint8_t ac_coupled);

/* AV+ / AV- in millivolts: mean absolute deviation above / below `ref`.
 * Writes 0,0 for NULL/empty/GND-like `force_zero`.  Uses ui_counts_to_mv_scale. */
void ui_av_mv(const uint16_t *samples, uint16_t n, uint16_t ref,
              uint16_t span, uint16_t mv_div, uint8_t force_zero,
              int32_t *avp_mv, int32_t *avn_mv);

#endif /* UI_FRAME_H */
