/* TIM3 hardware commit - the only place that touches TIM3 registers.
 *
 * Not part of the host test build: app/tim3.c holds the arithmetic, this
 * file just writes it.  TRGO is an update event (CR2.MMS = 010), which is
 * what the ADC will use in M6 (ADC1->CR2: EXTSEL = 110 = TIM3_TRGO,
 * EXTTRIG = 1) - the ADC side is deliberately NOT wired here yet, because
 * the acquisition engine still runs its own software-start cycle.
 */
#include "tim3.h"

#include "stm32f1xx_hal.h"

uint8_t tim3_hw_start(const struct tim3_config *cfg)
{
    if (cfg == NULL) {
        return 1U;
    }

    __HAL_RCC_TIM3_CLK_ENABLE();

    /* Reprogamme from a known state, stopped: the counter must not run while
     * PSC/ARR change, or the first period is a mixture of old and new. */
    TIM3->CR1  = 0U;
    TIM3->CR2  = 0U;
    TIM3->SMCR = 0U;                    /* internal clock, no slave mode   */
    TIM3->DIER = 0U;                    /* no interrupts: pacing only      */
    TIM3->CCER = 0U;                    /* no capture/compare outputs      */

    TIM3->PSC = cfg->psc;
    TIM3->ARR = cfg->arr;
    TIM3->CNT = 0U;

    TIM3->EGR = TIM_EGR_UG;             /* latch PSC/ARR, reload the count */
    TIM3->SR  = 0U;                     /* UG also sets UIF - clear it     */

    TIM3->CR2 = TIM_CR2_MMS_1;          /* MMS = 010: TRGO on update       */
    TIM3->CR1 = TIM_CR1_CEN;
    return 0U;
}

void tim3_hw_stop(void)
{
    TIM3->CR1 &= ~TIM_CR1_CEN;
    TIM3->CR2  = 0U;                    /* no TRGO edges while idle        */
}
