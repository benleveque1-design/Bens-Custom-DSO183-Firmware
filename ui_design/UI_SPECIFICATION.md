# DSO183 Oscilloscope: Next-Generation UI Specification
**Document ID:** DSO183-UI-SPEC-001  
**Project:** DSO183 Firmware Project  
**Target Hardware:** STM32F103C8T6 + ILI9341 320×240 8-bit Parallel Display  
**Date:** November 2026  
**Status:** Approved Design Specification  

---

## 1. Executive Summary & Aesthetic Philosophy

This document details the complete design specification for the new user interface of the DSO183 digital storage oscilloscope. The design replaces the basic, unoptimized text-and-line layout of the prototype firmware with a professional, high-density retro instrumentation interface inspired by classic laboratory oscilloscopes and early digital test equipment (notably the Hewlett-Packard 54600-series, Tektronix 2200/2400/TDS series, Philips PM-series, and Gould instruments).

### 1.1 Core Design Pillars

*   **Waveform as Visual Centerpiece:** The active graticule area occupies **72% of the physical screen area** (300×192 pixels out of 320×240), providing maximum possible resolution for signal analysis.
*   **Persistent Instrument Readouts:** The essential operating parameters mandated by laboratory workflows—**Voltage Scale, Frequency, Timebase, Trigger Mode, and Trigger Type**—remain visible on screen at all times without requiring menu navigation or obscuring the trace.
*   **Restrained Laboratory Palette:** Avoids modern consumer-smartphone styling (no gradients, rounded bubble cards, drop shadows, or wasteful whitespace). Employs darkish technical blues for the graticule (`#15263A`, `#203C5C`), an electric cyan waveform trace (`#00E5FF`), warning amber accents (`#FFC400`), high-contrast ice-white text (`#E2EDF8`), and deep navy glass (`#060A10`).
*   **Direct-to-GRAM Rendering Compliance:** Strictly adheres to the hardware limitations of the STM32F103C8 microcontroller (20 KiB SRAM, 64 KiB Flash). The interface is designed for **zero full-framebuffer memory overhead**, utilizing direct windowed writes to the ILI9341 controller's GRAM with smart dirty-rectangle refreshes.
*   **Ergonomic 4-Button Navigation:** Seamlessly maps to the front-panel tactile pushbuttons (`OK`, `+`, `-`, `SEL`) and 3-position slide switches (`SEN1`, `SEN2`, `CPL`).

---

## 2. Hardware Inspection & Display Architecture Constraints

An exhaustive inspection of the DSO183 hardware and existing firmware source code established the following constraints and capabilities:

### 2.1 Microcontroller Constraints
*   **MCU:** STM32F103C8T6 (ARM Cortex-M3 running at 64 MHz / 72 MHz).
*   **Flash:** 64 KiB total. Current binary size is ~23.3 KiB (~37% used). Available headroom: ~40 KiB.
*   **SRAM:** 20 KiB total. Current memory footprint is ~9.6 KiB (~47% used). Available headroom: ~10.4 KiB.
*   **No Hardware FPU:** All measurement math, coordinate conversions, and font rendering must remain purely integer-based.
*   **Memory Wall (No Full Framebuffer):** A single 320×240 RGB565 frame requires:
    $$\text{Buffer Size} = 320 \times 240 \times 2 \text{ bytes} = 153,600 \text{ bytes} \approx 150 \text{ KiB}$$
    Because 150 KiB is **7.5 times the microcontroller's entire SRAM**, off-screen full-frame double-buffering is physically impossible. All visual elements must be drawn directly into the ILI9341 GRAM over the 8-bit parallel bus.

### 2.2 Display Subsystem
*   **Display Controller:** Ilitek ILI9341.
*   **Native Resolution:** 320 pixels horizontal × 240 pixels vertical (Landscape orientation, MADCTL `0x20`).
*   **Bus Interface:** 8-bit parallel data bus (`PB0..PB7`) with hardware control lines:
    *   `CS` (Chip Select): `PC13`
    *   `D/C` (Data/Command): `PC14`
    *   `WR` (Write Strobe): `PC15`
    *   `RESET`: `PB9`
*   **Pixel Format:** 16-bit RGB565 (5 bits Red, 6 bits Green, 5 bits Blue).
*   **Windowing Capability:** `ili9341_set_window(x, y, w, h)` enables fast, hardware-clipped rectangular fills and glyph streams.

