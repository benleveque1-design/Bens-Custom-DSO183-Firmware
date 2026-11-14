/* Front-panel menu: DSO183 momentary buttons BTN1-BTN4, sampled in SysTick.
 *
 * DSO183 "How The Circuits Work": "Buttons BTN1-BTN4 are directly connected to
 * pins PB12-PB15 of the microcontroller ... internal pull-up ... pressed = low."
 * Physical order top-to-bottom (user-confirmed): OK, +, -, SEL.
 * Mapping used here: PB12=OK, PB13=+, PB14=-, PB15=SEL (active low).
 *
 * Main trace screen (MENU_NONE):
 *   SEL tap   - cycle focused parameter (Timebase, Trigger mode, Level)
 *   SEL hold  - enter settings/calibration menu
 *   + / -     - adjust focused parameter (hold to repeat)
 *   OK        - toggle RUN / HOLD
 *
 * Settings menu open:
 *   SEL tap   - next page (wraps through CAL pages to NONE)
 *   SEL hold  - close menu
 *   + / -     - adjust current page (hold to repeat); - steps backward
 *   OK        - page action / step forward (CAL ZERO: capture)
 *   idle 20 s - close menu
 *
 * Persistence is debounced (MENU_SAVE_MS). */
#include "menu.h"

extern uint8_t s_trig_slope;
#include "board.h"
#include "acq.h"
#include "timebase.h"
#include "trigger.h"
#include "settings.h"
#include "cal_store.h"
#include "cal.h"
#include "cal_table.h"
#include "wave_store.h"
#include "runstate.h"
#include "ili9341.h"
#include "ui.h"
#include "status.h"

#include "stm32f1xx_hal.h"
#include <stdio.h>
#include <string.h>

/* DSO183 momentary buttons, GPIOB, active low (pressed => IDR bit = 0). */
#define BTN_OK_PIN     GPIO_PIN_12
#define BTN_PLUS_PIN   GPIO_PIN_13
#define BTN_MINUS_PIN  GPIO_PIN_14
#define BTN_SEL_PIN    GPIO_PIN_15
#define BTN_ALL_MASK   (BTN_OK_PIN | BTN_PLUS_PIN | BTN_MINUS_PIN | BTN_SEL_PIN)

#define BTN_DEBOUNCE_MS   20U
#define BTN_HOLD_MS      700U
#define BTN_REPEAT_MS    180U   /* +/- repeat rate while held */
#define BTN_REPEAT_FIRST 400U   /* delay before first +/- repeat */
#define MENU_IDLE_MS  120000U  /* 2 min: keep settings/diag readable (was 20s) */
#define MENU_SAVE_MS     800U
#define NOTE_MS         2500U

/* Event types (queued from SysTick, drained in thread context). */
#define EV_SEL_PRESS   1U
#define EV_SEL_HOLD    2U
#define EV_PLUS        3U
#define EV_MINUS       4U
#define EV_OK          5U
#define EVQ_LEN       16U

static volatile uint8_t  s_ev_type[EVQ_LEN];
static volatile uint32_t s_ev_ms[EVQ_LEN];
static volatile uint8_t  s_ev_head;
static volatile uint8_t  s_ev_tail;

static uint8_t  s_btn_ready;
static uint16_t s_btn_raw;         /* last raw pressed mask (active-high in mask) */
static uint16_t s_btn_stable;      /* debounced pressed mask */
static uint32_t s_btn_change_ms;
static uint32_t s_btn_press_ms[4]; /* per button: OK,PLUS,MINUS,SEL */
static uint32_t s_btn_repeat_ms[4];
static uint8_t  s_btn_long[4];     /* hold/repeat state per button */

static void menu_config_buttons(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /* PB12-15 are inputs (ili9341 CRH sets CNF=10/MODE=00). Ensure pull-ups:
     * for input pull-up/down mode, ODR=1 selects pull-up. */
    gpio.Pin = BTN_ALL_MASK;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);
    GPIOB->BSRR = BTN_ALL_MASK;  /* explicit pull-up */
}

/* Returns pressed buttons as a mask of BTN_*_PIN bits (active low on pin). */
static uint16_t menu_buttons_pressed_mask(void)
{
    uint16_t idr = (uint16_t)GPIOB->IDR;
    return (uint16_t)((~idr) & (uint16_t)BTN_ALL_MASK);
}

static void ev_push(uint8_t type)
{
    uint8_t next = (uint8_t)((s_ev_head + 1U) % EVQ_LEN);

    if (next == s_ev_tail) {
        return;                 /* queue full: drop the newest event */
    }
    s_ev_type[s_ev_head] = type;
    s_ev_ms[s_ev_head] = HAL_GetTick();
    s_ev_head = next;
}

