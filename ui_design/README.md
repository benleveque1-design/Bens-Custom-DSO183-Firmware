# DSO183 Oscilloscope: UI Design Package

> **Design Exercise Notice:**  
> This directory contains a complete, staged UI design proposal for the DSO183 oscilloscope.  
> **No existing project files have been modified. No firmware implementation or flashing has taken place.**  
> All assets, visual mockups, specifications, and interactive simulators are staged exclusively within `ui_design/`.

---

## Deliverables Summary

This design package delivers the requested retro electronic instrumentation user interface for the DSO183 oscilloscope:

1. **Hardware & Architecture Inspection Report:** Detailed in [UI_SPECIFICATION.md](./UI_SPECIFICATION.md#2-hardware-inspection--display-architecture-constraints) covering the STM32F103C8 microcontroller constraints (64K Flash / 20K RAM), ILI9341 8-bit parallel bus, zero-framebuffer direct-to-GRAM architecture, tactile pushbuttons (`OK`, `+`, `-`, `SEL`), and analog switch ladder (`SEN1`, `SEN2`, `CPL`).
2. **UI Specification & Design System:** Comprehensive specification in [UI_SPECIFICATION.md](./UI_SPECIFICATION.md) defining spatial coordinates, 10×8 division graticule geometry, center crosshair graduation ticks, RGB565 palette, 5×7 monospace typography, custom glyphs, and interaction state machine.
3. **Visual Mockups (5 Complete Screens):** Available in `ui_design/mockups/` in **native 320×240 1:1 pixel PNG**, **1280×960 4× high-resolution presentation PNG**, and **scalable vector SVG**.
4. **Interactive Hardware Simulator:** An interactive browser-based oscilloscope simulator in [ui_design/interactive/index.html](./interactive/index.html) allowing live exploration of all screens, CRT phosphor scanline effects, and tactile button controls.
5. **Implementation Architecture:** Step-by-step technical plan in [UI_SPECIFICATION.md](./UI_SPECIFICATION.md#8-implementation-strategy-for-existing-display-architecture) showing how the new UI can be integrated into the existing C codebase with zero additional RAM overhead and zero screen flicker.

---

## Screen Mockup Gallery

| # | Screen Description | Native 1:1 PNG | 4× Presentation PNG | Scalable Vector SVG |
|---|--------------------|:--------------:|:-------------------:|:-------------------:|
| **1** | **Main Scope: RUN State**<br>Waveform centerpiece, persistent Voltage, Frequency, Timebase, Trigger mode, Trigger type, Ground index `1▶`, Trigger flag `◀T` | [`screen_01_main_run.png`](./mockups/screen_01_main_run.png) | [`screen_01_main_run@4x.png`](./mockups/screen_01_main_run@4x.png) | [`mockup_screen_01_main_run.svg`](./mockups/mockup_screen_01_main_run.svg) |
| **2** | **Main Scope: HOLD / STOP State**<br>Amber HOLD badge, frozen 10.00 kHz square wave with ringing, automated frozen measurements | [`screen_02_main_hold.png`](./mockups/screen_02_main_hold.png) | [`screen_02_main_hold@4x.png`](./mockups/screen_02_main_hold@4x.png) | [`mockup_screen_02_main_hold.svg`](./mockups/mockup_screen_02_main_hold.svg) |
| **3** | **Quick-Focus Parameter Selection**<br>Non-modal active selection (`[TB: 1.00ms/d]`) allowing immediate +/- stepping while waveform remains 100% visible | [`screen_03_quick_focus.png`](./mockups/screen_03_quick_focus.png) | [`screen_03_quick_focus@4x.png`](./mockups/screen_03_quick_focus@4x.png) | [`mockup_screen_03_quick_focus.svg`](./mockups/mockup_screen_03_quick_focus.svg) |
| **4** | **Settings & Calibration Menu**<br>Floating retro console card for configuring Timebase, Trigger Mode, Trigger Level, Cal Zero, Offset, and Gain | [`screen_04_menu_settings.png`](./mockups/screen_04_menu_settings.png) | [`screen_04_menu_settings@4x.png`](./mockups/screen_04_menu_settings@4x.png) | [`mockup_screen_04_menu_settings.svg`](./mockups/mockup_screen_04_menu_settings.svg) |
| **5** | **Hardware Telemetry & Calibration Matrix**<br>Full-screen diagnostic readout showing STM32F103 telemetry, ILI9341 bus verification, switch ADC counts, and 9-point zero calibration table | [`screen_05_diagnostics.png`](./mockups/screen_05_diagnostics.png) | [`screen_05_diagnostics@4x.png`](./mockups/screen_05_diagnostics@4x.png) | [`mockup_screen_05_diagnostics.svg`](./mockups/mockup_screen_05_diagnostics.svg) |

---

## Visual Direction & Aesthetic Hallmarks

*   **Retro Instrumentation Aesthetic:** Inspired by classic laboratory oscilloscopes (HP 54600, Tektronix 2200/2400 series, Gould, Philips).
*   **High Contrast & Readability:** Luminous electric cyan (`#00E5FF`) waveform trace on dark navy glass background (`#060A10`), with darkish technical blue reticle (`#15263A`, `#2B4E73`), and high-contrast ice-white (`#E2EDF8`) for primary readouts.
*   **Intelligent Screen Allocation:**
    *   **Waveform Graticule:** 300×192 px ($72\%$ of display area, $Y = 20..212$).
    *   **Top Edge (Height 18 px):** Dedicated to Run State (`[RUN]` / `[HOLD]`), Trigger Mode (`AUTO`/`NORM`/`SING`), Slope (`[/]`/`[\]`), Trigger Level (`T:+1.25V`), Lock Badge (`TRIG'D`), and Host PC-Link.
    *   **Compact Bottom Edge (Height 24 px):** Row 1 houses persistent scales (`CH1: 500mV/div DC 1X`, `TB: 1.00ms/div`, `f: 1.000kHz`); Row 2 houses live measurements (`Vpp`, `Vrms`, `Vmax`, `Vmin`) or Quick-Focus parameter adjustment prompts.
    *   **Side Margins:** Left margin holds Ground reference flag `1▶` (`#00E5FF`), right margin holds Trigger crossing flag `◀T` (`#FFB800`).
*   **No Smartphone Clutter:** Zero rounded card bubbles, no gradients, no drop shadows, no wasteful margins, and no scanline distortions.

---

## How to Review the Interactive Simulator

To preview the design interactively with CRT scanlines and simulated front-panel buttons:

1. Open `ui_design/interactive/index.html` in any modern web browser:
   ```bash
   # Linux/macOS
   xdg-open ui_design/interactive/index.html || open ui_design/interactive/index.html
   ```
2. Or use Python's built-in web server:
   ```bash
   python3 -m http.server 8080 --directory ui_design/interactive
   ```
   Then navigate to `http://localhost:8080` in your browser.

---

## How to Re-generate Mockups

The mockup generation script uses the hardware font bitmaps directly from `fonts/glcdfont.c` to produce pixel-exact renders:

```bash
python3 ui_design/mockups/generate_mockups.py
```
This generates all 320×240 PNGs, 1280×960 `@4x.png` presentation images, and SVG vectors in `ui_design/mockups/`.