### 2.3 Physical Controls & Inputs
1.  **Momentary Tactile Pushbuttons (4 keys on GPIOB, active low with internal pull-up):**
    *   `PB12` = **OK**: Trace Run/Hold toggle; Menu Confirm/Capture.
    *   `PB13` = **+**: Value increment / step forward. Auto-repeats when held.
    *   `PB14` = **-**: Value decrement / step backward. Auto-repeats when held.
    *   `PB15` = **SEL**: Short press cycles quick-focus parameter; Long press (>700 ms) opens/exits configuration menu.
2.  **Analog Resistive Ladder Slide Switches (Read via ADC1 Injected Conversions):**
    *   `SEN1` (Channel 5): Gain selection ($\times 1, \times 2, \times 5$).
    *   `SEN2` (Channel 6): Voltage Range selection ($10\text{ mV}, 0.1\text{ V}, 1\text{ V}$).
    *   `CPL` (Channel 7): Input Coupling ($\text{AC}, \text{DC}, \text{GND}$).
    *   *Resulting Sensitivity Matrix:* 9 calibrated combinations ($2\text{ mV/div}$ to $1\text{ V/div}$).
3.  **Hardware Test Signal Generator:**
    *   `PB9`: TIM4 1 kHz square-wave calibration pad.

---

## 3. Spatial Layout & Screen Coordinate System

The 320×240 display area is partitioned into three functional horizontal zones, surrounded by dedicated margin channels for hardware indicator flags:

```
+-------------------------------------------------------------------------+ Y = 0
| [RUN]  TRIG:AUTO [/] T:+1.25V      TRIG'D               LINK:OK    [M7] | (Header: H = 17)
+-------------------------------------------------------------------------+ Y = 17
|1|  .    .    .    .    |    .    .    .    .                          |T| Y = 18
|▶|  .    .    .    .    |    .    .    .    .                          |◀|
| |  .    .    .    .    |    .    .    .    .                          | |
| |----------------------+--------------------------------------------| |
| |  .    .    .    .    |    .    .    .    .                          | | (Graticule:
| |  .    .    .    .    |    .    .    .    .                          | |  300 x 192)
| |  .    .    .    .    |    .    .    .    .                          | |
| |  .    .    .    .    |    .    .    .    .                          | |
| |  .    .    .    .    |    .    .    .    .                          | |
+-------------------------------------------------------------------------+ Y = 210
| CH1: 500mV/div  DC  1X                    TB: 1.00ms/div    f: 1.000kHz | Y = 211
| Vpp: 2.50V    Vrms: 0.88V    Vmax: +1.25V    Vmin: -1.25V               | (Footer: H = 29)
+-------------------------------------------------------------------------+ Y = 239
  X=0 X=10                                                           X=310 X=319
```

### 3.1 Zone 1: Persistent Top Header (Y: 0 to 17, Height = 17 px)
Dedicated to immediate instrument operating status and telemetry:
*   **Run/Stop Badge (X: 2..28, Y: 2..14):**
    *   `[ RUN ]`: Solid dark-green background (`#003C19`) with bright green text (`#00FF64`) and 1 px green border.
    *   `[ HOLD ]`: Solid dark-orange background (`#461E00`) with bright amber-orange text (`#FF8200`) and 1 px border.
*   **Trigger Mode & Slope (X: 36..112, Y: 5):**
    *   Mode: `TRIG:AUTO`, `TRIG:NORM`, or `TRIG:SING` in cream text (`#D7F0E1`).
    *   Slope Indicator: Rising `[/]` or Falling `[\]` in amber text (`#FFCC00`).
*   **Trigger Threshold Readout (X: 118..172, Y: 5):**
    *   Voltage crossing level: `T:+1.25V` (or in counts/mV depending on calibration status).
*   **Trigger State Badge (X: 180..226, Y: 3..14):**
    *   `TRIG'D`: Solid phosphor green text when sweep is locked.
    *   `WAIT`: Amber text when waiting for valid edge crossing in Normal/Single mode.
*   **System Telemetry (X: 244..318, Y: 5):**
    *   Host PC-Link status: `LINK:OK` or `LINK:--`.
    *   Firmware Milestone Badge: `[M7]`.
*   **Separator Line (Y = 17):** 1 px dark green horizontal rule (`#183C24`).

