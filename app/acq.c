/* M5 acquisition engine - see acq.h for the design contract and provenance.
 *
 * Every runtime interaction with the peripherals is written register-level
 * here, because the vendored HAL contains unbounded waits in exactly the
 * paths this engine needs (ADC enable, calibration, conversion poll, DMA
 * poll - verified by grep).  The style follows board_iwdg.c: explicit
 * register names from the CMSIS headers, explicit deadlines.
 *
 * The local wait helpers measure their deadline with HAL_GetTick(), so they
 * require HAL_Init() to have run and SysTick to be ticking.  That holds for
 * every call path today (main() initialises HAL first); a future caller from
 * before HAL_Init() would spin forever, so do not add one.
 */
#include "status.h"
#include "acq.h"
#include "ladder.h"
#include "acq_window.h"
#include "trigger.h"
#include "adc_owner.h"
#include "tim3_hw.h"
#include "timebase.h"
#include "board_iwdg.h"

#include "stm32f1xx_hal.h"

#include <string.h>

/* ------------------------------------------------------------------ state */

static uint16_t s_buf[ACQ_SAMPLES];       /* DMA circular target - 2 KiB */
/* Frozen copy handed to the caller after every cycle.  The DMA is parked for
 * the copy and then RESUMED at the same ring position (not restarted), so the
 * caller reads stable data while the ring keeps its full pre-trigger history
 * for the next capture. */
static uint16_t s_snap[ACQ_SAMPLES];      /* immutable snapshot - 2 KiB   */
static uint32_t s_snap_pos;               /* write position at freeze     */
static uint8_t  s_snap_valid;             /* 0 until the first snapshot   */

static volatile uint8_t  s_event;         /* 0 none, 1 triggered, 2 timeout */
static volatile uint32_t s_trigger_index; /* latched watchdog position      */
static uint8_t           s_inited;
static uint8_t           s_timebase;      /* index pacing TIM3 */
static uint8_t           s_ring_primed;   /* 1 once the ring has wrapped     */
static uint16_t          s_resume_pos;    /* ring pos at the last DMA resume */
/* Mode policy lives here; the hardware event is still the analog watchdog.
 * Armed defaults to 1 so SINGLE is usable immediately after acq_init(). */
static struct trig_fsm   s_trig = { TRIG_MODE_AUTO, 1U };
static uint16_t          s_trig_level = ACQ_TRIGGER_LEVEL;
static uint8_t           s_trig_slope_hw;  /* 0 rising, 1 falling (AUTO->0) */

/* Static build-time assertions: the watchdog thresholds and the buffer
 * length are compile-time configuration, so failing loudly at build time
 * beats a silent hardware nonsense. */
_Static_assert(ACQ_SAMPLES <= 1024U,      "DMA buffer over the RAM budget");
_Static_assert(ACQ_SAMPLES <= 65535U,     "DMA CNDTR is 16-bit");
_Static_assert(ACQ_TRIGGER_LEVEL <= 4095U, "trigger level beyond 12 bits");
/* acq_cycle() never feeds the IWDG, so a timeout at or above the watchdog
 * period would turn a clean ACQ_TIMEOUT into a reset. */
_Static_assert(ACQ_MAX_WAIT_MS < ACQ_IWDG_TIMEOUT_MS,
               "acquisition timeout must be shorter than the IWDG period");
/* The signal channel must not collide with the front-panel ladder channels:
 * input_scan() and the acquisition engine both drive ADC1's regular group. */
_Static_assert(ACQ_SIGNAL_CHANNEL != LADDER_CH_SEN1, "signal ch clashes with SEN1");
_Static_assert(ACQ_SIGNAL_CHANNEL != LADDER_CH_SEN2, "signal ch clashes with SEN2");
_Static_assert(ACQ_SIGNAL_CHANNEL != LADDER_CH_CPL,  "signal ch clashes with CPL");

/* ------------------------------------------------------------ wait helpers */

/* Every busy wait in this file funnels through these two: a HAL_GetTick
 * deadline (wrap-safe via the signed-difference trick), and the IWDG can
 * never be starved because SysTick keeps firing while we poll. */
static uint8_t wait_bit_clear(volatile uint32_t *reg, uint32_t mask,
                              uint32_t timeout_ms)
{
    uint32_t deadline_start = HAL_GetTick();

    while ((*reg & mask) != 0U) {
        if ((int32_t)(HAL_GetTick() - (deadline_start + timeout_ms)) >= 0) {
            return 1U;              /* timed out - caller reports HW error */
        }
        __asm volatile ("nop");
    }
    return 0U;
}

static void ms_delay(uint32_t ms)
{
    HAL_Delay(ms);
}

/* --------------------------------------------------------- trigger band */

/* Write the one-sided analog-watchdog band for the selected direction.
 * Only the edge the user asked for can leave the window, so an input that
 * sits on the "waiting" side cannot trigger until it actually crosses. */
