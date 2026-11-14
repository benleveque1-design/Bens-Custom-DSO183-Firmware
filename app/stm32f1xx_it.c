/* Interrupt handlers for the custom firmware.
 *
 * Only the handlers that are actually enabled are implemented.  Everything else
 * stays aliased to the startup file's Default_Handler trap, so a surprise
 * interrupt is loud and obvious instead of silently corrupting state.
 *
 * SysTick is the one that matters: HAL_Init() enables it, and HAL_Delay() and the
 * HAL tick depend on it.  The first M3b build forgot this and the board landed in
 * the trap loop within a millisecond.
 */
#include "status.h"
#include "acq.h"
#include "memdiag.h"
#include "menu.h"
#include "stm32f1xx_hal.h"

/* Acquisition interrupts (M5): both hooks live in acq.c and share its
 * module state; they only clear flags and latch an event - the SysTick
 * handler above keeps being the watchdog feeder, even during ISR
 * sequences, because neither hook touches the IWDG and neither blocks. */
void DMA1_Channel1_IRQHandler(void)
{
    memdiag_msp_sample();
    acq_isr_dma();
}

void ADC1_2_IRQHandler(void)
{
    memdiag_msp_sample();
    acq_isr_adc();
}

void SysTick_Handler(void)
{
    memdiag_msp_sample();
    HAL_IncTick();
    status_heartbeat_inc();
    menu_button_tick();          /* 1 ms front-panel sampler (debounced) */
    /* Deliberately NOT a watchdog feed.  The IWDG is fed once per main-loop
     * iteration in main().  Feeding it here as well would keep a hung main
     * loop alive forever, so a lockup (for example a surprise IRQ landing in
     * Default_Handler, which leaves SysTick running) could never be recovered
     * by the watchdog.  The heartbeat stays here because it must advance even
     * while the main loop is blocked in a long HAL_Delay. */
}

/* Console RX for the PC link: the ring buffer is filled here, never in loop
 * context, so a blocking transmit cannot lose incoming command bytes. */
extern void board_uart_rx_isr(void);

void USART1_IRQHandler(void)
{
    board_uart_rx_isr();
}