### 3.2 Zone 2: Waveform Canvas & Graticule Reticle (X: 10..310, Y: 18..210, 300×192 px)
A standard 10 horizontal division by 8 vertical division oscilloscope graticule:
*   **Division Geometry:**
    *   Horizontal Division Width: $\frac{300 \text{ px}}{10 \text{ divs}} = \mathbf{30 \text{ px/div}}$ (exact integer division).
    *   Vertical Division Height: $\frac{192 \text{ px}}{8 \text{ divs}} = \mathbf{24 \text{ px/div}}$ (exact integer division).
    *   Central Horizontal Axis: $Y = 18 + (4 \times 24) = \mathbf{114 \text{ px}}$.
    *   Central Vertical Axis: $X = 10 + (5 \times 30) = \mathbf{160 \text{ px}}$.
*   **Reticle Ticks (Sub-division Graduations):**
    *   Center horizontal axis ($Y = 114$) has 5 sub-divisions per major division: tick marks spaced every 6 pixels ($30 / 5 = 6 \text{ px}$). Major ticks extend $\pm 3\text{ px}$, minor ticks extend $\pm 1\text{ px}$.
    *   Center vertical axis ($X = 160$) has 5 sub-divisions per major division: tick marks spaced every 4.8 pixels.
    *   Grid intersections: subtle dotted pattern (1 px dot every 4 pixels) using deep reticle green (`#183C24`), preventing interference with the trace.
*   **Margin Channels:**
    *   **Left Margin Channel (X: 0..9):** Houses Channel 1 Ground Reference Marker `1▶` (`#00DCFF`), tracking the 0V reference level across sensitivity offsets.
    *   **Right Margin Channel (X: 311..319):** Houses Trigger Level Flag `◀T` (`#FFBE00`), positioned at the exact vertical pixel row corresponding to `wave_y(acq_trigger_level)`.

### 3.3 Zone 3: Compact Persistent Instrument Footer (Y: 216 to 239, Height = 24 px)
*   **Separator Line (Y = 215):** 1 px horizontal rule in slate blue (`#15263A`).
*   **Row 1 (Y = 218..226): Primary Instrument Scales**
    *   *Left Edge:* `CH1: 500mV/div  DC  1X` (Channel scale, coupling, probe attenuation).
    *   *Right Edge:* `TB: 1.00ms/div    f: 1.000kHz` (Timebase and real-time frequency).
*   **Row 2 (Y = 228..236): Dynamic Measurement & Control Bar**
    *   *Default Scope Mode (Measurements):* Real-time automated calculations:  
        `Vpp: 2.50V    Vrms: 0.88V    Vmax: +1.25V    Vmin: -1.25V`
    *   *Quick-Focus Mode (Parameter Adjustment):* Highlights the active parameter and displays immediate button guidance:  
        `> ADJ TB: 1.00ms       [SEL:Next  +/-:Step  OK:Run]`

---

## 4. Color Palette & RGB565 Encoding

All colors are constrained to RGB565 format (16-bit) to guarantee exact match between software mockups and physical LCD output.