static void apply_trigger_band(void)
{
    if (s_trig_slope_hw == 1U) {
        uint32_t lo = ((uint32_t)s_trig_level > ACQ_TRIGGER_HYST)
                    ? ((uint32_t)s_trig_level - ACQ_TRIGGER_HYST) : 0U;

        ADC1->LTR = lo;
        ADC1->HTR = 4095U;
    } else {
        uint32_t hi = (uint32_t)s_trig_level + ACQ_TRIGGER_HYST;

        if (hi > 4095U) {
            hi = 4095U;
        }
        ADC1->LTR = 0U;
        ADC1->HTR = hi;
    }
}

/* Most recent sample the DMA has written.  DMA writes s_buf[index] and then
 * decrements CNDTR, so the next write position is ACQ_SAMPLES - CNDTR and
 * the last completed sample is one before it. */
static uint16_t acq_latest_sample(void)
{
    uint32_t ndtr = DMA1_Channel1->CNDTR & 0xFFFFU;
    uint32_t pos  = (ndtr <= ACQ_SAMPLES)
                  ? ((ACQ_SAMPLES - ndtr) % ACQ_SAMPLES) : 0U;
    uint32_t idx  = (pos + ACQ_SAMPLES - 1U) % ACQ_SAMPLES;

    return s_buf[idx];
}

/* Bounded wait for the input to reach the rearm side of the band.  Without
 * it, arming while the signal is already past the threshold latches the
 * watchdog on the first conversion, which is exactly the random-phase
 * window this engine used to display.  The budget is a few display
 * divisions of wall time, so a periodic signal gets at least one chance to
 * return to the waiting side; if it never does (DC, GND, level outside the
 * signal) the caller still proceeds and the timeout/AUTO path takes over.
 * Extremely slow timebases poll at 1 ms instead of spinning, and the IWDG
 * is fed so a legitimate multi-second wait cannot reset the instrument. */
static void wait_trigger_rearm(void)
{
    uint32_t tick_ns = timebase_tick_ns(s_timebase);
    uint32_t budget_ms;
    uint32_t t0;

    /* A few divisions of time is enough for a periodic signal to revisit the
     * waiting side.  Keep this short: acq_cycle blocks the whole UI, so a
     * large budget (especially on slow timebases, or when the level is outside
     * the signal) makes buttons and waveform updates feel frozen.  If the
     * budget expires we still arm and proceed - at worst the cycle may latch a
     * less-stable phase, which is far better than stalling the instrument. */
    budget_ms = (uint32_t)(((uint64_t)tick_ns * 64ULL) / 1000000ULL) + 2U;
    if (budget_ms > 20U) {
        budget_ms = 20U;
    }
    t0 = HAL_GetTick();
    {
        uint32_t fed_at = t0;

        while ((int32_t)(HAL_GetTick() - (t0 + budget_ms)) < 0) {
            uint16_t s = acq_latest_sample();
            uint32_t now = HAL_GetTick();
            uint8_t  ready;

            if (now != fed_at) {
                fed_at = now;
                board_iwdg_feed();
            }
            if (s_trig_slope_hw == 1U) {
                uint32_t hi = (uint32_t)s_trig_level + ACQ_TRIGGER_HYST;

                if (hi > 4095U) {
                    hi = 4095U;
                }
                ready = (s >= hi) ? 1U : 0U;   /* falling: wait until high */
            } else {
                uint32_t lo = ((uint32_t)s_trig_level > ACQ_TRIGGER_HYST)
                            ? ((uint32_t)s_trig_level - ACQ_TRIGGER_HYST) : 0U;

                ready = (s <= lo) ? 1U : 0U;   /* rising: wait until low    */
            }
            if (ready != 0U) {
                return;
            }
            if (tick_ns > 100000U) {
                ms_delay(1U);
            }
        }
        return;
    }
}

/* ------------------------------------------------------ board-level config */

/* Sampling time of the signal channel: SMPR fields cover channels 0..9 in
 * SMPR2 (3 bits each at bit position 3*ch) and 10..17 in SMPR1.  Only the
 * signal channel plus (in M6) the ladder channels ever need this; call is
 * idempotent. */
static void adc_channel_sample_time(uint32_t channel, uint32_t smp_bits)
{
    volatile uint32_t *smpr;
    uint32_t shift;

    if (channel <= 9U) {
        smpr  = &ADC1->SMPR2;
        shift = 3U * channel;
    } else {
        smpr  = &ADC1->SMPR1;
        shift = 3U * (channel - 10U);
    }
    *smpr = (*smpr & ~(7U << shift)) | (smp_bits << shift);
}

/* Clocks, ADC prescaler and the signal pin: called from the HAL init hook so
 * a future HAL-based M6 timebase keeps the same bring-up order. */
static void acq_clocks_and_pins(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin   = ACQ_SIGNAL_PIN;
    gpio.Mode  = GPIO_MODE_ANALOG;       /* no pull: the front end drives it */
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ACQ_SIGNAL_PORT, &gpio);

    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_ADC_CONFIG(ACQ_ADC_PRESCALER);   /* PCLK2/6 -> <=14 MHz ADC clock */
}

/* ------------------------------------------------------------ DMA path */

