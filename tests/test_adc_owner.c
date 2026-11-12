/* Host unit tests for the ADC1 ownership token (app/adc_owner.c). */
#include "adc_owner.h"

#include <stdio.h>

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

int main(void)
{
    adc_owner_reset();
    CHECK(adc_owner_get() == ADC_OWNER_NONE);

    /* Free -> acquisition takes it, ladder is refused. */
    CHECK(adc_owner_acquire(ADC_OWNER_ACQ) == 0U);
    CHECK(adc_owner_get() == ADC_OWNER_ACQ);
    CHECK(adc_owner_acquire(ADC_OWNER_INPUT) == 1U);
    CHECK(adc_owner_get() == ADC_OWNER_ACQ);      /* unchanged by the refusal */

    /* Idempotent re-take, then the wrong owner cannot free it. */
    CHECK(adc_owner_acquire(ADC_OWNER_ACQ) == 0U);
    CHECK(adc_owner_release(ADC_OWNER_INPUT) == 1U);
    CHECK(adc_owner_get() == ADC_OWNER_ACQ);

    /* Owner releases -> ladder can take it and give it back. */
    CHECK(adc_owner_release(ADC_OWNER_ACQ) == 0U);
    CHECK(adc_owner_get() == ADC_OWNER_NONE);
    CHECK(adc_owner_acquire(ADC_OWNER_INPUT) == 0U);
    CHECK(adc_owner_release(ADC_OWNER_INPUT) == 0U);
    CHECK(adc_owner_get() == ADC_OWNER_NONE);

    /* Invalid owner values are refused in both directions. */
    CHECK(adc_owner_acquire(ADC_OWNER_NONE) == 1U);
    CHECK(adc_owner_acquire(ADC_OWNER_COUNT) == 1U);
    CHECK(adc_owner_acquire(255U) == 1U);
    CHECK(adc_owner_get() == ADC_OWNER_NONE);
    CHECK(adc_owner_release(ADC_OWNER_NONE) == 1U);   /* nothing held      */
    CHECK(adc_owner_release(ADC_OWNER_ACQ) == 1U);

    /* Reset clears a held token (used at init). */
    CHECK(adc_owner_acquire(ADC_OWNER_ACQ) == 0U);
    adc_owner_reset();
    CHECK(adc_owner_get() == ADC_OWNER_NONE);

    if (failures == 0) {
        printf("adc_owner: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
