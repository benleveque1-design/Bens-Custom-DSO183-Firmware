# Release readiness — v0.1.0

**DSO183 custom firmware — repository description, topics, tagline, and reproduction instructions**

This document consolidates the metadata and reproduction instructions for the
first public release. Nothing here constitutes a push or a release — those
require explicit approval from the maintainer.

---

## Repository description

**One-liner (tagline):**
> Replacement firmware for the JYE Tech DSO183 (18301K) digital storage
> oscilloscope — modular, host-testable, built from analysis of the stock
> instrument.

**Full description:**
> The DSO183 custom firmware is an open-source replacement firmware for the
> JYE Tech DSO183 (18301K) digital storage oscilloscope built around the
> STM32F103C8T6. It provides acquisition (ADC1 → DMA1 → circular buffer), a
> software trigger engine, an ILI9341-compatible display driver, front-panel
> ladder-switch decode, and a CRC-framed PC link over the console UART. All
> pure-logic modules are host-testable without hardware. The project is
> community-driven, not a race to the most sophisticated firmware — the goal is
> to improve the original DSO183, make its internals accessible, and create a
> foundation others can build upon.

**Short description (for package registries):**
> Open-source replacement firmware for the JYE Tech DSO183 digital storage
> oscilloscope (STM32F103, host-testable, community-driven).

---

## Suggested GitHub topics

`ds0183`, `dso183`, `oscilloscope`, `stm32f103`, `stm32`, `embedded`, `firmware`,
`digital-storage-oscilloscope`, `jye-tech`, `host-testable`, `reverse-engineered`,
`open-source-hardware`, `adc`, `trigger`, `dma`, `uart`, `ili9341`

---

## Reproduction instructions (for reviewers and downstream builders)

### 1. Clone

```bash
git clone https://github.com/benleveque1-design/Bens-Custom-DSO183-Firmware.git Bens-Custom-DSO183-Firmware
cd Bens-Custom-DSO183-Firmware
git checkout v0.1.0        # after tag creation, requires maintainer approval
```

### 2. Install prerequisites

```bash
# Debian/Ubuntu
sudo apt-get install gcc-arm-none-eabi binutils-arm-none-eabi make python3
pip3 install pyocd          # flashing only
```

### 3. Build

```bash
make            # build/dso183.bin, .hex, .elf + size report
```

Expect: deterministic build (two clean builds byte-identical), image ~37004 B,
37 pages, flash ~60% of the 60 KiB application area.

### 4. Test

```bash
make test       # all host unit tests pass
make guard      # 0 static safety issues
make check      # full internal-consistency cross-check (incl. FLASH_HANDOVER.md)
```

### 5. Verify build identity

The built `build/dso183.bin` should match:

| Item | Expected value |
|------|----------------|
| Size | 37004 bytes |
| Pages | 37 |
| Build sequence | 0xB71E (46878) → `S=B71E` |
| Reset vector | 0x08008115 |
| MD5 | `5ec9cb7360ba88478139e2666962efd0` |

`make check` cross-checks `FLASH_HANDOVER.md` against the built image — a
mismatch fails the build.

### 6. Flash (user-controlled, optional)

```bash
make flash-dry                                    # dry run, writes nothing
AUTHORIZED=1 python3 tools/flash/write_image.py \
    --image build/dso183.bin                      # real flash, read-back verified
```

Never use `st-flash write` on the STM32F1 — it mass-erases the chip and
destroys calibration/settings.

---

## What is NOT part of this release

- Vendor binary (not included — licensing reasons)
- Ghidra reverse-engineering workspace (not included — licensing reasons)
- Pre-flashed hardware (the maintainer does not flash instruments)
- Any claim of compatibility with DSO183 revisions other than the single
  tested unit (STM32F103 medium density rev A, display ID 0x858552)

---

## Approval required

Per project ground rules: no push to a public remote, no tag, and no GitHub
release without explicit maintainer approval. This document is the proposal;
the maintainer decides when (or whether) to proceed.