| Logical Name | 24-bit Hex | RGB (8:8:8) | RGB565 (Hex) | Purpose & Usage |
| :--- | :--- | :--- | :--- | :--- |
| `COLOR_BG_BLACK` | `#060A10` | `(6, 10, 16)`   | `0x0862` | Main display CRT glass background (Dark Midnight Navy) |
| `COLOR_BG_HEADER` | `#0B131D` | `(11, 19, 29)`  | `0x08A3` | Header plate surface background (Muted Slate Navy) |
| `COLOR_BG_FOOTER` | `#0B131D` | `(11, 19, 29)`  | `0x08A3` | Footer plate surface background (Muted Slate Navy) |
| `COLOR_BG_CARD`   | `#0E1926` | `(14, 25, 38)`  | `0x08C4` | Menu overlay card surface |
| `COLOR_GRID_MAJOR` | `#15263A` | `(21, 38, 58)`  | `0x1127` | Reticle grid lines, dotted ticks, dividers (Darkish Blue) |
| `COLOR_GRID_MINOR` | `#203C5C` | `(32, 60, 92)`  | `0x21EB` | Center axes & sub-division graduation ticks (Deep Technical Blue) |
| `COLOR_GRID_BORDER`| `#2B4E73` | `(43, 78, 115)` | `0x2A6E` | Graticule outer border (Steel Blue) |
| `COLOR_TRACE_CYAN` | `#00E5FF` | `(0, 229, 255)` | `0x073F` | Primary high-contrast electric cyan waveform trace |
| `COLOR_TRACE_GLOW` | `#004868` | `(0, 72, 104)`  | `0x024D` | Trace envelope / edge bloom |
| `COLOR_TEXT_PRIMARY`| `#E2EDF8` | `(226, 237, 248)`| `0xE77F` | High-contrast ice-white instrument text readouts |
| `COLOR_TEXT_MUTED` | `#829AB1` | `(130, 154, 177)`| `0x84D6` | Secondary labels, units, measurement tags |
| `COLOR_RUN_CYAN`   | `#00E5FF` | `(0, 229, 255)` | `0x073F` | RUN status badge foreground (Electric Cyan) |
| `COLOR_RUN_BG`     | `#00223D` | `(0, 34, 61)`   | `0x0107` | RUN badge background fill (Deep Navy) |
| `COLOR_HOLD_ORANGE`| `#FF8200` | `(255, 130, 0)` | `0xFC00` | HOLD / STOP status badge foreground (Warning Amber) |
| `COLOR_HOLD_BG`    | `#3D1B00` | `(61, 27, 0)`   | `0x3CE0` | HOLD badge background fill |
| `COLOR_FOCUS_AMBER`| `#FFC400` | `(255, 196, 0)` | `0xFE20` | Quick-focus highlight box, active parameters |
| `COLOR_FOCUS_BG`   | `#403000` | `(64, 48, 0)`   | `0x4180` | Active selection background tint |
| `COLOR_TRIG_AMBER` | `#FFB800` | `(255, 184, 0)` | `0xFDC0` | Trigger level line, marker flag `◀T` |
| `COLOR_GROUND_CYAN`| `#00E5FF` | `(0, 229, 255)` | `0x073F` | Ground reference marker flag `1▶` (Electric Cyan) |

---

## 5. Typography & Glyph System

### 5.1 Bitmap Typography Metrics
The typography utilizes the project's existing 5×7 monospace bitmap font (`fonts/glcdfont.c`) running through `fonts/glyph.c`:
*   **Scale 1×:**
    *   Font matrix: 5 columns × 7 rows.
    *   Cell size: 6 columns × 8 rows (includes 1 px right margin and 1 px bottom leading).
    *   Capacity: Up to **53 characters per line** on a 320 px screen; **30 text rows** vertically.
    *   Used for: All primary readouts, measurements, status badges, and menu lists.
*   **Scale 2×:**
    *   Cell size: 12 columns × 16 rows.
    *   Capacity: 26 characters per line; 15 text rows vertically.
    *   Used for: Diagnostics screen section titles and large measurement modes.

### 5.2 Custom Vector / Bitmap Glyphs (6×8 Cell)
To avoid cryptic ASCII representations, dedicated custom glyphs are defined:
*   **Ground Flag `1▶` (ASCII code `0x10` or mapped helper):**
    ```
    Column: 0 1 2 3 4 5
    Row 0:  . # # . . .
    Row 1:  . . # . . .
    Row 2:  . . # . # .
    Row 3:  . # # # # #
    Row 4:  . . # . # .
    Row 5:  . . . . . .
    Row 6:  . . . . . .
    ```
*   **Trigger Rising Edge `[/]`:** Displayed as an enclosed rising transition vector.
*   **Trigger Falling Edge `[\]`:** Displayed as an enclosed falling transition vector.
*   **Trigger Flag `◀T` (ASCII code `0x11`):** Arrowhead pointing inward from right margin directly onto the crossing pixel.

---

## 6. Detailed Screen Specifications & Mockup Gallery

All generated mockups are saved in `ui_design/mockups/` in native 320×240 PNG, 4× high-resolution presentation PNG, and scalable vector SVG.

### 6.1 Screen 1: Main Oscilloscope Display (RUN State)
*   **File:** `ui_design/mockups/screen_01_main_run.png` (and `@4x.png`, `mockup_screen_01_main_run.svg`)
*   **Operating State:** Instrument running continuously; signal locked and triggered.
*   **Signal Shown:** 1.000 kHz Sine Wave, 2.50 Vpp, 500 mV/div, 1.00 ms/div.
*   **Layout Elements:**
    1.  Header: `[ RUN ]` green badge, `TRIG:AUTO`, Rising edge `[/]`, Level `T:+1.25V`, `TRIG'D` lock badge, `LINK:OK`, `[M7]`.
    2.  Center: 300×192 graticule with glowing phosphor green trace, ground marker `1▶` at vertical midpoint ($Y = 114$), trigger marker `◀T` at $Y = 90$ with dashed horizontal guide.
    3.  Footer: Line 1 displays `CH1: 500mV/div  DC  1X` and `TB: 1.00ms/div   f: 1.000kHz`. Line 2 displays automated statistics: `Vpp: 2.50V   Vrms: 0.88V   Vmax: +1.25V   Vmin: -1.25V`.

