# Hardware identification & compatibility

**DSO183 custom firmware — hardware notes**

## Target instrument

**JYE Tech DSO183 (18301K)** digital storage oscilloscope.

## Microcontroller

**STM32F103C8T6** (medium-density Cortex-M3, LQFP48)
- 64 KiB flash, 20 KiB SRAM
- No hardware FPU (all firmware maths is integer/fixed-point)
- ADC1: 12-bit successive-approximation, up to 1 MHz (14 MHz ADC clock max)
- DMA1 channel 1 services ADC1
- TIM3: general-purpose timer, used to pace ADC conversions via TRGO
- USART1: console / PC-link on PA9 (TX) / PA10 (RX)
- IWDG: independent watchdog, armed and fed every superloop iteration

## Display

**ILI9341-compatible** 320×240 landscape, 8-bit 8080 parallel bus, RGB565.

**Important caveat — controller identity:** On the tested unit the display
controller read back ID **0x858552**, not 0x9341. It is a compatible
controller that accepts the ILI9341 init sequence (with the vendor's MADCTL
0x68). The panel reads `ID 858552` on the SYSTEM DIAG screen. Do not assume
every DSO183 carries a genuine ILI9341; the read path on other units should be
re-verified before trusting the ID.

Display pin map (verified against the vendor firmware):
| Signal | Pin | Notes |
|--------|-----|-------|
| Data D0..D7 | PB0..PB7 | 8080 data byte |
| /CS | PC13 | active low |
| D/C | PC14 | low = command, high = data |
| /WR | PC15 | latched on low→high edge |
| /RESET | PB9 | active low |

There is **no framebuffer** (a 320×240 RGB565 frame would be 150 KiB, far
more than the 20 KiB SRAM). All rendering writes directly to the display GRAM
through windowed sub-region updates.

## Front panel

**Pushbuttons** (four, active low, on GPIOB):
| Button | Pin | Function |
|--------|-----|----------|
| OK | PB12 | Run/Hold toggle, confirm / activate |
| + | PB13 | Step up / adjust |
| − | PB14 | Step down / adjust |
| SEL | PB15 | Focus cycle, long-press to open menu |

**Slide switches** (three, read via ADC1 resistor ladder, not GPIOs):
| Switch | ADC channel | Pin | Function |
|--------|-------------|-----|----------|
| SEN1 (gain) | ch6 | PA6 | ×1 / ×2 / ×5 |
| SEN2 (range) | ch5 | PA5 | 10 mV / 0.1 V / 1 V |
| CPL (coupling) | ch7 | PA7 | AC / DC / GND |

The firmware decodes each raw reading to {0,1,2} using the vendor's thresholds
(0x600 / 0xA00) and derives the sensitivity combination
`combo = 4 + 3·SEN1 + SEN2` (nine combinations, 4..12) and
`coupling = 2 − code(CPL)`.

**Coupling is analogue-only on this board:** the AC/DC/GND switch re-routes
the front end in hardware; no ADC channel or GPIO observes its position. The
firmware can only label it "set by switch." The stock "Set CPL (SW1) to …"
prompt cannot be honoured automatically.

**Signal input channel:** ADC1 channel 2 (PA2 / ADC12_IN2) per the vendor's
ADC1 init (`SQR3 = 2`, watchdog channel 2). This has been confirmed in the
vendor binary and the stock ADC configuration read live on the bench, but
that PA2 is the physical BNC input on *your* unit is an open bench item —
confirm before trusting measured numbers.

## What has been tested

Exactly **one instrument** was used for bench validation:

- STM32F103 medium density, revision A (`DBGMCU_IDCODE = 0x20036410`,
  `CPUID = 0x412FC231` Cortex-M3 r2p1)
- Display controller ID read back as **0x858552** (compatible, not genuine ILI9341)
- Console UART on an FT232R at 115200 8N1
- ST-LINK/V2 for read-only SWD access (serial `12010C1C8418303030303032` on the
  unit used during development)

**Compatibility with other DSO183 hardware revisions, clones, display panels,
or ST-Link adapters has not been tested and is not claimed.** If you are
porting to a different unit, start by re-verifying the display controller ID,
the ladder channel mapping, the signal input channel, and the pushbutton pin.

## What is NOT supported / not tested

- Other DSO183 revisions (claiming compatibility would be dishonest)
- Different display controllers or resolutions
- External trigger input, external clock, or additional channels (the DSO183
  is a single-channel instrument)
- USB (the DSO183 USB socket is power-only; the PC link uses the console UART)
- A buzzer/beeper (none exists on this board)
