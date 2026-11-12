/* ILI9341 display driver for the DSO183.
 *
 * Interface decoded from the vendor firmware (0x08001cb8 / 0x08001cec):
 *
 *   data bus  PB0..PB7   (8-bit parallel)
 *   CS        PC13       (low while a byte is transferred)
 *   D/C       PC14       (high = command, low = data)
 *   WR        PC15       (high pulse after the byte is on the bus)
 *   RESET     PB9        (active low)
 *
 * The controller was identified by the literal 0x9341 in the vendor code.
 * Initialisation is the standard ILI9341 sequence, because the vendor's own
 * stream contains data bytes loaded from variables and could not be recovered
 * byte-exactly.
 *
 * Geometry: 320 x 240 landscape, RGB565 (pixel format 0x55), MADCTL 0x20 (MV, RGB).
 */
#ifndef ILI9341_H
#define ILI9341_H

#include <stdint.h>

#define ILI9341_WIDTH  320
#define ILI9341_HEIGHT 240

/* This panel runs with MADCTL BGR=0 (the vendor's final 0x20), so the 16-bit
 * word is standard RGB565: R4:0 | G5:0 | B4:0.  Call sites stay in plain RGB. */
#define ILI9341_RGB565(r, g, b)                                    \
    ((uint16_t)((((uint16_t)(r) & 0xF8u) << 8) |                   \
                (((uint16_t)(g) & 0xFCu) << 3) |                   \
                 (((uint16_t)(b) & 0xF8u) >> 3)))

#define ILI9341_BLACK   ILI9341_RGB565(0, 0, 0)
#define ILI9341_WHITE   ILI9341_RGB565(255, 255, 255)
#define ILI9341_RED     ILI9341_RGB565(255, 0, 0)
#define ILI9341_GREEN   ILI9341_RGB565(0, 255, 0)
#define ILI9341_BLUE    ILI9341_RGB565(0, 0, 255)
#define ILI9341_YELLOW  ILI9341_RGB565(255, 255, 0)
#define ILI9341_CYAN    ILI9341_RGB565(0, 255, 255)
#define ILI9341_MAGENTA ILI9341_RGB565(255, 0, 255)
#define ILI9341_GREY    ILI9341_RGB565(128, 128, 128)

/* Reset the panel and run the vendor's initialisation sequence. */
void ili9341_init(void);

/* Set the GRAM window; all following pixel data lands inside it. */
void ili9341_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

/* Fill the whole screen with one colour. */
void ili9341_fill_screen(uint16_t color);

/* Fill a rectangle. */
void ili9341_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/* Draw one character from the 5x7 font (scale 1..4). */
void ili9341_draw_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale);

/* Read the controller ID back over the bus (the vendor's sequence: command 0x04,
 * then the controller drives the data bus and we sample it on the PB8 read
 * strobe).  Returns 0 when the read fails.  This is the objective proof that the
 * display bus works in both directions. */
uint32_t ili9341_read_id(void);
uint32_t ili9341_read_madctl(void);

/* Draw a NUL-terminated string. */
void ili9341_print(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale);

#endif /* ILI9341_H */
