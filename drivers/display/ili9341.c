#include "ili9341.h"

#include "board.h"
#include "glyph.h"
#include "stm32f1xx_hal.h"

/* Control lines, taken from the vendor firmware itself (command writer
 * 0x08001cb8, data writer 0x08001cec, ID reader 0x0800203c):
 *
 *   PB0..PB7  data      (GPIOB->CRL = 0x33333333 while writing)
 *   PC13      CS        LOW = selected, HIGH = idle
 *   PC14      D/C       LOW = command, HIGH = data (and HIGH when idle)
 *   PC15      WR        byte is latched by the LOW -> HIGH edge
 *   PB8       /RD       LOW makes the panel drive the bus, HIGH when idle
 *   PB9       reset     active LOW; idle/released HIGH
 *
 * The vendor's write sequence, instruction for instruction:
 *
 *   str r2,[r3,#0x14]   @ BRR  +0x14 -> PC13 = 0   (select)
 *   str r1,[r3,#0x14]   @ BRR  -> PC14 = 0         (command; data uses BSRR)
 *   ... data byte ORed into GPIOB->ODR ...
 *   str r0,[r3,#0x14]   @ BRR  -> PC15 = 0         (WR low)
 *   str r0,[r3,#0x10]   @ BSRR -> PC15 = 1         (WR rise = latch)
 *   str r1,[r3,#0x10]   @ BSRR -> PC14 = 1         (idle: D/C high)
 *   str r2,[r3,#0x10]   @ BSRR -> PC13 = 1         (idle: deselect)
 *
 * Both polarities were inverted in every earlier revision of this driver (CS
 * asserted HIGH, D/C HIGH for commands), so the panel was deselected for every
 * byte and never received a single command - hence the permanent white screen.
 */
#define LCD_CTRL   GPIOC
#define LCD_DATA   GPIOB
#define PIN_CS     GPIO_PIN_13
#define PIN_DC     GPIO_PIN_14
#define PIN_WR     GPIO_PIN_15
#define PIN_RST    GPIO_PIN_9
#define PIN_RD     GPIO_PIN_8    /* read strobe (the vendor samples the ID here) */

/* One BSRR write can set and reset pins at the same time (low half = set,
 * high half = reset). */
static inline void lcd_pins(uint32_t set_mask, uint32_t reset_mask)
{
    LCD_CTRL->BSRR = set_mask | (reset_mask << 16);
}

/* Bus settling delay.  The ILI9341 needs >=10 ns of data setup time and >=10 ns
 * per WR phase; our writes are several times faster than the vendor's, so we
 * give the panel real time instead of racing it. */
static inline void lcd_settle(void)
{
    __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP();
}

/* Write one byte to the data bus, preserving the upper byte of GPIOB. */
static inline void lcd_data_byte(uint8_t v)
{
    LCD_DATA->ODR = (LCD_DATA->ODR & 0xFF00u) | v;
}

static inline void lcd_bus_write(uint8_t v, int is_command)
{
    GPIOC->BRR  = PIN_CS;               /* select                              */
    if (is_command) {
        GPIOC->BRR  = PIN_DC;           /* D/C low = command                   */
    } else {
        GPIOC->BSRR = PIN_DC;           /* D/C high = data                     */
    }
    lcd_data_byte(v);
    lcd_settle();
    GPIOC->BRR  = PIN_WR;               /* WR low                              */
    lcd_settle();
    GPIOC->BSRR = PIN_WR;               /* WR rise latches the byte            */
    lcd_settle();
    GPIOC->BSRR = PIN_DC;
    GPIOC->BSRR = PIN_CS;               /* back to idle                        */
}

static inline void lcd_cmd(uint8_t c)
{
    lcd_bus_write(c, 1);
}

static inline void lcd_data(uint8_t d)
{
    lcd_bus_write(d, 0);
}

/* The vendor's own init stream, transcribed from 0x08001d1c..0x08001e84.  This
 * replaces the generic Adafruit table: it is the only sequence known to bring up
 * this exact panel, and it carries several vendors-specific writes (0xE4, 0xB0,
 * 0x0A) rather than the usual Adafruit set.
 * MADCTL: the vendor writes 0x68 (MX|MV|BGR) at 0x08001d64; we write the
 * same 0x68.  An earlier revision wrote 0x20 (MV only), which mirrored the
 * whole picture left-to-right on this panel - the vendor constant had been
 * copied without its MX bit.
 * (MV only) on purpose.  MV=1 exchanges the row and column address roles,
 * so with MV the 0x2A range is the ROW axis (horizontal, 0..319) and 0x2B is
 * the COLUMN axis (vertical, 0..239) - which is why set_window sends x to
 * 0x2A and y to 0x2B.  We leave BGR off so that plain RGB565 constants
 * (0xF800 = red) map straight through, and we leave MX/MY off so nothing is
 * mirrored - a mirrored text screen would be unreadable.  Verified on
 * hardware (camera shows red/green/blue bands top-to-bottom).
 * 0xB0/0xB2 parameter bytes are the vendor's (0x00, not 0x0A): bit 3 of
 * 0xB0 is DMI and must be 0, because this driver writes 2 bytes per pixel.
 * The vendor additionally sends 0xE4 (27 00 10) and 0xC4 (20); we leave
 * both out - 0xE4 takes a single parameter, so the vendor's extra bytes
 * would be parsed as commands.
 * Format: command, data count, data bytes...; command 0x00 marks a delay whose
 * next byte is milliseconds. */