/* Configure and start the circular transfer: ADC1 DR -> s_buf, halfwords. */
static uint8_t dma_start(void)
{
    uint8_t rc;

    /* Abort any previous transfer FIRST.  EN is cleared by writing CCR, so
     * polling it before that write can only time out.  The read-back below
     * proves the disable actually landed. */
    DMA1_Channel1->CCR   = 0U;
    rc = wait_bit_clear(&DMA1_Channel1->CCR, DMA_CCR_EN, ACQ_DMA_STOP_MS);
    if (rc != 0U) {
        status_acq_hw_errors_inc();
        return 1U;
    }
    DMA1_Channel1->CPAR  = (uint32_t)&(ADC1->DR);
    DMA1_Channel1->CMAR  = (uint32_t)(uintptr_t)s_buf;
    DMA1_Channel1->CNDTR = ACQ_SAMPLES;
    DMA1_Channel1->CCR   = DMA_CCR_EN | DMA_CCR_TCIE | DMA_CCR_TEIE
                 | DMA_CCR_CIRC | DMA_CCR_MINC
                 | DMA_CCR_PSIZE_0 | DMA_CCR_MSIZE_0    /* 16-bit both sides */
                 | DMA_CCR_PL_1;                        /* high priority     */
    return 0U;
}

/* Quiesce: stop conversions first, then the DMA (mirror of the vendor's
 * stop path, F-06, minus its unbounded behaviour).  Idempotent; bounded by
 * the module wait budgets. */
static void adc_quiesce(void)
{
    uint8_t rc;

    ADC1->SR = 0U;                        /* all flags: EOC/JEOC/STRT/JSTRT/AWD */
    DMA1_Channel1->CCR = 0U;
    rc = wait_bit_clear(&DMA1_Channel1->CCR, DMA_CCR_EN, ACQ_DMA_STOP_MS);
    if (rc != 0U) {
        status_acq_hw_errors_inc();       /* EN did not acknowledge in time */
    }
    ADC1->CR2 &= ~ADC_CR2_ADON;
    rc = wait_bit_clear(&ADC1->CR2, ADC_CR2_ADON, ACQ_ADC_STOP_MS);
    if (rc != 0U) {
        status_acq_hw_errors_inc();
    }
}

/* ------------------------------------------------------------ public */

void acq_isr_dma(void)
{
    uint32_t isr = DMA1->ISR;

    if ((isr & DMA_ISR_TCIF1) != 0U) {
        DMA1->IFCR = DMA_IFCR_CTCIF1;     /* clear-by-write pattern */
        status_acq_dma_tc_inc();          /* buffer reloaded: liveness */
    }
    if ((isr & DMA_ISR_TEIF1) != 0U) {
        DMA1->IFCR = DMA_IFCR_CTEIF1;
        status_acq_hw_errors_inc();       /* bus error - counted, never silent */
    }
}

void acq_isr_adc(void)
{
    /* Not running?  Then this interrupt is not ours: leave the ADC
     * registers completely alone (disarming AWDIE or clearing SR here
     * would silently break whatever else is using the ADC - e.g. the
     * diagnostic screen's one-shot reads). */
    if (s_inited == 0U) {
        return;
    }
    if ((ADC1->SR & ADC_SR_AWD) == 0U) {
        return;                           /* not ours - flags we did not enable */
    }
    /* One-shot latch: DISARM the interrupt first, so a standing
     * out-of-window signal cannot re-fire the ISR (free-running conversions
     * at ~530 kS/s would bury the SysTick feeder). */
    ADC1->CR1 &= ~ADC_CR1_AWDIE;
    ADC1->SR   = 0U;
    /* Write position = ACQ_SAMPLES - remaining CNDTR (the watchdog fired on
     * the conversion before the one being written now, so this lands one
     * index after the triggering sample - see acq_gate::trigger_index). */
    {
        uint32_t ndtr = DMA1_Channel1->CNDTR & 0xFFFFU;
        uint32_t idx  = (ndtr <= ACQ_SAMPLES) ? (ACQ_SAMPLES - ndtr) : 0U;

        if (s_event == 0U) {
            s_trigger_index = idx;
            s_event = 1U;                 /* the event main() is waiting for */
        }
    }
    status_acq_triggers_inc();
}

/* Stop the DMA transfer and leave it disabled (used on every failure path
 * once the channel has been configured). */
static void dma_stop_and_park(void)
{
    DMA1_Channel1->CCR &= ~DMA_CCR_EN;
    (void)wait_bit_clear(&DMA1_Channel1->CCR, DMA_CCR_EN, ACQ_DMA_STOP_MS);
}

/* Bounded wait for the pacing timer to move at least one sample. */
static uint8_t wait_cndtr_changed(uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();

    while ((int32_t)(HAL_GetTick() - (t0 + timeout_ms)) < 0) {
        if (DMA1_Channel1->CNDTR != ACQ_SAMPLES) {
            return 0U;
        }
    }
    return 1U;
}

/* Re-arm DMA and TIM3 after a freeze.  The ADC's regular-group setup is
 * untouched, so this is just the post-pause half of acq_resume(). */
