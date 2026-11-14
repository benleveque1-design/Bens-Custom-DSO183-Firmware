/* Minimal working bring-up with a functional diagnostic screen */
#include "board.h"
#include "board_iwdg.h"
#include "diag.h"
#include "ili9341.h"
#include "input.h"
#include "ladder.h"
#include "acq.h"
#include "bench.h"
#include "continuity.h"
#include "link.h"
#include "link_cmd.h"
#include "link_host.h"
#include "frame.h"
#include "render.h"
#include "ui.h"
#include "waveform.h"
#include "status.h"
#include "cal.h"
#include "cal_store.h"
#include "cal_table.h"
#include "wave_store.h"
#include "runstate.h"
#include "menu.h"
#include "trigger.h"
#include "timebase.h"
#include "ets_phase.h"
#include "ets_epoch.h"
#include "settings.h"
#include "memdiag.h"
#include "stm32f1xx_hal.h"

#include <stdio.h>
#include <string.h>

#define MAIN_IWDG_TIMEOUT_MS 10000U

#define DIAG_FG  ILI9341_GREEN
#define DIAG_BG  ILI9341_BLACK

/* Vendor front-panel tables, indexed by the raw switch level 0..2.
 * SEN1 (range)   0 -> 1 V,   1 -> 0.1 V, 2 -> 10 mV  (vendor 0x0800d887)
 * SEN2 (gain)    0 -> x5,    1 -> x2,    2 -> x1     (vendor 0x0800d9eb)
 * The vendor combines them as combo = 4 + 3*SEN1 + SEN2 and its full
 * 9-entry label table (vendor 0x0800d9..) is
 *   5V, 2V, 1V, 0.5V, 0.2V, 0.1V, 50mV, 20mV, 10mV
 * for combo 4..12.  That is range MULTIPLIED by gain, not divided:
 * 1V x5 = 5V/div, 1V x2 = 2V/div, 10mV x5 = 50mV/div, and so on. */
static const uint16_t k_range_mv[3] = { 1000U, 100U, 10U };
static const uint16_t k_gain_x[3]   = { 5U, 2U, 1U };
static uint16_t s_mv = 100U;      /* decoded mV per division (range*gain) */
static uint8_t  s_rng_lvl = 1U;   /* 0 = 1 V, 1 = 0.1 V, 2 = 10 mV      */
static uint8_t  s_gain_lvl = 2U;  /* 0 = x5, 1 = x2, 2 = x1             */

/* ADC counts across the 8 vertical divisions.
 *
 * The slide switches drive an analogue attenuator/gain stage: whichever
 * range is selected, the front end normalises that full-scale input to the
 * same ADC span.  Only the LABEL changes (1 V/div, 5 V/div, ...), not the
 * counts per division.  Deriving the span from mV/div (the previous
 * behaviour) therefore shrank the trace by the range ratio and made a
 * 3.3 V signal invisible at 1 V/div.
 *
 * Calibrated 2026-11-09 against the known 3.3 V / 1 kHz test signal at the
 * 1 V range, where the signal is unclipped:
 *   x1: 175 counts for 3.3 V over 3.30 div -> 424 counts / 8 div
 *   x2:  87 counts for 3.3 V over 1.65 div -> 422
 *   x5:  37 counts for 3.3 V over 0.66 div -> 448 (coarse: only 37 counts)
 * 424 makes the 1 V/x1 readout exactly 3.3 V.  The 0.1 V and 10 mV ranges
 * cannot be calibrated with a 3.3 V input (it is far off-screen there and
 * the front-end amplifier compresses it); they use the same span because
 * the hardware normalises each range to this same full-scale span. */
#define SCOPE_SPAN_COUNTS 424U
static uint16_t s_span = SCOPE_SPAN_COUNTS;  /* ADC counts across 8 div */
static uint8_t  s_cont = 0U;
static uint8_t clock_config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
    osc.PLL.PLLMUL = RCC_PLL_MUL16;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        status_init_error_set(STATUS_ERR_OSC);
        return 0U;
    }
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
        status_init_error_set(STATUS_ERR_CLOCK);
        return 0U;
    }
    /* HAL_RCC_ClockConfig re-arms SysTick for the new HCLK (hal_rcc.c:847)
     * and updates SystemCoreClock before doing so, so the tick stays 1 ms -
     * no extra call needed here. */
    return 1U;
}

/* The console is optional: if the UART did not initialise, the screen must
 * still work - and every byte written to a dead UART would cost a 100 ms
 * timeout, so the writes are skipped entirely in that case. */
static uint8_t s_console_ok;

static uint8_t console_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    /* PA9 = TX: alternate function push-pull.  PA10 = RX: plain input.
     * (Driving RX as an output would fight the adapter's TX driver on the
     * same wire - the pin must only ever be an input.) */
    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);

    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200U;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK) {
        status_init_error_set(STATUS_ERR_UART);
        return 0U;
    }
    s_console_ok = 1U;
    return 1U;
}

static void puts_(const char *s)
{
    if (s_console_ok == 0U) {
        return;                      /* no UART: never block on it */
    }
    while (*s) {
        if (HAL_UART_Transmit(&huart1, (uint8_t *)s, 1U, 100U) != HAL_OK) {
            s_console_ok = 0U;       /* link dead: stop trying */
            return;
        }
        s++;
    }
}

/* Collect everything the screen shows, from live sources only.
 * The display reads are the proof that the bus works in both directions; the
 * ADC scan is the front-panel input evidence (which channel carries the
 * slide-switch ladder is still open - U-02a - so the raw counts of every
 * channel are shown). */
/* ------------------------------------------------------------------ PC link */

static uint8_t s_combo_idx;           /* range * 3 + gain from the ladder  */
static uint8_t s_combo_valid;         /* 1 = the switches decoded a combo  */
static struct menu s_menu;            /* front-panel menu (one instance)   */
static uint8_t s_link_seq;
static uint8_t s_rxbuf[LINK_CMD_LEN];
static uint8_t s_rxlen;

static uint16_t s_meas_vpp;
static uint16_t s_meas_vrms;
static uint16_t s_meas_min;
static uint16_t s_meas_max;
static uint16_t s_meas_mean;
static uint32_t s_meas_freq_hz;
static uint16_t s_meas_duty_x10;
static int32_t  s_avp_mv;          /* average of samples above the 0 V reference */
static int32_t  s_avn_mv;          /* average of samples below the 0 V reference */
static uint8_t  s_cpl = 0U;        /* 0 DC, 1 AC, 2 GND, 0xFF unknown (CPL SW1) */
static uint8_t  s_trig_locked;     /* 1 = last displayed frame was edge-aligned */
uint8_t s_trig_slope = 2U; /* 0 rising, 1 falling, 2 auto (shared with menu) */

/* Trigger diagnostic telemetry snapshot (read-only, per recorded acquisition).
 * Instrumentation only; easy to remove/disable. */
static volatile uint16_t s_diag_seq;
static volatile uint8_t  s_diag_gate_triggered;
static volatile uint16_t s_diag_hw_index;
static volatile uint16_t s_diag_hw_count;   /* raw TIM3 count of that capture */
static volatile uint16_t s_diag_refined_index;
static volatile uint8_t  s_diag_refined_found;
static volatile uint8_t  s_diag_fallback; /* 0 refined, 1 HW idx, 2 tail/untriggered */
static volatile uint8_t  s_diag_timebase;
static volatile uint8_t  s_diag_trig_slope_snap;
static volatile uint16_t s_diag_trig_level_snap;
static volatile uint8_t  s_diag_trig_mode_snap;

/* Display window = 10 divisions x 32 samples/div (timebase.h).  It was 256,
 * i.e. only 8 divisions: every timebase was then 25% fast and a slow
 * timebase undersampled badly (a 1 kHz signal at 5 ms/div landed on only
 * ~6 samples per period, so the trace aliased into nonsense).  320 matches
 * the timebase table and the 1024-sample acquisition ring. */
#define BENCH_WINDOW       320U
#define BENCH_PRE           96U   /* 30% pre-trigger, matches TRIG_PRE_TRIGGER */
#define BENCH_TIMEOUT_MS  1000U
#define BENCH_DUMP_SAMPLES   8U
#define TRIG_SEARCH_LEN    512U   /* software edge-refinement search window */

static uint16_t s_frame[BENCH_WINDOW];
/* Shared transient scratch (M3 RAM optimisation).
 * Sequential lifetime contract in one acq_bench_poll iteration (main thread,
 * synchronous, not reentrant, not ISR/DMA accessible; no pointer escapes):
 *   R (refinement) — trig_search valid only inside trig_refine() (filled from
 *                    acq ring, consumed by refinement scan); dead on return.
 *   E (ETS sweep)  — same storage may be reused as a BENCH_WINDOW-uint16_t
 *                    transient build buffer only inside ets_accumulate(), after
 *                    trig_refine() has returned and before Phase T; dead on
 *                    ets_accumulate() return (contents consumed within call).
 *   T (stream TX)  — tx valid only in the streaming branch after final
 *                    frame/ETS/averaging work (link_encode_frame then blocking
 *                    HAL_UART_Transmit via console_write); dead after transmit.
 * Phases are strictly sequential and never live simultaneously. Re-audit if
 * lifetimes, call ordering, UART TX mode, or buffer sizes change. */
static union {
    uint16_t trig_search[TRIG_SEARCH_LEN];
    uint8_t  tx[LINK_OVERHEAD + 2U * BENCH_WINDOW];
} s_acq_scratch;
_Static_assert(TRIG_SEARCH_LEN >= BENCH_WINDOW,
               "ETS tmpbuf alias requires trig_search >= BENCH_WINDOW");
static uint8_t  s_acq_ok;
static uint16_t s_means[UI_PLOT_W];
static uint16_t s_disp[UI_PLOT_W];
static uint8_t  s_disp_valid;
static uint8_t  s_scope_first = 1U;
static uint32_t s_acq_cycles;
static uint32_t s_acq_timeouts;
static uint32_t s_acq_errors;

/* (b) Average-cycle: trigger-aligned accumulation of N frames. */
static uint8_t  s_avg_n = 1U;          /* 1(off),2,4,8 */
static uint32_t s_avg_acc[BENCH_WINDOW];
static uint8_t  s_avg_count;
static uint16_t s_avg_len;

/* ETS (equivalent-time sampling) accumulation for REPT timebases 0..4.
 * The epoch state machine (counter lifecycle, readiness, reconstruction) is
 * app/ets_epoch.c so it can be host-tested; this file owns only the mapping
 * of a captured sweep into bins using the Stage B1 phase estimator. */
/* reset_reason is seeded at boot (see main) so GETV 39 reports
 * ETS_RESET_INIT before the first sweep, matching the pre-B4.25 boot value.
 * It is NOT a static initialiser: that would move this ~1.9 KiB aggregate
 * from .bss into .data and cost the same amount of flash. */
static struct ets_epoch s_ets;
/* Compile-time link between the ETS window geometry and the bench frame. */
_Static_assert(ETS_EPOCH_WINDOW == BENCH_WINDOW, "ETS window must match BENCH_WINDOW");
_Static_assert(ETS_EPOCH_PRE == BENCH_PRE, "ETS pre-trigger must match BENCH_PRE");
_Static_assert(ETS_EPOCH_WINDOW * 4U <= 4096U, "ETS sum/cnt RAM stays inside the budget");
static uint16_t s_frame_len;

/* (a) Wave recall display state. */
static uint8_t  s_wave_recalled;

static void scope_draw(const struct frame *f);
static void scope_force_redraw(void);
static void ui_big_draw(void);
static int32_t  ui_counts_to_mv(int32_t counts);
static uint16_t ui_frame_ref(uint16_t mean);

static void avg_reset(void);

static void console_write(const uint8_t *buf, uint32_t len)
{
    if ((s_console_ok == 0U) || (buf == NULL) || (len == 0U)) {
        return;
    }
    /* 100 ms covers a full 520-byte sample frame (~45 ms at 115200).  A
     * timeout must NOT disable the console: the UART is healthy, the old
     * 20 ms budget simply expired mid-frame and killed all further output. */
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)len, 100U);
}

static uint8_t op_timebase(uint8_t index)
{
    if (acq_set_timebase(index) == ACQ_OK) {
        avg_reset();
        scope_force_redraw();
        return 1U;
    }
    return 0U;
}

static uint8_t op_trigger(uint16_t level)
{
    return (acq_set_trigger_level(level) == ACQ_OK) ? 1U : 0U;
}

static uint8_t op_span(uint16_t span)
{
    if ((span < 16U) || (span > 4095U)) {
        return 0U;
    }
    s_span = span;
    return 1U;
}

static uint8_t op_run(uint8_t run)
{
    runstate_run_set(run);
    if (run != 0U) {
        s_wave_recalled = 0U; /* resume live */
        avg_reset();
        scope_force_redraw();
    }
    return 1U;
}

static uint8_t op_stream(uint8_t on)
{
    runstate_stream_set(on);
    return 1U;
}

static uint16_t op_status(void)
{
    return (uint16_t)((runstate_run() != 0U ? 0x01U : 0x00U)
                    | (runstate_stream() != 0U ? 0x02U : 0x00U));
}

static uint8_t op_trig_mode(uint8_t mode)
{
    if (acq_set_trigger_mode(mode) != ACQ_OK) {
        return 0U;
    }
    if (mode == (uint8_t)TRIG_MODE_SINGLE) {
        (void)acq_trigger_arm();
    }
    s_menu.trigger_mode = mode;
    s_menu.dirty = 1U;
    s_menu.last_change_ms = HAL_GetTick();
    return 1U;
}

static uint8_t op_trig_slope(uint8_t slope)
{
    if (slope > 2U) {
        return 0U;
    }
    s_trig_slope = slope;
    s_menu.dirty = 1U;
    s_menu.last_change_ms = HAL_GetTick();
    return 1U;
}

static void avg_reset(void)
{
    s_avg_count = 0U;
    s_avg_len = 0U;
}

static void ets_epoch_context(struct ets_epoch_context *ctx)
{
    ctx->tb         = acq_timebase_get();
    ctx->trig_mode  = acq_trigger_mode_get();
    ctx->trig_slope = s_trig_slope;
    ctx->trig_level = acq_trigger_level_get();
}

static uint8_t op_cal_set_offset_mv(int16_t mv)
{
    struct cal c = *cal_active();
    c.offset_uv = (int32_t)mv * 1000L;
    cal_active_set(&c);
    s_menu.cal_offset_uv = c.offset_uv;
    s_menu.dirty_cal = 1U;
    s_menu.last_change_ms = HAL_GetTick();
    return 1U;
}

static uint8_t op_cal_set_gain_x1000(uint16_t g)
{
    struct cal c = *cal_active();
    if (g == 0U) { return 0U; }
    c.gain_q16 = (int32_t)(((uint32_t)g * 65536U + 500U) / 1000U);
    cal_active_set(&c);
    s_menu.cal_gain_q16 = c.gain_q16;
    s_menu.dirty_cal = 1U;
    s_menu.last_change_ms = HAL_GetTick();
    return 1U;
}

static uint8_t op_cal_save(void)
{
    /* Persist base cal; cal_store packs the live table state as well. */
    return (cal_store_write(cal_active()) == 0U) ? 1U : 0U;
}

