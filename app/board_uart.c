/* The console UART instance.
 *
 * The HAL declares `extern UART_HandleTypeDef huart1;` in stm32f1xx_hal_uart.h;
 * the instance lives here so that application code only ever touches the handle
 * through board.h.
 */
#include "board.h"
#include "stm32f1xx_hal.h"

UART_HandleTypeDef huart1;

/* ---- console RX ring -------------------------------------------------------
 * The PC link sends 10-byte command packets.  Polling the data register from
 * the main loop loses bytes while a long text line is being transmitted, so
 * the interrupt fills this ring instead and the loop drains it.
 */
#define UART_RX_RING 64U

static volatile uint8_t  s_rx_ring[UART_RX_RING];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;

void board_uart_rx_start(void)
{
    s_rx_head = 0U;
    s_rx_tail = 0U;
    USART1->CR1 |= USART_CR1_RXNEIE;
    NVIC_EnableIRQ(USART1_IRQn);
}

void board_uart_rx_isr(void)
{
    uint8_t byte;
    uint16_t next;

    if ((USART1->SR & USART_SR_RXNE) == 0U) {
        (void)USART1->DR;              /* reading SR then DR clears ORE/FE */
        return;
    }
    byte = (uint8_t)(USART1->DR & 0xFFU);
    next = (uint16_t)((s_rx_head + 1U) % UART_RX_RING);
    if (next != s_rx_tail) {            /* drop on overflow, never block  */
        s_rx_ring[s_rx_head] = byte;
        s_rx_head = next;
    }
}

uint8_t board_uart_get_byte(uint8_t *out)
{
    if ((out == NULL) || (s_rx_tail == s_rx_head)) {
        return 0U;
    }
    *out = s_rx_ring[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1U) % UART_RX_RING);
    return 1U;
}
