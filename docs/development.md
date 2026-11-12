# Development setup — DSO183 custom firmware

**DSO183 custom firmware — developer guide**

This document walks through getting a host ready to build, test, and (optionally)
flash the DSO183 custom firmware. It is written so a new contributor can go
from a fresh clone to a verified build in one sitting.

---

## 1. Prerequisites

| Tool | Purpose | Tested version |
|------|---------|----------------|
| `arm-none-eabi-gcc` (+ `arm-none-eabi-size`, `arm-none-eabi-objcopy`) | Cross-compiler for the STM32F103 (Cortex-M3) | 13.2.1 |
| GNU Make | Build driver | any recent |
| Python 3 | Host test harness, docs guard, flash tools | 3.x |
| `pyocd` | Flashing only — `tools/flash/write_page.py` (page-wise writer) | any recent |

### Installing the ARM toolchain

On Debian/Ubuntu:

```bash
sudo apt-get install gcc-arm-none-eabi binutils-arm-none-eabi make python3
```

On macOS (Homebrew):

```bash
brew install arm-none-eabi-gcc make python
```

Verify:

```bash
arm-none-eabi-gcc --version
make --version
python3 --version
```

### Flash-only prerequisite

Flashing is **user-controlled only** — this project never flashes your
instrument for you. If you intend to flash, install pyocd:

```bash
pip3 install pyocd
```

The real write path is `tools/flash/write_page.py` (pyocd), and it
**refuses to run without `AUTHORIZED=1`** in the environment. `st-flash write`
is **not used** on the STM32F1 path: it mass-erases the chip and destroys the
calibration/settings storage pages and the vendor device record.

---

## 2. Clone and build

```bash
git clone <your-fork-url> Bens-Custom-DSO183-Firmware
cd Bens-Custom-DSO183-Firmware
make            # firmware image: build/dso183.bin, .hex, .elf + size report
```

Output artefacts land in `build/` (gitignored — never committed).

### Size budget

`make size` prints the flash/RAM budget report:

- Flash: image must fit within the 60 KiB application area (the last 4 KiB
  page is reserved for the calibration store).
- RAM: ~81% of 20 KiB is statically used; no framebuffer.

---

## 3. Test cycle

```bash
make test       # build and run every host unit test (native compiler, no HAL)
make guard      # static safety checks (0 issues expected)
make check      # build + tests + size + documentation/artefact cross-check
```

- `make test` builds each `tests/test_*.c` as its own binary against the
  pure-logic production modules (no HAL needed).
- `make guard` runs `tools/guard.py` and reports static safety issues.
- `make check` is the single command that answers "is this tree internally
  consistent?". It builds the image, runs every host test, checks the
  flash/RAM budget, and verifies that `FLASH_HANDOVER.md` still describes the
  artefact that was just built. A drift between docs and the built image
  fails the check.

### Deterministic builds

Two clean builds from the same tree produce byte-identical images (verified
with `make clean && make` twice and comparing MD5s). The build identity
(`S=` byte, shown on the SYSTEM DIAG screen) is derived from a checksum of
the application sources, so every distinct source state gets its own identity.

---

## 4. Flashing (dry run first — writes nothing)

The flash path is entirely manual and user-authorized:

```bash
make flash-dry                                    # lists pages, writes nothing
```

For a real flash:

```bash
AUTHORIZED=1 python3 tools/flash/write_image.py \
    --image build/dso183.bin
```

The writer is page-wise with read-back verification and freezes the IWDG
during erase so a page erase cannot reset the target. It preserves the
reserved flash map (settings, calibration, waveform slots, vendor device
record).

**Never** use `st-flash write` on the STM32F1: it mass-erases the chip and
destroys calibration/settings. See `FLASH_HANDOVER.md` for the full flash
procedure, reserved flash map, and recovery path.

---

## 5. Repository layout

```
app/            firmware: acquisition, trigger, timebase, measurements,
                calibration, UI/menu, PC link, flash store, run state
drivers/        vendored STM32F1xx HAL (ST) + CMSIS (ARM) + ILI9341 display driver
fonts/          Adafruit GFX 5x7 font (vendored, BSD) + glyph rasteriser
linker/         STM32F103C8 linker script (60 KiB app, last 4 KiB reserved)
startup/        vector table + reset handler
tests/          host unit tests (native, no HAL)
tools/          build/release helpers, flash tools (page-wise writer),
                documentation guard, PC-link host tests
docs/           project documentation (architecture, hardware, features,
                limitations, roadmap, licensing, development)
ui_design/      UI-SPEC-001 mockups and specification (design artifacts,
                not build inputs)
build/          gitignored: compiled artefacts (never committed)
```

Pure-logic modules (`app/acq.c`, `app/trigger.c`, `app/timebase.c`,
`app/measure.c`, `app/waveform.c`, `app/render.c`, `app/frame.c`, `app/cal.c`,
`app/link.c`, `app/settings.c`, `app/ladder.c`, `app/diag.c`, …) contain no
HAL dependency and compile/run on a host for unit testing. Hardware glue lives
only in `main()` and the board support files (`board_*.c`, `stm32f1xx_it.c`,
the display driver).

---

## 6. Adding or changing source

- Edit files under `app/`, `drivers/display/`, `fonts/`, `startup/`, or
  `linker/` as needed.
- Rebuild: `make` (header changes are tracked via `-MMD -MP` `.d` files —
  editing a header rebuilds the objects that include it).
- Re-run `make check` to confirm the tree stays internally consistent,
  including the `FLASH_HANDOVER.md` ↔ built image cross-check.

---

## 7. Getting help

- `CONTRIBUTING.md` — how to build, test and submit pull requests
- `CODE_OF_CONDUCT.md` — community standards
- `SECURITY.md` — security disclosure policy
- Issue templates: `.github/ISSUE_TEMPLATE/bug_report.md`,
  `.github/ISSUE_TEMPLATE/feature_request.md`
- Questions? Open an issue. Contributors at every experience level are welcome.