static enum acq_status pacing_resume(void)
{
    struct tim3_config cfg;

    /* Re-enable ADC DMA requests only; the circular channel stays enabled,
     * so CNDTR/CMAR continue from the freeze position. */
    ADC1->CR2 |= ADC_CR2_DMA;
    s_resume_pos = (uint16_t)((ACQ_SAMPLES
                     - (DMA1_Channel1->CNDTR & 0xFFFFU)) % ACQ_SAMPLES);
    if ((tim3_config_for(s_timebase, &cfg) != 0U)
        || (tim3_hw_start(&cfg) != 0U)) {
        status_acq_hw_errors_inc();
        return ACQ_HW_ERROR;
    }
    return ACQ_OK;
}

enum acq_status acq_set_timebase(uint8_t index)
{
    struct tim3_config cfg;
    const struct timebase_step *step;

    if (tim3_config_for(index, &cfg) != 0U) {
        return ACQ_PARAM;
    }
    /* Reject the equivalent-time-only steps: TIM3 would be programmed to a
     * tick shorter than one ADC conversion, so samples would repeat.  The
     * engine has no interleaved/repetitive acquisition to fall back on. */
    step = timebase_entry(index);
    if ((step == NULL) || (step->single_shot == 0U)) {
        return ACQ_PARAM;
    }
    if (s_inited == 0U) {
        return ACQ_NOT_INITED;
    }
    tim3_hw_stop();                       /* stop pacing while it changes */
    /* Match the ADC aperture to the sample interval: the faster ranges need
     * a shorter sampling time or a conversion cannot finish within one
     * trigger period (50 us/div requires SMP=1.5).  Apply while pacing is
     * stopped so the change takes effect before the next conversion. */
    adc_channel_sample_time(ACQ_SIGNAL_CHANNEL,
                            (uint32_t)timebase_smp_code(index));
    /* The timer keeps its counter running into the new period only until
     * the next update; restart both so the first new interval is full. */
    if (tim3_hw_start(&cfg) != 0U) {      /* 0 = started, 1 = bad cfg */
        status_acq_hw_errors_inc();
        return ACQ_HW_ERROR;
    }
    s_timebase = index;
    return ACQ_OK;
}

enum acq_status acq_init(void)
{
    enum acq_status rc = ACQ_OK;

    /* ADC1 is shared with the front-panel ladder reader.  Continuous
     * acquisition takes the token for its whole lifetime; a ladder read
     * attempted meanwhile fails instead of corrupting SQR3. */
    if (adc_owner_acquire(ADC_OWNER_ACQ) != 0U) {
        status_acq_hw_errors_inc();
        return ACQ_HW_ERROR;
    }

    /* A re-init invalidates any previous snapshot: the hardware state it
     * came from no longer exists. */
    s_snap_valid = 0U;

    adc_quiesce();                        /* safe on a cold boot, idempotent */

    if (s_inited == 0U) {
        acq_clocks_and_pins();
    }

    /* Regular group: 1 conversion on ACQ_SIGNAL_CHANNEL, right-aligned,
     * continuous. */
    /* CR1: scan off, discontinuous off.  The watchdog enable bits are part
     * of this write - the AWD interrupt itself is armed per cycle. */
    ADC1->CR1 = 0U;
    ADC1->CR1 = ((uint32_t)ACQ_SIGNAL_CHANNEL & ADC_CR1_AWDCH)
              | ADC_CR1_AWDSGL           /* watchdog monitors ONE channel   */
              | ADC_CR1_AWDEN;           /* ...on the regular group         */

    /* CR2: right align (0), one conversion per TIM3 TRGO (EXTTRIG=1,
     * EXTSEL=101), continuous mode OFF so the pacing timer really is the
     * sample clock, DMA request enabled.
     * ADON is NOT set yet - enable below, then calibrate. */
    ADC1->CR2 = ((uint32_t)ACQ_EXTSEL_TIM3_TRGO << ADC_CR2_EXTSEL_Pos)
              | ADC_CR2_EXTTRIG
              | ADC_CR2_DMA;

    ADC1->SQR1 = 0U;                      /* L[3:0] = 0 -> 1 conversion */
    ADC1->SQR3 = ACQ_SIGNAL_CHANNEL;      /* first (only) conversion = ch2 */
    /* Sample time is finalised by acq_set_timebase() below per range;
     * start from the baseline so calibration/conversion setup is valid. */
    adc_channel_sample_time(ACQ_SIGNAL_CHANNEL, (uint32_t)TIMEBASE_SMP_BASELINE);

    /* Trigger thresholds: keep the user's level and slope across a re-init.
     * Before this fix a recovery re-init silently reset the band to the
     * compile-time default while the getter still reported the user's
     * level.  The band is one-sided, see apply_trigger_band(). */
    apply_trigger_band();