static uint8_t ev_pop(uint8_t *type, uint32_t *ms)
{
    uint8_t tail = s_ev_tail;

    if (tail == s_ev_head) {
        return 0U;
    }
    *type = s_ev_type[tail];
    *ms = s_ev_ms[tail];
    s_ev_tail = (uint8_t)((tail + 1U) % EVQ_LEN);
    return 1U;
}

/* Per-button tick: debounce + press/hold/repeat events. idx 0..3 for
 * OK,PLUS,MINUS,SEL; pin is the BTN_*_PIN for that button. */
static void menu_tick_one(uint8_t idx, uint16_t pin, uint16_t raw_mask,
                           uint16_t stable_mask, uint32_t now)
{
    uint8_t pressed = ((raw_mask & pin) != 0U) ? 1U : 0U;
    uint8_t was     = ((stable_mask & pin) != 0U) ? 1U : 0U;

    if (pressed == was) {
        if (pressed == 0U) {
            return;
        }
        if (idx == 3U) { /* SEL: hold fires once */
            if ((s_btn_long[idx] == 0U)
                && ((now - s_btn_press_ms[idx]) >= BTN_HOLD_MS)) {
                s_btn_long[idx] = 1U;
                ev_push(EV_SEL_HOLD);
            }
            return;
        }
        /* OK: no repeat. +/-: repeat after initial delay. */
        if (idx == 0U) {
            return;
        }
        if (s_btn_long[idx] == 0U) {
            if ((now - s_btn_press_ms[idx]) >= BTN_REPEAT_FIRST) {
                s_btn_long[idx] = 1U;
                s_btn_repeat_ms[idx] = now;
                ev_push((idx == 1U) ? EV_PLUS : EV_MINUS);
            }
        } else if ((now - s_btn_repeat_ms[idx]) >= BTN_REPEAT_MS) {
            s_btn_repeat_ms[idx] = now;
            ev_push((idx == 1U) ? EV_PLUS : EV_MINUS);
        }
        return;
    }

    /* State changed: act on stable edges. */
    if (pressed != 0U) {
        s_btn_press_ms[idx] = now;
        s_btn_long[idx] = 0U;
    } else {
        /* Released: generate press for short SEL/OK (if hold didn't fire). */
        if (s_btn_long[idx] == 0U) {
            if (idx == 3U) {
                ev_push(EV_SEL_PRESS);
            } else if (idx == 0U) {
                ev_push(EV_OK);
            } else if (idx == 1U) {
                ev_push(EV_PLUS);
            } else {
                ev_push(EV_MINUS);
            }
        }
    }
}

void menu_button_tick(void)
{
    uint32_t now;
    uint16_t raw;
    uint8_t  i;
    static const uint16_t pins[4] = {
        BTN_OK_PIN, BTN_PLUS_PIN, BTN_MINUS_PIN, BTN_SEL_PIN
    };

    if (s_btn_ready == 0U) {
        return;
    }
    now = HAL_GetTick();
    raw = menu_buttons_pressed_mask();

    if (raw != s_btn_raw) {
        s_btn_raw = raw;
        s_btn_change_ms = now;
        return;
    }
    if ((now - s_btn_change_ms) < BTN_DEBOUNCE_MS) {
        return;
    }
    /* raw is stable for debounce interval: process edges/holds, then latch. */
    for (i = 0U; i < 4U; i++) {
        menu_tick_one(i, pins[i], raw, s_btn_stable, now);
    }
    s_btn_stable = raw;
}

static enum menu_state menu_next(enum menu_state s)
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

static void menu_note(struct menu *m, const char *text, uint32_t now)
{
    (void)snprintf(m->note, sizeof m->note, "%s", text);
    m->note_ms = now;
}