static uint8_t op_cal_reset(void)
{
    cal_active_set(&cal_identity);
    (void)cal_table_clear_all(cal_table_mutable());
    s_menu.cal_offset_uv = 0;
    s_menu.cal_gain_q16 = 65536;
    (void)cal_store_write(cal_active());
    s_menu.dirty_cal = 0U;
    return 1U;
}

static uint8_t op_wave_save(uint8_t slot)
{
    /* Save the current (possibly averaged) frame. */
    uint16_t len = s_frame_len;
    if (len == 0U || len > BENCH_WINDOW) { len = (uint16_t)BENCH_WINDOW; }
    return (wave_store_save(slot, s_frame, len) == 0U) ? 1U : 0U;
}

static uint8_t op_wave_recall(uint8_t slot)
{
    uint16_t len = 0U;
    if (wave_store_load(slot, s_frame, BENCH_WINDOW, &len) != 0U) {
        return 0U;
    }
    s_wave_recalled = 1U;
    s_frame_len = len;
    avg_reset();
    runstate_run_set(0U); /* HOLD to show recalled */
    (void)measure_compute(s_frame, len, &(struct frame){0}.stats); /* no-op */
    {
        struct frame rf;
        memset(&rf, 0, sizeof rf);
        rf.length = len;
        (void)measure_compute(s_frame, len, &rf.stats);
        s_meas_min = rf.stats.min; s_meas_max = rf.stats.max;
        s_meas_vpp = rf.stats.vpp; s_meas_vrms = rf.stats.rms;
        s_meas_mean = rf.stats.mean;
        s_meas_freq_hz = 0U; s_meas_duty_x10 = 0U;
        s_trig_locked = 0U;
        {
            uint16_t ref = ui_frame_ref(rf.stats.mean);
            int64_t sum_p = 0, sum_n = 0;
            uint32_t cnt_p = 0, cnt_n = 0;
            uint16_t k;
            for (k = 0U; k < rf.length; k++) {
                int32_t d = (int32_t)s_frame[k] - (int32_t)ref;
                if (d > 0) { sum_p += d; cnt_p++; }
                else if (d < 0) { sum_n += -d; cnt_n++; }
            }
            s_avp_mv = (cnt_p != 0U) ? ui_counts_to_mv((int32_t)(sum_p / (int64_t)cnt_p)) : 0;
            s_avn_mv = (cnt_n != 0U) ? -ui_counts_to_mv((int32_t)(sum_n / (int64_t)cnt_n)) : 0;
            if (s_cpl == 2U) { s_avp_mv = 0; s_avn_mv = 0; }
        }
        scope_force_redraw();
        /* Draw only when no overlay owns the screen; ui_handle_menu_requests
         * forces a repaint after closing the menu. */
        if ((s_menu.state == MENU_NONE) && (s_menu.diag_open == 0U)) {
            if (s_menu.disp_mode != 0U) { ui_big_draw(); } else { scope_draw(&rf); }
        }
    }
    return 1U;
}

static uint8_t op_set_avg(uint8_t n)
{
    if (!((n == 1U) || (n == 2U) || (n == 4U) || (n == 8U))) { return 0U; }
    s_avg_n = n;
    avg_reset();
    return 1U;
}

static uint16_t op_get_value(uint8_t sub)
{
    switch (sub) {
    case GETV_TRIG_LEVEL:  return acq_trigger_level_get();
    case GETV_TRIG_MODE:   return (uint16_t)acq_trigger_mode_get();
    case GETV_TIMEBASE:    return (uint16_t)acq_timebase_get();
    case GETV_SPAN:        return s_span;
    case GETV_STATUS:      return op_status();
    case GETV_VPP_COUNTS:  return s_meas_vpp;
    case GETV_VRMS_COUNTS: return s_meas_vrms;
    case GETV_MIN:         return s_meas_min;
    case GETV_MAX:         return s_meas_max;
    case GETV_FREQ_HZ:     return (s_meas_freq_hz > 65535U) ? 65535U : (uint16_t)s_meas_freq_hz;
    case GETV_DUTY_X10:    return s_meas_duty_x10;
    case GETV_BUILD_SEQ:   return (uint16_t)(status_build_seq & 0xFFFFU);
    case GETV_TRIG_SLOPE:  return (uint16_t)s_trig_slope;
    case GETV_CAL_OFFSET_MV: return (uint16_t)(int16_t)(cal_active()->offset_uv / 1000L);
    case GETV_CAL_GAIN_X1000:{
        int32_t g = cal_active()->gain_q16;
        uint32_t gx = (uint32_t)((g * 1000L + 32768L) / 65536L);
        return (gx > 65535U) ? 65535U : (uint16_t)gx;
    }
    case GETV_WAVE_SAVED_0: return wave_store_valid(0U) ? 1U : 0U;
    case GETV_WAVE_SAVED_1: return wave_store_valid(1U) ? 1U : 0U;
    case GETV_AVG_N:        return (uint16_t)s_avg_n;
    case GETV_DIAG_SEQ:            return s_diag_seq;
    case GETV_DIAG_GATE_TRIGGERED: return (uint16_t)s_diag_gate_triggered;
    case GETV_DIAG_HW_INDEX:       return s_diag_hw_index;
    case GETV_DIAG_REFINED_INDEX:  return s_diag_refined_index;
    case GETV_DIAG_REFINED_FOUND:  return (uint16_t)s_diag_refined_found;
    case GETV_DIAG_FALLBACK:       return (uint16_t)s_diag_fallback;
    case GETV_DIAG_TIMEBASE:       return (uint16_t)s_diag_timebase;
    case GETV_DIAG_TRIG_SLOPE:     return (uint16_t)s_diag_trig_slope_snap;
    case GETV_DIAG_TRIG_LEVEL:     return s_diag_trig_level_snap;
    case GETV_DIAG_TRIG_MODE:      return (uint16_t)s_diag_trig_mode_snap;
    case GETV_MEM_STACK_FREE_NOW:  {
        uint32_t v = memdiag_stack_free_now();
        return (v > 65535U) ? 65535U : (uint16_t)v;
    }
    case GETV_MEM_STACK_FREE_MIN:  {
        uint32_t v = memdiag_stack_free_min();
        return (v > 65535U) ? 65535U : (uint16_t)v;
    }
    case GETV_MEM_HEAP_USED:       {
        uint32_t v = memdiag_heap_used();
        return (v > 65535U) ? 65535U : (uint16_t)v;
    }
    case GETV_MEM_HEAP_HIGHWATER:  {
        uint32_t v = memdiag_heap_highwater();
        return (v > 65535U) ? 65535U : (uint16_t)v;
    }
    case GETV_MEM_MSP_LOW_WATER:   {
        /* Raw MSP address (32-bit) exposed as low 16 bits for append-only
         * 16-bit GETV field; use with derived headroom value. */
        uint32_t v = memdiag_msp_low_water();
        return (uint16_t)(v & 0xFFFFU);
    }
    case GETV_MEM_MSP_FREE_MIN:    {
        uint32_t v = memdiag_msp_free_min();
        return (v > 65535U) ? 65535U : (uint16_t)v;
    }
    case GETV_ETS_ACCEPTED_SWEEPS: return ets_epoch_sweeps(&s_ets);
    case GETV_ETS_REJECTED_SWEEPS: return ets_epoch_rejected(&s_ets);
    case GETV_ETS_ACCUM_PROGRESS:  return ets_epoch_sweeps(&s_ets);
    case GETV_ETS_FILLED_BINS:     return ets_epoch_filled(&s_ets);
    case GETV_ETS_READY:           return (uint16_t)ets_epoch_ready(&s_ets);
    case GETV_ETS_RESET_REASON:    return ets_epoch_reset_reason(&s_ets);
    case GETV_ETS_DIAG_SEQ:        return ets_epoch_diag_seq(&s_ets);
    case GETV_ETS_SRC_B1:          return ets_epoch_src_b1(&s_ets);
    case GETV_ETS_SRC_QUANTISED:   return ets_epoch_src_quantised(&s_ets);
    case GETV_ETS_REJECT_UNBOUND:  return ets_epoch_reject_unbound(&s_ets);
    case GETV_ETS_HW_ROLLOVER:     return ets_epoch_hw_rollover(&s_ets);
    case GETV_DIAG_HW_COUNT:       return s_diag_hw_count;
    default:               return 0U;
    }
}

static void link_handle(const struct link_cmd *cmd)
{
    static const struct link_ops ops = {
        op_timebase, op_trigger, op_span, op_run, op_stream, op_status,
        op_trig_mode, op_get_value, op_trig_slope,
        op_cal_set_offset_mv, op_cal_set_gain_x1000, op_cal_save, op_cal_reset,
        op_wave_save, op_wave_recall, op_set_avg
    };
    uint8_t status = LINK_ST_UNKNOWN;
    uint16_t value = 0U;
    uint8_t out[LINK_CMD_LEN];

    if (link_host_exec(cmd, &ops, &status, &value) == 0U) {
        return;
    }
    /* Unknown opcodes are dropped silently - no reply sent. */
    if (status == LINK_ST_UNKNOWN) {
        return;
    }
    if (link_rep_encode(out, (uint32_t)sizeof out, cmd->seq, cmd->op,
                        status, value) == (uint32_t)LINK_CMD_LEN) {
        console_write(out, (uint32_t)LINK_CMD_LEN);
    }
}

static void link_rx_byte(uint8_t b)
{
    if (s_rxlen == 0U) {
        if (b == (uint8_t)LINK_CMD_SOF0) {
            s_rxbuf[s_rxlen++] = b;
        }
        return;
    }
    if (s_rxlen == 1U) {
        if (b == (uint8_t)LINK_CMD_SOF1) {
            s_rxbuf[s_rxlen++] = b;
        } else {
            s_rxlen = (b == (uint8_t)LINK_CMD_SOF0) ? 1U : 0U;
        }
        return;
    }
    s_rxbuf[s_rxlen++] = b;
    if (s_rxlen == (uint8_t)LINK_CMD_LEN) {
        struct link_cmd cmd;

        s_rxlen = 0U;
        if (link_cmd_parse(s_rxbuf, (uint32_t)LINK_CMD_LEN, &cmd) != 0U) {
            link_handle(&cmd);
        }
    }
}

/* Drain the console receiver.  Polled on purpose: the interrupt would have to
 * outrank the acquisition and this runs once per loop iteration anyway. */
static void link_poll(void)
{
    uint8_t byte;

    while (board_uart_get_byte(&byte) != 0U) {
        link_rx_byte(byte);
    }
}

static void diag_snapshot(struct diag_state *st)
{
    uint8_t i;

    st->build_magic = status_build_magic;
    st->build_seq   = status_build_seq;
    st->boot_count  = status_boot_count;
    st->main_loops  = status_main_loops;
    st->heartbeat   = status_heartbeat;
    st->pa_idr      = GPIOA->IDR;    /* read-only: no pin is reconfigured */
    st->init_errors = status_init_errors;

    st->display_id = ili9341_read_id();
    st->madctl     = ili9341_read_madctl();
    /* publish the raw bus reads so they can be checked over SWD as well */
    status_display_id = st->display_id;

    /* The regular ADC group belongs to the acquisition engine while it is
     * running, so a scan without releasing the token came back as 0xFFFF in
     * every channel.  Pause it for the scan (bounded) and hand ADC1 back.
     * Without an engine (boot/diagnostic-only) acq_pause() reports
     * NOT_INITED and the scan simply runs on the free ADC. */
    {
        uint8_t paused = (acq_pause() == ACQ_OK) ? 1U : 0U;

        input_scan(st->adc);
        if (paused != 0U) {
            (void)acq_resume();
        }
    }
    for (i = 0U; i < DIAG_ADC_CHANNELS; i++) {
        status_adc_raw[i] = st->adc[i];
    }

    /* Front-panel ladder decode, using the vendor channel mapping
     * (SEN1 = ch6/PA6, SEN2 = ch5/PA5, CPL = ch7/PA7 - app/ladder.h).  The
     * raw rows above stay on screen, so a switch that moves a different
     * channel is immediately visible. */
    {
        uint8_t combo = 0U;
        uint8_t coupling = 0xFFU;
        uint16_t raw[3];

        raw[0] = st->adc[LADDER_CH_SEN1];
        raw[1] = st->adc[LADDER_CH_SEN2];
        raw[2] = st->adc[LADDER_CH_CPL];
        ladder_positions(raw, st->sw_pos);
        (void)ladder_decode(raw[0], raw[1], raw[2], &combo, &coupling);
        st->combo    = combo;
        st->coupling = coupling;
    }
}

/* Start the acquisition engine once, at the default timebase.  A failure is
 * recorded in the init-error row and the rest of the firmware keeps running:
 * a dead ADC must not cost the diagnostic screen or the console. */

/* ---- scope view (DSO183-UI-SPEC-001) ----------------------------------
 * Direct-to-GRAM, no framebuffer.  The plot is 300x192 at (10,18); the
 * graticule is dotted so the display bus stays quiet, and the trace is
 * updated differentially (erase pixels leaving the trace, draw pixels
 * entering it) before the grid is repainted.  Header and footer are
 * redrawn only when their contents change. */
static uint16_t s_prev_lo[UI_PLOT_W];
static uint16_t s_prev_hi[UI_PLOT_W];
static uint16_t s_cur_lo[UI_PLOT_W];
static uint16_t s_cur_hi[UI_PLOT_W];
static uint16_t s_prev_ncols;
static uint16_t s_prev_trig_y;
static uint16_t s_prev_ground_y;
static uint8_t  s_scope_has_prev;
static uint8_t  s_scope_plot_cleared;
static uint8_t  s_prev_guide_valid;
static uint32_t s_last_trig_idx;
static uint8_t  s_last_trig_valid;

static void scope_force_redraw(void)
{
    s_scope_plot_cleared = 0U;
    s_scope_has_prev = 0U;
    s_prev_guide_valid = 0U;
    s_disp_valid = 0U;
    s_last_trig_valid = 0U;
}

static void ui_point(uint16_t x, uint16_t y, uint16_t c)
{
    ili9341_fill_rect(x, y, 1U, 1U, c);
}

/* Integer square root (floor). */
static uint32_t ui_isqrt(uint32_t v)
{
    uint32_t r = 0U;
    uint32_t b = 1UL << 30;

    while (b > v) { b >>= 2; }
    while (b != 0U) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}

/* Current volts/division, in millivolts. */
static uint32_t ui_mv_per_div(void)
{
    return (s_mv != 0U) ? (uint32_t)s_mv : 100U;
}

/* Convert a signed ADC-count difference to millivolts using the scale that
 * the trace itself is drawn with (8 divisions across s_span counts). */
static int32_t ui_counts_to_mv(int32_t counts)
{
    uint32_t span = (s_span != 0U) ? (uint32_t)s_span : 4095U;
    int64_t mv = ((int64_t)counts * (int64_t)ui_mv_per_div() * 8) / (int64_t)span;
    return (int32_t)mv;
}

/* 0 V reference for the current frame: the stored zero for the decoded
 * sensitivity combo, else the frame mean (the vendor assumes mid-scale). */
