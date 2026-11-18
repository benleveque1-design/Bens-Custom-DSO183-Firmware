#ifndef MENU_H
#define MENU_H

#include <stdint.h>

/* Front-panel menu pages.  SEL cycles pages when the settings menu is open. */
enum menu_state {
    MENU_NONE = 0,
    MENU_TIMEBASE,
    MENU_TRIGGER_MODE,
    MENU_TRIGGER_LEVEL,
    MENU_TRIGGER_SLOPE,
    MENU_CAL_ZERO,
    MENU_CAL_OFFSET,
    MENU_CAL_GAIN,
    MENU_WAVE_SAVE,
    MENU_WAVE_LOAD,
    MENU_MEAS_MODE,
    MENU_CAL_RST,
    MENU_DEFAULTS,
    MENU_DISPLAY,
    MENU_SYS_DIAG
};

/* Quick-focus parameter on the main trace screen (SEL cycles, +/- adjusts). */
enum menu_focus {
    FOCUS_TIMEBASE = 0,
    FOCUS_TRIGGER_MODE,
    FOCUS_TRIGGER_LEVEL,
    FOCUS_TRIGGER_SLOPE,
    FOCUS_COUNT
};

struct menu {
    enum menu_state state;
    uint16_t button_down;      /* debounced pressed mask, for diagnostics  */
    uint8_t  focus;            /* menu_focus when state == MENU_NONE       */
    uint8_t  timebase_idx;
    uint8_t  trigger_mode;
    uint16_t trigger_level;
    uint8_t  trigger_slope;
    int32_t  cal_offset_uv;    /* microvolts at raw == 0                  */
    int32_t  cal_gain_q16;     /* microvolts per count, Q16               */
    uint8_t  combo_idx;        /* range * 3 + gain, 0..8                  */
    uint8_t  combo_valid;      /* 1 when the switches decode to a combo    */
    uint16_t frame_mean;       /* mean of the last frame, for zero capture */
    uint32_t last_activity_ms; /* last event, for the idle auto-close     */
    uint32_t last_change_ms;   /* last parameter change, for debounce     */
    uint32_t focus_ms;         /* last SEL press on the main screen       */
    uint8_t  coupling;         /* decoded CPL: 0 DC, 1 AC, 2 GND, 0xFF ?  */
    uint8_t  meas_mode;        /* 0 ALL, 1 AV, 2 CYCLE, 3 ALL DISP        */
    uint8_t  wave_slot;        /* selected waveform slot for save/load    */
    uint8_t  wave_req;         /* 1 save, 2 recall (consumed by main)     */
    uint8_t  disp_mode;        /* 0 = Display1 (scope), 1 = Display2 (big) */
    uint8_t  diag_open;        /* SYSTEM DIAG page is showing telemetry   */
    uint32_t r_sig;            /* painted-state signature (menu card)     */
    char     note[24];         /* transient status ("SAVED slot 0")       */
    uint32_t note_ms;
    uint8_t  dirty;            /* settings need saving                    */
    uint8_t  dirty_cal;        /* calibration needs saving                */
    uint8_t  drawn;            /* the settings card is currently painted  */
    /* Independent debounced persistence retry/notification state. */
    uint32_t settings_due_ms;  /* next eligible settings save attempt     */
    uint32_t cal_due_ms;       /* next eligible calibration save attempt  */
    uint8_t  settings_fail_count;
    uint8_t  cal_fail_count;
    uint32_t settings_note_ms; /* last throttled settings-fail note time  */
    uint32_t cal_note_ms;      /* last throttled cal-fail note time       */
    uint8_t  settings_failing; /* in a settings failure episode            */
    uint8_t  cal_failing;      /* in a calibration failure episode        */
};

void menu_init(struct menu *m);
void menu_poll(struct menu *m);
void menu_render(struct menu *m);

/* Mark settings persistence as needing a (debounced) save due to a user
 * change: resets any in-progress failure/backoff episode. */
void menu_settings_changed(struct menu *m, uint32_t now);
/* Mark calibration persistence as needing a (debounced) save due to a user
 * change: resets any in-progress failure/backoff episode. */
void menu_cal_changed(struct menu *m, uint32_t now);
/* Preserve dirty calibration state and schedule an independent retry after a
 * failed immediate save/reset attempt (bounded backoff). */
void menu_cal_retry(struct menu *m, uint32_t now);

/* Offer the current frame to the menu: the CAL ZERO page stores `mean` as the
 * zero level for `combo_idx` (range * 3 + gain).  The acquisition path calls
 * this once per frame; `combo_valid` is 0 when the switches decode to nothing,
 * in which case the zero page refuses to capture. */
void menu_frame_mean(struct menu *m, uint16_t mean, uint8_t combo_idx,
                     uint8_t combo_valid);

/* 1 ms sampler, called from SysTick_Handler.  Debounces the DSO183
 * momentary buttons (PB12=OK, PB13=+, PB14=-, PB15=SEL, active low) and
 * queues events; menu_poll() drains the queue in thread context. */
void menu_button_tick(void);

#endif /* MENU_H */