static int32_t clamp32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* One step of the current page: +1 forward, -1 backward. */
static void menu_step_once(struct menu *m, int dir, uint32_t now)
{
    switch (m->state) {
    case MENU_TIMEBASE: {
        int32_t idx = (int32_t)m->timebase_idx + dir;

        if (idx < 0) {
            idx = (int32_t)TIMEBASE_COUNT - 1;
        } else if (idx >= (int32_t)TIMEBASE_COUNT) {
            idx = 0;
        }
        if (acq_set_timebase((uint8_t)idx) == ACQ_OK) {
            m->timebase_idx = (uint8_t)idx;
            m->dirty = 1U;
            m->last_change_ms = now;
        }
        break;
    }
    case MENU_TRIGGER_MODE: {
        int32_t mode = ((int32_t)m->trigger_mode + dir) % 3;

        if (mode < 0) {
            mode += 3;
        }
        if (acq_set_trigger_mode((uint8_t)mode) == ACQ_OK) {
            if (mode == (int32_t)TRIG_MODE_SINGLE) {
                (void)acq_trigger_arm();
            }
            m->trigger_mode = (uint8_t)mode;
            m->dirty = 1U;
            m->last_change_ms = now;
        }
        break;
    }
    case MENU_TRIGGER_LEVEL: {
        int32_t level = clamp32((int32_t)m->trigger_level + (dir * 64),
                                0, 4095);

        /* Only touch flash bookkeeping when the value really moved: at the
         * clamp limit a held button repeats every 600 ms and an
         * unconditional dirty flag would keep the record permanently
         * unsaved (and then rewrite an identical record). */
        if ((uint16_t)level != m->trigger_level) {
            if (acq_set_trigger_level((uint16_t)level) == ACQ_OK) {
                m->trigger_level = (uint16_t)level;
                m->dirty = 1U;
                m->last_change_ms = now;
            }
        }
        break;
    }
    case MENU_TRIGGER_SLOPE: {
        int32_t sl = ((int32_t)s_trig_slope + dir) % 3;

        if (sl < 0) { sl += 3; }
        if ((uint8_t)sl != s_trig_slope) {
            s_trig_slope = (uint8_t)sl;
            m->trigger_slope = s_trig_slope;
            m->dirty = 1U;
            m->last_change_ms = now;
        }
        break;
    }
    case MENU_CAL_ZERO: {
        /* A tap captures the present frame mean as this combo's zero level;
         * the backward path (double tap) forgets it.  Both are idempotent, so
         * the net -1 of a double tap behaves the same as a single backward
         * step even though the forward step already ran. */
        if (m->combo_valid == 0U) {
            break;
        }
        if (dir > 0) {
            uint8_t changed = (uint8_t)(cal_table_present(cal_table_active(),
                                                          m->combo_idx) == 0U
                             || cal_table_zero(cal_table_active(),
                                               m->combo_idx) != m->frame_mean);

            if ((cal_table_set(cal_table_mutable(), m->combo_idx,
                               m->frame_mean) == 0U)
                && (changed != 0U)) {
                m->dirty_cal = 1U;
                m->last_change_ms = now;
            }
        } else if (cal_table_present(cal_table_active(), m->combo_idx) != 0U) {
            (void)cal_table_clear(cal_table_mutable(), m->combo_idx);
            m->dirty_cal = 1U;
            m->last_change_ms = now;
        }
        break;
    }
    case MENU_CAL_OFFSET: {
        int32_t offset = clamp32(m->cal_offset_uv + (dir * 8),
                                 -200000, 200000);

        if (offset != m->cal_offset_uv) {
            struct cal c;

            m->cal_offset_uv = offset;
            c.offset_uv = m->cal_offset_uv;
            c.gain_q16 = m->cal_gain_q16;
            cal_active_set(&c);
            m->dirty_cal = 1U;
            m->last_change_ms = now;
        }
        break;
    }
    case MENU_CAL_GAIN: {
        int32_t gain = clamp32(m->cal_gain_q16 + (dir * 1024),
                               16384, 262144);

        if (gain != m->cal_gain_q16) {
            struct cal c;

            m->cal_gain_q16 = gain;
            c.offset_uv = m->cal_offset_uv;
            c.gain_q16 = m->cal_gain_q16;
            cal_active_set(&c);
            m->dirty_cal = 1U;
            m->last_change_ms = now;
        }
        break;
    }
    case MENU_WAVE_SAVE:
    case MENU_WAVE_LOAD:
        /* +/- selects the slot; OK (menu_ok) saves / recalls it. */
        m->wave_slot = (m->wave_slot == 0U) ? 1U : 0U;
        break;
    case MENU_MEAS_MODE:
        m->meas_mode = (uint8_t)((m->meas_mode + ((dir > 0) ? 1U : 3U)) % 4U);
        break;
    case MENU_DISPLAY:
        m->disp_mode = (m->disp_mode == 0U) ? 1U : 0U;
        break;
    default:
        break;
    }
}

static void menu_step(struct menu *m, int steps, uint32_t now)
{
    int i;

    if (steps > 0) {
        for (i = 0; i < steps; i++) {
            menu_step_once(m, 1, now);
        }
    } else {
        for (i = 0; i < -steps; i++) {
            menu_step_once(m, -1, now);
        }
    }
}