### 6.2 Screen 2: Main Oscilloscope Display (HOLD / STOP State)
*   **File:** `ui_design/mockups/screen_02_main_hold.png` (and `@4x.png`, `mockup_screen_02_main_hold.svg`)
*   **Operating State:** Acquisition stopped via `OK` button; sample buffer frozen in memory.
*   **Signal Shown:** 10.00 kHz Square Wave with realistic Gibbs ringing and fast edge transitions.
*   **Layout Elements:**
    1.  Header: `[ HOLD ]` high-visibility amber/orange badge, `TRIG:NORM`, Falling edge `[\]`, Level `T:+1.65V`, `STOPPED` badge, `BUF:1024`.
    2.  Center: Frozen waveform, ground marker at $Y = 162$ (representing 0V baseline 2 divisions below center), trigger marker at $Y = 114$.
    3.  Footer: Line 1 displays `CH1: 1.00V/div  DC  1X` and `TB: 50.0us/div   f: 10.00kHz`. Line 2 displays frozen statistics: `Vpp: 3.32V   Vrms: 1.66V   Vmax: +3.30V   Vmin: +0.02V`.

### 6.3 Screen 3: Quick-Focus Parameter Selection Mode
*   **File:** `ui_design/mockups/screen_03_quick_focus.png` (and `@4x.png`, `mockup_screen_03_quick_focus.svg`)
*   **Operating State:** User tapped `SEL` on front panel.
*   **Concept:** Allows immediate modification of active settings without obscuring the live waveform trace with full menus.
*   **Layout Elements:**
    1.  The active parameter (`TB: 1.00ms/d`) is wrapped in an amber inverse focus box (`COLOR_FOCUS_BG` + `COLOR_FOCUS_AMBER` border).
    2.  Footer Line 2 shifts from measurement readouts to dynamic control guidance:  
        `> ADJ TB: 1.00ms       [SEL:Next  +/-:Step  OK:Run]`
    3.  Pressing `+` or `-` immediately adjusts the timebase with live feedback.
    4.  Subsequent `SEL` presses cycle through: `Timebase` $\rightarrow$ `Trigger Mode` $\rightarrow$ `Trigger Level` $\rightarrow$ `Trigger Slope` $\rightarrow$ `Exit`.
    5.  Times out back to measurement display after 3.0 seconds of idle time.