static uint16_t ui_zero_ref(uint16_t mean)
{
    if ((s_combo_valid != 0U)
        && (cal_table_present(cal_table_active(), s_combo_idx) != 0U)) {
        return cal_table_zero(cal_table_active(), s_combo_idx);
    }
    return mean;
}

static uint16_t ui_frame_ref(uint16_t mean)
{
    if (s_cpl == 1U) {
        return mean;                 /* AC: trace is mean-removed */
    }
    return ui_zero_ref(mean);
}

static void ui_fmt_unsigned_mv(char *b, size_t n, uint32_t mv)
{
    if (mv >= 1000U) {
        (void)snprintf(b, n, "%lu.%02luV",
                       (unsigned long)(mv / 1000U),
                       (unsigned long)((mv % 1000U) / 10U));
    } else {
        (void)snprintf(b, n, "%lumV", (unsigned long)mv);
    }
}

static void ui_fmt_signed_mv(char *b, size_t n, int32_t mv)
{
    uint32_t a = (mv < 0) ? (uint32_t)(-mv) : (uint32_t)mv;
    char sign = (mv < 0) ? '-' : '+';

    if (a >= 1000U) {
        (void)snprintf(b, n, "%c%lu.%02luV", sign,
                       (unsigned long)(a / 1000U),
                       (unsigned long)((a % 1000U) / 10U));
    } else {
        (void)snprintf(b, n, "%c%lumV", sign, (unsigned long)a);
    }
}

static void ui_fmt_scale(char *b, size_t n)
{
    uint32_t mv = ui_mv_per_div();

    if (mv >= 1000U) {
        (void)snprintf(b, n, "%lu.%02luV/div",
                       (unsigned long)(mv / 1000U),
                       (unsigned long)((mv % 1000U) / 10U));
    } else {
        (void)snprintf(b, n, "%lumV/div", (unsigned long)mv);
    }
}

static void ui_fmt_freq(char *b, size_t n, uint32_t hz)
{
    if (hz == 0U) {
        (void)snprintf(b, n, "--");
    } else if (hz >= 1000000U) {
        (void)snprintf(b, n, "%lu.%03luMHz",
                       (unsigned long)(hz / 1000000U),
                       (unsigned long)((hz % 1000000U) / 1000U));
    } else if (hz >= 1000U) {
        (void)snprintf(b, n, "%lu.%03lukHz",
                       (unsigned long)(hz / 1000U),
                       (unsigned long)(hz % 1000U));
    } else {
        (void)snprintf(b, n, "%luHz", (unsigned long)hz);
    }
}

static void ui_fmt_period(char *b, size_t n)
{
    uint32_t hz = s_meas_freq_hz;

    if (hz == 0U) {
        (void)snprintf(b, n, "--");
    } else if (hz >= 1000U) {
        uint32_t ns = 1000000000U / hz;
        if (ns >= 1000000U) {
            (void)snprintf(b, n, "%lu.%03lums",
                           (unsigned long)(ns / 1000000U),
                           (unsigned long)((ns % 1000000U) / 1000U));
        } else if (ns >= 1000U) {
            (void)snprintf(b, n, "%lu.%03luus",
                           (unsigned long)(ns / 1000U),
                           (unsigned long)(ns % 1000U));
        } else {
            (void)snprintf(b, n, "%luns", (unsigned long)ns);
        }
    } else {
        uint32_t us = 1000000U / hz;
        if (us >= 1000U) {
            (void)snprintf(b, n, "%lu.%03lums",
                           (unsigned long)(us / 1000U),
                           (unsigned long)(us % 1000U));
        } else {
            (void)snprintf(b, n, "%luus", (unsigned long)us);
        }
    }
}

static const char *ui_cpl_label(void)
{
    switch (s_cpl) {
    case 0U: return "DC";
    case 1U: return "AC";
    case 2U: return "GND";
    default: return "--";
    }
}

/* ---- persistent header ------------------------------------------------ */
struct ui_hdr_shadow {
    uint8_t valid;
    uint8_t run;
    uint8_t mode;
    uint8_t slope;
    uint8_t state;      /* 0 WAIT, 1 TRIG'D, 2 STOPPED */
    uint8_t link;
    uint16_t level;
};
static struct ui_hdr_shadow s_hdr;

static void ui_header_draw(void)
{
    char b[24];
    uint8_t run   = runstate_run();
    uint8_t mode  = acq_trigger_mode_get();
    uint8_t slope = s_trig_slope;
    uint8_t state = (run == 0U) ? 2U
                  : (s_trig_locked != 0U) ? 1U : 0U;
    uint8_t link  = (s_console_ok != 0U) ? 1U : 0U;
    uint16_t level = acq_trigger_level_get();

    if ((s_hdr.valid != 0U) && (s_hdr.run == run) && (s_hdr.mode == mode)
        && (s_hdr.slope == slope) && (s_hdr.state == state)
        && (s_hdr.link == link) && (s_hdr.level == level)) {
        return;
    }
    s_hdr.valid = 1U; s_hdr.run = run; s_hdr.mode = mode;
    s_hdr.slope = slope; s_hdr.state = state; s_hdr.link = link;
    s_hdr.level = level;

    ili9341_fill_rect(0U, 0U, UI_W, UI_HDR_H, UI_C_HDR_BG);

    /* RUN / HOLD badge */
    {
        uint16_t bg = (run != 0U) ? UI_C_RUN_BG : UI_C_HOLD_BG;
        uint16_t fg = (run != 0U) ? UI_C_GREEN : UI_C_HOLD_FG;
        ili9341_fill_rect(2U, 2U, 30U, 12U, fg);
        ili9341_fill_rect(3U, 3U, 28U, 10U, bg);
        ili9341_print(5U, 4U, (run != 0U) ? "RUN" : "HOLD", fg, bg, 1U);
    }

    /* trigger mode */
    (void)snprintf(b, sizeof b, "TRIG:%s",
                   (mode == TRIG_MODE_NORM) ? "NORM"
                 : (mode == TRIG_MODE_SINGLE) ? "SING" : "AUTO");
    ili9341_print(36U, 5U, b, UI_C_TEXT, UI_C_HDR_BG, 1U);

    /* slope glyph, boxed edge */
    {
        const char *s = (slope == 1U) ? "[\\]" : (slope == 2U) ? "[~]" : "[/]";
        ili9341_print(94U, 5U, s, UI_C_FOCUS, UI_C_HDR_BG, 1U);
    }

    /* trigger threshold */
    {
        int32_t mv = ui_counts_to_mv((int32_t)level
                                     - (int32_t)ui_frame_ref(s_meas_mean));
        uint32_t a = (mv < 0) ? (uint32_t)(-mv) : (uint32_t)mv;
        char sign = (mv < 0) ? '-' : '+';
        if (a >= 1000U) {
            (void)snprintf(b, sizeof b, "T:%c%lu.%02luV", sign,
                           (unsigned long)(a / 1000U),
                           (unsigned long)((a % 1000U) / 10U));
        } else {
            (void)snprintf(b, sizeof b, "T:%c%lumV", sign, (unsigned long)a);
        }
        ili9341_print(116U, 5U, b, UI_C_TEXT, UI_C_HDR_BG, 1U);
    }

    /* trigger state */
    {
        const char *s = (state == 2U) ? "STOPPED"
                      : (state == 1U) ? "TRIG'D" : "WAIT";
        uint16_t c = (state == 1U) ? UI_C_GREEN
                   : (state == 2U) ? UI_C_HOLD_FG : UI_C_TRIG;
        ili9341_print(176U, 5U, s, c, UI_C_HDR_BG, 1U);
    }

    ili9341_print(234U, 5U, (link != 0U) ? "LINK:OK" : "LINK:--",
                  (link != 0U) ? UI_C_MUTED : UI_C_HOLD_FG, UI_C_HDR_BG, 1U);
    ili9341_print(292U, 5U, "[M7]", UI_C_MUTED, UI_C_HDR_BG, 1U);
    ili9341_fill_rect(0U, UI_HDR_H, UI_W, 1U, UI_C_GRID);
}

/* ---- footer ----------------------------------------------------------- */
static char s_ftr1_shown[64];
static char s_ftr_right_shown[64];
static char s_ftr2_shown[64];
static uint8_t  s_ftr_focus_shown = 0xFFU;
static uint8_t  s_ftr_field_shown = 0xFFU;

/* Erase the amber focus box left behind by the previous field. */
static void ui_footer_unfocus(uint8_t focus)
{
    uint16_t fx, fw;
    char right[48];
    const char *tb = timebase_label(acq_timebase_get());

    (void)snprintf(right, sizeof right, "TB:%s/div f:--", (tb != NULL) ? tb : "?");
    if (focus == FOCUS_TIMEBASE) {
        fx = (uint16_t)(UI_W - 6U * 24U);
        fw = (uint16_t)(6U * 24U);
    } else {
        fx = 0U;
        fw = (uint16_t)(strlen(s_ftr1_shown) * 6U + 2U);
    }
    ili9341_fill_rect(fx, (uint16_t)(UI_FOOT_R1_Y - 2U), fw, 1U, UI_C_FTR_BG);
    ili9341_fill_rect(fx, (uint16_t)(UI_FOOT_R1_Y + 8U), fw, 1U, UI_C_FTR_BG);
    ili9341_fill_rect(fx, (uint16_t)(UI_FOOT_R1_Y - 2U), 1U, 11U, UI_C_FTR_BG);
    ili9341_fill_rect((uint16_t)(fx + fw - 1U), (uint16_t)(UI_FOOT_R1_Y - 2U),
                      1U, 11U, UI_C_FTR_BG);
}

static void ui_footer_row1(uint8_t focus_active, uint8_t focus)
{
    char left[48];
    char right[48];
    char scale[24];
    char freq[20];
    uint16_t rx;
    const char *tb = timebase_label(acq_timebase_get());

    ui_fmt_scale(scale, sizeof scale);
    ui_fmt_freq(freq, sizeof freq, s_meas_freq_hz);
    (void)snprintf(left, sizeof left, "CH1: %s %s 1X", scale, ui_cpl_label());
    (void)snprintf(right, sizeof right, "TB:%s/div f:%s",
                   (tb != NULL) ? tb : "?", freq);

    if ((focus_active == s_ftr_focus_shown)
        && (focus == s_ftr_field_shown)
        && (strcmp(left, s_ftr1_shown) == 0)
        && (strcmp(right, s_ftr_right_shown) == 0)) {
        return;
    }

    if ((focus_active == 0U) && (s_ftr_focus_shown != 0U)) {
        ui_footer_unfocus(s_ftr_field_shown);
    }

    ili9341_fill_rect(0U, (uint16_t)(UI_FOOT_R1_Y - 2U), UI_W, 11U, UI_C_FTR_BG);
    ili9341_print(0U, UI_FOOT_R1_Y, left, UI_C_TEXT, UI_C_FTR_BG, 1U);

    rx = (uint16_t)(UI_W - (uint16_t)strlen(right) * 6U);
    ili9341_print(rx, UI_FOOT_R1_Y, right, UI_C_TEXT, UI_C_FTR_BG, 1U);

    if (focus_active != 0U) {
        uint16_t fx = (focus == FOCUS_TIMEBASE) ? (uint16_t)(rx - 6U) : 0U;
        uint16_t fw = (focus == FOCUS_TIMEBASE)
                      ? (uint16_t)((uint16_t)strlen(right) * 6U + 6U)
                      : (uint16_t)(strlen(left) * 6U + 2U);
        ili9341_fill_rect(fx, (uint16_t)(UI_FOOT_R1_Y - 2U), fw, 1U, UI_C_FOCUS);
        ili9341_fill_rect(fx, (uint16_t)(UI_FOOT_R1_Y + 8U), fw, 1U, UI_C_FOCUS);
        ili9341_fill_rect(fx, (uint16_t)(UI_FOOT_R1_Y - 2U), 1U, 11U, UI_C_FOCUS);
        ili9341_fill_rect((uint16_t)(fx + fw - 1U), (uint16_t)(UI_FOOT_R1_Y - 2U),
                          1U, 11U, UI_C_FOCUS);
    }

    (void)snprintf(s_ftr1_shown, sizeof s_ftr1_shown, "%s", left);
    (void)snprintf(s_ftr_right_shown, sizeof s_ftr_right_shown, "%s", right);
    s_ftr_focus_shown = focus_active;
    s_ftr_field_shown = focus;
}

static uint8_t s_ftr2_focus_shown = 0xFFU;

/* ---- fixed-column measurement row -----------------------------------
 * The old measurement bar printed one variable-width string from x=0, so
 * every time a value changed width ("999mV" -> "1.02V") every field to its
 * right jumped sideways.  Four fixed 80-pixel cells (13 characters) give
 * each field a home; a cell is repainted only when its own text changes,
 * so the row is stable and cheap. */
#define FTR2_CELLS      4U
#define FTR2_CELL_W     (UI_W / FTR2_CELLS)          /* 80 px               */
#define FTR2_CELL_CHARS (FTR2_CELL_W / 6U)           /* 13 characters       */
static char    s_ftr2_cell[FTR2_CELLS][FTR2_CELL_CHARS + 1U];
static uint8_t s_ftr2_mode;                       /* 0 full row, 1 cells */
static uint8_t s_ftr2_cell_focus = 0xFFU;

static void ui_footer_row2(const char *text, uint8_t focus_active)
{
    uint16_t fg = (focus_active != 0U) ? UI_C_FOCUS : UI_C_TEXT;

    /* Leaving the fixed-cell measurement layout: force a full-row repaint
     * even if the text happens to match a cached string. */
    if (s_ftr2_mode != 0U) {
        s_ftr2_mode = 0U;
        s_ftr2_shown[0] = '\0';
    }
    if ((strcmp(text, s_ftr2_shown) == 0) && (focus_active == s_ftr2_focus_shown)) {
        return;
    }
    (void)snprintf(s_ftr2_shown, sizeof s_ftr2_shown, "%s", text);
    s_ftr2_focus_shown = focus_active;
    ili9341_fill_rect(0U, (uint16_t)(UI_FOOT_R2_Y - 1U), UI_W, 10U, UI_C_FTR_BG);
    ili9341_print(0U, UI_FOOT_R2_Y, text, fg, UI_C_FTR_BG, 1U);
}

/* ---- fixed-column measurement row ----------------------------------- */