/* OK on the open menu page: capture / save / confirm actions.  Kept apart
 * from menu_step_once() so a held +/- can never trigger a destructive page
 * action such as CAL RESET or DEFAULTS. */
static void menu_ok(struct menu *m, uint32_t now)
{
    switch (m->state) {
    case MENU_WAVE_SAVE:
        m->wave_req = 1U;
        break;
    case MENU_WAVE_LOAD:
        if (wave_store_valid(m->wave_slot) != 0U) {
            m->wave_req = 2U;
        } else {
            menu_note(m, "SLOT EMPTY", now);
        }
        break;
    case MENU_MEAS_MODE:
        m->meas_mode = (uint8_t)((m->meas_mode + 1U) % 4U);
        break;
    case MENU_CAL_RST:
        cal_active_set(&cal_identity);
        m->cal_offset_uv = 0;
        m->cal_gain_q16 = 65536;
        (void)cal_table_clear_all(cal_table_mutable());
        m->dirty_cal = 1U;
        m->last_change_ms = now;
        menu_note(m, "Clr Offset done", now);
        break;
    case MENU_DEFAULTS: {
        struct settings ds;

        settings_defaults(&ds);
        m->timebase_idx = ds.timebase_idx;
        m->trigger_mode = ds.trigger_mode;
        m->trigger_level = ds.trigger_level;
        m->focus = FOCUS_TIMEBASE;
        m->meas_mode = 0U;
        m->disp_mode = 0U;
        s_trig_slope = ds.trigger_slope;
        m->trigger_slope = s_trig_slope;
        (void)acq_set_timebase(m->timebase_idx);
        (void)acq_set_trigger_mode(m->trigger_mode);
        (void)acq_set_trigger_level(m->trigger_level);
        runstate_run_set(ds.run);
        runstate_stream_set(ds.stream);
        m->dirty = 1U;
        m->last_change_ms = now;
        menu_note(m, "Default restored", now);
        break;
    }
    case MENU_DISPLAY:
        m->disp_mode = (m->disp_mode == 0U) ? 1U : 0U;
        break;
    case MENU_SYS_DIAG:
        m->diag_open = (m->diag_open == 0U) ? 1U : 0U;
        break;
    default:
        menu_step(m, 1, now);
        break;
    }
}

static void menu_persist(struct menu *m, uint32_t now)
{
    if ((m->dirty != 0U) && ((now - m->last_change_ms) >= MENU_SAVE_MS)) {
        struct settings s;

        settings_defaults(&s);
        s.timebase_idx = m->timebase_idx;
        s.trigger_mode = m->trigger_mode;
        s.trigger_level = m->trigger_level;
        s.run = runstate_run();
        s.stream = 0U;   /* never persist PC-link streaming (freezes the UI) */
        s.trigger_slope = s_trig_slope;
        if (settings_save(&s) == 0U) {
            m->dirty = 0U;
        } else {
            m->last_change_ms = now;    /* retry after the debounce */
        }
    }
    if ((m->dirty_cal != 0U) && ((now - m->last_change_ms) >= MENU_SAVE_MS)) {
        struct cal c;

        c.offset_uv = m->cal_offset_uv;
        c.gain_q16 = m->cal_gain_q16;
        /* Persists the base pair AND the live zero table. */
        if (cal_store_write(&c) == 0U) {
            m->dirty_cal = 0U;
        } else {
            m->last_change_ms = now;    /* retry after the debounce */
        }
    }
}

static void focus_step(struct menu *m, int dir, uint32_t now)
{
    switch ((enum menu_focus)(m->focus % FOCUS_COUNT)) {
    case FOCUS_TIMEBASE: {
        int32_t idx = (int32_t)m->timebase_idx + dir;
        if (idx < 0) {
            idx = (int32_t)TIMEBASE_COUNT - 1;
        } else if (idx >= (int32_t)TIMEBASE_COUNT) {
            idx = 0;
        }
        if (acq_set_timebase((uint8_t)idx) == ACQ_OK) {
            m->timebase_idx = (uint8_t)idx;
            m->dirty = 1U;
            m->last_change_ms = now;
        }
        break;
    }
    case FOCUS_TRIGGER_MODE: {
        int32_t mode = ((int32_t)m->trigger_mode + dir) % 3;
        if (mode < 0) { mode += 3; }
        if (acq_set_trigger_mode((uint8_t)mode) == ACQ_OK) {
            if (mode == (int32_t)TRIG_MODE_SINGLE) {
                (void)acq_trigger_arm();
            }
            m->trigger_mode = (uint8_t)mode;
            m->dirty = 1U;
            m->last_change_ms = now;
        }
        break;
    }
    case FOCUS_TRIGGER_LEVEL: {
        int32_t level = clamp32((int32_t)m->trigger_level + (dir * 64), 0, 4095);
        if ((uint16_t)level != m->trigger_level) {
            if (acq_set_trigger_level((uint16_t)level) == ACQ_OK) {
                m->trigger_level = (uint16_t)level;
                m->dirty = 1U;
                m->last_change_ms = now;
            }
        }
        break;
    }
    case FOCUS_TRIGGER_SLOPE: {
        int32_t sl = ((int32_t)s_trig_slope + dir) % 3;
        if (sl < 0) { sl += 3; }
        if ((uint8_t)sl != s_trig_slope) {
            s_trig_slope = (uint8_t)sl;
            m->trigger_slope = s_trig_slope;
            m->dirty = 1U;
            m->last_change_ms = now;
        }
        break;
    }
    default:
        break;
    }
}

