#!/usr/bin/env python3
import re

with open('drivers/display/ili9341.c', 'r') as f:
    content = f.read()

# New lcd_cmd body (vendor-exact)
new_lcd_cmd = [
    "    /* Vendor's command writer (0x08001cb9):",
    "     *   BRR PC13 (CS = 0)",
    "     *   BRR PC14 (RS = 0)",
    "     *   ODR = byte (already on bus from lcd_data_byte)",
    "     *   BRR PC15 (WR = 0)",
    "     *   BSRR PC15 (WR = 1)  rising edge = write",
    "     *   BSRR PC14 (RS = 1)",
    "     *   BSRR PC13 (CS = 1)",
     "     */",
    "    LCD_CLR(GPIOC, PIN_ENABLE);      /* PC13 = 0 (CS low) */",
    "    LCD_CLR(GPIOC, PIN_DC);          /* PC14 = 0 (RS = command) */",
    "    lcd_data_byte(c);",
    "    LCD_CLR(GPIOC, PIN_WR);          /* PC15 = 0 (WR low) */",
    "    LCD_SET(GPIOC, PIN_WR);          /* PC15 = 1 (WR high, rising edge = write) */",
    "    LCD_SET(GPIOC, PIN_DC);          /* PC14 = 1 (RS = data)    */",
    "    LCD_SET(GPIOC, PIN_ENABLE);      /* PC13 = 1 (CS released) */",
]

new_lcd_data = [
    "    /* Vendor's data writer (0x08001ced):",
    "     *   BRR PC13 (CS = 0)",
    "     *   BSRR PC14 (RS = 1)",
    "     *   ODR = byte (already on bus from lcd_data_byte)",
    "     *   BRR PC15 (WR = 0)",
    "     *   BSRR PC15 (WR = 1)  rising edge = write",
    "     *   BSRR PC13 (CS = 1)",
     "     */",
    "    LCD_CLR(GPIOC, PIN_ENABLE);      /* PC13 = 0 (CS low) */",
    "    LCD_SET(GPIOC, PIN_DC);          /* PC14 = 1 (RS = data)    */",
    "    lcd_data_byte(d);",
    "    LCD_CLR(GPIOC, PIN_WR);          /* PC15 = 0 (WR low) */",
    "    LCD_SET(GPIOC, PIN_WR);          /* PC15 = 1 (WR high, rising edge = write) */",
    "    LCD_SET(GPIOC, PIN_ENABLE);      /* PC13 = 1 (CS released) */",
]

# Build the replacement strings
lcd_cmd_repl = "static inline void lcd_cmd(uint8_t c) {\n" + "\n".join(new_lcd_cmd) + "\n}"
lcd_data_repl = "static inline void lcd_data(uint8_t d) {\n" + "\n".join(new_lcd_data) + "\n}"

# Replace lcd_cmd body: find the function and replace its content
# Pattern: static inline void lcd_cmd(uint8_t c) { body }
pattern_cmd = r"static inline void lcd_cmd\(uint8_t c\) \{[^}]*\}"
content = re.sub(pattern_cmd, lcd_cmd_repl, content)

# Replace lcd_data body
pattern_data = r"static inline void lcd_data\(uint8_t d\) \{[^}]*\}"
content = re.sub(pattern_data, lcd_data_repl, content)

with open('drivers/display/ili9341.c', 'w') as f:
    f.write(content)

print('Replacement done')