static void ui_footer_row2_cells(const char *const cell[FTR2_CELLS],
                                 uint8_t focus_active)
{
    uint16_t fg = (focus_active != 0U) ? UI_C_FOCUS : UI_C_TEXT;
    uint8_t i;

    if ((s_ftr2_mode != 1U) || (s_ftr2_cell_focus != focus_active)) {
        ili9341_fill_rect(0U, (uint16_t)(UI_FOOT_R2_Y - 1U), UI_W, 10U,
                          UI_C_FTR_BG);
        for (i = 0U; i < FTR2_CELLS; i++) {
            s_ftr2_cell[i][0] = '\0';
        }
        s_ftr2_mode = 1U;
        s_ftr2_cell_focus = focus_active;
        s_ftr2_shown[0] = '\0';       /* full-row cache is stale now */
    }

    for (i = 0U; i < FTR2_CELLS; i++) {
        char buf[FTR2_CELL_CHARS + 1U];
        uint16_t x = (uint16_t)((uint16_t)i * FTR2_CELL_W);

        (void)snprintf(buf, sizeof buf, "%s",
                       (cell[i] != NULL) ? cell[i] : "");
        if (strcmp(buf, s_ftr2_cell[i]) == 0) {
            continue;                 /* this cell is already correct */
        }
        (void)snprintf(s_ftr2_cell[i], sizeof s_ftr2_cell[i], "%s", buf);
        ili9341_fill_rect(x, (uint16_t)(UI_FOOT_R2_Y - 1U), FTR2_CELL_W, 10U,
                          UI_C_FTR_BG);
        ili9341_print(x, UI_FOOT_R2_Y, buf, fg, UI_C_FTR_BG, 1U);
    }
}

static void ui_invalidate_chrome(void)
{
    s_hdr.valid = 0U;
    s_ftr1_shown[0] = '\0';
    s_ftr_right_shown[0] = '\0';
    s_ftr2_shown[0] = '\0';
    s_ftr2_mode = 0U;                 /* whatever was on row 2 is stale */
    s_ftr2_cell_focus = 0xFFU;
    s_ftr_focus_shown = 0xFFU;
    s_ftr_field_shown = 0xFFU;
}

static void ui_footer_draw(void)
{
    char b[128];
    char v1[20], v2[20], v3[20], v4[20];
    char f[20];
    uint32_t focus_on = 0U;
    uint8_t focus = FOCUS_TIMEBASE;
    uint32_t now = HAL_GetTick();
    uint8_t note_live;
    static uint8_t s_cpl_prev = 0xFFU;

    /* Front-panel CPL change: stock prompts "Set CPL (SW1) to ..."; the
     * switch is hardware-only here, so report the position it now holds. */
    if ((s_cpl <= 2U) && (s_cpl != s_cpl_prev)) {
        (void)snprintf(s_menu.note, sizeof s_menu.note, "CPL (SW1) set to %s",
                       (s_cpl == 0U) ? "DC" : (s_cpl == 1U) ? "AC" : "GND");
        s_menu.note_ms = now;
        s_cpl_prev = s_cpl;
    }

    if (s_menu.state == MENU_NONE) {
        uint32_t idle = now - s_menu.focus_ms;
        focus_on = (idle < 3000U) ? 1U : 0U;
    }
    focus = (uint8_t)(s_menu.focus % FOCUS_COUNT);
    note_live = (uint8_t)((s_menu.note[0] != '\0')
                          && ((now - s_menu.note_ms) < 2500U));

    ili9341_fill_rect(0U, UI_FOOT_SEP_Y, UI_W, 1U, UI_C_GRID);
    ui_footer_row1((uint8_t)focus_on, focus);

    if (note_live != 0U) {
        ui_footer_row2(s_menu.note, 1U);
        return;
    }

    if (focus_on != 0U) {
        const char *label;
        char val[24];

        switch ((enum menu_focus)focus) {
        case FOCUS_TIMEBASE:
            label = "TB";
            (void)snprintf(val, sizeof val, "%s/div",
                           timebase_label(acq_timebase_get()));
            break;
        case FOCUS_TRIGGER_MODE:
            label = "TRIG";
            (void)snprintf(val, sizeof val, "%s",
                           (acq_trigger_mode_get() == TRIG_MODE_NORM) ? "NORM"
                         : (acq_trigger_mode_get() == TRIG_MODE_SINGLE) ? "SINGLE"
                         : "AUTO");
            break;
        case FOCUS_TRIGGER_LEVEL:
            label = "LEVEL";
            (void)snprintf(val, sizeof val, "%u",
                           (unsigned)acq_trigger_level_get());
            break;
        default:
            label = "SLOPE";
            (void)snprintf(val, sizeof val, "%s",
                           (s_trig_slope == 1U) ? "FALL"
                         : (s_trig_slope == 2U) ? "AUTO" : "RISE");
            break;
        }
        (void)snprintf(b, sizeof b, ">ADJ %s:%s  SEL:Next +/-:Step OK:Run",
                       label, val);
        ui_footer_row2(b, 1U);
        return;
    }

    /* Measurement bar.  Values are trace-consistent: the same counts-to-mV
     * scale the plot uses, referenced to the stored 0 V level. */
    {
        uint16_t ref = ui_frame_ref(s_meas_mean);
        int32_t vpp_mv, max_mv, min_mv, rms_mv;
        uint16_t rms_ref;
        uint32_t var;

        if (s_cpl == 2U) {
            vpp_mv = 0; max_mv = 0; min_mv = 0; rms_mv = 0;
        } else {
            vpp_mv = ui_counts_to_mv((int32_t)s_meas_vpp);
            max_mv = ui_counts_to_mv((int32_t)s_meas_max - (int32_t)ref);
            min_mv = ui_counts_to_mv((int32_t)s_meas_min - (int32_t)ref);
            var = (uint32_t)s_meas_vrms * (uint32_t)s_meas_vrms;
            var -= ((uint32_t)s_meas_mean * (uint32_t)s_meas_mean);
            var += ((uint32_t)((int32_t)s_meas_mean - (int32_t)ref)
                    * (uint32_t)((int32_t)s_meas_mean - (int32_t)ref));
            rms_ref = (uint16_t)ui_isqrt(var);
            rms_mv = ui_counts_to_mv((int32_t)rms_ref);
        }

        ui_fmt_freq(f, sizeof f, s_meas_freq_hz);

        {
            char c0[32], c1[32], c2[32], c3[32];
            const char *cells[FTR2_CELLS];

            c0[0] = c1[0] = c2[0] = c3[0] = '\0';
            switch (s_menu.meas_mode) {
            case 1U: /* AV+ / AV- */
                ui_fmt_signed_mv(v1, sizeof v1, s_avp_mv);
                ui_fmt_signed_mv(v2, sizeof v2, s_avn_mv);
                (void)snprintf(c0, sizeof c0, "AV+:%s", v1);
                (void)snprintf(c1, sizeof c1, "AV-:%s", v2);
                (void)snprintf(c2, sizeof c2, "F:%s", f);
                break;
            case 2U: /* Period / duty */
                ui_fmt_period(v1, sizeof v1);
                (void)snprintf(c0, sizeof c0, "Per:%s", v1);
                (void)snprintf(c1, sizeof c1, "Duty:%u.%u%%",
                               (unsigned)(s_meas_duty_x10 / 10U),
                               (unsigned)(s_meas_duty_x10 % 10U));
                (void)snprintf(c2, sizeof c2, "F:%s", f);
                break;
            case 3U: /* All Disp. */
                ui_fmt_unsigned_mv(v1, sizeof v1, (vpp_mv < 0) ? 0U : (uint32_t)vpp_mv);
                ui_fmt_unsigned_mv(v2, sizeof v2, (rms_mv < 0) ? 0U : (uint32_t)rms_mv);
                ui_fmt_signed_mv(v3, sizeof v3, s_avp_mv);
                ui_fmt_signed_mv(v4, sizeof v4, s_avn_mv);
                (void)snprintf(c0, sizeof c0, "Vpp:%s", v1);
                (void)snprintf(c1, sizeof c1, "Vrms:%s", v2);
                (void)snprintf(c2, sizeof c2, "AV+:%s", v3);
                (void)snprintf(c3, sizeof c3, "AV-:%s", v4);
                break;
            default: /* Vpp / Vrms / Vmax / Vmin */
                ui_fmt_unsigned_mv(v1, sizeof v1, (vpp_mv < 0) ? 0U : (uint32_t)vpp_mv);
                ui_fmt_unsigned_mv(v2, sizeof v2, (rms_mv < 0) ? 0U : (uint32_t)rms_mv);
                ui_fmt_signed_mv(v3, sizeof v3, max_mv);
                ui_fmt_signed_mv(v4, sizeof v4, min_mv);
                (void)snprintf(c0, sizeof c0, "Vpp:%s", v1);
                (void)snprintf(c1, sizeof c1, "Vrms:%s", v2);
                (void)snprintf(c2, sizeof c2, "Vmax:%s", v3);
                (void)snprintf(c3, sizeof c3, "Vmin:%s", v4);
                break;
            }
            cells[0] = c0;
            cells[1] = c1;
            cells[2] = c2;
            cells[3] = c3;
            ui_footer_row2_cells(cells, 0U);
        }
    }
}

/* ---- graticule -------------------------------------------------------- */
static void ui_plot_grid(void)
{
    uint16_t i, x, y;

    for (i = 1U; i < UI_PLOT_DIV_X; i++) {
        x = (uint16_t)(UI_PLOT_X + i * UI_PLOT_PX_DIV_X);
        for (y = UI_PLOT_Y; y < (uint16_t)(UI_PLOT_Y + UI_PLOT_H); y = (uint16_t)(y + 4U)) {
            ui_point(x, y, UI_C_GRID);
        }
    }
    for (i = 1U; i < UI_PLOT_DIV_Y; i++) {
        y = (uint16_t)(UI_PLOT_Y + i * UI_PLOT_PX_DIV_Y);
        for (x = UI_PLOT_X; x < (uint16_t)(UI_PLOT_X + UI_PLOT_W); x = (uint16_t)(x + 4U)) {
            ui_point(x, y, UI_C_GRID);
        }
    }

    /* centre axes and graduation ticks */
    {
        uint16_t cx = (uint16_t)(UI_PLOT_X + UI_PLOT_W / 2U);
        uint16_t cy = (uint16_t)(UI_PLOT_Y + UI_PLOT_H / 2U);

        for (x = UI_PLOT_X; x < (uint16_t)(UI_PLOT_X + UI_PLOT_W); x = (uint16_t)(x + 2U)) {
            ui_point(x, cy, UI_C_GRID_HI);
        }
        for (y = UI_PLOT_Y; y < (uint16_t)(UI_PLOT_Y + UI_PLOT_H); y = (uint16_t)(y + 2U)) {
            ui_point(cx, y, UI_C_GRID_HI);
        }
        for (x = UI_PLOT_X; x < (uint16_t)(UI_PLOT_X + UI_PLOT_W); x = (uint16_t)(x + 6U)) {
            uint16_t len = (((x - UI_PLOT_X) % UI_PLOT_PX_DIV_X) == 0U) ? 3U : 1U;
            for (i = 1U; i <= len; i++) {
                ui_point(x, (uint16_t)(cy - i), UI_C_GRID_HI);
                ui_point(x, (uint16_t)(cy + i), UI_C_GRID_HI);
            }
        }
        for (y = UI_PLOT_Y; y < (uint16_t)(UI_PLOT_Y + UI_PLOT_H); y = (uint16_t)(y + 6U)) {
            uint16_t len = (((y - UI_PLOT_Y) % UI_PLOT_PX_DIV_Y) == 0U) ? 2U : 1U;
            for (i = 1U; i <= len; i++) {
                ui_point((uint16_t)(cx - i), y, UI_C_GRID_HI);
                ui_point((uint16_t)(cx + i), y, UI_C_GRID_HI);
            }
        }
    }

    /* border */
    ili9341_fill_rect(UI_PLOT_X, UI_PLOT_Y, UI_PLOT_W, 1U, UI_C_BORDER);
    ili9341_fill_rect(UI_PLOT_X, (uint16_t)(UI_PLOT_Y + UI_PLOT_H - 1U),
                      UI_PLOT_W, 1U, UI_C_BORDER);
    ili9341_fill_rect(UI_PLOT_X, UI_PLOT_Y, 1U, UI_PLOT_H, UI_C_BORDER);
    ili9341_fill_rect((uint16_t)(UI_PLOT_X + UI_PLOT_W - 1U), UI_PLOT_Y,
                      1U, UI_PLOT_H, UI_C_BORDER);
}

/* ---- margin flags ----------------------------------------------------- */
static void ui_plot_margins(uint16_t ground_y, uint16_t trig_y)
{
    int16_t r;

    /* left ground reference: 1 + right-pointing triangle */
    ili9341_fill_rect(0U, UI_PLOT_Y, UI_PLOT_X, UI_PLOT_H, UI_C_BG);
    ili9341_print(0U, (uint16_t)(ground_y - 4U), "1", UI_C_TRACE, UI_C_BG, 1U);
    for (r = -3; r <= 3; r++) {
        uint16_t w = (uint16_t)(4 - (r < 0 ? -r : r));
        ili9341_fill_rect(6U, (uint16_t)((int16_t)ground_y + r), w, 1U, UI_C_TRACE);
    }

    /* right trigger flag: left-pointing triangle + T */
    ili9341_fill_rect((uint16_t)(UI_PLOT_X + UI_PLOT_W), UI_PLOT_Y, 10U,
                      UI_PLOT_H, UI_C_BG);
    for (r = -3; r <= 3; r++) {
        uint16_t w = (uint16_t)(4 - (r < 0 ? -r : r));
        ili9341_fill_rect((uint16_t)(313 - w), (uint16_t)((int16_t)trig_y + r), w, 1U, UI_C_TRIG);
    }
    ili9341_print(313U, (uint16_t)(trig_y - 4U), "T", UI_C_TRIG, UI_C_BG, 1U);
}

