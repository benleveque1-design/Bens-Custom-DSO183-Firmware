#include "input.h"
#include "adc_owner.h"

#include "stm32f1xx_hal.h"

/* Bounded spin budget for the ADC handshake.  One conversion takes
 * (sample time 239.5 + 12.5) ADC clocks at 10.67 MHz = ~24 us, i.e. ~1500
 * CPU cycles at 64 MHz - so 200k spins is three orders of magnitude of
 * head-room, while still guaranteeing the UI cannot hang on a dead ADC. */
#define INPUT_ADC_SPINS 200000U

static uint8_t s_ready;

static uint8_t wait_bit_clear(volatile uint32_t *reg, uint32_t mask, uint32_t spins)
{
    while (((*reg) & mask) != 0U) {
        if (spins == 0U) {
            return 1U;               /* timed out */
        }
        spins--;
    }
    return 0U;
}

static uint8_t wait_bit_set(volatile uint32_t *reg, uint32_t mask, uint32_t spins)
{
    while (((*reg) & mask) == 0U) {
        if (spins == 0U) {
            return 1U;               /* timed out */
        }
        spins--;
    }
    return 0U;
}

/* Sample time per channel: SMPR2 holds channels 0..9, SMPR1 holds 10..15,
 * three bits per channel. */
static void input_sample_time(uint32_t channel, uint32_t smp_bits)
{
    if (channel <= 9U) {
        uint32_t shift = channel * 3U;
        ADC1->SMPR2 = (ADC1->SMPR2 & ~(0x07UL << shift)) | (smp_bits << shift);
    } else {
        uint32_t shift = (channel - 10U) * 3U;
        ADC1->SMPR1 = (ADC1->SMPR1 & ~(0x07UL << shift)) | (smp_bits << shift);
    }
}

uint8_t input_init(void)
{
    if (s_ready != 0U) {
        return 1U;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    /* PA0..PA7 all analog: the ladder is on PA5/PA6/PA7 (ch5/6/7 per
     * ladder.h) and PA2 is the signal input.  In particular PA7 is CPL on
     * this board - it must never be driven as a digital output, or the ADC
     * samples TIM4's square wave instead of the switch ladder. */
    GPIOA->CRL = 0U;

    /* ADC clock = PCLK2 / 6 = 10.67 MHz, inside the 14 MHz F1 limit. */
    __HAL_RCC_ADC_CONFIG(RCC_ADCPCLK2_DIV6);

    /* Stop whatever state the ADC was in, then power it up and calibrate.
     * ADON is a plain control bit, not a self-clearing status flag: writing 0
     * disables immediately and writing 1 leaves it set, so polling it proves
     * nothing.  Read it back as a bus sanity check; RSTCAL/CAL below are the
     * real liveness test because those bits DO self-clear. */
    ADC1->CR2 &= ~ADC_CR2_ADON;
    ADC1->CR1 = 0U;
    ADC1->CR2 = ADC_CR2_ADON;
    if ((ADC1->CR2 & ADC_CR2_ADON) == 0U) {
        return 0U;                      /* ADON write did not stick */
    }

    ADC1->CR2 |= ADC_CR2_RSTCAL;
    if (wait_bit_clear(&ADC1->CR2, ADC_CR2_RSTCAL, INPUT_ADC_SPINS) != 0U) {
        return 0U;
    }
    ADC1->CR2 |= ADC_CR2_CAL;
    if (wait_bit_clear(&ADC1->CR2, ADC_CR2_CAL, INPUT_ADC_SPINS) != 0U) {
        return 0U;
    }

    /* The vendor's injected group (JSQR 0x002394C0) is not used here: on
     * this clone it raises JEOC but JDR1..3 stay at zero, so CPL would read
     * as a permanent DC.  The ladder is sampled from the regular group
     * instead, during acquisition pause (see main.c ladder_refresh()).
     * Configure the long sample time the vendor uses for every ladder
     * channel; input_read_channel() re-applies it per conversion anyway. */
    input_sample_time(5U, ADC_SAMPLETIME_239CYCLES_5);
    input_sample_time(6U, ADC_SAMPLETIME_239CYCLES_5);
    input_sample_time(7U, ADC_SAMPLETIME_239CYCLES_5);

    s_ready = 1U;
    return 1U;
}

int input_read_channel(uint8_t ch, uint16_t *out)
{
    if ((s_ready == 0U) || (out == NULL)) {
        return -1;
    }
    /* Unsigned range check: valid for any FIRST/LAST pair and free of the
     * "comparison is always false" warning a direct `ch < FIRST` would
     * produce with FIRST == 0. */
    if (((uint32_t)ch - (uint32_t)INPUT_ADC_FIRST_CHANNEL)
        > (uint32_t)(INPUT_ADC_LAST_CHANNEL - INPUT_ADC_FIRST_CHANNEL)) {
        return -1;
    }
    /* One-shot reads share ADC1's regular group with the acquisition
     * engine.  Take the token: if acquisition holds it, refuse rather than
     * retarget the DMA stream at a ladder channel. */
    if (adc_owner_acquire(ADC_OWNER_INPUT) != 0U) {
        return -1;
    }

    ADC1->SQR1 = 0U;                 /* one conversion in the regular group */
    ADC1->SQR3 = ch;                 /* ...on this channel                  */
    input_sample_time(ch, ADC_SAMPLETIME_239CYCLES_5);

    ADC1->SR = 0U;                   /* clear EOC and friends              */
    ADC1->CR2 |= ADC_CR2_SWSTART;    /* software start                      */

    if (wait_bit_set(&ADC1->SR, ADC_SR_EOC, INPUT_ADC_SPINS) != 0U) {
        /* Conversion did not complete.  On the F1 a read of DR clears the
         * EOC flag, so drain DR here: otherwise a LATER call would see the
         * stale EOC of this aborted conversion and return the previous
         * channel's data as if it were fresh. */
        (void)ADC1->DR;
        (void)adc_owner_release(ADC_OWNER_INPUT);
        return -1;                   /* conversion never completed          */
    }

    *out = (uint16_t)(ADC1->DR & 0x0FFFU);
    (void)adc_owner_release(ADC_OWNER_INPUT);
    return 0;
}

void input_scan(uint16_t *dst)
{
    uint8_t ch;

    if (dst == NULL) {
        return;
    }
    for (ch = INPUT_ADC_FIRST_CHANNEL; ch <= INPUT_ADC_LAST_CHANNEL; ch++) {
        uint16_t v;

        if (input_read_channel(ch, &v) != 0) {
            v = INPUT_ADC_INVALID;
        }
        dst[ch - INPUT_ADC_FIRST_CHANNEL] = v;
    }
}