    /* Enable + stabilise + calibrate - bounded, register-level. */
    SET_BIT(ADC1->CR2, ADC_CR2_ADON);
    ms_delay(2U);                         /* >= t_STAB ~ 1 us, use 2 ms    */
    ADC1->CR2 |= ADC_CR2_RSTCAL;
    if (wait_bit_clear(&ADC1->CR2, ADC_CR2_RSTCAL, ACQ_CAL_TIMEOUT_MS) != 0U) {
        status_acq_hw_errors_inc();
        return ACQ_HW_ERROR;
    }
    ADC1->CR2 |= ADC_CR2_CAL;
    if (wait_bit_clear(&ADC1->CR2, ADC_CR2_CAL, ACQ_CAL_TIMEOUT_MS) != 0U) {
        status_acq_hw_errors_inc();
        adc_quiesce();
        return ACQ_HW_ERROR;
    }

    if (dma_start() != 0U) {
        adc_quiesce();
        return ACQ_HW_ERROR;
    }

    /* Start the pacing timer.  From here every update event makes TRGO
     * pace one conversion, so the sample rate is the selected timebase.
     * s_inited must be set FIRST: acq_set_timebase() refuses to touch the
     * timer before the engine is ready (that ordering bug made every boot
     * fail with ACQ_NOT_INITED, leaving the ADC configured but silent). */
    {
        uint8_t resume_tb = (s_inited != 0U) ? s_timebase
                                             : (uint8_t)ACQ_DEFAULT_TIMEBASE;

        s_inited = 1U;
        if (acq_set_timebase(resume_tb) != ACQ_OK) {
            s_inited = 0U;
            status_acq_hw_errors_inc();
            dma_stop_and_park();
            return ACQ_HW_ERROR;
        }
    }

    /* Proof that conversions really started: the DMA counter must leave its
     * initial value.  A timer running with a dead ADC, or a trigger source
     * the ADC does not accept, both look like "configured fine" otherwise. */
    if (wait_cndtr_changed(ACQ_START_MS) != 0U) {
        s_inited = 0U;
        status_acq_hw_errors_inc();
        tim3_hw_stop();
        dma_stop_and_park();
        return ACQ_HW_ERROR;
    }

    /* A re-init after a hardware fault must still leave SINGLE armed, or the
     * instrument would sit in HOLD forever with no obvious way to recover. */
    if (s_trig.mode == (uint8_t)TRIG_MODE_SINGLE) {
        s_trig.armed = 1U;
    }

    /* NVIC: the two IRQs this engine owns.  Priority 2: SysTick (priority 0,
     * HAL_Init's TICK_INT priority) can always preempt these handlers, so a
     * slow/blocked acquisition ISR can never stall the watchdog feeder -
     * the same policy comment arm lives in stm32f1xx_it.c. */
    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 2U, 0U);
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
    HAL_NVIC_SetPriority(ADC1_2_IRQn, 2U, 0U);
    HAL_NVIC_EnableIRQ(ADC1_2_IRQn);

    s_inited   = 1U;
    s_event    = 0U;
    s_trigger_index = 0U;
    s_ring_primed = 0U;   /* dma_start() emptied the ring */
    return rc;
}

enum acq_status acq_pause(void)
{
    if (s_inited == 0U) {
        return ACQ_NOT_INITED;
    }

    tim3_hw_stop();            /* no new triggers                      */
    /* Keep the DMA channel itself running: disabling it was what lost the
     * ring position and produced the zero-tail windows.  Gate the ADC's DMA
     * request instead, so the ladder's software conversions cannot write
     * into the sample ring but the circular transfer state survives. */
    ADC1->CR2 &= ~ADC_CR2_DMA;

    /* Clear EXTTRIG so a software start works for the ladder reads.
     * With EXTTRIG set the F1 ignores SWSTART and the conversion waits
     * for a TIM3 edge that no longer comes - every ladder read timed
     * out (0xFFFF) before this line existed. */
    /* Point the regular trigger at SWSTART (keeping EXTTRIG) so the ladder
     * reader's software start works.  Clearing EXTTRIG instead does NOT work
     * on this chip - with EXTTRIG clear a bare SWSTART is ignored and every
     * ladder read times out (65535); EXTSEL=111 + EXTTRIG + SWSTART completes
     * immediately (measured over SWD). */
    ADC1->CR2 = (ADC1->CR2 & ~ADC_CR2_EXTSEL)
              | ((uint32_t)ACQ_EXTSEL_SWSTART << ADC_CR2_EXTSEL_Pos);
    (void)adc_owner_release(ADC_OWNER_ACQ);
    return ACQ_OK;
}

enum acq_status acq_resume(void)
{
    struct tim3_config cfg;

    if (s_inited == 0U) {
        return ACQ_NOT_INITED;
    }
    if (adc_owner_acquire(ADC_OWNER_ACQ) != 0U) {
        return ACQ_HW_ERROR;   /* someone else took it during the pause */
    }