static void scope_draw(const struct frame *f)
{
    memdiag_msp_sample();
    struct wave_geom  geom = { UI_PLOT_Y, UI_PLOT_H };
    struct wave_range range;
    uint16_t trig_y;
    uint16_t ground_y;
    uint16_t ncols;
    uint16_t i;
    uint16_t centre;
    int32_t  shift = 0;

    if ((f == NULL) || (f->length == 0U) || (f->stats.n == 0U)) {
        return;
    }

    /* Range centre: AC removes the mean, GND parks on the 0 V reference,
     * DC centres on the stored zero (or the frame mean). */
    if (s_cpl == 1U) {
        centre = 2048U;
        shift = (int32_t)2048U - (int32_t)f->stats.mean;
    } else if (s_cpl == 2U) {
        centre = ui_zero_ref(f->stats.mean);
    } else {
        centre = ui_zero_ref(f->stats.mean);
    }

    {
        uint32_t span = s_span;
        uint32_t lo;

        if (span < 16U)   { span = 16U; }
        if (span > 4095U) { span = 4095U; }
        lo = (centre > (span / 2U)) ? ((uint32_t)centre - span / 2U) : 0U;
        {
            uint32_t hi = lo + span;
            if (hi > 4095U) { hi = 4095U; }
            range.v_min = (uint16_t)lo;
            range.v_max = (uint16_t)hi;
        }
    }

    trig_y = wave_y((uint16_t)((int32_t)acq_trigger_level_get() + shift),
                    &range, &geom);
    ground_y = wave_y(centre, &range, &geom);
    if (trig_y > (uint16_t)(UI_PLOT_Y + UI_PLOT_H - 1U)) { trig_y = (uint16_t)(UI_PLOT_Y + UI_PLOT_H - 1U); }
    if (ground_y > (uint16_t)(UI_PLOT_Y + UI_PLOT_H - 1U)) { ground_y = (uint16_t)(UI_PLOT_Y + UI_PLOT_H - 1U); }

    if ((s_scope_first != 0U) || (s_scope_plot_cleared == 0U)) {
        ili9341_fill_rect(0U, UI_PLOT_Y, UI_W, UI_PLOT_H, UI_C_BG);
        s_scope_plot_cleared = 1U;
        s_scope_has_prev = 0U;
        s_prev_guide_valid = 0U;
        s_disp_valid = 0U;
    }

    ncols = wave_column_means(s_frame, f->length, UI_PLOT_W, s_means, UI_PLOT_W);

    if (s_cpl == 2U) {
        ncols = 0U;              /* GND: flat baseline, no waveform */
    } else if (s_disp_valid == 0U) {
        for (i = 0U; i < ncols; i++) {
            int32_t v = (int32_t)s_means[i] + shift;
            if (v < 0) { v = 0; } else if (v > 4095) { v = 4095; }
            s_disp[i] = (uint16_t)v;
        }
        s_disp_valid = 1U;
    } else {
        /* Temporal smoothing to reduce noise/jaggedness.  With the trigger
         * now anchored to the hardware event successive frames land on the
         * same phase, so this does not blend two positions into a ghost
         * trace.  Large changes (signal appears/disappears, vertical shift)
         * must track quickly or the trace "builds" and "fades" over seconds,
         * so use a fast alpha when the step is big and a gentle one for the
         * small sample-to-sample noise on a steady trace. */
        for (i = 0U; i < ncols; i++) {
            int32_t v = (int32_t)s_means[i] + shift;
            int32_t diff;

            if (v < 0) { v = 0; } else if (v > 4095) { v = 4095; }
            diff = v - (int32_t)s_disp[i];
            if (diff < 0) { diff = -diff; }
            if (diff > 64) {
                /* Big move: follow immediately (keeps transitions crisp). */
                s_disp[i] = (uint16_t)v;
            } else if (diff > 16) {
                /* Moderate: 50/50 to settle quickly. */
                s_disp[i] = (uint16_t)(((uint32_t)s_disp[i] + (uint32_t)v + 1U) / 2U);
            } else {
                /* Steady-state noise: gentle smoothing. */
                s_disp[i] = (uint16_t)(((uint32_t)s_disp[i] * 3U + (uint32_t)v + 2U) / 4U);
            }
        }
    }

    for (i = 0U; i < ncols; i++) {
        uint16_t yy = wave_y(s_disp[i], &range, &geom);
        uint16_t lo = yy, hi = yy;
        if (i > 0U) {
            uint16_t py = wave_y(s_disp[i - 1U], &range, &geom);
            if (py < lo) lo = py;
            if (py > hi) hi = py;
        }
        s_cur_lo[i] = lo;
        s_cur_hi[i] = hi;
    }

    /* erase the previous trigger guide before the grid is repainted */
    if (s_prev_guide_valid != 0U) {
        ili9341_fill_rect(UI_PLOT_X, s_prev_trig_y, UI_PLOT_W, 1U, UI_C_BG);
    }

    if (s_scope_has_prev != 0U) {
        /* Erase only what the previous trace occupied and the new one does
         * not.  The current trace is drawn once, after the grid, below -
         * drawing it here as well made every frame repaint the whole trace
         * twice and contributed to the visible "rebuilding" sweep. */
        for (i = 0U; i < s_prev_ncols; i++) {
            uint16_t plo = s_prev_lo[i], phi = s_prev_hi[i];
            uint16_t clo = (i < ncols) ? s_cur_lo[i] : 0xFFFFU;
            uint16_t chi = (i < ncols) ? s_cur_hi[i] : 0U;
            if ((i >= ncols) || (phi < clo) || (plo > chi)) {
                ili9341_fill_rect((uint16_t)(UI_PLOT_X + i), plo, 1U,
                                  (uint16_t)(phi - plo + 1U), UI_C_BG);
            } else {
                if (plo < clo) {
                    ili9341_fill_rect((uint16_t)(UI_PLOT_X + i), plo, 1U,
                                      (uint16_t)(clo - plo), UI_C_BG);
                }
                if (phi > chi) {
                    ili9341_fill_rect((uint16_t)(UI_PLOT_X + i), (uint16_t)(chi + 1U),
                                      1U, (uint16_t)(phi - chi), UI_C_BG);
                }
            }
        }
    }

    ui_plot_grid();

    /* trigger guide: dashed amber line at the crossing level */
    for (i = 0U; i < UI_PLOT_W; i = (uint16_t)(i + 12U)) {
        uint16_t w = ((uint16_t)(UI_PLOT_W - i) > 6U) ? 6U : (uint16_t)(UI_PLOT_W - i);
        ili9341_fill_rect((uint16_t)(UI_PLOT_X + i), trig_y, w, 1U, UI_C_TRIG);
    }
    s_prev_trig_y = trig_y;
    s_prev_guide_valid = 1U;

    /* trace over the grid (and track it for differential erase). */
    if (s_cpl == 2U) {
        uint16_t prev_ground = s_prev_ground_y;

        if ((s_scope_has_prev != 0U) && (s_prev_ncols == 1U)
            && (prev_ground != ground_y)) {
            ili9341_fill_rect(UI_PLOT_X, prev_ground, UI_PLOT_W, 1U, UI_C_BG);
        }
        ili9341_fill_rect(UI_PLOT_X, ground_y, UI_PLOT_W, 1U, UI_C_TRACE);
        s_prev_lo[0] = ground_y;
        s_prev_hi[0] = ground_y;
        s_prev_ncols = 1U;
    } else {
        for (i = 0U; i < ncols; i++) {
            ili9341_fill_rect((uint16_t)(UI_PLOT_X + i), s_cur_lo[i], 1U,
                              (uint16_t)(s_cur_hi[i] - s_cur_lo[i] + 1U), UI_C_TRACE);
            s_prev_lo[i] = s_cur_lo[i];
            s_prev_hi[i] = s_cur_hi[i];
        }
        s_prev_ncols = ncols;
    }
    s_prev_ground_y = ground_y;
    s_scope_has_prev = 1U;

    ui_plot_margins(ground_y, trig_y);
}

/* ---- menu -> main requests ------------------------------------------- */
static void ui_handle_menu_requests(void)
{
    uint8_t req = s_menu.wave_req;
    uint32_t now = HAL_GetTick();

    if (req == 0U) {
        return;
    }
    s_menu.wave_req = 0U;

    if (req == 1U) {
        uint8_t ok = op_wave_save(s_menu.wave_slot);
        (void)snprintf(s_menu.note, sizeof s_menu.note,
                       (ok != 0U) ? "SAVED slot %u" : "SAVE failed",
                       (unsigned)s_menu.wave_slot);
    } else if (op_wave_recall(s_menu.wave_slot) != 0U) {
        s_menu.state = MENU_NONE;
        s_menu.diag_open = 0U;
        (void)snprintf(s_menu.note, sizeof s_menu.note, "RECALLED slot %u",
                       (unsigned)s_menu.wave_slot);
        ui_invalidate_chrome();
        scope_force_redraw();
    } else {
        (void)snprintf(s_menu.note, sizeof s_menu.note, "SLOT EMPTY");
    }
    s_menu.note_ms = now;
}

/* ---- Display2: large numeric readout (stock "Display2") --------------- */
static void ui_big_draw(void)
{
    static uint32_t s_sig;
    static uint8_t  s_valid;
    char v[24], r[24], f[20], p[20], ap[20], an[20];
    char line[48];
    uint32_t sig;
    uint16_t ref = ui_frame_ref(s_meas_mean);
    uint16_t rms_ref;
    uint32_t var;
    int32_t vpp_mv, rms_mv;
    uint16_t y;

    if (s_cpl == 2U) {
        vpp_mv = 0; rms_mv = 0;
    } else {
        vpp_mv = ui_counts_to_mv((int32_t)s_meas_vpp);
        var = (uint32_t)s_meas_vrms * (uint32_t)s_meas_vrms;
        var -= ((uint32_t)s_meas_mean * (uint32_t)s_meas_mean);
        var += ((uint32_t)((int32_t)s_meas_mean - (int32_t)ref)
                * (uint32_t)((int32_t)s_meas_mean - (int32_t)ref));
        rms_ref = (uint16_t)ui_isqrt(var);
        rms_mv = ui_counts_to_mv((int32_t)rms_ref);
    }
    ui_fmt_unsigned_mv(v, sizeof v, (vpp_mv < 0) ? 0U : (uint32_t)vpp_mv);
    ui_fmt_unsigned_mv(r, sizeof r, (rms_mv < 0) ? 0U : (uint32_t)rms_mv);
    ui_fmt_freq(f, sizeof f, s_meas_freq_hz);
    ui_fmt_period(p, sizeof p);
    ui_fmt_signed_mv(ap, sizeof ap, s_avp_mv);
    ui_fmt_signed_mv(an, sizeof an, s_avn_mv);

    sig = (uint32_t)(v[0] ^ r[0] ^ f[0] ^ p[0]) * 131U
        + (uint32_t)(v[1] & 0x7FU) + ((uint32_t)runstate_run() << 20)
        + ((uint32_t)s_menu.meas_mode << 21U);
    if ((s_valid != 0U) && (sig == s_sig)) {
        return;
    }
    s_valid = 1U;
    s_sig = sig;

    ili9341_fill_rect(0U, UI_PLOT_Y, UI_W, UI_PLOT_H, UI_C_BG);
    ili9341_print(UI_PLOT_X, (uint16_t)(UI_PLOT_Y + 4U),
                  "DISPLAY2  LARGE READOUT", UI_C_MUTED, UI_C_BG, 1U);
    ili9341_fill_rect(UI_PLOT_X, (uint16_t)(UI_PLOT_Y + 14U),
                      UI_PLOT_W, 1U, UI_C_GRID);

    y = (uint16_t)(UI_PLOT_Y + 26U);
    (void)snprintf(line, sizeof line, "Vpp %s", v);
    ili9341_print(UI_PLOT_X + 8U, y, line, UI_C_TRACE, UI_C_BG, 2U);
    (void)snprintf(line, sizeof line, "Vrms %s", r);
    ili9341_print(UI_PLOT_X + 8U, (uint16_t)(y + 30U), line, UI_C_TRACE, UI_C_BG, 2U);
    (void)snprintf(line, sizeof line, "Freq %s", f);
    ili9341_print(UI_PLOT_X + 8U, (uint16_t)(y + 60U), line, UI_C_TEXT, UI_C_BG, 2U);
    (void)snprintf(line, sizeof line, "Cycle %s", p);
    ili9341_print(UI_PLOT_X + 8U, (uint16_t)(y + 90U), line, UI_C_TEXT, UI_C_BG, 2U);
    (void)snprintf(line, sizeof line, "AV+:%s AV-:%s", ap, an);
    ili9341_print(UI_PLOT_X + 8U, (uint16_t)(y + 120U), line, UI_C_MUTED, UI_C_BG, 1U);
    ili9341_print(UI_PLOT_X + 8U, (uint16_t)(y + 134U),
                  (runstate_run() != 0U) ? "RUN - live input" : "HOLD - frozen",
                  UI_C_MUTED, UI_C_BG, 1U);
}

/* Re-render the last captured frame (used when an overlay closes while the
 * instrument is in HOLD and no new acquisition arrives). */
static void ui_repaint_scope(void)
{
    struct frame rf;

    if (s_frame_len == 0U) {
        return;
    }
    memset(&rf, 0, sizeof rf);
    rf.length = s_frame_len;
    (void)measure_compute(s_frame, s_frame_len, &rf.stats);
    scope_draw(&rf);
}

/* ---- SYSTEM DIAG full-screen telemetry (stock "Board info") ---------- */
static uint8_t s_diag_force;
static uint8_t s_diag_painted;

/* The old page rewrote every row, the cal block and three status lines five
 * times a second, so the counters were unreadable.  Two seconds is legible,
 * and each line is now compared with what is already on the glass so only
 * genuinely changed text is sent.  The caches cost 558 bytes of .bss. */
#define DIAG_REFRESH_MS 2000U
static char s_diag_rows_prev[DIAG_ROWS][DIAG_COLS];
static char s_diag_cal_prev[CAL_COMBO_COUNT][16];
static char s_diag_status_prev[3][48];

static void diag_print_changed(uint16_t x, uint16_t y, const char *text,
                               uint16_t fg, char *prev, size_t prev_size)
{
    if (strcmp(prev, text) == 0) {
        return;
    }
    ili9341_print(x, y, text, fg, UI_C_BG, 1U);
    (void)snprintf(prev, prev_size, "%s", text);
}

static void ui_diag_draw(void)
{
    static uint32_t s_last_ms;
    char rows[DIAG_ROWS][DIAG_COLS];
    struct diag_state st;
    uint32_t now = HAL_GetTick();
    uint8_t i;
    uint8_t full;

    if ((s_diag_force == 0U) && (s_diag_painted != 0U)
        && ((now - s_last_ms) < DIAG_REFRESH_MS)) {
        return;
    }
    full = (s_diag_force != 0U) || (s_diag_painted == 0U);
    s_diag_force = 0U;
    s_last_ms = now;

    diag_snapshot(&st);
    diag_rows(rows, &st);

    if (full != 0U) {
        ili9341_fill_screen(UI_C_BG);
        ili9341_print(2U, 2U, "SYSTEM DIAG", UI_C_TRACE, UI_C_BG, 1U);
        ili9341_print(320U - 9U * 6U, 2U, "[OK:BACK]", UI_C_MUTED, UI_C_BG, 1U);
        ili9341_fill_rect(2U, 12U, 316U, 1U, UI_C_GRID);
        ili9341_print(2U, 15U, "Board info:", UI_C_TEXT, UI_C_BG, 1U);
        ili9341_print(2U, 25U, "CFW: 113-18301-070", UI_C_TRIG, UI_C_BG, 1U);
        ili9341_fill_rect(2U, 35U, 316U, 1U, UI_C_GRID);
        ili9341_print(2U, 140U, "CAL ZEROS (ADC counts, - = unset)",
                      UI_C_MUTED, UI_C_BG, 1U);
        /* Force the first pass to paint every line. */
        memset(s_diag_rows_prev, 0, sizeof s_diag_rows_prev);
        memset(s_diag_cal_prev, 0, sizeof s_diag_cal_prev);
        memset(s_diag_status_prev, 0, sizeof s_diag_status_prev);
        s_diag_painted = 1U;
    }

    for (i = 0U; i < DIAG_ROWS; i++) {
        diag_print_changed(2U, (uint16_t)(38U + (uint16_t)i * 10U), rows[i],
                           (i == 0U) ? UI_C_MUTED : UI_C_TEXT,
                           s_diag_rows_prev[i], sizeof s_diag_rows_prev[i]);
    }

    for (i = 0U; i < CAL_COMBO_COUNT; i++) {
        char b[16];
        uint16_t x = (uint16_t)(2U + (uint16_t)(i % 5U) * 58U);
        uint16_t y = (uint16_t)(150U + (uint16_t)(i / 5U) * 10U);

        if (cal_table_present(cal_table_active(), i) != 0U) {
            (void)snprintf(b, sizeof b, "C%u:%-4u", (unsigned)i,
                           (unsigned)cal_table_zero(cal_table_active(), i));
        } else {
            (void)snprintf(b, sizeof b, "C%u:-   ", (unsigned)i);
        }
        diag_print_changed(x, y, b, UI_C_TRIG,
                           s_diag_cal_prev[i], sizeof s_diag_cal_prev[i]);
    }

    {
        char b[48];
        (void)snprintf(b, sizeof b, "RUN %-3s AVG %-2u CPL %-4s SLP %-4s    ",
                       (runstate_run() != 0U) ? "YES" : "NO",
                       (unsigned)s_avg_n, ui_cpl_label(),
                       (s_trig_slope == 1U) ? "FALL"
                       : (s_trig_slope == 2U) ? "AUTO" : "RISE");
        diag_print_changed(2U, 172U, b, UI_C_TEXT,
                           s_diag_status_prev[0], sizeof s_diag_status_prev[0]);
        (void)snprintf(b, sizeof b, "TB %-8s TRIG %-4s LVL %-4u      ",
                       timebase_label(acq_timebase_get()),
                       (acq_trigger_mode_get() == TRIG_MODE_NORM) ? "NORM"
                       : (acq_trigger_mode_get() == TRIG_MODE_SINGLE) ? "SING"
                       : "AUTO",
                       (unsigned)acq_trigger_level_get());
        diag_print_changed(2U, 182U, b, UI_C_TEXT,
                           s_diag_status_prev[1], sizeof s_diag_status_prev[1]);
        (void)snprintf(b, sizeof b, "STK n%4u wm%4u msp%4u HP%3u/%3u",
                       (unsigned)memdiag_stack_free_now(),
                       (unsigned)memdiag_stack_free_min(),
                       (unsigned)memdiag_msp_free_min(),
                       (unsigned)memdiag_heap_used(),
                       (unsigned)memdiag_heap_highwater());
        diag_print_changed(2U, 192U, b, UI_C_TEXT,
                           s_diag_status_prev[2], sizeof s_diag_status_prev[2]);
        (void)snprintf(b, sizeof b, "SPEC DSO183-UI-SPEC-001  %-7s    ",
                       (s_menu.meas_mode == 0U) ? "ALL"
                       : (s_menu.meas_mode == 1U) ? "AV"
                       : (s_menu.meas_mode == 2U) ? "CYCLE" : "ALLDISP");
        /* Reuse prev[2] cache slot was replaced above; keep spec static on a
         * non-cached direct print only when changed by full repaint logic not
         * tracked separately here. */
        ili9341_print(2U, 202U, b, UI_C_MUTED, UI_C_BG, 1U);
    }
}

