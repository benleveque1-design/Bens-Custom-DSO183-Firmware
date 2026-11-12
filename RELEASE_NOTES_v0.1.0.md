# Release notes — DSO183 custom firmware v0.1.0

**First public release. Pre-release baseline — not a product.**

This is the first public release of the DSO183 custom firmware, a replacement
firmware for the JYE Tech DSO183 (18301K) digital storage oscilloscope built
from analysis of the stock instrument.

---

## What this release provides

| Area | Capability | Verification |
|------|-----------|--------------|
| Display | ILI9341-compatible 320×240 RGB565 panel, 10×8 dotted graticule, live trace, persistent header, settings menu, SYSTEM DIAG | Bench-verified |
| Acquisition | ADC1 ch2 (PA2) → DMA1 ch1 → 1024-sample circular buffer; immutable snapshot frozen on trigger; bounded waits | Bench-verified (partly) |
| Timebase | TIM3-paced ADC conversions, 1 µs/div … 50 s/div table (24 steps); 5 fastest steps (1–20 µs/div) flagged equivalent-time-only | Table host-tested; pacing bench-verified down to 50 µs/div |
| Trigger | ADC analog watchdog, one-sided band with hysteresis, software edge refinement, AUTO/NORM/SINGLE policies, trigger-aligned averaging | Host-tested; bench-verified partly (50 µs/div falling edge verified; rising edge has known misalignment under investigation) |
| Front panel | 4 pushbuttons (PB12 OK, PB13 +, PB14 −, PB15 SEL); 3 slide switches via ADC1 resistor ladder (gain ×1/×2/×5, range 10 mV/0.1 V/1 V, coupling AC/DC/GND); 9 sensitivity combinations | Switch decode bench-verified; coupling switch is analogue-only |
| Measurements | Vpp, Vrms, Vmax, Vmin (integer, 64-bit sums, no FP); frequency/duty require periodic signal | Maths host-tested; Vpp shown on panel |
| Calibration | CRC records in reserved flash (never erased by image writes), 2 alternating settings slots, base offset/gain pair, 9-entry per-sensitivity zero table, CAL ZERO menu | Storage host-tested; absolute volts/div scaling from base pair not yet applied |
| PC link | CRC-16/CCITT-framed commands and replies over USART1 (115200 8N1): NOP, SET_TIMEBASE, SET_TRIGGER, SET_SPAN, RUN, STOP, GET_STATUS, STREAM | Verified on hardware |
| Watchdog | IWDG armed, fed every superloop iteration, 10 s timeout | Verified |

---

## What is NOT in this release

- RMS/frequency/duty readouts on the panel (maths exist; not all displayed)
- Save/load waveform, AV+/AV−/Cycle/All Disp. display modes
- Per-sensitivity calibration applied to the trace (base pair stored but not applied)
- 1–20 µs/div timebases (need equivalent-time / repetitive sampling; rejected by `acq_set_timebase()`)
- Equivalent-time / repetitive (ETS/REPT) sampling — planned future work
- The vendor's TIM4_CCR1 trigger-position mechanism
- Any dependency on the ATSHA204A secure element (removed by design)

See `docs/FEATURES.md` for the full verified / host-tested / experimental /
known limitations breakdown.

---

## Hardware identification & compatibility

**Tested revision:** exactly one instrument (STM32F103 medium density rev A,
display controller ID 0x858552 — a compatible controller, not a genuine
ILI9341 — which accepts the 9341 init sequence). Compatibility with other
DSO183 hardware revisions, clones, or display panels has **not** been tested
and is **not** claimed.

See `docs/HARDWARE.md` for the pin map, the display-controller caveat, and
the specific identification values observed on the tested unit.

**Open hardware items:** pushbutton pin (PA8 candidate unproven), signal input
physical landing (PA2), ladder channel bench re-confirmation, 50 µs/div
rising-edge trigger misalignment.

---

## Build, installation and flashing

### Prerequisites

- `arm-none-eabi-gcc` (tested 13.2.1) + `arm-none-eabi-size`, `arm-none-eabi-objcopy`
- GNU Make
- Python 3
- For flashing only: pyocd (`write_page.py`)

### Build

```bash
make            # firmware image (build/dso183.bin, .hex, .elf) + size report
make test       # host unit tests
make guard      # static safety checks
make check      # build + tests + size + documentation cross-check
```

### Flashing (user-controlled only)

This project never flashes your instrument for you.

```bash
make flash-dry                                    # lists pages, writes nothing
AUTHORIZED=1 python3 tools/flash/write_image.py \
    --image build/dso183.bin                      # page-wise, read-back verified
```

Never use `st-flash write` on the STM32F1: it mass-erases the chip and
destroys calibration/settings. See `FLASH_HANDOVER.md` for the full flash
procedure and reserved flash map.

---

## Build identity (this release)

| Item | Value |
|------|-------|
| Image | `build/dso183.bin` (37004 bytes), 37 pages |
| Target range | 0x08000000 .. 0x080093FF |
| Build sequence | 0xB71E (46878) → panel row 0 reads `B=4453424D S=B71E` |
| Reset vector | 0x08008115 (thumb bit set) |
| MD5 | `5ec9cb7360ba88478139e2666962efd0` |
| SP | 0x20005000 |

The image in this release has **not been flashed** by the maintainer from this
exact tree. Physical flash and bench validation remain the responsibility of
the person building it.

---

## Known issues

- 50 µs/div rising-edge trigger is deterministically misaligned (falling edge verified aligned) — under investigation
- Vpp readout reports a pure count ratio; absolute volts/div scaling from the base calibration pair is not yet applied
- Pushbutton pin (PA8) unproven on hardware
- AC/DC/GND coupling switch is analogue-only; firmware cannot observe its position
- Single instrument tested only

See `docs/FEATURES.md` and `docs/ROADMAP.md` for details.

---

## License

Custom application code: MIT (see `LICENSE`).
Third-party components: STM32 HAL (ST BSD), CMSIS (ARM), Adafruit GFX font (BSD).
See `docs/LICENSING.md` for full details and reverse-engineering uncertainties.

---

## Community

- `CONTRIBUTING.md` — how to build, test, and submit pull requests
- `CODE_OF_CONDUCT.md` — community standards
- `SECURITY.md` — security disclosure policy
- Issue templates: `.github/ISSUE_TEMPLATE/`

Questions? Open an issue. Want to help? Read CONTRIBUTING.md.