    /* Undo everything the ladder read changed: the channel, the sample
     * time (the reader leaves 239.5 cycles behind) and the trigger, which
     * pause() had to disable.  Restore the aperture for the active timebase
     * (fast ranges need SMP=1.5 to fit within one sample interval). */
    ADC1->SQR3 = ACQ_SIGNAL_CHANNEL;
    adc_channel_sample_time(ACQ_SIGNAL_CHANNEL,
                            (uint32_t)timebase_smp_code(s_timebase));
    ADC1->CR2 = (ADC1->CR2 & ~ADC_CR2_EXTSEL)
              | ((uint32_t)ACQ_EXTSEL_TIM3_TRGO << ADC_CR2_EXTSEL_Pos);
    ADC1->CR2 |= ADC_CR2_EXTTRIG;
    /* Re-enable ADC DMA requests; the channel was never disabled.  The
     * position where we resume is the boundary after the ladder gap, and
     * acq_cycle waits for pre-trigger headroom beyond it. */
    ADC1->CR2 |= ADC_CR2_DMA;
    s_resume_pos = (uint16_t)((ACQ_SAMPLES
                     - (DMA1_Channel1->CNDTR & 0xFFFFU)) % ACQ_SAMPLES);
    if ((tim3_config_for(s_timebase, &cfg) != 0U)
        || (tim3_hw_start(&cfg) != 0U)) {
        return ACQ_HW_ERROR;
    }
    {
        uint32_t tick = timebase_tick_ns(s_timebase);
        uint32_t start_ms = (tick / 1000000U) * 2U + 20U;

        if (start_ms > 250U) { start_ms = 250U; }
        if (wait_cndtr_changed(start_ms) != 0U) {
            return ACQ_HW_ERROR;
        }
    }
    return ACQ_OK;
}

enum acq_status acq_set_trigger_level(uint16_t level)
{
    if (s_inited == 0U) {
        return ACQ_NOT_INITED;
    }
    /* Remember the applied level for the trigger marker (the ADC registers
     * hold the hysteresis-expanded, one-sided band, not the user's level). */
    s_trig_level = (level > 4095U) ? 4095U : level;
    /* The F1 watchdog fires on out-of-band samples; the band is one-sided
     * so only the requested edge direction can fire. */
    apply_trigger_band();
    return ACQ_OK;
}

enum acq_status acq_set_trigger_slope(uint8_t slope)
{
    if (slope > 2U) {
        return ACQ_PARAM;
    }
    if (s_inited == 0U) {
        return ACQ_NOT_INITED;
    }
    /* AUTO ("either edge") still has to pick one direction for a repeatable
     * display; rising is the conventional choice and matches the marker. */
    slope = (slope == 1U) ? 1U : 0U;
    if (slope != s_trig_slope_hw) {
        s_trig_slope_hw = slope;
        apply_trigger_band();
    }
    return ACQ_OK;
}

enum acq_status acq_set_trigger_mode(uint8_t mode)
{
    if (s_inited == 0U) {
        return ACQ_NOT_INITED;
    }
    if (mode > (uint8_t)TRIG_MODE_SINGLE) {
        return ACQ_PARAM;
    }
    s_trig.mode = mode;
    if (mode == (uint8_t)TRIG_MODE_SINGLE) {
        s_trig.armed = 1U;      /* entering SINGLE means "capture one" */
    }
    return ACQ_OK;
}

enum acq_status acq_trigger_arm(void)
{
    if (s_inited == 0U) {
        return ACQ_NOT_INITED;
    }
    trig_fsm_arm(&s_trig);
    return ACQ_OK;
}

enum acq_status acq_cycle(uint32_t timeout_ms, uint16_t post_samples,
                          struct acq_gate *out)
{
    uint32_t t0;
    uint8_t  triggered = 0U;

    if (s_inited == 0U) {
        return ACQ_NOT_INITED;
    }
    if ((out == NULL) || (timeout_ms == 0U)) {
        return ACQ_PARAM;
    }
    if (timeout_ms > ACQ_MAX_WAIT_MS) {
        timeout_ms = ACQ_MAX_WAIT_MS;
    }

    /* The stages below can each wait for seconds on a slow timebase.  Each
     * one is individually bounded well under the IWDG period, but the sum is
     * not: prime (7 s) + pre-fill (4 s) + trigger (5 s) + post-fill (8 s)
     * could exceed it and reset a perfectly healthy instrument.  Feed at the
     * stage boundaries; a real hang still resets because every wait is
     * bounded by HAL_GetTick(). */
    board_iwdg_feed();

    out->triggered     = 0U;
    out->fresh         = 0U;
    out->trigger_index = 0U;
    out->write_index   = s_snap_pos;
    out->duration_ms   = 0U;

    /* SINGLE already took its one window: stay parked without touching the
     * sample path, so the displayed capture cannot be overwritten. */
    if ((s_trig.mode == (uint8_t)TRIG_MODE_SINGLE)
        && (s_trig.armed == 0U)) {
        return ACQ_OK;
    }