/* ---- chrome / overlay transition ------------------------------------- */
static void ui_transition_poll(void)
{
    static uint8_t p_chrome = 0xFFU;
    static uint8_t p_diag = 0xFFU;
    static uint8_t p_disp = 0xFFU;
    uint8_t diag = (s_menu.diag_open != 0U) ? 1U : 0U;
    uint8_t chrome = ((s_menu.state == MENU_NONE) && (diag == 0U)) ? 1U : 0U;
    uint8_t disp = (s_menu.disp_mode != 0U) ? 1U : 0U;

    if ((chrome == p_chrome) && (diag == p_diag) && (disp == p_disp)) {
        return;
    }

    if (p_diag == 1U) {
        /* leaving the full-screen telemetry page: nothing is reusable */
        ili9341_fill_screen(UI_C_BG);
    } else if (chrome != 0U) {
        /* the settings card or a mode change dirtied the plot */
        ili9341_fill_rect(0U, UI_PLOT_Y, UI_W, UI_PLOT_H, UI_C_BG);
    }

    ui_invalidate_chrome();
    scope_force_redraw();
    s_diag_force = 1U;
    p_chrome = chrome;
    p_diag = diag;
    p_disp = disp;

    if (chrome != 0U) {
        if (disp != 0U) {
            ui_big_draw();
        } else {
            ui_repaint_scope();
        }
    }
}

static void ui_chrome_draw(void)
{
    if (s_menu.diag_open != 0U) {
        ui_diag_draw();
        return;
    }
    ui_header_draw();
    ui_footer_draw();
}

static void acq_bench_init(void)
{
    if (acq_init() != ACQ_OK) {
        status_init_error_set(STATUS_ERR_ACQ);
        return;
    }
    if (acq_set_timebase(ACQ_DEFAULT_TIMEBASE) != ACQ_OK) {
        status_init_error_set(STATUS_ERR_ACQ);
        return;
    }
    
    /* Load persisted settings if available */
    static struct settings s_settings;
    settings_load(&s_settings);
    
    /* Apply settings to acquisition engine.  We only reach this point when
     * acq_init() and acq_set_timebase() already succeeded (early returns
     * above), so the engine is ready - no need to gate on s_acq_ok here. */
    /* Apply timebase if valid: only single-shot-capable entries are usable. */
    if (s_settings.timebase_idx < TIMEBASE_COUNT) {
        acq_set_timebase(s_settings.timebase_idx);
    }
    /* Apply trigger mode */
    if (s_settings.trigger_mode <= (uint8_t)TRIG_MODE_SINGLE) {
        acq_set_trigger_mode(s_settings.trigger_mode);
        if (s_settings.trigger_mode == TRIG_MODE_SINGLE) {
            (void)acq_trigger_arm();
        }
    }
    /* Apply trigger level (0 is valid, so do not skip it) */
    acq_set_trigger_level((s_settings.trigger_level > 4095U)
                          ? 4095U : s_settings.trigger_level);
    if (s_settings.trigger_slope <= 2U) {
        s_trig_slope = s_settings.trigger_slope;
    }
    /* Apply run state.  Streaming is session-only: it must never be
     * restored from settings, because while it is active the UI and the
     * front-panel ladder decode are skipped and the instrument looks
     * frozen.  The PC link re-enables it on demand. */
    runstate_run_set(s_settings.run);
    runstate_stream_set(0U);
    
    /* Load the persisted instrument calibration (falls back to identity). */
    {
        struct cal persisted;
        if (cal_store_read(&persisted) == 0) {
            cal_active_set(&persisted);
        }
    }
    
    s_acq_ok = 1U;
    puts_("acq ok\r\n");
}

/* One acquisition cycle plus its report.  The wait is bounded by
 * BENCH_TIMEOUT_MS, which is far below the 10 s IWDG, so a dead signal ends
 * in ACQ_TIMEOUT (still a valid window) instead of a reset. */
/* Switch ladder: three 3-position switches on ADC1 ch5/ch6/ch7.  Measured
 * levels are ~0, ~2112 and ~3577 counts, so the decode is a two-threshold
 * slice.  Vendor mapping (app/ladder.h):
 *   ch6 (SEN1) = range   1V / 0.1V / 10mV
 *   ch5 (SEN2) = gain    x1 / x2 / x5 (multiplies the range)
 *   ch7 (CPL)  = coupling, vendor coupling = 2 - code(raw)
 * The vendor tables are used directly in raw-level order, with no extra
 * inversion.  An earlier revision used the tables in the opposite
 * orientation (10mV/0.1V/1V and x1/x2/x5), which displayed range and gain
 * backwards relative to the physical switch positions. */
static uint8_t sw_level(uint16_t v)
{
    if (v == INPUT_ADC_INVALID) { return 3U; }
    if (v < 600U)   { return 0U; }
    if (v < 3000U)  { return 1U; }
    return 2U;
}

static void ladder_refresh(void)
{
    static uint32_t s_next_ms;
    uint16_t adc_values[INPUT_ADC_CHANNEL_COUNT];
    uint16_t rng = INPUT_ADC_INVALID;
    uint16_t gain = INPUT_ADC_INVALID;
    uint8_t old_cpl = s_cpl;
    uint16_t old_span = s_span;
    uint32_t now = HAL_GetTick();

    /* The ladder only moves when a human flips a switch, so pausing the
     * acquisition every frame bought nothing and cost a DMA stop/restart
     * (visible glitches while the front end settled).  10 Hz is far faster
     * than the switches and keeps the pause rate negligible. */
    if ((int32_t)(now - s_next_ms) < 0) {
        return;
    }
    s_next_ms = now + 100U;

    if (acq_pause() == ACQ_OK) {
        /* All three ladder nodes are ordinary analog inputs (PA5/PA6/PA7).
         * PA7 is no longer driven by TIM4; reading it as CPL from the
         * injected group returned zero on this clone and left CPL stuck at
         * DC.  The acquisition engine has released ADC1 here, so the
         * regular group can sample the ladder safely. */
        uint16_t cpl_raw = INPUT_ADC_INVALID;

        input_scan(adc_values);
        (void)acq_resume();

        gain = adc_values[LADDER_CH_SEN2];   /* ch5 = gain  */
        rng  = adc_values[LADDER_CH_SEN1];   /* ch6 = range */
        cpl_raw = adc_values[LADDER_CH_CPL]; /* ch7 = CPL   */

        /* Vendor inversion: coupling = 2 - code(raw).  A failed read leaves
         * the last good label. */
        if (cpl_raw != INPUT_ADC_INVALID) {
            uint8_t cpl_lvl = sw_level(cpl_raw);

            if (cpl_lvl < 3U) {
                uint8_t nc = ladder_coupling(cpl_lvl);

                if (nc <= 2U) {
                    s_cpl = nc;
                }
            }
        }
    }

    {
        uint8_t r = sw_level(rng);
        uint8_t g = sw_level(gain);

        /* A failed read must not disturb the last good decode: the panel
         * would otherwise jump to a stale/default span every time the ADC is
         * busy. */
        if ((r >= 3U) || (g >= 3U)) {
            /* Still honour a coupling/GND rendering transition. */
            if (s_cpl != old_cpl) {
                scope_force_redraw();
            }
            return;
        }
        /* The front panel is authoritative for the display scale.
         *
         * This used to recompute only when the switch levels changed, so a
         * PC-link SET_SPAN (e.g. 4095 from the protocol regression suite)
         * survived forever: the trace then occupied a fraction of the plot
         * and looked blank at 1 V/div.  Re-asserting every refresh keeps the
         * display consistent with the switches no matter what the link left
         * behind.  SET_SPAN still changes the live span, it is just
         * overridden at the next 10 Hz ladder refresh rather than
         * permanently.
         *
         * The gain multiplies the range (vendor 9-entry table:
         * 1V x5 = 5V, 1V x2 = 2V, 10mV x5 = 50mV, ...).  The display span
         * itself is constant: the front end normalises each range to the
         * same ADC span, so only the label changes with the switches. */
        {
            uint16_t mv = (uint16_t)(k_range_mv[r] * k_gain_x[g]);

            if (mv == 0U) { mv = 1U; }
            s_span = SCOPE_SPAN_COUNTS;
            s_mv = mv;
            s_rng_lvl = r;
            s_gain_lvl = g;
        }
        s_combo_idx = (uint8_t)(r * 3U + g);
        s_combo_valid = 1U;
    }

    /* Vertical scale or coupling mode changed: differential-erase state was
     * computed for the old mapping and would leave horizontal artifacts if
     * reused.  AC <-> DC also moves the vertical centre (AC removes the DC
     * component), so it needs a full repaint just like AC <-> GND. */
    if ((s_span != old_span) || (s_cpl != old_cpl)) {
        scope_force_redraw();
    }
}

/* Software edge refinement around the hardware trigger.
 *
 * The ADC analog watchdog is one-sided now (acq_set_trigger_slope), so its
 * latched index already marks the requested edge direction; the remaining
 * error is only the ADC-to-ISR latency.  Search a small window CENTRED on
 * gate->trigger_index instead of a blind 512-sample backward scan: the old
 * scan could pick an unrelated earlier edge whenever the hardware event was
 * close to the window boundary, and it could not find anything at all when
 * the trigger level sat outside the captured signal - the "wave keeps
 * rebuilding" symptom.  Within that window the crossing NEAREST to the
 * hardware event is chosen (trig_scan_nearest), because at a fast timebase
 * the window can hold several periods and the first crossing in it may be
 * far from the edge the watchdog actually latched.  When the local scan
 * finds no clean edge (very slow signal, or a spurious first-conversion
 * latch) the hardware index is the best available answer and the caller
 * keeps it. */
#define TRIG_REFINE_BACK 128U
#define TRIG_REFINE_FWD   48U

static uint32_t trig_refine(const struct acq_gate *gate, uint8_t *found)
{
    memdiag_msp_sample();
    const uint16_t *ring = acq_samples();
    uint16_t ring_size = (uint16_t)acq_sample_count();
    uint16_t n = (uint16_t)(TRIG_REFINE_BACK + TRIG_REFINE_FWD);
    uint16_t start;
    uint16_t i;
    struct trig_cfg cfg;
    int32_t edge;

    *found = 0U;
    s_last_trig_valid = 0U;
    if ((ring == NULL) || (ring_size == 0U) || (gate == NULL)
        || (gate->triggered == 0U)) {
        return 0U;
    }
    if (n > ring_size) {
        n = ring_size;
    }
    if (n > TRIG_SEARCH_LEN) {
        n = TRIG_SEARCH_LEN;
    }
    start = (uint16_t)(((uint32_t)gate->trigger_index + (uint32_t)ring_size
                        - (uint32_t)TRIG_REFINE_BACK) % (uint32_t)ring_size);
    for (i = 0U; i < n; i++) {
        s_acq_scratch.trig_search[i] =
            ring[(uint16_t)(((uint32_t)start + (uint32_t)i)
                             % (uint32_t)ring_size)];
    }
    cfg.level = acq_trigger_level_get();
    cfg.hyst_counts = ACQ_TRIGGER_HYST;
    /* AUTO still aligns on one repeatable direction (rising), matching the
     * one-sided hardware band. */
    cfg.slope = (s_trig_slope == TRIG_FALLING) ? TRIG_FALLING : TRIG_RISING;

    {
        uint32_t centre = (TRIG_REFINE_BACK < n) ? TRIG_REFINE_BACK
                                                 : (uint32_t)(n - 1U);

        edge = trig_scan_nearest(s_acq_scratch.trig_search, (uint32_t)n,
                                 centre, &cfg);
    }
    if (edge >= 0) {
        *found = 1U;
        s_last_trig_idx = ((uint32_t)start + (uint32_t)edge) % (uint32_t)ring_size;
        s_last_trig_valid = 1U;
        return s_last_trig_idx;
    }
    /* Fallback: strict Schmitt refinement can fail when the rearm condition
     * is never satisfied inside the search window (e.g. trigger level near a
     * signal extreme so level±hyst is outside the waveform).  The hardware
     * watchdog already latched the intended edge direction; pick the nearest
     * simple threshold crossing to the hardware event instead of discarding
     * refinement entirely. */
    {
        uint32_t centre = (TRIG_REFINE_BACK < n) ? TRIG_REFINE_BACK
                                                 : (uint32_t)(n - 1U);
        uint16_t lvl = cfg.level;
        uint32_t best = 0U;
        uint32_t best_dist = 0xFFFFFFFFUL;
        int32_t found_simple = -1;
        uint32_t i;
        for (i = 1U; i < (uint32_t)n; i++) {
            uint16_t p = s_acq_scratch.trig_search[i - 1U];
            uint16_t c = s_acq_scratch.trig_search[i];
            uint8_t cross = 0U;
            if (cfg.slope == TRIG_FALLING) {
                cross = ((p > lvl) && (c <= lvl)) ? 1U : 0U;
            } else {
                cross = ((p < lvl) && (c >= lvl)) ? 1U : 0U;
            }
            if (cross != 0U) {
                uint32_t dist = (i >= centre) ? (i - centre) : (centre - i);
                if (dist < best_dist) {
                    best_dist = dist;
                    best = i;
                    found_simple = (int32_t)i;
                }
            }
        }
        if (found_simple >= 0) {
            *found = 1U;
            s_last_trig_idx = ((uint32_t)start + (uint32_t)best) % (uint32_t)ring_size;
            s_last_trig_valid = 1U;
            return s_last_trig_idx;
        }
    }
    return 0U;
}