void menu_init(struct menu *m)
{
    struct settings s;
    struct cal c;
    uint8_t i;

    if (m == NULL) {
        return;
    }
    menu_config_buttons();
    s_ev_head = 0U;
    s_ev_tail = 0U;
    s_btn_raw = menu_buttons_pressed_mask();
    s_btn_stable = s_btn_raw;
    s_btn_change_ms = HAL_GetTick();
    for (i = 0U; i < 4U; i++) {
        s_btn_press_ms[i] = 0U;
        s_btn_repeat_ms[i] = 0U;
        s_btn_long[i] = 0U;
    }
    s_btn_ready = 1U;

    memset(m, 0, sizeof *m);
    m->state = MENU_NONE;
    m->focus = FOCUS_TIMEBASE;
    m->timebase_idx = ACQ_DEFAULT_TIMEBASE;
    m->trigger_mode = TRIG_MODE_AUTO;
    m->trigger_level = ACQ_TRIGGER_LEVEL;
    m->trigger_slope = 2U;
    m->cal_offset_uv = 0;
    m->cal_gain_q16 = 65536;

    runstate_init();

    if (settings_load(&s) == 0U) {
        if (s.timebase_idx < TIMEBASE_COUNT) {
            m->timebase_idx = s.timebase_idx;
        }
        if (s.trigger_mode <= (uint8_t)TRIG_MODE_SINGLE) {
            m->trigger_mode = s.trigger_mode;
        }
        if (s.trigger_level <= 4095U) {
            m->trigger_level = s.trigger_level;
        }
        if (s.trigger_slope <= 2U) {
            s_trig_slope = s.trigger_slope;
        }
        runstate_run_set(s.run);
        /* Streaming is a session-only PC-link mode: while it is active the
         * UI and the front-panel ladder decode are deliberately skipped
         * (the link owns the bandwidth).  Restoring a persisted 1 here
         * booted the instrument with a frozen screen and dead switches, so
         * it always starts non-streaming; the link can re-enable it. */
        runstate_stream_set(0U);
    }
    m->trigger_slope = s_trig_slope;
    if (cal_store_read(&c) == 0U) {
        cal_active_set(&c);
        m->cal_offset_uv = c.offset_uv;
        m->cal_gain_q16 = c.gain_q16;
    }
    m->last_activity_ms = HAL_GetTick();
    m->last_change_ms = m->last_activity_ms;
}

