# Project Images

This folder contains curated images for the DSO183 custom firmware project.

## Bench Photos (`bench/`)

Real hardware bench validation photos of the JYE Tech DSO183 running the custom firmware.

| File | Description |
|------|-------------|
| `01-boot-baseline.jpg` | Initial boot screen on the tested instrument |
| `02-boot-screen.jpg` | Boot screen with build identity |
| `03-live-trace.jpg` | Live waveform trace on the display |
| `04-span-424.jpg` | Span setting at 424 counts |
| `05-final-trace.jpg` | Final validated trace |
| `06-timebase-50us.jpg` | 50 µs/div timebase in operation |
| `07-trigger-test.jpg` | Trigger event captured |
| `08-top-positions.jpg` | Top-of-screen trigger positions |
| `09-mode-initial.jpg` | Initial state (remote verify sequence) |
| `10-mode-run-1ms-auto.jpg` | RUN mode, 1 ms/div, AUTO trigger |
| `11-mode-run-0p5ms.jpg` | RUN mode, 0.5 ms/div |
| `12-mode-run-2ms.jpg` | RUN mode, 2 ms/div |
| `13-mode-norm-1ms.jpg` | NORM trigger mode, 1 ms/div |
| `14-mode-single-1ms.jpg` | SINGLE trigger mode, 1 ms/div |
| `15-mode-hold-1ms.jpg` | HOLD mode, 1 ms/div |
| `16-choppy-trace-0.jpg` | Choppy trace investigation, frame 0 |
| `17-choppy-trace-1.jpg` | Choppy trace investigation, frame 1 |
| `18-choppy-trace-2.jpg` | Choppy trace investigation, frame 2 |
| `19-choppy-trace-3.jpg` | Choppy trace investigation, frame 3 |

**Note:** All bench photos were taken on the single tested instrument (STM32F103 medium-density rev A, display controller ID 0x858552). Results on other DSO183 revisions may vary.

## UI Mockups (`ui/`)

UI design mockups from `ui_design/UI_SPECIFICATION.md`:

| File | Description |
|------|-------------|
| `01-main-run.png` | Main screen — RUN mode |
| `02-main-hold.png` | Main screen — HOLD mode |
| `03-quick-focus.png` | Quick-focus overlay |
| `04-menu-settings.png` | Settings menu |
| `05-diagnostics.png` | SYSTEM DIAG screen |

## Usage in documentation

Images are referenced from `README.md` and `docs/HARDWARE.md` using relative paths like `docs/images/bench/03-live-trace.jpg`.