/* Accumulate one triggered sweep into the ETS equivalent-time frame.
 * Returns 1 if a reconstructed frame is ready in s_frame/f, 0 to keep
 * accumulating.  A sweep contributes only when it can be associated with a
 * hardware-captured crossing from the same arming generation (B4.27) and the
 * phase-source selector finds a trustworthy placement:
 *   - B1 two-sample interpolation when the sampling aperture resolved the
 *     edge (resolved sine/triangle, wide square);
 *   - trigger-quantised interval midpoint when the edge completed inside one
 *     sample interval, so the aperture carries no sub-sample phase;
 *   - rejection otherwise (no refined edge, unbound capture, level not
 *     bracketed).
 * Rejected sweeps update no bin and do not increment the accepted-sweep
 * count (the target counts accepted sweeps only).  Diagnostics/rejection
 * (normative): a rejected sweep is a triggered, ETS-eligible sweep that
 * reaches the accumulation decision point and fails to produce a valid
 * accepted contribution; untriggered and non-eligible polls are excluded.
 * The epoch state machine (bounded accepted counter, readiness, failed-attempt
 * invalidation and gap-safe reconstruction) lives in app/ets_epoch.c. */
static uint8_t ets_accumulate(const struct acq_gate *gate, uint32_t trig_idx,
                              uint8_t refined, struct frame *f)
{
    memdiag_msp_sample();
    struct ets_epoch_context ctx;
    const struct timebase_step *ets_step;
    const struct timebase_step *hw_step;
    uint32_t hw_counts;
    uint32_t ets_counts;
    enum ets_phase_edge edge;
    struct ets_source_input src;
    struct ets_source_result pick;
    struct acq_trigger_hw hw;
    uint16_t win_min;
    uint16_t win_max;
    uint16_t i;
    uint16_t n;
    uint8_t  hw_bound;
    /* Transient lane for this whole call: the sweep window, then the
     * reconstruction target.  Never published until it is complete. */
    uint16_t *const scratch = s_acq_scratch.trig_search;

    if ((gate == NULL) || (gate->triggered == 0U) || (f == NULL)) {
        return 0U; /* not a triggered ETS-eligible sweep */
    }
    if (acq_ets_active() == 0U) {
        return 0U; /* ETS not active/applicable */
    }

    ets_epoch_context(&ctx);
    ets_step = timebase_entry(ctx.tb);
    hw_step  = timebase_entry(acq_base_timebase_get());
    if ((ets_step == NULL) || (hw_step == NULL) || (ets_step->single_shot != 0U)) {
        return 0U; /* ETS not active/applicable */
    }

    /* Context changes invalidate the epoch before any accumulator is
     * touched.  Otherwise, a sweep arriving after a completed epoch starts
     * the next epoch (the completed diagnostics stayed observable until
     * here), which is what keeps `ready` from outliving its data. */
    {
        uint16_t reason = 0U;

        if (ets_epoch_context_differs(&s_ets, &ctx, &reason) != 0U) {
            ets_epoch_reset(&s_ets, &ctx, reason);
        } else {
            ets_epoch_start_sweep(&s_ets);
        }
    }

    if (refined == 0U) {
        ets_epoch_note_rejected(&s_ets);
        return 0U;
    }

    /* HW-edge <-> refined-edge association: trig_refine() searches a bounded
     * window centred on the hardware latch, so a refined index outside that
     * window cannot be the crossing the watchdog saw.  Reject rather than
     * place the sweep on an unrelated edge. */
    {
        uint32_t back = (uint32_t)(((uint32_t)gate->trigger_index
                                    + ACQ_SAMPLES - trig_idx) % ACQ_SAMPLES);
        uint32_t fwd  = (uint32_t)((trig_idx + ACQ_SAMPLES
                                    - (uint32_t)gate->trigger_index)
                                   % ACQ_SAMPLES);

        if ((back > TRIG_REFINE_BACK) && (fwd >= TRIG_REFINE_FWD)) {
            ets_epoch_note_rejected(&s_ets);
            return 0U;
        }
    }

    /* Bind the hardware capture to this arming generation before touching
     * any bin.  An unbound capture means this sweep cannot be associated
     * with the event that armed it (rearm, pause/resume, trigger or
     * timebase change in between). */
    hw_bound = acq_trigger_hw_read(&hw, gate->arm_generation);
    if (hw_bound == 0U) {
        ets_epoch_note_reject_unbound(&s_ets);
        ets_epoch_note_rejected(&s_ets);
        return 0U;
    }
    if (hw.rollover != 0U) {
        ets_epoch_note_hw_rollover(&s_ets);
    }

    hw_counts = ((uint32_t)hw_step->tick_psc + 1U) * ((uint32_t)hw_step->tick_arr + 1U);
    ets_counts = ((uint32_t)ets_step->tick_psc + 1U) * ((uint32_t)ets_step->tick_arr + 1U);
    if ((hw_counts == 0U) || (ets_counts == 0U)) {
        ets_epoch_note_rejected(&s_ets);
        return 0U;
    }

    /* Effective edge direction: AUTO is treated as rising (documented semantics). */
    edge = (s_trig_slope == TRIG_FALLING) ? ETS_PHASE_FALLING : ETS_PHASE_RISING;

    {
        struct frame tmpf;
        /* Phase E: reuse s_acq_scratch.trig_search as transient ETS sweep buffer.
         * R (trig_refine) completed before ets_accumulate(); T (stream TX), if
         * any, runs after ets_accumulate() returns. Synchronous use only. */
        uint8_t rc = frame_build(acq_samples(), (uint16_t)acq_sample_count(),
                                 trig_idx, BENCH_WINDOW, BENCH_PRE,
                                 scratch, BENCH_WINDOW, &tmpf);

        if ((rc != 0U) || (tmpf.length <= (BENCH_PRE + 1U))) {
            ets_epoch_note_rejected(&s_ets);
            return 0U;
        }

        /* Window rails for the undersampled-edge test.  Bounded by the
         * window length (320 comparisons per accepted sweep). */
        n = tmpf.length;
        win_min = scratch[0];
        win_max = scratch[0];
        for (i = 1U; i < n; i++) {
            if (scratch[i] < win_min) { win_min = scratch[i]; }
            if (scratch[i] > win_max) { win_max = scratch[i]; }
        }

        src.y_prev  = scratch[BENCH_PRE - 1U];
        src.y_curr  = scratch[BENCH_PRE];
        src.win_min = win_min;
        src.win_max = win_max;
        src.level   = ctx.trig_level;
        src.edge    = edge;
        src.hw_interval_counts = hw_counts;
        src.hw_bound = 1U;   /* bound above                            */
        src.refined  = 1U;   /* checked before frame_build             */

        /* B4.27: B1 when the aperture resolved the edge, trigger-quantised
         * midpoint when it did not, rejected when neither is trustworthy.
         * The TIM3 capture is never used as a phase. */
        pick = ets_source_select(&src);
        if (pick.source == ETS_SOURCE_REJECTED) {
            /* Rejected: no fabricated phase, no bin/count changes. */
            ets_epoch_note_rejected(&s_ets);
            return 0U;
        }
        ets_epoch_note_source(&s_ets, (enum ets_phase_source)pick.source);

        for (i = 0U; i < n; i++) {
            int32_t bi;

            if (ets_phase_bin_index(i, BENCH_PRE, BENCH_WINDOW,
                                    hw_counts, ets_counts, pick.phase_q16, &bi) != 0U) {
                ets_epoch_add(&s_ets, (uint16_t)bi, scratch[i]);
            }
        }
    }

    ets_epoch_note_accepted(&s_ets);
    if (ets_epoch_target_reached(&s_ets) == 0U) {
        return 0U;
    }

    /* Reconstruct into the transient lane, not straight into s_frame: an
     * attempt that fails coverage (or whose frame cannot be measured) must
     * leave the previously published frame byte-identical.  Publishing
     * mid-build is what exposed partially filled frames to the stream and
     * wave-save consumers in B4.24 (defect D). */
    if (ets_epoch_reconstruct(&s_ets, scratch) != ETS_EPOCH_READY) {
        /* Insufficient coverage: the epoch was invalidated (ready=0,
         * reason=ETS_RESET_INCOMPLETE) rather than left to overflow. */
        return 0U;
    }

    if (measure_compute(scratch, BENCH_WINDOW, &f->stats) != 0U) {
        /* A frame that cannot be measured must not be advertised as ready,
         * and must not replace the last good frame. */
        ets_epoch_note_failure(&s_ets);
        return 0U;
    }

    memcpy(s_frame, scratch, (size_t)BENCH_WINDOW * sizeof s_frame[0]);
    s_frame_len = BENCH_WINDOW;
    f->length = BENCH_WINDOW;
    f->win.start = 0U;
    f->win.length = BENCH_WINDOW;
    f->win.trigger_pos = BENCH_PRE;
    return 1U;
}