    /* Prime the ring once: wait for the circular transfer to wrap so every
     * index holds a real sample.  The DMA runs continuously; this is only a
     * first-boot condition. */
    if (s_ring_primed == 0U) {
        uint32_t tick = timebase_tick_ns(s_timebase);
        uint64_t need_ms = ((uint64_t)ACQ_SAMPLES * (uint64_t)tick)
                         / 1000000ULL + 50ULL;
        uint32_t prime_ms = (need_ms > 7000ULL) ? 7000U : (uint32_t)need_ms;
        uint32_t prime_deadline = HAL_GetTick() + prime_ms;
        uint32_t prev = DMA1_Channel1->CNDTR & 0xFFFFU;

        for (uint32_t waited = 0U; waited <= prime_ms; waited++) {
            uint32_t ndtr = DMA1_Channel1->CNDTR & 0xFFFFU;

            if (ndtr > (prev + (ACQ_SAMPLES / 2U))) {
                break;                    /* wrapped: ring is fully valid  */
            }
            prev = ndtr;
            if ((int32_t)(HAL_GetTick() - prime_deadline) >= 0) {
                break;
            }
            ms_delay(1U);
            board_iwdg_feed();
        }
        s_ring_primed = 1U;
        s_resume_pos = (uint16_t)((ACQ_SAMPLES
                     - (DMA1_Channel1->CNDTR & 0xFFFFU)) % ACQ_SAMPLES);
    }
    board_iwdg_feed();

    /* Pre-trigger headroom from the last resume point (ladder pause or the
     * previous freeze).  The samples between that point and the trigger must
     * exist before the window is placed, or its pre-trigger part reads the
     * gap/discontinuity. */
    {
        uint32_t tick = timebase_tick_ns(s_timebase);
        uint64_t need_ms = ((uint64_t)TRIG_PRE_TRIGGER * (uint64_t)tick)
                         / 1000000ULL + 20ULL;
        uint32_t pre_ms = (need_ms > 4000ULL) ? 4000U : (uint32_t)need_ms;
        uint32_t pre_deadline = HAL_GetTick() + pre_ms;

        for (uint32_t waited = 0U; waited <= pre_ms; waited++) {
            uint32_t ndtr = DMA1_Channel1->CNDTR & 0xFFFFU;
            uint32_t pos = (ndtr <= ACQ_SAMPLES)
                         ? ((ACQ_SAMPLES - ndtr) % ACQ_SAMPLES) : 0U;

            if (acq_ring_advance(s_resume_pos, (uint16_t)pos,
                                 (uint16_t)ACQ_SAMPLES) >= TRIG_PRE_TRIGGER) {
                break;
            }
            if ((int32_t)(HAL_GetTick() - pre_deadline) >= 0) {
                break;                    /* extreme timebase: carry on    */
            }
            ms_delay(1U);
            board_iwdg_feed();
        }
    }
    board_iwdg_feed();

    /* Arm only when the input is on the waiting side of the one-sided band.
     * Arming while it is already past the threshold latches the watchdog on
     * the first conversion, giving the random-phase window this engine used
     * to show.  Bounded, and harmless when it expires (DC/GND/timeout). */
    wait_trigger_rearm();

    /* Rearm the one-shot watchdog: disarm, clear the stale flag, reset the
     * event, arm again.  Order matters: the flag must be CLEAN before the
     * interrupt is enabled, otherwise arming latches an old crossing. */
    ADC1->CR1 &= ~ADC_CR1_AWDIE;
    ADC1->SR   = 0U;
    s_event        = 0U;
    s_trigger_index = 0U;
    ADC1->CR1 |= ADC_CR1_AWDIE;

    t0 = HAL_GetTick();
    {
        /* A DMA/ADC hardware error during the wait means the sample buffer
         * is stale - report it instead of handing back a plausible-looking
         * window. */
        uint32_t err_before = status_acq_hw_errors;
        uint8_t  hw_err     = 0U;
        uint32_t fed_at     = HAL_GetTick();

        /* Tight poll, deliberately: at the fastest timebase the whole
         * 1024-sample ring is replaced in under 2 ms, so the old
         * HAL_Delay(2) could observe the latch only after every
         * post-trigger sample had already been overwritten.  The caller's
         * timeout bounds this loop; the IWDG is fed once per millisecond
         * (the loop is tick-bounded, so this cannot mask a real hang). */
        while (s_event == 0U) {
            uint32_t now = HAL_GetTick();

            if (now != fed_at) {
                fed_at = now;
                board_iwdg_feed();
            }
            if (status_acq_hw_errors != err_before) {
                hw_err = 1U;
                break;
            }
            if ((int32_t)(now - (t0 + timeout_ms)) >= 0) {
                s_event = 2U;             /* no crossing: AUTO-style fallback */
                break;
            }
        }
        if (hw_err != 0U) {
            out->triggered     = 0U;
            out->trigger_index = 0U;
            out->write_index   = 0U;
            out->duration_ms   = HAL_GetTick() - t0;
            return ACQ_HW_ERROR;
        }
    }