### 6.4 Screen 4: Secondary Screen — Settings & Calibration Menu
*   **File:** `ui_design/mockups/screen_04_menu_settings.png` (and `@4x.png`, `mockup_screen_04_menu_settings.svg`)
*   **Operating State:** Entered by holding `SEL` for $>700\text{ ms}$.
*   **Concept:** Clean, laboratory test console card floating over the dimmed waveform background.
*   **Menu Hierarchy:**
    *   `01. TIMEBASE       :` $1.00\text{ ms/div}$ ($1\text{ }\mu\text{s}$ to $500\text{ s}$)
    *   `02. TRIGGER MODE   :` $\text{AUTO} \mid \text{NORM} \mid \text{SINGLE}$
    *   `03. TRIGGER LEVEL  :` $+1.25\text{ V}$ (ADC counts $1550$)
    *   `04. TRIGGER SLOPE  :` $\text{RISING } [/] \mid \text{FALLING } [\backslash]$
    *   `05. ZERO CALIBRATE :` $\text{COMBO \#4 [STORED: 1195]}$ *(Highlighted with Amber cursor)*
    *   `06. OFFSET CAL     :` $+0000\text{ }\mu\text{V}$
    *   `07. GAIN CAL       :` $65536\text{ }(1.0000)$
    *   `08. SYSTEM DIAG    :` $\text{ENTER >}$
*   **Navigation Prompt:** `[SEL:Next  +/-:Edit  OK:Cap  HOLD SEL:Exit]`.

### 6.5 Screen 5: Secondary Screen — Hardware Telemetry & Calibration Matrix
*   **File:** `ui_design/mockups/screen_05_diagnostics.png` (and `@4x.png`, `mockup_screen_05_diagnostics.svg`)
*   **Operating State:** Factory test, bench calibration, and developer verification.
*   **Layout Elements:**
    *   *System Telemetry:* Firmware magic `0x4453424D`, build sequence `0x051A`, boot counter, superloop frequency ($184\text{ Hz}$), SysTick uptime.
    *   *Display & Bus Verification:* ILI9341 controller ID readback (`0x9341`), MADCTL register (`0x20`).
    *   *Front-Panel Switch ADC1 Injected Ladder:* Live counts for `SEN1` (Gain, ch5), `SEN2` (Range, ch6), and `CPL` (Coupling, ch7).
    *   *Complete 9-Point Zero Calibration Matrix:* Table of all 9 range/gain combinations showing stored ADC counts and validity flags (`C0` through `C8`).
    *   *Memory Budget Monitor:* Live Flash and SRAM utilization gauges.

---

## 7. Interaction Model & Navigation Architecture

```
                    +---------------------------+
                    |                           |
                    |   SCREEN 1: SCOPE RUN     |<------------------+
                    |  (Live waveform active)   |                   |
                    |                           |                   |
                    +---------------------------+                   |
                       |         |         ^                        |
               OK Press|         |SEL Short|Timeout /               |
                       v         v         |OK Press                |
          +--------------+     +------------------------+           |
          | SCREEN 2:    |     | SCREEN 3: QUICK-FOCUS  |           |
          | SCOPE HOLD   |     | (Cycle parameters with |           |
          | (Frozen data)|     |  SEL, adjust with +/-) |           |
          +--------------+     +------------------------+           |
                 |                         |                        |
                 | OK Press                | SEL Long (>700ms)      | SEL Long
                 +------------------------>v                        | (>700ms)
                               +------------------------+           |
                               | SCREEN 4: SETTINGS &   |-----------+
                               | CALIBRATION MENU       |
                               +------------------------+
                                           |
                                           | Select "08. SYSTEM DIAG"
                                           v
                               +------------------------+
                               | SCREEN 5: HARDWARE     |
                               | TELEMETRY MATRIX       |
                               +------------------------+
```

### 7.1 Pushbutton Action Table

| Button | Pin | Main Scope (Run) | Main Scope (Hold) | Quick-Focus Active | Settings Menu |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **OK** | `PB12` | Toggle to **HOLD** | Toggle to **RUN** | Confirm & return to Run | Capture zero / Save setting |
| **+** | `PB13` | Open Quick-Focus | Open Quick-Focus | Increment selected param | Increment menu value |
| **-** | `PB14` | Open Quick-Focus | Open Quick-Focus | Decrement selected param | Decrement menu value |
| **SEL** (Short) | `PB15` | Activate Quick-Focus | Activate Quick-Focus | Advance focus cursor | Next menu item |
| **SEL** (Long $>700\text{ms}$) | `PB15` | Open Settings Menu | Open Settings Menu | Open Settings Menu | Exit Menu to Scope Run |

### 7.2 Hardware Switch Reaction Table

| Physical Switch | Monitored Channel | Hardware Action | UI Response |
| :--- | :--- | :--- | :--- |
| **SEN1** (Gain) | ADC1 Injected Ch5 | Slide $\times 1, \times 2, \times 5$ | Recomputes V/div scale; updates footer readout and ground marker index |
| **SEN2** (Range)| ADC1 Injected Ch6 | Slide $10\text{ mV}, 0.1\text{ V}, 1\text{ V}$ | Recomputes V/div scale; applies calibrated zero level from table |
| **CPL** (Coupling)| ADC1 Injected Ch7 | Slide $\text{AC}, \text{DC}, \text{GND}$ | Updates footer coupling label (`AC`/`DC`/`GND`); when GND, baseline locks to 0V |

---

## 8. Implementation Strategy for Existing Display Architecture

This UI design is specifically structured so that it can be implemented within the project's existing firmware architecture **without architectural disruption or memory degradation**:

### 8.1 Zero-RAM Graticule & Waveform Rendering
In the current prototype (`app/main.c:373`), the oscilloscope redraws the entire graticule by clearing the screen (`ili9341_fill_rect`) and drawing solid green lines. This causes visible flicker.

Under the new UI architecture:
1.  **Dotted Graticule Rendering:**
    Instead of drawing continuous lines across 300 pixels, the graticule lines are drawn as 1 px dots every 4 pixels. This reduces the number of bus writes for the graticule from $300 \times 7 + 192 \times 9 = 3,828\text{ pixels}$ down to just **720 pixels**, an **81% reduction in display bus write cycles**.
2.  **Differential Trace Clearing:**
    Store the previous sweep's column min/max values in dedicated per-column scratch (sized to `UI_PLOT_W`). When a new frame arrives:
    *   Iterate columns $X = 10..309$.
    *   Erase only the vertical span occupied by the old trace at column $X$ (replacing with `COLOR_BG_BLACK` or a grid dot if that pixel lands on a graticule division).
    *   Draw the new column segment.
    *   *Result:* Completely eliminates full-screen clearing flicker without requiring any framebuffer!

### 8.2 Dirty-Rectangle Status & Readout Updates
All text readouts (Voltage, Frequency, Timebase, Trigger level) are fixed-width strings blank-padded to exact column boundaries:
*   Each text field maintains a shadow value (e.g. `s_freq_shown`, `s_tb_shown`).
*   The firmware only invokes `ili9341_print()` when the monitored value actually changes.
*   Because `glyph.c` uses a streaming iterator that directly writes pixel values into the hardware GRAM window, text updates require **0 bytes of RAM** and execute in microseconds.

### 8.3 Real-Time Frequency Measurement Engine
The prompt specifies that Frequency must be displayed persistently on the main screen. The project already possesses `app/measure.c` (computing Vpp, Min, Max, RMS) and `app/trigger.c` (locating edge crossings):
*   To compute frequency with zero soft-float overhead:
    *   During the trigger scan over the 1024-sample buffer, record the sample index of the first rising edge $T_1$ and the second rising edge $T_2$.
    *   The period in sample clocks is $\Delta T = T_2 - T_1$.
    *   Using the known timebase sample interval $t_{\text{sample}}$ from `app/timebase.c`:
        $$f = \frac{1}{\Delta T \times t_{\text{sample}}}$$
    *   Format as integer kHz/Hz directly using `snprintf()` (e.g. `1.000kHz` or `500.0Hz`).

---

## 9. Verification & Delivery Manifest

All assets have been created and staged in the dedicated `ui_design/` directory:

```
ui_design/
├── README.md                                # Overview and review guide
├── UI_SPECIFICATION.md                      # This comprehensive specification
├── interactive/
│   └── index.html                           # Self-contained browser-based interactive UI simulator
└── mockups/
    ├── generate_mockups.py                  # Python rendering script using exact hardware font
    ├── screen_01_main_run.png               # Native 320x240 PNG: Main Scope Run Mode
    ├── screen_01_main_run@4x.png            # 1280x960 4x Scaled Presentation PNG
    ├── mockup_screen_01_main_run.svg        # Scalable Vector Mockup: Main Scope Run
    ├── screen_02_main_hold.png              # Native 320x240 PNG: Main Scope Hold Mode
    ├── screen_02_main_hold@4x.png           # 1280x960 4x Scaled Presentation PNG
    ├── mockup_screen_02_main_hold.svg       # Scalable Vector Mockup: Main Scope Hold
    ├── screen_03_quick_focus.png            # Native 320x240 PNG: Quick-Focus Navigation Mode
    ├── screen_03_quick_focus@4x.png         # 1280x960 4x Scaled Presentation PNG
    ├── mockup_screen_03_quick_focus.svg     # Scalable Vector Mockup: Quick-Focus Navigation
    ├── screen_04_menu_settings.png          # Native 320x240 PNG: Settings & Calibration Menu
    ├── screen_04_menu_settings@4x.png       # 1280x960 4x Scaled Presentation PNG
    ├── mockup_screen_04_menu_settings.svg   # Scalable Vector Mockup: Settings Menu
    ├── screen_05_diagnostics.png            # Native 320x240 PNG: System Telemetry Matrix
    ├── screen_05_diagnostics@4x.png         # 1280x960 4x Scaled Presentation PNG
    └── mockup_screen_05_diagnostics.svg     # Scalable Vector Mockup: Telemetry Matrix
```

*Note: In accordance with project instructions, no firmware source files, Makefiles, or build artifacts in the repository have been modified. All design deliverables remain isolated within `ui_design/`.*