static void acq_bench_poll(void)
{
    memdiag_msp_sample();
    if (s_acq_ok == 0U) {
        return;
    }

    /* Keep the hardware watchdog band in step with the UI slope.  menu.c
     * and the PC link both change s_trig_slope directly; syncing here makes
     * every path take effect without duplicating the register write in each
     * one.  Idempotent, so this is cheap. */
    (void)acq_set_trigger_slope(s_trig_slope);

    if (runstate_run() == 0U) {
        return;                       /* HOLD: keep the last frame */
    }

    struct acq_gate gate;
    struct frame f;
    char line[96];
    enum acq_status rc;

    if (s_acq_ok == 0U) {
        return;
    }

    /* AUTO must refresh quickly; NORM/SINGLE get the full capture window.
     * Keep AUTO short so the UI/buttons stay responsive when no edge fires
     * (e.g. signal removed) - acq_cycle blocks the main loop for up to this
     * long.  60 ms gives ~16 fallback updates/s worst-case instead of 4. */
    {
        uint32_t wait_ms = (acq_trigger_mode_get() == (uint8_t)TRIG_MODE_AUTO)
                         ? 60U : BENCH_TIMEOUT_MS;

        rc = acq_cycle(wait_ms, (uint16_t)(BENCH_WINDOW - BENCH_PRE), &gate);
    }
    if (rc == ACQ_HW_ERROR) {
        s_acq_errors++;
        if (acq_init() != ACQ_OK) {
            s_acq_ok = 0U;             /* park rather than hammer it */
            puts_("acq stopped\r\n");
        }
        return;
    }
    if ((rc != ACQ_OK) && (rc != ACQ_TIMEOUT)) {
        return;
    }
    if (gate.fresh == 0U) {
        /* NORM/SINGLE without a trigger: the previous snapshot is still the
         * truth, so do not count, re-measure or redraw it. */
        return;
    }

    s_acq_cycles++;
    if (rc == ACQ_TIMEOUT) {
        s_acq_timeouts++;
    }

    /* AWD has no slope selectivity and fires on either band-exit direction,
     * so its trigger_index gives phase-inconsistent alignment (trace scrolls).
     * Refine with a software hysteresis edge scan for stable display. */
    {
        uint8_t refined = 0U;
        uint8_t aligned = 0U;
        uint8_t ets_active = acq_ets_active();
        uint32_t trig_idx = trig_refine(&gate, &refined);
        uint32_t used_idx = 0U;
        uint8_t  fallback = 2U; /* 0 refined, 1 hw index, 2 tail/untriggered */

        /* On ETS ranges the equivalent-time path builds its own transient
         * window and only publishes s_frame when a reconstruction completes.
         * Building a raw single sweep into s_frame here first would clobber
         * the last good reconstructed frame on every accumulating poll (and
         * expose a raw sweep to wave-save/stream consumers). */
        if (refined != 0U) {
            used_idx = trig_idx;
            fallback = 0U;
            if ((ets_active == 0U)
                && (frame_build(acq_samples(), (uint16_t)acq_sample_count(),
                                trig_idx, BENCH_WINDOW, BENCH_PRE,
                                s_frame, BENCH_WINDOW, &f) != 0U)) {
                return;
            }
            aligned = 1U;
        } else if (gate.triggered != 0U) {
            used_idx = (uint32_t)gate.trigger_index;
            fallback = 1U;
            if ((ets_active == 0U)
                && (frame_build(acq_samples(), (uint16_t)acq_sample_count(),
                                gate.trigger_index, BENCH_WINDOW, BENCH_PRE,
                                s_frame, BENCH_WINDOW, &f) != 0U)) {
                return;
            }
            aligned = 1U;
        } else {
            used_idx = (uint32_t)gate.write_index;
            fallback = 2U;
            if ((ets_active == 0U)
                && (frame_build_tail(acq_samples(), (uint16_t)acq_sample_count(),
                                     gate.write_index, BENCH_WINDOW,
                                     s_frame, BENCH_WINDOW, &f) != 0U)) {
                return;
            }
        }

        /* Diagnostic snapshot for last recorded acquisition (instrumentation). */
        s_diag_seq++;
        s_diag_gate_triggered = (gate.triggered != 0U) ? 1U : 0U;
        s_diag_hw_index = (uint16_t)gate.trigger_index;
        s_diag_hw_count = acq_trigger_hw_count();
        s_diag_refined_index = (uint16_t)used_idx;
        s_diag_refined_found = (refined != 0U) ? 1U : 0U;
        s_diag_fallback = fallback;
        s_diag_timebase = (uint8_t)acq_timebase_get();
        s_diag_trig_slope_snap = s_trig_slope;
        s_diag_trig_level_snap = acq_trigger_level_get();
        s_diag_trig_mode_snap = (uint8_t)acq_trigger_mode_get();

        /* ETS: accumulate triggered sweeps into equivalent-time frame. */
        if (ets_active != 0U) {
            if (gate.triggered != 0U) {
                if (ets_accumulate(&gate, trig_idx, refined, &f) == 0U) {
                    return; /* accumulate more sweeps */
                }
                aligned = 1U;
            } else {
                return; /* wait for triggered sweeps */
            }
        }

        s_frame_len = f.length;

        /* AUTO must find its own trigger level.  With a fixed level the
         * analogue front end's bias (near 1000 counts here) sits below the
         * default 2048, so every cycle timed out and the display re-captured
         * an untriggered window at a random phase - the classic "wave keeps
         * restarting" symptom.
         *
         * Re-centre not only on a timeout but also whenever the level is
         * OUTSIDE the captured signal.  With the old two-sided watchdog a
         * level beyond the signal still "triggered" on every conversion
         * (the whole signal was out of band), so the timeout branch never
         * ran and the display stayed at a random phase forever.  A level the
         * user set inside the signal is never touched.  NORM/SINGLE keep
         * their level (a missed trigger there is meaningful). */
        if (acq_trigger_mode_get() == (uint8_t)TRIG_MODE_AUTO) {
            uint16_t lvl = acq_trigger_level_get();
            uint16_t lo = f.stats.min;
            uint16_t hi = f.stats.max;
            uint8_t  outside = ((hi > lo)
                                && ((lvl < lo) || (lvl > hi))) ? 1U : 0U;

            if ((rc == ACQ_TIMEOUT) || (outside != 0U)) {
                uint16_t mid = (uint16_t)(((uint32_t)lo + (uint32_t)hi) / 2U);

                if (mid != lvl) {
                    (void)acq_set_trigger_level(mid);
                }
            }
        }

        /* (b) Average-cycle: accumulate N trigger-aligned frames, output, repeat. */
        if ((s_avg_n > 1U) && (aligned != 0U)) {
            uint16_t i;
            if ((s_avg_len != 0U) && (s_avg_len != f.length)) {
                avg_reset();
            }
            if (s_avg_count == 0U) {
                memset(s_avg_acc, 0, sizeof(s_avg_acc));
                s_avg_len = f.length;
            }
            for (i = 0U; i < f.length; i++) {
                s_avg_acc[i] += (uint32_t)s_frame[i];
            }
            s_avg_count++;
            if (s_avg_count >= s_avg_n) {
                for (i = 0U; i < f.length; i++) {
                    s_frame[i] = (uint16_t)((s_avg_acc[i] + (s_avg_n / 2U)) / (uint32_t)s_avg_n);
                }
                (void)measure_compute(s_frame, f.length, &f.stats);
                /* keep averaged frame displayed; start next batch */
                s_avg_count = 0U;
                s_avg_len = f.length;
            } else {
                /* accumulating: show partial average for feedback */
                for (i = 0U; i < f.length; i++) {
                    s_frame[i] = (uint16_t)((s_avg_acc[i] + (s_avg_count / 2U)) / (uint32_t)s_avg_count);
                }
                (void)measure_compute(s_frame, f.length, &f.stats);
            }
        } else if ((s_avg_n > 1U) && (aligned == 0U)) {
            /* Untriggered: don't pollute accumulator, show live frame. */
        }
        s_trig_locked = aligned;
        s_wave_recalled = 0U; /* live frame supersedes recall */
    }
    /* Cache measurements for on-screen display and PC-link GET_VALUE. */
    s_meas_vpp  = f.stats.vpp;
    s_meas_vrms = f.stats.rms;
    s_meas_min  = f.stats.min;
    s_meas_max  = f.stats.max;
    s_meas_mean = f.stats.mean;

    /* AV+ / AV-: mean deviation above / below the 0 V reference. */
    {
        uint16_t ref = ui_frame_ref(f.stats.mean);
        int64_t sum_p = 0, sum_n = 0;
        uint32_t cnt_p = 0, cnt_n = 0;
        uint16_t k;

        for (k = 0U; k < f.length; k++) {
            int32_t d = (int32_t)s_frame[k] - (int32_t)ref;
            if (d > 0) { sum_p += d; cnt_p++; }
            else if (d < 0) { sum_n += -d; cnt_n++; }
        }
        s_avp_mv = (cnt_p != 0U) ? ui_counts_to_mv((int32_t)(sum_p / (int64_t)cnt_p)) : 0;
        s_avn_mv = (cnt_n != 0U) ? -ui_counts_to_mv((int32_t)(sum_n / (int64_t)cnt_n)) : 0;
        if (s_cpl == 2U) { s_avp_mv = 0; s_avn_mv = 0; }
    }
    {
        uint32_t period_ns = 0U;
        const struct timebase_step *tb = timebase_entry(acq_timebase_get());
        if (tb != NULL) {
            uint64_t ticks = (uint64_t)(tb->tick_psc + 1U) * (uint64_t)(tb->tick_arr + 1U);
            period_ns = (uint32_t)(ticks * 1000000000ULL / (uint64_t)TIMEBASE_TIM3_HZ);
        }
        (void)measure_freq_duty(s_frame, f.length, period_ns, &s_meas_freq_hz, &s_meas_duty_x10);
    }
    /* Continuity: a short to ground pulls the whole window near zero. */

    s_cont = continuity_update(s_cont, f.stats.max,
                               CONTINUITY_ON_COUNTS, CONTINUITY_OFF_COUNTS);

    if (runstate_stream() != 0U) {
        uint32_t n = link_encode_frame(s_acq_scratch.tx,
                                       (uint32_t)sizeof s_acq_scratch.tx,
                                       s_link_seq, s_frame, f.length);

        if (n != 0U) {
            s_link_seq++;
            console_write(s_acq_scratch.tx, n);
        }
    } else {
        /* Human-readable line at most twice a second: with the trigger now
         * firing immediately the loop runs in milliseconds, and printing every
         * cycle both floods the console and starves command reception. */
        static uint32_t s_last_text_ms = 0U;
        uint32_t now = HAL_GetTick();

        if (((now - s_last_text_ms) >= 500U)
            && (bench_format_stats(&f, s_acq_cycles, s_acq_timeouts, s_acq_errors,
                                   line, (uint32_t)sizeof line) == 0U)) {
            s_last_text_ms = now;
            puts_(line);
        }
    }
    if ((s_acq_cycles == 1U) && (s_scope_first != 0U)) {
        char hdr[24];

        s_scope_first = 0U;
        ili9341_fill_screen(ILI9341_BLACK);
        (void)snprintf(hdr, sizeof hdr, "B=%08lX S=%04lX",
                       (unsigned long)status_build_magic,
                       (unsigned long)(status_build_seq & 0xFFFFU));
        ili9341_print(0U, 0U, hdr, ILI9341_GREEN, ILI9341_BLACK, 1U);
    }

    /* Every frame, not just the first: otherwise the panel stays on the
     * first capture and the front-panel switches look dead. */
    /* Streaming owns the link: skip the ladder pause and the 320-column
     * redraw, which cost more than the 45 ms frame transmit. */
    if (runstate_stream() == 0U) {
        ladder_refresh();
        s_menu.coupling = s_cpl;
        if ((s_menu.state == MENU_NONE) && (s_menu.diag_open == 0U)) {
            if (s_menu.disp_mode != 0U) {
                ui_big_draw();
            } else {
                scope_draw(&f);
            }
        }

        /* Hand the frame mean to the menu so the CAL ZERO page can capture it
         * as the zero level of the currently selected sensitivity combo.
         * Only while the ladder is being decoded: during streaming the combo
         * index is stale, and a zero captured against the wrong combo would
         * shift the trace on a range that was never calibrated. */
        menu_frame_mean(&s_menu, f.stats.mean, s_combo_idx, s_combo_valid);
    }

    if (s_acq_cycles == 1U) {
        if (bench_format_samples(s_frame, BENCH_DUMP_SAMPLES, line,
                                 (uint32_t)sizeof line) == 0U) {
            puts_(line);
        }
    }
}

/* ------------------------------------------------------------ TIM4: 1kHz 3.3V cal/test outputs (SQR + TSIG)
 *
 * DSO183 "How The Circuits Work":
 *   - "Square wave signal output (SQR) ... J6. The original square wave is
 *     generated by the microcontroller and output from pin PA11" (0-3.3V).
 *     -> PA11 (LQFP48 pin 32), R21 -> J6/SQR.
 *   - "Test signal output (TSIG), connected to the test signal output port J7."
 *     The labeled "1 kHz 3.3 V" header is TSIG/J7 (distinct from SQR/J6 and
 *     from the conditioned sine/triangle/square waveform-generator outputs).
 *     User-identified TSIG candidate on the schematic: PA1 (pin 11). PA15 is
 *     also driven as a freed candidate (JTDI released by SWJ_CFG=010).
 *
 * Stock used TIM3_CH2 (PA7) for a dynamic 50% output, but TIM3 is
 * acquisition-owned (variable timebase/TRGO) in this firmware. We generate a
 * fixed 1 kHz, 50% duty, 0-3.3V push-pull square via TIM4 update IRQ instead.
 *
 * TIM4 kernel clock = 64 MHz here (APB1 timer clock = 2 x PCLK1; this build
 * runs HSI/2 x PLL16 = 64 MHz, not the 72 MHz assumed by older notes).
 * PSC=63 => 1 MHz. ARR=499 => 2 kHz update; toggle on each update => 1 kHz.
 *
 * Pins driven (safe diagnostics; find which reaches J7/TSIG "1kHz 3.3V" header):
 *   PA1  (pin 11) — TSIG/J7 candidate (overrides input_init analog config).
 *   PA11 (pin 32) — SQR/J6, documented.
 *   PA15 (pin 38) — freed JTDI candidate.
 *   PA8/PA12      — safe fallbacks (DSO183 USB power-only).
 * NOT driven: PA7 (CPL ladder input), PA0/2/5/6 ladder/ADC, PA3/PA4 analog,
 * PA9/10 UART, PA13/14 SWD, PB0-15 LCD/bus, PC13 LCD CS.
 *
 * PA7 used to be in this list.  It is the CPL slide-switch ladder node
 * (ADC_IN7) and driving it as a 1 kHz output made CPL flip between DC and
 * GND; it is now analog and sampled with the other ladder channels. */
#define CAL_TEST_PIN_SQR   GPIO_PIN_11  /* PA11 pin 32 -> J6 SQR (documented) */
#define CAL_TEST_PIN_TSIG  GPIO_PIN_1   /* PA1  pin 11 -> J7 TSIG candidate */
#define CAL_TEST_PINS      (CAL_TEST_PIN_SQR | CAL_TEST_PIN_TSIG | \
                            GPIO_PIN_8 | GPIO_PIN_12 | GPIO_PIN_15)

void TIM4_IRQHandler(void)
{
    if ((TIM4->SR & 1U) != 0U) {      /* UIF */
        uint32_t odr;
        TIM4->SR = (uint16_t)~1U;     /* clear UIF */
        odr = GPIOA->ODR & CAL_TEST_PINS;
        /* Atomic set/reset: clear pins currently high, set pins currently low. */
        GPIOA->BSRR = (odr << 16) | (CAL_TEST_PINS & ~odr);
    }
}

static void tim4_generator_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_TIM4_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin   = CAL_TEST_PINS;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);
    GPIOA->BRR = CAL_TEST_PINS;

    TIM4->CR1 = 0U;
    TIM4->CR2 = 0U;
    TIM4->SMCR = 0U;
    TIM4->DIER = 0U;

    TIM4->PSC = 63;          /* 64 MHz / 64 = 1 MHz */
    TIM4->ARR = 499;         /* 1 MHz / 500 = 2 kHz update => 1 kHz toggle */
    TIM4->CNT = 0U;
    TIM4->EGR = 1U;          /* latch PSC/ARR */
    TIM4->SR  = 0U;

    TIM4->DIER = 1U;         /* UIE */
    HAL_NVIC_SetPriority(TIM4_IRQn, 0, 0);  /* highest: clean 1kHz edges */
    HAL_NVIC_EnableIRQ(TIM4_IRQn);

    TIM4->CR1 = 1U;          /* CEN */
}

int main(void)
{
    uint32_t cnt = 0;

    memdiag_stack_paint_early();
    HAL_Init();
    board_free_jtag_pins();
    (void)clock_config();    /* failure sets STATUS_ERR_* and continues */
    (void)console_init();
    board_uart_rx_start();
    board_iwdg_init(MAIN_IWDG_TIMEOUT_MS);

    status_boot_count_init();      /* survives a watchdog reset (.noinit) */
    cal_store_init();              /* calibration persistence from .noinit */
    puts_("\r\nDSO183 DIAG OK\r\n");

    ili9341_init();
    ili9341_fill_screen(ILI9341_BLACK);
    if (input_init() == 0U) {
        status_init_error_set(STATUS_ERR_ADC);
    }
    /* After input_init(): it writes GPIOA->CRL = 0 (PA0..PA7 all analog),
     * which would clear PA1. TSIG/J7 "1kHz 3.3V" candidate is PA1
     * (schematic/user).  PA7 is NOT driven: it is the CPL ladder input. */
    tim4_generator_init();   /* 1 kHz 3.3V on PA1(TSIG?)/PA11(SQR)/PA15/PA8/PA12 */

    menu_init(&s_menu);

    /* ETS accumulator starts life as an empty epoch whose outcome code is
     * ETS_RESET_INIT, exactly as the pre-B4.25 boot state reported.  The
     * bins stay in .bss (they are zero either way); only the reason code is
     * written here so the aggregate is not pulled into .data. */
    s_ets.reset_reason = ETS_RESET_INIT;

    /* Live acquisition.  It takes ADC1's ownership token for its lifetime;
     * the ladder/diagnostic scans pause it and take the token briefly (see
     * ladder_refresh() and diag_snapshot()). */
    acq_bench_init();

    /* Apply the persisted/default trigger mode now that the engine exists:
     * menu_init() runs before acq_init(), so it must not call these. */
    if (s_acq_ok != 0U) {
        (void)acq_set_trigger_mode(s_menu.trigger_mode);
        if (s_menu.trigger_mode == TRIG_MODE_SINGLE) {
            (void)acq_trigger_arm();
        }
    }

    {
        char rows[DIAG_ROWS][DIAG_COLS];
        struct diag_state st;
        uint8_t r;

        diag_snapshot(&st);
        diag_rows(rows, &st);
        for (r = 0U; r < DIAG_ROWS; r++) {
            ili9341_print(diag_row_x(r), diag_row_y(r), rows[r],
                          DIAG_FG, DIAG_BG, DIAG_TEXT_SCALE);
        }
    }
    puts_("diag screen ok\r\n");

    for (;;) {
        char rows[DIAG_ROWS][DIAG_COLS];
        struct diag_state st;
        uint8_t r;

        status_main_loops++;
        /* Keep the binary stream clean: interleaved ASCII would force every
         * host to resynchronise between sample frames. */
        if (((cnt % 10U) == 0U) && (runstate_stream() == 0U)) {
            puts_("alive\r\n");
        }
        cnt++;

        if ((s_acq_ok == 0U) && ((cnt % 2U) == 0U)) {  /* diag only without acq */
            diag_snapshot(&st);
            diag_rows(rows, &st);
            /* rows 1..9 are live (counters, bus reads, ADC rows, ladder
             * decode, init errors); row 0 (build identity) stays static. */
            for (r = 1U; r <= 9U; r++) {
                ili9341_print(diag_row_x(r), diag_row_y(r), rows[r],
                              DIAG_FG, DIAG_BG, DIAG_TEXT_SCALE);
            }
        }
        link_poll();
        menu_poll(&s_menu);
        ui_handle_menu_requests();
        ui_transition_poll();
        acq_bench_poll();
        menu_render(&s_menu);
        ui_chrome_draw();
        board_iwdg_feed();          /* guard rail 9: feed every iteration */
        HAL_Delay((runstate_stream() != 0U) ? 0U : 1U);  /* 1 ms: UI/acq fast */
    }
}