void menu_poll(struct menu *m)
{
    uint32_t now;
    uint32_t ev_ms;
    uint8_t  ev;

    if (m == NULL) {
        return;
    }
    now = HAL_GetTick();

    /* The PC link can change the engine behind the menu's back; the engine
     * is the single source of truth, so mirror it before handling events. */
    m->timebase_idx = acq_timebase_get();
    m->trigger_mode = acq_trigger_mode_get();
    m->trigger_level = acq_trigger_level_get();

    while (ev_pop(&ev, &ev_ms) != 0U) {
        m->last_activity_ms = ev_ms;

        if (ev == EV_SEL_HOLD) {
            /* SEL hold: enter settings/calibration menu, or close it. */
            if (m->state == MENU_NONE) {
                m->state = MENU_TIMEBASE;
                m->diag_open = 0U;
            } else {
                m->state = MENU_NONE;
                m->diag_open = 0U;
            }
            continue;
        }

        if (m->state == MENU_NONE) {
            /* Main trace screen: SEL cycles focus, +/- adjust, OK = RUN/HOLD. */
            switch (ev) {
            case EV_SEL_PRESS:
                m->focus = (uint8_t)((m->focus + 1U) % FOCUS_COUNT);
                m->focus_ms = ev_ms;
                break;
            case EV_PLUS:
                focus_step(m, 1, ev_ms);
                m->focus_ms = ev_ms;
                break;
            case EV_MINUS:
                focus_step(m, -1, ev_ms);
                m->focus_ms = ev_ms;
                break;
            case EV_OK:
                runstate_run_set((uint8_t)(!runstate_run()));
                m->dirty = 1U;
                m->last_change_ms = ev_ms;
                menu_note(m, runstate_run() ? "Running" : "Stopped", ev_ms);
                break;
            default:
                break;
            }
        } else {
            /* Settings menu open: SEL next page, +/- adjust, OK action. */
            switch (ev) {
            case EV_SEL_PRESS:
                m->state = menu_next(m->state);
                m->diag_open = 0U;
                break;
            case EV_PLUS:
                menu_step(m, 1, ev_ms);
                break;
            case EV_MINUS:
                menu_step(m, -1, ev_ms);
                break;
            case EV_OK:
                menu_ok(m, ev_ms);
                break;
            default:
                break;
            }
        }
    }

    if ((m->state != MENU_NONE)
        && ((now - m->last_activity_ms) >= MENU_IDLE_MS)) {
        m->state = MENU_NONE;
        m->diag_open = 0U;
    }
    if ((m->note[0] != '\0') && ((now - m->note_ms) >= NOTE_MS)) {
        m->note[0] = '\0';
    }
    m->button_down = menu_buttons_pressed_mask();
    menu_persist(m, now);
}

/* Encoded description of what the CAL ZERO page shows, so the repaint test
 * can be a single comparison: 0xFFFF = no combo decoded, 0x8000 | zero =
 * a zero is stored, 0 = absent. */
static uint16_t zero_page_state(const struct menu *m)
{
    if (m->combo_valid == 0U) {
        return 0xFFFFU;
    }
    if (cal_table_present(cal_table_active(), m->combo_idx) == 0U) {
        return 0U;
    }
    return (uint16_t)(0x8000U
                      | cal_table_zero(cal_table_active(), m->combo_idx));
}

/* ---- settings card (DSO183-UI-SPEC-001 screen 4) ----------------------
 * A floating test-console card over the frozen waveform.  It is repainted
 * only when one of the shown values changes: the signature below mixes the
 * selected page and every value the card can display. */
#define CARD_X      26
#define CARD_Y      28
#define CARD_W      268
#define CARD_H      180
#define CARD_ROW_H  10
#define CARD_ROW0   (CARD_Y + 18)
#define CARD_TX     (CARD_X + 6)
#define CARD_VX     (CARD_X + 146)

static void card_str(char *b, size_t n, const char *s)
{
    (void)snprintf(b, n, "%s", (s != NULL) ? s : "?");
}

static const char *disp_label(uint8_t mode)
{
    return (mode != 0U) ? "DISPLAY2" : "DISPLAY1";
}

static const char *meas_label(uint8_t mode)
{
    switch (mode) {
    case 1U: return "AV +/-";
    case 2U: return "CYCLE/DUTY";
    case 3U: return "ALL DISP.";
    default: return "ALL";
    }
}

static const char *slope_label(void)
{
    if (s_trig_slope == 1U) { return "FALL [\\]"; }
    if (s_trig_slope == 2U) { return "AUTO [~]"; }
    return "RISE [/]";
}