static const uint8_t k_init_seq[] = {
    0x11, 0,
    0x00, 120,
    0x00, 120,
    0x00, 120,
    0x00, 120,
    0x00, 120,
    0x36, 1, 0x68,
    0x3A, 1, 0x55,
    0x13, 0,
    0xB0, 2, 0x00, 0xE0,
    0xB2, 5, 0x0C, 0x0C, 0x00, 0x33, 0x33,
    0xB7, 1, 0x35,
    0xBB, 1, 0x2B,
    0xC0, 1, 0x2C,
    0xC2, 1, 0x01,
    0xC3, 1, 0x11,
    0xC6, 1, 0x0F,
    0xD0, 2, 0xA4, 0xA1,
    0xE0, 14, 0xD0, 0x0A, 0x05, 0x0E, 0x15, 0x0D, 0x37, 0x43,
              0x47, 0x09, 0x15, 0x12, 0x16, 0x19,
    0xE1, 14, 0xD0, 0x0A, 0x05, 0x0D, 0x0C, 0x06, 0x2D, 0x44,
              0x40, 0x0E, 0x1C, 0x18, 0x16, 0x19,
    0x0A, 1, 0x18,
    0x29, 0,
    0x00, 250,
    0x00, 250,
};

/* The pin modes match the vendor firmware on every pin the LCD uses
 * (measured on the running instrument):
 *   GPIOB->CRL = 0x33333333   PB0..PB7  = mode 3
 *   GPIOB->CRH = 0x88883433   PB8/PB9/PB11 = 3, PB10 = 4, PB12..PB15 = 8
 *   GPIOC->CRL = 0x44444444
 *   GPIOC->CRH = 0x33300000   PC13/PC14/PC15 = mode 3
 * One deliberate difference: the vendor leaves PB12/PB13 as push-pull
 * outputs (its CRH is 0x88333433), while this driver makes PB12..PB15
 * inputs with pull-up because it does not know what those nets drive and
 * must not energise them.  Nothing in the LCD wiring uses them.
 * Mode 3 on the F1 is "alternate function push-pull", which is what the vendor uses
 * for the data bus; the HAL's GPIO_MODE_AF_PP (0x02) is a *reserved* F1 encoding, so
 * the registers are written directly here.
 */
static void lcd_gpios_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIOB->CRL = 0x33333333u;
    GPIOB->CRH = 0x88883433u;
    GPIOC->CRL = 0x44444444u;
    GPIOC->CRH = 0x33300000u;

    GPIOB->ODR &= 0xFF00u;
    /* /RD must idle HIGH.  Its ODR reset value is 0, and while /RD is low the
     * panel drives D0..D7 itself - so leaving it low made the panel fight every
     * byte we wrote, which is why nothing ever reached the controller. */
    GPIOB->BSRR = PIN_RD;
    GPIOC->BSRR = PIN_CS | PIN_DC | PIN_WR;
    GPIOB->BSRR = PIN_RST;
}

static void lcd_replay(const uint8_t *seq, uint32_t len)
{
    uint32_t i = 0;

    while (i < len) {
        uint8_t cmd = seq[i++];
        uint8_t n = seq[i++];

        if (cmd == 0x00u) {
            HAL_Delay(n);
            continue;
        }
        lcd_cmd(cmd);
        while (n--) {
            lcd_data(seq[i++]);
        }
    }
}

void ili9341_init(void)
{
    lcd_gpios_init();

    /* The vendor's reset routine (0x08001d1c): PB9 high, 65 ms, PB9 low,
     * 65 ms, PB9 high, 10 x 65 ms.  Reset is active LOW and is left released. */
    HAL_GPIO_WritePin(GPIOB, PIN_RST, GPIO_PIN_SET);
    HAL_Delay(50U);
    HAL_GPIO_WritePin(GPIOB, PIN_RST, GPIO_PIN_RESET);
    HAL_Delay(50U);
    HAL_GPIO_WritePin(GPIOB, PIN_RST, GPIO_PIN_SET);
    HAL_Delay(500U);

    lcd_replay(k_init_seq, sizeof(k_init_seq));

    ili9341_set_window(0, 0, ILI9341_WIDTH, ILI9341_HEIGHT);
}