    /* Post-trigger fill.  A window that starts `pre` samples before the
     * trigger needs (window - pre) samples AFTER it; freezing at the
     * interrupt would leave only pre-trigger history and a discontinuity
     * mid-screen.  Wait for a bounded number of conversions, tracked with
     * the wrap-safe ring distance, then freeze. */
    board_iwdg_feed();
    if (s_event == 1U) {
        if (post_samples == 0U) {
            triggered = 1U;
        } else {
            uint32_t base = s_trigger_index % ACQ_SAMPLES;
            /* The budget must scale with the sample interval: waiting for
             * 224 samples takes 0.7 ms at 0.1 ms/div but 3.5 s at
             * 500 ms/div.  A fixed 1 s budget made every cycle at 200 ms/div
             * and slower give up and re-capture at a random phase. */
            uint32_t post_ms = ACQ_POST_FILL_MS;
            {
                uint32_t tick = timebase_tick_ns(s_timebase);
                uint64_t need = ((uint64_t)post_samples * (uint64_t)tick)
                              / 1000000ULL + 50ULL;

                if (need > 8000ULL) { need = 8000ULL; } /* stay under IWDG */
                if (need > (uint64_t)post_ms) { post_ms = (uint32_t)need; }
            }
            uint32_t post_deadline = HAL_GetTick() + post_ms;

            /* Wait for the post-trigger samples with a 1 ms tick per
             * iteration.  The previous loop spun 1000 times with NO delay,
             * which takes only microseconds: the DMA advances one sample per
             * 3.1 us (fast) to 156 us (5 ms/div), so the counter could never
             * reach `post_samples` before the guard ran out and every cycle
             * was reported as a timeout - even though the watchdog fired.
             * The iteration count is now the millisecond budget, so the
             * bound is real wall-clock time and still finite for static
             * checking. */
            for (uint32_t waited = 0U; waited <= post_ms; waited++) {
                uint32_t ndtr = DMA1_Channel1->CNDTR & 0xFFFFU;
                uint32_t pos  = (ndtr <= ACQ_SAMPLES)
                              ? ((ACQ_SAMPLES - ndtr) % ACQ_SAMPLES) : 0U;

                if (acq_ring_advance((uint16_t)base, (uint16_t)pos,
                                     (uint16_t)ACQ_SAMPLES) >= post_samples) {
                    triggered = 1U;
                    break;
                }
                if ((int32_t)(HAL_GetTick() - post_deadline) >= 0) {
                    break;                /* fall back to the tail window */
                }
                ms_delay(1U);
                board_iwdg_feed();
            }
        }
    }

    /* Mode policy: AUTO falls back to the newest window, NORM/SINGLE hold
     * the previous display.  A hold leaves the pacing timer and DMA running;
     * the next call simply re-arms the watchdog. */
    {
        enum trig_action action = trig_fsm_action(
            &s_trig, triggered, (triggered == 0U) ? 1U : 0U);

        if (action == TRIG_ACTION_HOLD) {
            out->duration_ms = HAL_GetTick() - t0;
            out->write_index = s_snap_pos;
            return ACQ_OK;
        }
    }

    /* Freeze: stop the pacing timer, gate the ADC DMA request, let any
     * in-flight transfer land, then copy.  The DMA channel stays enabled so
     * its CNDTR/CMAR survive; only the requests stop. */
    tim3_hw_stop();
    ADC1->CR2 &= ~ADC_CR2_DMA;
    {
        uint32_t prev = DMA1_Channel1->CNDTR & 0xFFFFU;

        for (uint32_t i = 0U; i < 5U; i++) {
            uint32_t cur;

            ms_delay(1U);
            cur = DMA1_Channel1->CNDTR & 0xFFFFU;
            if (cur == prev) {
                break;               /* the ring is no longer moving */
            }
            prev = cur;
        }
    }

    {
        uint32_t ndtr = DMA1_Channel1->CNDTR & 0xFFFFU;

        memcpy(s_snap, s_buf, sizeof s_snap);
        s_snap_pos   = (ndtr <= ACQ_SAMPLES)
                     ? ((ACQ_SAMPLES - ndtr) % ACQ_SAMPLES) : 0U;
        s_snap_valid = 1U;
    }

    out->fresh       = 1U;
    out->duration_ms = HAL_GetTick() - t0;
    out->write_index = s_snap_pos;

    /* Restart for the next cycle.  Each cycle is a fresh capture, so the
     * ring is re-based at index 0; the copy gap is not part of any window. */
    if (pacing_resume() != ACQ_OK) {
        out->triggered     = 0U;
        out->trigger_index = 0U;
        return ACQ_HW_ERROR;
    }

    if (triggered != 0U) {
        out->triggered     = 1U;
        out->trigger_index = s_trigger_index;
        return ACQ_OK;
    }
    out->triggered     = 0U;
    out->trigger_index = 0U;
    status_acq_timeouts_inc();
    return ACQ_TIMEOUT;
}

const uint16_t *acq_samples(void)
{
    /* Until the first cycle there is no snapshot; the live buffer is the
     * only sensible answer (and no caller reads it before a cycle). */
    return (s_snap_valid != 0U) ? s_snap : s_buf;
}

uint32_t acq_sample_count(void)
{
    return ACQ_SAMPLES;
}

uint8_t acq_timebase_get(void)
{
    return s_timebase;
}

uint16_t acq_trigger_level_get(void)
{
    return s_trig_level;
}

uint8_t acq_trigger_mode_get(void)
{
    return s_trig.mode;
}