/* Label + value of one card row (row 0 = item 01). */
static void card_item(const struct menu *m, uint8_t row, char *label,
                      size_t ln, char *val, size_t vn)
{
    static const char *const s_labels[] = {
        "TIMEBASE", "TRIGGER MODE", "TRIGGER LEVEL", "TRIGGER SLOPE",
        "ZERO CALIBRATE", "OFFSET CAL", "GAIN CAL", "WAVE SAVE",
        "WAVE LOAD", "MEAS MODE", "CAL RESET", "DEFAULTS",
        "DISPLAY", "SYSTEM DIAG"
    };
    uint16_t zstate = zero_page_state(m);

    if (row >= (uint8_t)(sizeof s_labels / sizeof s_labels[0])) {
        label[0] = '\0';
        val[0] = '\0';
        return;
    }
    (void)snprintf(label, ln, "%02u.%s", (unsigned)(row + 1U), s_labels[row]);

    switch (row) {
    case 0U: {
        const char *tb = timebase_label(m->timebase_idx);
        (void)snprintf(val, vn, "%s/div", (tb != NULL) ? tb : "?");
        break;
    }
    case 1U:
        card_str(val, vn, (m->trigger_mode == TRIG_MODE_NORM) ? "NORM"
                      : (m->trigger_mode == TRIG_MODE_SINGLE) ? "SINGLE"
                      : "AUTO");
        break;
    case 2U:
        (void)snprintf(val, vn, "%u ct", (unsigned)m->trigger_level);
        break;
    case 3U:
        card_str(val, vn, slope_label());
        break;
    case 4U:
        if (zstate == 0xFFFFU) {
            card_str(val, vn, "NO SWITCH");
        } else if ((zstate & 0x8000U) != 0U) {
            (void)snprintf(val, vn, "C%u STORED %u",
                           (unsigned)m->combo_idx, (unsigned)(zstate & 0x0FFFU));
        } else {
            (void)snprintf(val, vn, "C%u NOT SET", (unsigned)m->combo_idx);
        }
        break;
    case 5U:
        (void)snprintf(val, vn, "%+ld uV", (long)m->cal_offset_uv);
        break;
    case 6U: {
        long whole = (long)(m->cal_gain_q16 / 65536L);
        long frac  = (long)(((m->cal_gain_q16 % 65536L) * 10000L + 32768L) / 65536L);
        (void)snprintf(val, vn, "%ld.%04ld", whole, frac);
        break;
    }
    case 7U:
    case 8U:
        (void)snprintf(val, vn, "SLOT %u %s", (unsigned)m->wave_slot,
                       (wave_store_valid(m->wave_slot) != 0U) ? "USED" : "EMPTY");
        break;
    case 9U:
        card_str(val, vn, meas_label(m->meas_mode));
        break;
    case 10U:
        card_str(val, vn, "OK:CLEAR");
        break;
    case 11U:
        card_str(val, vn, "OK:RESTORE");
        break;
    case 12U:
        card_str(val, vn, disp_label(m->disp_mode));
        break;
    default:
        card_str(val, vn, (m->diag_open != 0U) ? "OPEN >" : "ENTER >");
        break;
    }
}

/* Incremental card painting.
 *
 * Repainting the whole 14-row card on every button press made the menu
 * visibly flash grey each time it was touched.  The card is now painted in
 * full only when it is ENTERED; while it stays open, a page change repaints
 * just the old and new selected rows, and a value change repaints just that
 * row.  The old `s_paint_page != m->state` test treated every SEL page move
 * as a full-card repaint, which is the grey flash the user saw.  Cached
 * strings cost 14 x 24 + 48 bytes of .bss. */
static char    s_row_val[14][24];
static char    s_prompt_prev[48];
static uint8_t s_sel_prev = 0xFFU;
static uint8_t s_run_shown = 0xFFU;

/* Paint the RUN/HOLD badge in the card title without touching the rest of
 * the title bar. */
static void menu_paint_run_badge(uint8_t run)
{
    uint16_t x = (uint16_t)(CARD_X + CARD_W - 6U - 4U * 6U);

    ili9341_fill_rect(x, (uint16_t)(CARD_Y + 5U), (uint16_t)(4U * 6U), 8U,
                      UI_C_CARD);
    ili9341_print(x, (uint16_t)(CARD_Y + 5U), (run != 0U) ? "RUN" : "HOLD",
                  (run != 0U) ? UI_C_GREEN : UI_C_HOLD_FG, UI_C_CARD, 1U);
}

static void menu_paint_row(const struct menu *m, uint8_t i)
{
    char label[24];
    char val[24];
    uint16_t y = (uint16_t)(CARD_ROW0 + (uint16_t)i * CARD_ROW_H);
    uint8_t sel = (m->state == (enum menu_state)(i + 1U)) ? 1U : 0U;
    uint16_t bg = (sel != 0U) ? UI_C_FOCUS : UI_C_CARD;
    uint16_t fg = (sel != 0U) ? UI_C_BG : UI_C_TEXT;
    uint16_t vf = (sel != 0U) ? UI_C_BG : UI_C_TRIG;

    card_item((struct menu *)m, i, label, sizeof label, val, sizeof val);
    ili9341_fill_rect((uint16_t)(CARD_X + 3U), (uint16_t)(y - 1U),
                      (uint16_t)(CARD_W - 6U), CARD_ROW_H, bg);
    ili9341_print(CARD_TX, y, label, fg, bg, 1U);
    ili9341_print(CARD_VX, y, val, vf, bg, 1U);
}