static uint32_t lcd_read_cmd(uint8_t cmd, uint8_t count)
{
    uint32_t v = 0u;
    uint8_t i;

    lcd_cmd(cmd);

    GPIOC->BRR  = PIN_CS;               /* stay selected for the whole read    */
    GPIOC->BSRR = PIN_DC;               /* D/C high = data                     */
    GPIOB->CRL  = 0x44444444u;          /* hand the data pins to the panel     */
    HAL_Delay(5U);

    /* Measured on this panel: the FIRST byte clocked out after a command is the
     * command byte itself (0x0B -> 0B 68, 0x04 -> 04 85 85 52).  Reading it as
     * data shifted every value by one byte, which is why the ID row showed
     * 0000 instead of the real ID.  Clock it out and discard it. */
    GPIOB->BRR = PIN_RD;
    HAL_Delay(1U);
    (void)(GPIOB->IDR & 0xFFu);
    GPIOB->BSRR = PIN_RD;
    HAL_Delay(1U);

    for (i = 0u; i < count; i++) {
        GPIOB->BRR = PIN_RD;            /* /RD low: the panel drives            */
        HAL_Delay(1U);
        v = (v << 8) | (uint32_t)(GPIOB->IDR & 0xFFu);
        GPIOB->BSRR = PIN_RD;
        HAL_Delay(1U);
    }

    GPIOB->CRL  = 0x33333333u;
    GPIOC->BSRR = PIN_CS;
    return v;
}

uint32_t ili9341_read_id(void)
{
    /* 0x04 (RDDID) is the command this panel answers; 0xD3 returns only zeros.
     * Three data bytes: 85 85 52 -> 0x858552 on the bench unit. */
    return lcd_read_cmd(0x04u, 3u);
}

/* Reads MADCTL back (0x0B).  Our init writes 0x68 (MX|MV|BGR) there, so a 0x68
 * in the returned byte proves the command bus, the data bus and the read
 * strobe all work. */
uint32_t ili9341_read_madctl(void)
{
    return lcd_read_cmd(0x0Bu, 1u);
}

void ili9341_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint16_t x1 = (uint16_t)(x + w - 1u);
    uint16_t y1 = (uint16_t)(y + h - 1u);

    /* MADCTL 0x68 (MX|MV|BGR): 0x2A carries the horizontal range (0..319),
     * 0x2B the vertical range (0..239).  The vendor's window routine
     * (0x08002788) sends exactly these four bytes per range. */
    lcd_cmd(0x2A);                       /* column address set */
    lcd_data((uint8_t)(x >> 8));
    lcd_data((uint8_t)(x & 0xFFu));
    lcd_data((uint8_t)(x1 >> 8));
    lcd_data((uint8_t)(x1 & 0xFFu));

    lcd_cmd(0x2B);                       /* page address set */
    lcd_data((uint8_t)(y >> 8));
    lcd_data((uint8_t)(y & 0xFFu));
    lcd_data((uint8_t)(y1 >> 8));
    lcd_data((uint8_t)(y1 & 0xFFu));

    lcd_cmd(0x2C);                       /* memory write */
}

void ili9341_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    uint32_t pixels = (uint32_t)w * h;
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFFu);

    if (w == 0u || h == 0u) {
        return;
    }
    ili9341_set_window(x, y, w, h);
    while (pixels--) {
        lcd_data(hi);
        lcd_data(lo);
    }
}

void ili9341_fill_screen(uint16_t color)
{
    ili9341_fill_rect(0, 0, ILI9341_WIDTH, ILI9341_HEIGHT, color);
}

void ili9341_draw_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale)
{
    struct glyph_iter it;
    uint16_t px;

    if (scale == 0u) {
        scale = 1u;
    }
    if (scale > GLYPH_MAX_SCALE) {
        scale = GLYPH_MAX_SCALE;
    }

    /* The cell is 6*scale wide and 8*scale high, and exactly that many
     * pixels are written (row-major, x fastest) - the window and the pixel
     * count must agree or the text drifts.  See fonts/glyph.h. */
    ili9341_set_window(x, y,
                        (uint16_t)(GLYPH_CELL_COLS * scale),
                        (uint16_t)(GLYPH_CELL_ROWS * scale));

    glyph_iter_init(&it, c, scale, fg, bg);
    while (glyph_next(&it, &px) != 0u) {
        lcd_data((uint8_t)(px >> 8));
        lcd_data((uint8_t)(px & 0xFFu));
    }
}

void ili9341_print(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale)
{
    while (*s) {
        ili9341_draw_char(x, y, *s, fg, bg, scale);
        x = (uint16_t)(x + 6u * scale);
        s++;
    }
}
