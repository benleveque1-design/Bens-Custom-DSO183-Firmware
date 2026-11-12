#include "adc_owner.h"

static uint8_t s_owner = ADC_OWNER_NONE;

static uint8_t valid_owner(uint8_t who)
{
    return (who == ADC_OWNER_ACQ || who == ADC_OWNER_INPUT) ? 1U : 0U;
}

uint8_t adc_owner_acquire(uint8_t who)
{
    if (valid_owner(who) == 0U) {
        return 1U;
    }
    if (s_owner == who) {
        return 0U;                     /* already ours - idempotent */
    }
    if (s_owner != ADC_OWNER_NONE) {
        return 1U;                     /* busy: do not steal it     */
    }
    s_owner = who;
    return 0U;
}

uint8_t adc_owner_release(uint8_t who)
{
    if ((valid_owner(who) == 0U) || (s_owner != who)) {
        return 1U;                     /* never clear another owner */
    }
    s_owner = ADC_OWNER_NONE;
    return 0U;
}

uint8_t adc_owner_get(void)
{
    return s_owner;
}

void adc_owner_reset(void)
{
    s_owner = ADC_OWNER_NONE;
}