static void menu_paint_prompt(const struct menu *m, uint32_t now)
{
    static const char prompt[] = "[SEL:Next +/-:Edit OK:Act HOLD SEL:Exit]";
    uint8_t note_live = (uint8_t)((m->note[0] != '\0')
                       && ((now - m->note_ms) < NOTE_MS));
    const char *text = (note_live != 0U) ? m->note : prompt;

    if (strcmp(text, s_prompt_prev) == 0) {
        return;
    }
    (void)snprintf(s_prompt_prev, sizeof s_prompt_prev, "%s", text);
    ili9341_fill_rect((uint16_t)(CARD_X + 3U), (uint16_t)(CARD_Y + CARD_H - 12U),
                      (uint16_t)(CARD_W - 6U), 10U, UI_C_CARD);
    ili9341_print((uint16_t)(CARD_X + 6U), (uint16_t)(CARD_Y + CARD_H - 11U),
                  text, (note_live != 0U) ? UI_C_FOCUS : UI_C_MUTED,
                  UI_C_CARD, 1U);
}

void menu_render(struct menu *m)
{
    uint32_t now;
    uint8_t i;
    uint8_t sel;

    if (m == NULL) {
        return;
    }
    if (m->state == MENU_NONE) {
        m->drawn = 0U;
        s_sel_prev = 0xFFU;
        return;
    }
    if (m->diag_open != 0U) {
        m->drawn = 0U;       /* main.c owns the full-screen telemetry page */
        return;
    }

    now = HAL_GetTick();

    if (m->drawn == 0U) {
        /* Entering the menu: the card area may still hold the scope, so a
         * full paint is required once.  Page changes afterwards are handled
         * incrementally below. */
        m->drawn = 1U;
        s_sel_prev = 0xFFU;

        /* Frame + title */
        ili9341_fill_rect(CARD_X, CARD_Y, CARD_W, CARD_H, UI_C_BORDER);
        ili9341_fill_rect((uint16_t)(CARD_X + 1U), (uint16_t)(CARD_Y + 1U),
                          (uint16_t)(CARD_W - 2U), (uint16_t)(CARD_H - 2U),
                          UI_C_CARD);
        ili9341_print((uint16_t)(CARD_X + 6U), (uint16_t)(CARD_Y + 5U),
                      "SETTINGS & CALIBRATION", UI_C_TRACE, UI_C_CARD, 1U);
        s_run_shown = runstate_run();
        menu_paint_run_badge(s_run_shown);
        ili9341_fill_rect((uint16_t)(CARD_X + 4U), (uint16_t)(CARD_Y + 14U),
                          (uint16_t)(CARD_W - 8U), 1U, UI_C_GRID);

        for (i = 0U; i < 14U; i++) {
            char label[24];
            menu_paint_row(m, i);
            card_item(m, i, label, sizeof label, s_row_val[i],
                      sizeof s_row_val[i]);
        }
        s_sel_prev = (uint8_t)((m->state >= MENU_TIMEBASE)
                               && (m->state <= MENU_SYS_DIAG))
                   ? (uint8_t)(m->state - 1U) : 0xFFU;
        s_prompt_prev[0] = '\0';
        menu_paint_prompt(m, now);
        return;
    }

    /* Menu already open.  Repaint a row only when its value changed. */
    for (i = 0U; i < 14U; i++) {
        char label[24];
        char val[24];

        card_item(m, i, label, sizeof label, val, sizeof val);
        if (strcmp(val, s_row_val[i]) != 0) {
            (void)snprintf(s_row_val[i], sizeof s_row_val[i], "%s", val);
            menu_paint_row(m, i);
        }
    }

    /* Moving the selection (SEL) repaints just the two rows whose
     * highlight changed - never the whole card, so there is no grey flash. */
    sel = (uint8_t)((m->state >= MENU_TIMEBASE) && (m->state <= MENU_SYS_DIAG))
        ? (uint8_t)(m->state - 1U) : 0xFFU;
    if (sel != s_sel_prev) {
        if (s_sel_prev < 14U) {
            menu_paint_row(m, s_sel_prev);   /* clears the old highlight */
        }
        if (sel < 14U) {
            menu_paint_row(m, sel);          /* draws the new highlight   */
        }
        s_sel_prev = sel;
    }

    /* OK toggles RUN/HOLD while the menu is open: refresh the badge only. */
    if (runstate_run() != s_run_shown) {
        s_run_shown = runstate_run();
        menu_paint_run_badge(s_run_shown);
    }

    menu_paint_prompt(m, now);
}

void menu_frame_mean(struct menu *m, uint16_t mean, uint8_t combo_idx,
                     uint8_t combo_valid)
{
    if (m == NULL) {
        return;
    }
    m->frame_mean = mean;
    if (combo_idx >= (uint8_t)CAL_COMBO_COUNT) {
        m->combo_valid = 0U;
        return;
    }
    m->combo_idx = combo_idx;
    m->combo_valid = (combo_valid != 0U) ? 1U : 0U;
}
