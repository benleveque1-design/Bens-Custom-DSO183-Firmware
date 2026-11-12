/* Board pin map for the DSO183.
 *
 * Only pins that are already verified from the vendor firmware are listed here.
 * Everything else must be confirmed before use (see
 * analysis/m1_frontpanel_storage.md and analysis/m1_display.md).
 */
#ifndef BOARD_H
#define BOARD_H

#include "stm32f1xx_hal.h"

/* the console UART instance lives in board_uart.c */
extern UART_HandleTypeDef huart1;

/* There is no heartbeat LED: PC13 is the display chip select (see below). */

/* Front panel push button: PIN NOT YET KNOWN.
 *
 * The old note here ("a push button on PA19, active low, superloop
 * 0x080018ae") was NOT verified and PA19 does not even exist on the
 * STM32F103C8 (LQFP48: PA0..PA15 only) - a macro like that is a landmine:
 * HAL_GPIO_Init would silently do nothing for a pin that is not there.
 *
 * Best candidate so far: PA8.  The vendor firmware reads GPIOA->IDR at
 * boot (0x0800185C) and tests bit 8 (`lsls r6,r3,#0x17` + `bmi`); when PA8
 * is low it runs a debounce/delay routine.  PA12 is read nearby but its
 * result is unused there.
 *
 * Until a pin is confirmed on real hardware there is deliberately NO macro
 * for it - the diagnostic screen only READS GPIOA and never reconfigures a
 * pin.  The slide switches are NOT GPIOs: they are an ADC1 ladder
 * (app/ladder.h). */

/* Display (ILI9341, 8-bit 8080 parallel) - verified in analysis/m1_display.md:
 *   data  PB0..PB7
 *   CS    PC13
 *   D/C   PC14
 *   WR    PC15
 *   RESET PB9
 */
#define LCD_PORT_DATA   GPIOB
#define LCD_PIN_DATA    GPIO_PIN_0 /* PB0..PB7 used as a byte */
#define LCD_PORT_CTRL   GPIOC
#define LCD_PIN_CS      GPIO_PIN_13
#define LCD_PIN_DC      GPIO_PIN_14
#define LCD_PIN_WR      GPIO_PIN_15
#define LCD_PIN_RESET   GPIO_PIN_9 /* PB9 */

/* Console: USART1 on PA9 (TX) / PA10 (RX) - the vendor's console pins. */
#define CONSOLE_UART    USART1
#define CONSOLE_TX_PIN  GPIO_PIN_9
#define CONSOLE_RX_PIN  GPIO_PIN_10

/**
 * @brief  Release the JTAG pins (PB3, PB4, PA15) so they can be used as GPIO.
 *
 *         The DSO183's display data bus is PB0..PB7, and on the F1 PB3/PB4 are
 *         JTAG pins by default.  While JTAG is enabled the JTAG block overrides
 *         the GPIO output on those two pins, so two bits of every byte we push
 *         to the panel are wrong.  The vendor firmware evidently has JTAG
 *         disabled: on its running build PB0..PB7 read back exactly what the ODR
 *         holds, while in ours PB3/PB4 read 1 regardless of the ODR.
 *
 *         SWJ_CFG = 01 ("without JTAG") frees PB3/PB4/PA15 and keeps the
 *         debug port on PA13/PA14, so SWD - our only way into the board -
 *         stays available.
 */
static inline void board_free_jtag_pins(void)
{
    __HAL_RCC_AFIO_CLK_ENABLE();
    /* SWJ_CFG = 0b010 (RM0008 AFIO_MAPR bits 26:24): JTAG-DP disabled, SW-DP
     * enabled.  That releases PB3 (JTDO), PB4 (NJTRST) and PA15 (JTDI) for
     * GPIO while keeping SWD alive on PA13/PA14.
     *
     * An earlier comment here claimed the clone encodes SWJ_CFG backwards and
     * that 0b01 was the value that released the pins.  That was a misreading:
     * 0b001 is "full SWJ without NJTRST", which frees PB4 but leaves PB3 as
     * JTDO - exactly the 0x18 read-back that was observed.  0b010 is the
     * correct, datasheet-defined value; do not change it to 0b001 or 0x33333333.
     * The vendor writes 0x33333333 early (0x08001b14) but runs with the JTAG
     * block released by its own later sequence. */
    AFIO->MAPR = 0x02000000u;
}


/* Console RX: interrupt-fed ring buffer for the PC link. */
void    board_uart_rx_start(void);
void    board_uart_rx_isr(void);
uint8_t board_uart_get_byte(uint8_t *out);

#endif /* BOARD_H */
