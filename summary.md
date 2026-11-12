## Objective
DSO183 custom firmware — 8-channel ADC switch mapping, display driver working with decoded range/gain, scaled Vpp measurements, and host-testable acquisition engine.

## Display Driver Configuration (VERIFIED WORKING)

### Bus Polarity (validated on hardware)
- PB0..PB7: data bus
- PC13: CS (active LOW)
- PC14: D/C (LOW=command, HIGH=data)
- PC15: WR (byte latched on LOW→HIGH edge)
- PB8: /RD (LOW makes panel drive bus)
- PB9: RST (active LOW)

### MADCTL
- Value: 0x20 (MV=1, RGB mode)
- MV=1 transposes X↔Y in GRAM

### set_window (no swap)
- 0x2A (column address set) receives x (0..239)
- 0x2B (page address set) receives y (0..319)
- With MV=1, GRAM axes are exchanged; the transpose happens in GRAM

### Colour Encoding
- Standard RGB565 via ILI9341_RGB565(r,g,b)
- NOT BGR-packed

### Init Sequence
- Vendor's own init stream (transcribed from 0x08001d1c..0x08001e84)
- MADCTL = 0x20 (was 0x68 in vendor, changed to 0x20 for RGB mode)
- **MUST NOT include 0xE4 command** (causes white band on left)
- **MUST NOT include 0xC4 command** (causes white band on left)
- 0xE0 and 0xE1 gamma curves included

### Test Pattern (compensates for vertical flip)
- fill_rect(0, 0, 320, 80, ILI9341_BLUE)   → top (display shows Red)
- fill_rect(0, 80, 320, 80, ILI9341_GREEN) → middle (display shows Green)
- fill_rect(0, 160, 320, 80, ILI9341_RED)  → bottom (display shows Blue)
- Result: Red top, Green middle, Blue bottom (correct)

### Build
```bash
cd custom-firmware
make
```

### Flashing (safe path only)
`st-flash write` is **never used**: on the STM32F1 path it mass-erases the chip,
destroying the settings/calibration page at `0x0800F800`. The only write path is
`tools/flash/write_page.py` (pyocd, one 1 KiB page per invocation, read-back
verified before reset, flash re-locked afterwards). It requires an explicit
`AUTHORIZED=1` authorization per write:

```bash
cd custom-firmware
make flash-dry                    # prints what *would* happen; never writes
# one 1 KiB page per invocation - the image is written page by page:
AUTHORIZED=1 python tools/flash/write_page.py \
    --image <1KiB_page.bin> --page 0x08000NNN
```

### Recovery
Never run `st-flash erase` (mass erase destroys the settings page). Restore the
known-good v6 image one page at a time through the same page-wise path; the
settings page at `0x0800FC00` is never written, so the device record survives.

```bash
AUTHORIZED=1 python tools/flash/write_page.py \
    --image <v6_page1k.bin> --page 0x08000NNN
```

### Verified Results
- fill_screen(WHITE) = completely white
- fill_screen(RED) = fully red, no band
- fill_screen(BLUE) = clean blue, no band
- 3-band test = Red top, Green middle, Blue bottom, no white band
- Flash: 18156 B / 61440 B (29%), RAM: 7520 B / 20480 B (36%)
- **NEW: 8-channel ADC switch mapping implemented - range/gain channels decoded**
- **NEW: Panel state display shows decoded range × gain (e.g., "0.1V x2 Vpp 0.4d")**
- **NEW: Scaled Vpp measurements reflect actual volts/division**

## Relevant Files
- `drivers/display/ili9341.c` — driver implementation
- `drivers/display/ili9341.h` — colour macro, geometry constants
- `app/main.c` — test pattern
- `artifacts/patch/dso183_patched_full_v6.bin` — known-good recovery image

## 2026-10-26 — M6 groundwork (host-side only, nothing flashed)

New pure-logic modules, following the `acq_stats.h`/`ladder.h` conventions
(no floats, no allocation, bounded loops, host-testable):

- `app/trigger.{c,h}` — software edge trigger on the sample buffer:
  two-state scan (WAIT_REARM -> ARMED) with an ADC-counts hysteresis band,
  rising/falling slope, NULL/argument fail-fast.  Policy FSM for the three
  vendor modes (AUTO / NORM / SINGLE with explicit re-arm), and
  `trig_window_start()` placing the trigger at ~30 % pre-trigger
  (TRIG_PRE_TRIGGER = 96 of the 320-sample window).  This is the piece the
  ADC analog watchdog cannot provide: slope selectivity, hysteresis and
  hold-off.
- `app/timebase.{c,h}` — the timebase table: 27 steps of the 1-2-5
  sequence (1 us, 2 us, 5 us, ... 500 s), matching the on-screen order
  0 = 1 us/div (fastest) .. 26 = 500 s/div (slowest), which is what the
  investigation notes imply ('fast path for timebases <= 12', 'slow range
  18..21').  Entries carry exact baked TIM3 PSC/ARR for the verified 32 MHz
  APB1 tick (31.25 ns/count); every step is error-free.  `single_shot == 0`
  on idx 0..4: their ticks (31.25 .. 625 ns) are shorter than the 1312.5 ns
  minimum ADC conversion (SMP=1.5+12.5 at PCLK2/6 = 42 timer counts), so
  they require equivalent-time sampling that the engine does not implement
  yet — flagged honestly, not faked; single-shot ranges run from 50 us/div
  (idx 5) upward.  The earlier 1-2.5-5 draft was superseded once the actual
  on-screen list was provided; the Ghidra-decoded vendor TIM3 tables
  (0x0800d942 / 0x0800dbbc) are indexed slowest-first (idx 0 = largest tick)
  and therefore are NOT the primary 1 us..500 s table — they are either a
  secondary/decimation path or indexed by (26 - timebase); not used.

Tests: `tests/test_trigger.c` (trigger scan hysteresis/noise/falling/slope
fail-fast cases, FSM modes, window placement) and `tests/test_timebase.c`
(27-step shape, tick accuracy to 0.5 count, accessor saturation, labels).
`make test`: all four suites pass.  Firmware build:
flash 18156 B / 61440 B (29 %), RAM 7520 B / 20480 B (36 %).  The new modules
are compiled into the image and GC'd until main() wires them in.

Open: U-02a bench confirmation of the ADC clock limit (still unverified on
hardware); U-03 partially resolved — vendor TIM3 tables decoded but are
slowest-first, so the primary 1 us..500 s table now comes from the
published on-screen list.  M5 bench verification still deferred pending
AUTH (tools/flash/write_page.py page-wise path, no st-flash).

## 2026-10-28 - diagnostic screen (replaces colour-bar test)

`app/diag.{c,h}` is a pure-logic content layer: it renders 5 content rows of a
fixed 26-character grid (drawn at text scale 2, so 26 x 12 px = 312 px wide on
the 320 px panel):

    row 0  B=4453424D S=4E     (build magic + sequence)
    row 1  BOOT <n>           (1 = cold boot, >1 = warm resets)
    row 2  LOOP <n> T <n>     (main loops + SysTick heartbeat)
    row 3  ID 9341 M 20       (display ID / MADCTL read back over the bus)
    row 4  PA0-15 <hex>       (raw GPIOA input register, READ-ONLY)

Rows 5..9 now carry the ADC channel counts, the decoded switch positions and
the error register (see the later DIAG entries).  All rows are blank-padded to fixed width
so a refreshed value can never leave ghost digits on the panel.  ILI9341
driver and its byte-exact verified `set_window` sequence are used untouched.

`app/main.c` now draws this screen instead of the 3-colour-band test; the
counter rows (1..4) are refreshed at ~1 Hz from live status, applying no
configuration change to GPIOA (the input register is only read).

Host tests: `tests/test_diag.c` (row content and fixed width).  `make test`
now runs five suites (diag added to TEST_LIBS).  Firmware build:
flash 10668 B / 61440 B (17 %), RAM 3640 B / 20480 B (17 %).

NOT done (would need flashing to verify on the instrument): whether MADCTL
read-back actually returns 0x20 and whether `ili9341_read_id()` returns
0x9341 on this unit - both are read-only over the already working display
bus, but the numbers shown on the diag screen are the first on-device
verification of that read path.

## 2026-10-28 (b) - diagnostic screen v2: real input evidence + read-path fixes

The first version of the diagnostic screen showed a display ID that could not
be right, and it had no way to show a front-panel input.  Both were fixed.

**Defect 1 - the display ID / MADCTL were derived from the wrong bytes.**
`drivers/display/ili9341.c:lcd_read_cmd()` assembles the answer as
`id = (id << 8) | byte`, i.e. the FIRST byte read ends up in the MOST
significant position.  The screen masked that value with `& 0xFFFF` /
`& 0xFF`, which for a healthy panel yields `ID 4108` (not `9341`) and
`M 00` (not `20`) - a healthy bus would have looked broken.  The raw values
are now kept and the interesting bytes are picked with documented shifts
(`app/diag.h`):

    manufacturer = (raw >> 16) & 0xFF   -> 0x93
    device       = (raw >>  8) & 0xFF   -> 0x41
    madctl byte  = (raw >> 24) & 0xFF   -> 0x20

The manufacturer/device shifts are robust to whether the panel sends a
leading dummy byte or not, so they work for both `0x00934108` and
`0x93410800` style answers.  The row now reads `ID 9341 M 20`, and a dead
bus reads `ID FFFF M 00` (or `ID 0000 M 00`) - visibly broken, never
accidentally plausible.

**Defect 2 - `status_display_id` was a dead variable.**  It was declared and
documented as "the controller ID read back over the bus (0x9341 ...), or 0 if
the read failed" but nothing ever wrote it.  The firmware now publishes the
raw bus read there (`status_display_id = st->display_id`), so the same
evidence is available over SWD as on the panel.

**New: front-panel input evidence (`app/input.{c,h}`).**  The slide switches
(CPL, SEN1, SEN2) are read by the vendor from a resistive ladder on an ADC1
input; WHICH channel carries it is still open (U-02a).  Rather than guess,
the new module exposes a bounded one-shot reader for channels 0..7 (PA0..PA7)
and the screen shows the raw count of every channel:

    row 5  A0-3 0100 0111 0122 0133
    row 6  A4-7 0144 0155 0166 0177

Flipping a switch then identifies the ladder channel empirically (the
channel whose count moves), which is the missing piece for real input
handling.  The reader is deliberately conservative: ADC1 only, channels 0..7
only (PA8/PA9 are not free - PA9/PA10 are the console UART, PA13/PA14 are
SWD), no DMA, one conversion at a time, every wait bounded (200k spins) so a
dead ADC cannot hang the UI, and it never touches the M5 acquisition
engine's DMA1 channel 1 usage (that engine initialises the ADC registers
itself when it starts).  The raw counts are also published to
`status_adc_raw[8]` for SWD-side inspection.

`app/main.c` was restructured around one `diag_snapshot()` helper so the
first paint and the 1 Hz refresh read exactly the same sources (no drift
between the two paths).

Host tests: `tests/test_diag.c` grew the raw-read fixtures, the two ADC
rows and an explicit "a failed read must look failed" case.  `make test`:
five suites pass.  Firmware build: flash 11308 B / 61440 B (18 %), RAM
3664 B / 20480 B (17 %).

Still unverified on hardware (needs an authorised flash): the ID/MADCTL
read-back, the ADC channel map, and whether the earlier white-screen boot
failure is gone.  Nothing was flashed in this session.

### 2026-10-28 (c) - vendor image decode + the ladder channel map

The Ghidra/Capstone sweep of `dso183_original.bin` produced four facts that
change our firmware:

**F-1 - the display ID the vendor expects is `0x9341`.**  At 0x080017FC the
vendor calls its display read (0x0800203D) and compares the LOW 16 bits with
`0x9341`; on a match it stores `0x9341` to RAM 0x200002A0 and 0x200002A2.
Our driver returns the raw 4-byte answer `0x00934108`, and the diag layer
extracts `manufacturer = (raw>>16)&0xFF = 0x93`, `device = (raw>>8)&0xFF =
0x41` - i.e. exactly the vendor's `0x9341`.  Our extraction is correct.
(An earlier note in this file that put 0x200002A0 on the "GPIOA pointer" was
wrong: it is the RAM cell holding the display ID.)

**F-2 - the front-panel ladder is read from ADC1 INJECTED ranks, not regular
channels.**  The vendor switch reader at 0x0800336C..0x080033EA loads the
ADC1 base (0x40012400) and reads `+0x3C`, `+0x40`, `+0x44` = JDR1, JDR2,
JDR3, compares each with `0xA00` / `0x600` (code 0 / 1 / 2) and derives

    combo    = 4 + 3*code(JDR1) + code(JDR2)
    coupling = 2 - code(JDR3)

which matches `app/ladder.c` exactly.

**F-3 - the injected channel map (this is the U-02a answer).**  The vendor's
ADC1 init at 0x08005924 writes `JSQR = 0x002394C0`.  Decoded field by field
against the CMSIS definitions (JSQ1 bits 4:0, JSQ2 bits 9:5, JSQ3 bits
14:10, JSQ4 bits 19:15, JL bits 21:20):

    set bits of 0x002394C0 : 6, 7, 10, 12, 15, 16, 17, 21
    JSQ1 = 0  (PA0)      JSQ2 = 6  (PA6)
    JSQ3 = 1  (PA1)      JSQ4 = 7  (PA7)
    JL   = 3             -> three injected conversions

so the ladder nodes are **ADC channel 0 (SEN1), channel 6 (SEN2) and channel
1 (CPL)**.  These are ordinary analog nodes, so a REGULAR conversion of those
channels samples the same voltages - no injected trigger is needed, and the
F1 ADC has no JSVR/JSWGRT status register to wait for anyway (the F1 CMSIS
ADC struct ends at DR; the F1 HAL never references JSVR/JSWGRT).

**What changed in the firmware.**  `app/ladder.h` now carries the
vendor-derived mapping (`LADDER_CH_SEN1 = 0`, `LADDER_CH_SEN2 = 6`,
`LADDER_CH_CPL = 1`) plus `ladder_positions()` and `ladder_decode()`;
`app/diag.{c,h}` gained two rows and `app/main.c` fills them from the live
ADC scan:

    row 7  SW 1 0 2          (SEN1 SEN2 CPL positions, 3 = no reading)
    row 8  CMB 7 CPL 0       (sensitivity combination 4..12, coupling 0..2)

The raw channel rows (5, 6) stay on screen next to them, so if the mapping
is wrong the moving channel is visible immediately.  `tests/test_ladder.c`
covers the new decode (including the "a missing reading invalidates the
decode" case), `tests/test_diag.c` covers the new rows.

**F-5 - a better candidate for the push button.**  At boot (0x0800185C) the
vendor reads `GPIOA->IDR` and tests bit 8 (PA8): if PA8 is low it calls a
debounce/delay routine.  PA12 is also read nearby but its result is unused
there.  PA8 is now the strongest candidate for the front-panel button
(U-07c) - but it stays unwired: no evidence from hardware yet, and the
diag screen only ever READS GPIOA, never reconfigures a pin.

**Open hazard for the acquisition work.**  `app/input.c` clears all of
`GPIOA->CRL` to make PA0..PA7 analog.  If the analog front end uses any of
PA0..PA7 as a digital output, that is destructive - it must be re-checked
against the vendor's port setup before the M5 acquisition engine is enabled
on top of the diag screen.

**Still unverified on hardware:** everything above (needs an authorised
flash).  Nothing was flashed in this session; the device still runs the
known-good v6 image restored from the 2026-10-24 backup.

### 2026-10-28 (d) - audit pass (host-only) and the flash hand-over

The user took over the flashing; this pass is a code audit with no device
access.  Six real defects were found and fixed; two suspected defects were
dismissed with evidence.

**Fixed 1 - the BOOT counter could never show a reset loop.**  The startup
code copies `.data` and zeroes `.bss`, and `status_boot_count` lived in
`.bss`, so every boot started from zero again - the counter was worthless for
the one job it had (detecting an IWDG reset loop).  The linker script now has a
`.noinit (NOLOAD)` section after `.bss` (outside the startup's zeroing range
`[_sbss,_ebss)`), and `status_boot_count_init()` keeps the count there with a
`BOOT` magic word beside it: a cold boot (RAM undefined) resets the count to
1, a warm reset increments it.  Verified in the ELF: `_ebss == _snoinit ==
0x20000248`, `s_boot` at `0x20000248`, i.e. exactly outside the zeroed range,
and the section is NOLOAD (no bytes in the image).

**Fixed 2 - unbounded init waits.**  `clock_config()` and `console_init()`
used to spin in `for(;;){}` on failure: a dead oscillator produced a white
screen, no evidence, and an unarmed watchdog.  Both now return a status and
record a bit in the new `status_init_errors` field (OSC 0x01, CLOCK 0x02,
UART 0x04, ADC 0x08), and the firmware continues to the screen.  Row 9 shows
it: `ERR 00` means every init step succeeded.

**Fixed 3 - `input_init()` could fail silently.**  It now returns 1/0 and the
failure sets `STATUS_ERR_ADC`.

**Fixed 4 - the console could stall the loop.**  `puts_()` wrote to the UART
unconditionally with a 100 ms timeout per byte.  It is now gated on a
`console_ok` flag that is cleared for good if a single transmit fails, so a
dead link can never eat the loop time.

**Fixed 5 - PA10 (USART1 RX) was configured as an output.**  Both console
pins were set to `GPIO_MODE_AF_PP`, i.e. the MCU drove the RX wire that the
USB adapter's TX also drives - two drivers fighting on one line.  PA9 stays
alternate-function push-pull; PA10 is now a plain input.

**Fixed 6 - the new live rows would have frozen.**  The refresh loop
repainted rows 1..8 while the ladder rows are 7 and 8 and the new error row
is 9 - so the ERR row (and any later row) would never update.  The loop now
repaints rows 1..9.

**Dismissed 1 - "the SysTick was left at the pre-clock 8 MHz".**
`HAL_RCC_ClockConfig` re-arms the tick itself
(`drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc.c:843-847`: SystemCoreClock
is updated, then `HAL_InitTick(TICK_INT_PRIORITY)`), so `HAL_Delay(500)` is a
true 500 ms after the clock switch.  No change needed; a comment in
`app/main.c` records it.

**Dismissed 2 - "the screen paint could trip the watchdog".**  The ILI9341
write path contains no `HAL_Delay` (the delays are only in the init stream
and the 4x read-ID strobe), and every loop in the driver is counter-bounded,
so painting the rows cannot hang or starve the 10 s IWDG.

**New hardware fact (from the driver header):** the ILI9341 data bus is
**PB0..PB7**, not PB8..PB15.  ADC channels 8/9 are therefore unusable, which
confirms the choice of PA0..PA7 for the front-panel reader.

**New tool: `tools/flash/write_image.py`.**  `write_page.py` handles exactly
one page per invocation (deliberately), which makes a 12 KiB image a
hand-typed loop - and a mistyped page address is how a working device gets
destroyed.  The new wrapper splits the image into 1 KiB pages, pads the tail
with 0xFF (erased flash, not zeros), calls the approved writer per page,
stops at the first failure, and REFUSES any range that overlaps the settings
page 0x0800F800..0x0800FFFF (the 64 KiB recovery image needs
`--page-count 62` for exactly that reason).  It has a `--dry-run` mode that
touches no hardware.

Build state after the audit: `make` green, flash 11832 B / 61440 B (19 %),
RAM 3672 B / 20480 B (17 %), `make test` 5/5 suites pass, `make diag-preview`
shows all ten rows (`ERR 00` included).

### 2026-10-28 (e) - audit round 3 (host-only) + final image facts

Audit of the code that is actually IN the flashed image (the acquisition
engine is linked in even for the diagnostic build: the vector table keeps
`DMA1_Channel1_IRQHandler` / `ADC1_2_IRQHandler`, which call `acq_*`).

**Defect 7 - `app/board.h` carried an invalid, unverified pin macro.**
`#define GPIO_PIN_BUTTON1 GPIO_PIN_19` claimed "a push button on PA19,
active low, superloop 0x080018ae".  That decode was never confirmed, and
PA19 does not exist on the STM32F103C8 (LQFP48: PA0..PA15 only), so the
macro was a landmine: `HAL_GPIO_Init` would silently do nothing for a pin
that is not there.  The macro is REMOVED and replaced by a comment that
records the current state: the push-button pin is UNKNOWN, PA8 is the
strongest candidate (the vendor reads `GPIOA->IDR` at boot, 0x0800185C, and
tests bit 8), and the diagnostic screen only READS GPIOA - it never
reconfigures a pin.

**Defect 8 - `acq_isr_adc()` touched the ADC even when the engine was not
running.**  The old order cleared `SR` and disarmed `AWDIE` before the
`s_inited` check, so a stray watchdog interrupt could silently break the
diagnostic screen's one-shot ADC reads.  The `s_inited` check now comes
first and the ADC registers are left completely alone.

**Defect 9 - `acq_cycle()` hid hardware errors.**  A DMA/ADC error during
the wait produced a plausible-looking window from a stale buffer.  The
cycle now snapshots `status_acq_hw_errors` and returns `ACQ_HW_ERROR` if it
changed, instead of handing back stale samples.

**Verified (no change needed).**  `app/ui.c` (grid helpers only, not
referenced from main); `app/board_uart.c` (a single `UART_HandleTypeDef`);
`app/status.c` boot state (magic `0x424F4F54` in `.noinit`, cold boot ->
count 1, warm reset -> count+1, published into `status_boot_count`);
the linker/startup pairing (`.data` copied, `.bss` zeroed, `.noinit` NOT
zeroed: `_ebss == _snoinit == 0x20000248`).

**Final image facts (for the flash hand-over).**

    build/dso183.bin   11828 bytes -> 12 pages of 1 KiB (0x08000000..0x08002C00)
    vector table       SP 0x20005000, reset 0x080026A9 (thumb bit set)
    build identity     magic 0x4453424D, build sequence 0xAF (175)
                       -> the panel must read "B=4453424D S=AF"
    flash              11828 B / 61440 B (19 %)
    RAM                 3672 B / 20480 B (17 %)
    make test          5/5 suites pass

The build sequence is a checksum of `app/*.c` and `app/*.h`, so any further
source edit changes `S=` - rebuild before flashing and compare the ELF
value with the panel.

### 2026-10-28 (f) - audit round 4/5: the ladder map and the signal channel were WRONG

The earlier vendor decode of the ADC setup was re-done against the LOCAL CMSIS
header (`drivers/CMSIS/Device/ST/STM32F1xx/Include/stm32f103xb.h`), which is
the authoritative register map for this repo - not a recollection.

**F1 ADC register map (from the header).**  `ADC_TypeDef`: SR 0x00, CR1 0x04,
CR2 0x08, SMPR1 0x0C, SMPR2 0x10, JOFR1..4 0x14..0x20, HTR 0x24, LTR 0x28,
SQR1 0x2C, SQR2 0x30, SQR3 0x34, JSQR 0x38, JDR1 0x3C, JDR2 0x40, JDR3 0x44,
JDR4 0x48, DR 0x4C.  SQR1 = SQ13..SQ16 + L (bits 20..23); SQR2 = SQ7..SQ12;
SQR3 = SQ1..SQ6 (SQ1 at bits 0..4).  So `SQR1 = 0` (L = 0 -> ONE conversion)
plus `SQR3 = channel` is the correct F1 way to program a single conversion -
our `app/acq.c` and `app/input.c` are right.

**JSQR = 0x002394C0, decoded correctly this time.**  Set bits 6, 7, 10, 12,
15, 16, 17, 21 -> JSQ1 = ch0, JSQ2 = ch6, JSQ3 = ch5, JSQ4 = ch7, JL field = 2
-> THREE injected conversions.  And on F1 the ranks do NOT start at JSQ1: the
ST HAL macro `ADC_JSQR_RK_JL` puts rank 1 into JSQ(4-JL+1), so with three
conversions the used slots are JSQ2, JSQ3, JSQ4:

    rank 1 = JSQ2 = ch6 (PA6) -> JDR1
    rank 2 = JSQ3 = ch5 (PA5) -> JDR2
    rank 3 = JSQ4 = ch7 (PA7) -> JDR3

The earlier note ("JSQ3 = 1", "JL = 3 meaning three conversions") was wrong on
the channel AND on which slots the ranks occupy.

**Defect 10 - the ladder channel map was wrong.**  `app/ladder.h` mapped
SEN1 = ch0, SEN2 = ch6, CPL = ch1 (derived from the bad decode).  The vendor
truth is SEN1 = ch6 (JDR1), SEN2 = ch5 (JDR2), CPL = ch7 (JDR3).  Constants
corrected, header comment rewritten with the corrected decode and the F1
rank-slot rule, `tests/test_ladder.c` expectations updated.

**Defect 11 - the signal channel was wrong.**  The vendor's ADC1 init writes
`CR1 = 0x00800302` (AWDEN, AWDSGL, 12-bit, **AWDCH = 2**) and `SQR3 = 2`, i.e.
the regular group converts channel 2 and the analog watchdog watches exactly
that channel.  The signal input is therefore **ADC1_IN2 = PA2**, not PA0.
`app/acq.h`: `ACQ_SIGNAL_CHANNEL` 0 -> 2, `ACQ_SIGNAL_PIN` GPIO_PIN_0 ->
GPIO_PIN_2 (still to be confirmed on the bench).

**Defect 12 - a stale EOC could survive an aborted conversion.**  On F1 a
read of DR clears the EOC flag (the F1 HAL documents this at
stm32f1xx_hal_adc.c:1462), so after a conversion timeout the flag must be
drained, otherwise the NEXT read could see the stale EOC of the aborted
conversion and hand back the previous channel's data as fresh.  `app/input.c`
now drains `ADC1->DR` on the timeout path.

**Verified OK (no change).**  F1 SR flags are cleared by writing 0
(`__HAL_ADC_CLEAR_FLAG` = `WRITE_REG(SR, ~FLAG)`), so our `ADC1->SR = 0U`
before SWSTART is correct; `acq_cycle()`'s wait loop is bounded by a
wrap-safe `HAL_GetTick` deadline (the deadline mechanism depends on SysTick
being alive, which is inherent to the HAL); the ILI9341 write path has no
unbounded wait and no `HAL_Delay`; `diag.c` truncates every row through
`snprintf(dst, DIAG_COLS, ...)`, so no row can overflow the 26-char field.

**Defect 13 - the hand-over pointed at a tool that did not exist.**
`FLASH_HANDOVER.md` promised `tools/flash/live_check.py` for the objective
SWD verification, but the file was never written.  It exists now: it resolves
the `status_*` symbols from the ELF with `arm-none-eabi-nm`, connects with
pyocd and READS the counters over SWD - no writes, no halt, no reset, no
flash operation - and prints two samples 1 s apart so the heartbeat and the
main-loop counter can be compared.  `--symbols-only` resolves the addresses
without touching the probe.  The ADC array is read byte-wise because
`status_adc_raw` sits at an odd address (0x20000212) and a 32-bit read there
would straddle into the neighbouring variable.

**Image facts after this round.**  flash 11832 B / 61440 B (19 %), RAM
3672 B / 20480 B (17 %), build sequence 219 = 0xDB -> the panel must read
`B=4453424D S=DB`.  `make test` 5/5.  Nothing flashed; the instrument still
runs the known-good v6 image.

### 2026-10-28 (g) - re-audit round 6: the build system was lying to us

**Defect 14 (the important one) - no header dependency tracking.**  The
object rule was

    $(BUILD)/%.o: %.c | $(BUILD)
            $(CC) -c $(CFLAGS) $< -o $@

with no `-MMD -MP` and no `-include`.  Editing a HEADER therefore did not
rebuild the objects that include it, so the .bin kept the OLD header
contents.  This was not theoretical: the ladder channel map (D10) and the
signal channel (D11) live in HEADERS, and the image that was about to be
flashed still contained the pre-fix values `ch0/ch6/ch1` and `PA0`.  The
tell was a clean rebuild producing a DIFFERENT .bin than the incremental
one.  Fix: `-MMD -MP` in `CFLAGS` and in the host-test compile line, plus

    -include $(wildcard $(BUILD)/*.d)

in the Makefile.  Verified: `touch app/ladder.h && make -n` now rebuilds
`ladder.o` (and `status.o`), and the linked ELF carries the corrected
immediates 6 / 5 / 7 for SEN1 / SEN2 / CPL.

**Defect 15 - an always-false comparison in `app/input.c`.**
`if ((ch < INPUT_ADC_FIRST_CHANNEL) || (ch > INPUT_ADC_LAST_CHANNEL))`
compares an unsigned channel against 0 -> `-Wextra` warned
"comparison is always false" on every build.  Replaced with an unsigned
range test that is correct for any FIRST/LAST pair:

    if (((uint32_t)ch - (uint32_t)INPUT_ADC_FIRST_CHANNEL)
        > (uint32_t)(INPUT_ADC_LAST_CHANNEL - INPUT_ADC_FIRST_CHANNEL))

**Also removed** the duplicate `C_SOURCES += app/ui.c` (the `app/*.c`
wildcard already picks it up; the duplicate was harmless only because
`notdir` collapses it).

**Warnings pass.**  Every one of our own sources was recompiled with
`-Wall -Wextra -Wshadow -Wundef -Wpointer-arith -Wcast-align
-Wstrict-prototypes -Wmissing-prototypes -Wswitch-default -Wredundant-decls
-Wnested-externs -Wold-style-definition -Wmissing-declarations`: the only
hits were the input.c tautology (fixed) and missing prototypes for the
three ISRs in `app/stm32f1xx_it.c` (the startup file declares them; the
project flags do not include `-Wmissing-prototypes`, so the normal build is
clean).  The newlib `_read/_write/_close/_lseek` linker warnings come from
pulling in `snprintf`; they are the standard nano stubs, harmless.

**Final image facts (re-verified after a clean rebuild).**

    build/dso183.bin   11832 bytes -> 12 pages (0x08000000..0x08002C00)
    vector table       SP 0x20005000, reset 0x080026AD (thumb bit set)
    build identity     magic 0x4453424D, build sequence 0x1B (27)
                       -> the panel must read "B=4453424D S=1B"
    flash              11832 B / 61440 B (19 %)
    RAM                 3672 B / 20480 B (17 %)
    make test          5/5 suites pass
    determinism        two clean builds -> byte-identical .bin
    dry run            write_image.py lists 12 pages, writes nothing
    live_check.py      resolves all 9 status_* symbols (--symbols-only)

`FLASH_HANDOVER.md` was updated accordingly (S=1B, reset vector, and a
build-provenance note about the dependency fix).

### 2026-10-28 (h) - audit round 7: the display driver had drifted from the vendor

Round 6 audited our code; round 7 audited the DISPLAY path against the
vendor binary and found three real defects in the driver we were about to
flash.

**Vendor ground truth (capstone over dso183_original.bin).**

* The vendor's window routine is at 0x08002788 and takes (x, w, y, h).  It
  sends 0x2A (row range) and 0x2B (column range) with EXACTLY FOUR bytes
  each:
  `0x2A: x>>8, x&0xFF, x1>>8, x1&0xFF` and
  `0x2B: y>>8, y&0xFF, y1>>8, y1&0xFF`, with `x1 = x + w - 1` and
  `y1 = y + h - 1`.
* The vendor's init (0x08001d1c..0x08001e84) writes MADCTL (0x36) with
  **0x68** (MX|MV|BGR) at 0x08001d64, then 0x3A=0x55, 0xE4=27 00 10,
  0x13, 0xB0=00 E0, 0xB2=0C 0C 00 33 33, 0xB7=35, 0xBB=2B, 0xC0=2C,
  0xC2=01, 0xC3=11, 0xC4=20, 0xC6=0F, 0xD0=A4 A1, 0xE0/0xE1 gamma,
  0x0A=18, 0x29.

**Defect 16 (real, would corrupt the text) - the 0x2B payload had five
bytes.**  We sent `y>>8, y&0xFF, y1>>8, x1&0xFF, y1&0xFF`.  The panel reads
exactly four parameters, so it took `x1&0xFF` as the row END (instead of
`y1&0xFF`) and then swallowed the fifth byte as a stray command.  For the
diagnostic screen (12x16 px character cells) that shrank the vertical window
to a few rows and pushed the rest of the glyph data past the window end -
exactly the kind of corruption that makes a screen look broken.  The payload
is now byte-identical to the vendor's.

**Defect 17 (real, would have mangled the colours) - wrong 0xB0/0xB2
parameters.**  We had `0xB0 = 0A E0`; the vendor sends `00 E0`.  Bit 3 of
0xB0 is DMI (display memory interface): 0 = RGB565, 1 = RGB666.  This driver
writes two bytes per pixel, so DMI MUST be 0 - with 0x0A the panel would
have expected three bytes per pixel and the whole pixel stream would have
been misaligned.  `0xB2` carried the same wrong byte and is now `0C 0C 00 33
33` like the vendor's.

**Defect 18 - stale comments.**  The driver header still described MADCTL as
0x68 and a later comment described set_window as "MADCTL 0x00: no transpose".
Both are wrong: the code writes 0x20 (MV only).  The comments now explain
the deliberate deviation: the vendor's 0x68 has BGR set (its own colour
constants are BGR-ordered) and MX set (mirroring); we keep BGR off so plain
RGB565 constants map straight through and keep MX/MY off so the text is not
mirrored - a mirrored text screen would be unreadable.  With MV=1 the 0x2A
range is the horizontal axis (0..319) and 0x2B the vertical (0..239), which
is why set_window sends x to 0x2A and y to 0x2B.  We still leave the
vendor's 0xE4 and 0xC4 out: 0xE4 takes a single parameter, so the vendor's
three bytes would be parsed as two stray commands.

**Defect 19 - `trig_window_start()` could hand out a window past the end of
the buffer.**  It now takes the buffer length and clamps the start to
`buffer_len - window_len` (the host test now pins that case).

**Defect 20 - the build identity did not cover the display driver.**  The
`BUILD_SEQ` checksum only hashed `app/*.c` and `app/*.h`, so a driver change
altered the .bin while the panel's `S=` stayed the same - the flashed image
could not be told apart from the previous one.  It now hashes the driver and
the font table as well.

**Final image facts after this round.**

    build/dso183.bin   11824 bytes -> 12 pages of 1 KiB
    build sequence     53 (0x35)  -> the panel must read "B=4453424D S=35"
    flash              11824 B / 61440 B (19 %)
    RAM                 3672 B / 20480 B (17 %)
    make test          5/5 suites pass

Nothing was flashed; the device still runs the known-good v6 image.

## 2026-10-28 (i) - audit round 8: the text path was broken (glyph scaling) and the preview was lying

**Defect 21 (serious, would have ruined the screen) - `ili9341_draw_char()`
scaled the glyphs wrong and produced fewer pixels than the window it
opened.**  The old loop repeated each font pixel `scale` times in X and
`scale` times in X again (a nested horizontal loop) instead of repeating
the ROW in Y, and it never emitted the blank 6th column of the cell.
Consequences on the device: the characters were stretched 4x wide and not
tall at all, and after 8 rows the pixel count no longer matched the
window, so the text drifted and the rest of the cell was garbage.
The fix: the glyph rasteriser moved into a pure, host-testable module
`fonts/glyph.{c,h}` (a zero-RAM iterator: `glyph_iter_init()` /
`glyph_next()` produce the cell pixel by pixel, row-major, x fastest).
The driver now opens a 6*scale x 8*scale window and writes exactly
`48*scale*scale` pixels, each font pixel repeated as a scale x scale
block, with the blank column and blank row filled with the background
colour.  `tests/test_glyph.c` pins the rules: cell size, SQUARE scaling
(every pixel of the 2x cell is the 2x2 block of the 1x cell - the exact
property the old code violated), blank column/row, blank space, the
known 'A' glyph of the vendored font table, and fail-fast on NULL.

**Defect 22 - the build identity did not follow the sources it hashes.**
`build/build_seq.h` was regenerated only when `app/*.c` / `app/*.h`
changed, while the checksum itself hashes the driver and the font table
as well.  A driver-only change (like defect 21) left `S=` unchanged, so
the panel could not be told apart from the previous image.  The Makefile
now has one `DIGEST_FILES` list that feeds BOTH the checksum and the
generated header's prerequisites.

**Defect 23 - the offline preview had its own copy of the font logic.**
`tools/render_diag.py` re-implemented the 5x7 font in Python, so it
rendered the INTENDED picture while the driver produced garbage - that is
exactly why defect 21 survived the previous rounds (the preview looked
fine).  The preview is now rendered by the production modules:
`tools/panel_render.c` paints the whole 320x240 panel with `app/diag.c`
and `fonts/glyph.c` into `build/panel.ppm`, and the Python step only
converts PPM -> PNG.  The fixture values live in `tools/diag_fixture.h`
(shared by `tools/dump_diag.c` and `tools/panel_render.c`) and use the
REAL build sequence, so the preview shows the same `S=` the panel will.
Verified by eye: 10 green rows, correct 2x scaling, no drift, no
clipping.

**Final image facts after this round.**

    build/dso183.bin   11920 bytes -> 12 pages of 1 KiB (0x08000000..0x08002C00)
    vector table       SP 0x20005000, reset 0x08002705 (thumb bit set)
    build sequence     110 (0x6E) -> the panel must read "B=4453424D S=6E"
    flash              11920 B / 61440 B (19 %)
    RAM                 3672 B / 20480 B (17 %)
    make test          6/6 suites pass (acq_stats, diag, glyph, ladder, timebase, trigger)
    write_image.py --dry-run  12 pages, "dry run: nothing was written"

Nothing was flashed; the device still runs the known-good v6 image.

**The GPIOA hazard (from the audit rounds) is now downgraded to LOW risk.**
The vendor only touches GPIOA->CRL for pin 4 (functions at 0x08002504 /
0x0800251c clear bits 16..19 and set that nibble to a floating input) and
reads GPIOA->IDR bit 8 (PA8) at boot; the ADC nodes are PA2 (signal),
PA5/PA6/PA7 (ladder, injected conversions).  Nothing in the vendor
firmware drives PA0..PA7 as an output, so our `app/input.c` writing
`GPIOA->CRL = 0` (PA0..PA7 -> analog, the mode the ADC wants) cannot
silently disable a vendor output; at worst a panel LED driven by PA4
stays off.  CRH (PA8..PA15) is never touched, so the console pins and
SWD are unaffected.

## 2026-11-03 - status audit; TIM3 clock defect fixed

Audited the tree against the M0..M9 plan.  The current image is still the
diagnostic screen only: `acq_init`/`acq_cycle`, `trig_*`, `timebase_*`,
`acq_stats_compute` and `ui_*` are all unreferenced and removed by
`--gc-sections`, so no acquisition, trigger, timebase or waveform code was
present in the then-11,920-byte image.

**Defect fixed (P1): the timebase table was 2x fast.**  The table and
`tests/test_timebase.c` assumed a 32 MHz timer tick (31.25 ns/count).
`main.c` sets APB1 = HCLK/2 = 32 MHz, but on STM32F1 the timer clock is
2 x PCLK1 whenever the APB1 prescaler is not 1 (RM0008 clock tree), so
TIM3 is 64 MHz.  Every baked `(PSC+1)(ARR+1)` product was therefore half
the required count and every range would have displayed half the selected
time.  The table is now doubled for the 64 MHz tick, `TIMEBASE_TIM3_HZ`
is defined as 2 x `TIMEBASE_APB1_HZ`, `timebase_tick_ns()` uses
15.625 ns/count, and `test_timebase.c` asserts the clock derivation and
the new 84-count single-shot floor.  `make test` passes 7/7 (the new
`tests/test_ui.c` was added at the same time).

Also fixed in the same pass:

* `ui.h` used `UI_H_PIX` as the include guard, colliding with the panel
  height, and defined a redundant `UI_H_PIX_PIX`; `ui_in_grid()` ignored
  `x` and hardcoded the lower row bound.  The guard is now `UI_FRAME_H`,
  the geometry derives from `UI_GRID_W`/`UI_GRID_H`, and
  `tests/test_ui.c` covers the grid and the valid region.
* `input_init()` no longer polls the non-self-clearing ADON bit; it reads
  ADON back once and relies on RSTCAL/CAL (which do self-clear) as the
  liveness test.
* `dma_start()` now disables the channel before polling EN.  The old order
  could only time out when a transfer was already running.
* The dead bus-telemetry fields (`status_display_bus_probe`,
  `status_bus_log*`, `status_strobe_*`, `status_bus_idr_*`,
  `status_display_id_full`, `status_display_writes`) were removed instead
  of being left as a misleading API; `tools/flash_cycle.py` already tests
  for their presence before reading them.
* `acq.c` now fails the build if `ACQ_SIGNAL_CHANNEL` collides with any
  ladder channel.

Final image after these changes: 11,892 B flash (19 %), 3,672 B RAM
(17 %), build sequence 0x8F (143), md5
`7c7bad02936a3c94696e9dee2cdaaacd`, reset vector `0x080026E9`.  Nothing
was flashed; the board was still unreachable (ST-Link IDCODE error).

Still open (M5/M6 work, unchanged by this audit):

* `acq.c` programs SMP = 239.5 cycles (23.6 us/sample), while the
  `single_shot` flags assume the 1.5-cycle minimum (1.3125 us).  With the
  current setting single-shot is only possible from about 1 ms/div; M6
  needs per-range sample-time programming before the faster single-shot
  entries are usable.
* `acq_cycle()` polls with `HAL_Delay(2)` and does not freeze the circular
  DMA buffer while it is read.
* `input_scan()` reconfigures ADC1->SQR3 on every call and will conflict
  with the acquisition engine; the vendor uses injected ranks for the
  ladder.
* There is still no TIM3 driver: the table is correct, but nothing
  programs TIM3/TRGO to pace the ADC yet.
* `guard_greps.sh` lives only in the investigation tree and is not wired
  into this project's build.

## 2026-11-03 (later) - flash-path and bring-up fixes v2

The re-audit found that the *firmware* would boot, but the *flash
procedure* would fail: v6 runs a real ~8 ms IWDG (patch cave
`0x0800EC00`: `RLR=4`, `PR=/64`), and the writer never froze it, so a
20-40 ms page erase reset the target mid-write.  Worse, `write_image.py`
reset after **every** page, and page 0's reset vector (`0x080026E9`) lives
in page 9, so the first reset ran erased flash and locked up.  Fixed:

* `tools/flash/write_page.py` now sets `DBGMCU_CR` bit 8 (`DBG_IWDG_STOP`)
  **before** halting the core and aborts if the bit does not stick.  The
  freeze is deliberately left set (debug-only; it only stops the IWDG
  while the core is halted and is cleared by a power cycle).
* `tools/flash/write_image.py` now resets **only after the final page**
  (never mid-image) and the `--from-page` resume bug is fixed (it used the
  loop index instead of `first + i`, so a resume wrote page-0 content into
  the resumed region).
* `tools/flash/reset_run.py` (new) is the `SYSRESETREQ` + release step for
  the `--no-reset` flow; it is gated on `AUTHORIZED=1` and touches only
  the core debug registers.
* `startup/startup_stm32f103c8.s` now calls `SystemInit` (sets `SCB->VTOR`
  to the flash base and `SystemCoreClock`); previously the startup relied
  on `BOOT0=0` aliasing flash at `0x00000000` with VTOR left at 0.
* `app/stm32f1xx_it.c` no longer feeds the IWDG from `SysTick_Handler`; the
  feed is once per main-loop iteration.  Feeding from SysTick kept a hung
  main loop alive forever, so a lockup was never recovered by the watchdog.
* `app/board.h` `board_free_jtag_pins()` comment corrected: the value
  `0x02000000` (SWJ_CFG = `0b010`) is right; `0b001` is "full SWJ without
  NJTRST", which frees PB4 but leaves PB3 as JTDO - the observed `0x18`
  read-back was normal F1 behaviour, not a reversed clone encoding.

Final image after these changes: 11,964 B flash (19 %), 3,672 B RAM
(17 %), build sequence 0x0E (14), md5
`42ba287fac28a81e35e37bf66ffd6cde`, reset vector `0x0800272D`.  Nothing
was flashed; `AUTHORIZED` remains 0 and the board still runs v6.

## 2026-11-03 (later) - per-range ADC sample-time policy

`timebase_smp_code(index)` now selects the ADC SMP code per range instead
of one fixed value for all of them.  The rule is exact in integers: a
conversion with SMP code c takes (c_cycles + 12.5) ADC clocks, one ADC
clock is 6 TIM3 counts, so the interval must satisfy
`3 * (cycles_x2 + 25) <= tick counts`.  The policy keeps the shortest
aperture that still settles (7.5 cycles); only the fastest paced range
(50 us/div, 100 counts) drops to 1.5 cycles.  REPT-only ranges return
code 0, since TIM3 does not pace them.

`ACQ_SAMPLE_TIME` is now `ADC_SAMPLETIME_7CYCLES_5` and is actually used
by `acq_init()`; the previous fixed 239.5 cycles (22.5 us per conversion)
did not fit any range below about 1 ms/div.  `tests/test_timebase.c`
gains `test_smp_policy()`: the SMP table, the 50 us/div boundary, the
1 ms/div baseline, and a sweep proving every single-shot entry fits its
chosen aperture while REPT entries stay at code 0.

Build: 11,964 B flash (19 %), 3,672 B RAM (17 %), no project warnings,
`make test` 7/7.  Build sequence 0x06, md5
`357dd19d99c033d05db63c0c84533bf1`, reset vector `0x0800272D`
(unchanged).  Nothing was flashed.

## 2026-11-03 (later) - TIM3 pacing driver

The timebase table and the sample-time policy were already in place but
nothing programmed the timer.  Added the driver in the same split the rest
of the tree uses: arithmetic host-testable, registers in one small file.

* `app/tim3.{c,h}` - `tim3_config_for(index, &cfg)` hands out the baked
  `(PSC, ARR)` pair and `tim3_counts(cfg)` returns `(psc+1)*(arr+1)`.  A bad
  index returns 1 and leaves the caller's struct untouched rather than
  silently falling back to range 0 (which would pace at the wrong rate).
* `app/tim3_hw.{c,h}` - `tim3_hw_start()` enables the TIM3 clock, stops the
  counter, clears CR2/SMCR/DIER/CCER, writes PSC/ARR/CNT, latches with
  `EGR = UG`, clears the UIF that UG sets, then selects `CR2.MMS = 010`
  (TRGO on update) and enables the counter; `tim3_hw_stop()` clears CEN and
  switches TRGO off.
* `tests/test_tim3.c` - checks spot values (idx 0 = 0/1, idx 5 = 0/99,
  idx 14 = 1/49999, idx 26 = 15624/63999), the rejection paths (index 27,
  index 255, NULL struct, NULL to `tim3_counts`), and sweeps all 27 ranges
  against the table, including the 16-bit register-width contract.

The ADC is NOT slaved to TRGO yet: `acq_init()` still uses the software
start (`EXTTRIG = 0`, `SWSTART`) and its own free-running cycle.  Wiring
`ADC1->CR2` to `EXTSEL = 110` / `EXTTRIG = 1` is the remaining M6 step and
belongs with the buffer-freeze/trigger work, so that a change is measurable
in one go rather than two half-verified ones.

    build/dso183.bin   11964 bytes -> 12 pages (0x08000000..0x08002C00)
    vector table       SP 0x20005000, reset 0x0800272D
    build sequence     0xEF (239) -> panel must read "B=4453424D S=EF"
    md5                71761966d219225920f066240f526fe9
    flash / RAM        11964 B / 61440 B (19 %), 3672 B / 20480 B (17 %)
    make test          8/8 suites pass (acq_stats, diag, glyph, ladder,
                       tim3, timebase, trigger, ui)

Nothing was flashed; the board still runs v6.

## 2026-11-03 (later) - trigger window placement and copy

M6 needs a linear window with the trigger at a known column, but the DMA
buffer is circular and the write position is a free-running counter.
`app/acq_window.{c,h}` (pure logic) separates the two operations:

* `acq_win_place(buf_size, trigger_index, window, pre, &win)` - reduces the
  trigger counter mod the buffer, computes `start = (trig + size - pre) % size`
  and records `trigger_pos = pre`.  Rejects `window < 2`, `window > size`,
  `pre >= window`, a zero buffer and a NULL output, leaving the caller's
  struct untouched (same strictness as `tim3_config_for`).
* `acq_win_copy(buf, size, &win, dst, cap)` - bounded copy handling exactly
  one wrap, returning `min(length, cap)` (0 on bad arguments).

`tests/test_acq_window.c` covers the centred window, the wrap case
(trigger at index 2 with 4 pre-samples -> start 14, sequence 114,115,100..105),
trigger-on-first-column, a counter several buffer-lengths past the end,
the full-buffer window, cap truncation and every rejection path.  The
five failing assertions on the first run were the test's own arithmetic
(start is `trig - pre`, not `size - pre`); the module was correct.

    build/dso183.bin   11964 bytes -> 12 pages
    vector table       SP 0x20005000, reset 0x0800272D
    build sequence     0xC7 (199) -> panel must read "B=4453424D S=C7"
    md5                65b50940c995d0a684b29f478dc7b254
    flash / RAM        11964 B / 61440 B (19 %), 3672 B / 20480 B (17 %)
    make test          9/9 suites pass (acq_stats, acq_window, diag, glyph,
                       ladder, tim3, timebase, trigger, ui)

Still not wired into `acq.c`: it keeps reading its live buffer.  The freeze
point (stop DMA1 channel 1, copy, restart) is hardware behaviour and goes
in with the TRGO switch so the bench session tests one change.

Nothing was flashed; the board still runs v6.

## 2026-11-03 (later) - explicit ADC1 ownership token

Audit finding behind this step: `acq.c` runs ADC1 continuously through DMA
while `input_read_channel()` rewrites `SQR3` for a one-shot ladder read.  If
both run, the DMA stream is retargeted at a ladder channel and both results
are silently wrong.  The vendor avoids this by reading the ladder from ADC1's
INJECTED ranks (JDR1..3); that refactor is still open, so the conflict is now
made explicit instead of silent:

* `app/adc_owner.{c,h}` - a single token with owners `NONE`, `ACQ`, `INPUT`.
  `adc_owner_acquire()` refuses when another owner holds it, is idempotent
  for the current owner, and rejects `NONE`/out-of-range values; the token
  counts no references, so one release frees it.  `adc_owner_release()`
  refuses a non-owner (and any invalid value), so a stray release cannot
  clear someone else's claim.  `adc_owner_reset()` is the boot path.
* `app/input.c` - `input_read_channel()` takes `ADC_OWNER_INPUT` before
  touching `SQR1/SQR3` and releases it on every exit (success and EOC
  timeout).  When acquisition holds the ADC it returns -1, and `input_scan()`
  already records `INPUT_ADC_INVALID` for that channel.
* `app/acq.c` - `acq_init()` takes `ADC_OWNER_ACQ` for its whole lifetime and
  returns `ACQ_HW_ERROR` (incrementing `status_acq_hw_errors`) if the ladder
  reader holds it.
* `tests/test_adc_owner.c` - free -> acq -> input refused -> idempotent
  re-take -> wrong-owner release refused -> acq release -> input takes and
  releases -> invalid values refused in both directions -> reset clears.

The first test run caught a real semantic hole: `adc_owner_release(NONE)`
succeeded when nothing was held, because the comparison `s_owner != who` is
false for two `NONE` values.  `release()` now rejects invalid owner values
first.

    build/dso183.bin   12040 bytes -> 12 pages (capacity 12288, 248 spare)
    vector table       SP 0x20005000, reset 0x08002779
    build sequence     0xC6 (198) -> panel must read "B=4453424D S=C6"
    md5                f7c5301ed2e917dd34989995c5ced327
    flash / RAM        12040 B / 61440 B (19 %), 3672 B / 20480 B (17 %)
    make test          10/10 suites pass (acq_stats, acq_window, adc_owner,
                       diag, glyph, ladder, tim3, timebase, trigger, ui)

Note: the image now reaches 12,040 B, i.e. 98 % of the 12-page span the
current writer touches (every page up to 0x08002FFF).  Adding the acquisition
engine to the image will need the page count raised deliberately (the writer
derives it from the image size), still well inside the 60 KiB application
area.

Nothing was flashed; the board still runs v6.

## 2026-11-03 (later) - integer waveform measurements

`app/measure.{c,h}` closes the measurement half of M6 with no floating point
and no HAL, because the F1 has no FPU and a soft-float `sqrt()` would cost
flash and unbounded cycles in the display path:

* `measure_compute(samples, n, out)` fills `min`, `max`, `vpp`, `mean`, `rms`
  and the used count `n`.  Sums are 64-bit (`1024 x 4095^2` does not fit in
  32), the mean square is rounded to nearest before the root, so a symmetric
  square wave reads its true midpoint instead of one count low.
* `measure_isqrt()` is the classic restoring bit-by-bit integer root
  (`1 << 30` seed, 16 iterations); it is exported so the tests can check it
  against exact squares (0, 1, 4, 15, 16, 4095^2, 4095^2 - 1, `0xFFFFFFFF`).
* Rejections are explicit and clear `out->n`: NULL buffer, NULL out, `n == 0`.
  A stale window can never be silently re-reported.

Tests (`tests/test_measure.c`) cover exact squares, a flat 1000-count window
(min = max = mean = rms), a full-swing square `{0,4095,0,4095}`
(mean 2048, rms 2895, vpp 4095), a single sample, a 256-sample pseudo-random
window checked against a `double` reference within one count, and every
rejection path.  The host link gained `-lm` for that reference only; the
target build never links libm.

    build/dso183.bin   12040 bytes -> 12 pages
    vector table       SP 0x20005000, reset 0x08002779
    build sequence     0x73 (115) -> panel must read "B=4453424D S=73"
    md5                2348e6606fd1749fead491cf95372f0f
    flash / RAM        12040 B / 61440 B (19 %), 3672 B / 20480 B (17 %)
    make test          11/11 suites pass (acq_stats, acq_window, adc_owner,
                       diag, glyph, ladder, measure, tim3, timebase,
                       trigger, ui)

`measure.c` is not referenced by the image yet, so `--gc-sections` still
drops it; the size is unchanged and only the identity byte moved.  It becomes
live when the acquisition path starts handing its window to the renderer.

Nothing was flashed; the board still runs v6.

## 2026-11-03 (later) - frame assembly seam

`app/frame.{c,h}` turns "ring buffer plus trigger index" into "linear window
plus measurements" in one call, so the hardware path only has to hand over
three numbers (`ring`, `ring_size`, `trig_index`) and the arithmetic stays
host-testable:

    uint8_t frame_build(ring, ring_size, trig_index, window, pre,
                        dst, cap, struct frame *out);

* Returns 0 on success, 1 for a parameter error (all of `acq_window`'s
  rejections propagate unchanged; `out->length` and `out->stats.n` are
  cleared first so a caller cannot read the previous frame by accident), and
  **2 for a valid window that the destination truncated**.  Truncation is
  reported rather than hidden: a short frame otherwise looks perfectly
  plausible on screen and in the readouts.
* On failure the statistics are computed over the samples actually copied,
  so `stats.n` always describes what is in `dst`.

Tests (`tests/test_frame.c`): a straight window (trigger 8, window 8, pre 4
-> indices 4..11, min 104 / max 111 / Vpp 7 / mean 108), a wrapping window
(trigger 2 -> 114, 115, 100..105), the free-running DMA counter (one and four
whole laps later must give the identical window and statistics), truncation
to two samples (returns 2, `stats.n == 2`), and every rejection path
including a window larger than the ring and `pre == window`.

The first run failed on `mean == 107`: 104..111 sums to 860 over 8 samples =
107.5, and `measure_compute()` rounds half up, so 108 is correct.  The test
was wrong, not the module.

    build/dso183.bin   12040 bytes -> 12 pages
    vector table       SP 0x20005000, reset 0x08002779
    build sequence     0xE5 (229) -> panel must read "B=4453424D S=E5"
    md5                11eeba5c80163dd89a5b0dc1fa6af6bc
    flash / RAM        12040 B / 61440 B (19 %), 3672 B / 20480 B (17 %)
    make test          12/12 suites pass (acq_stats, acq_window, adc_owner,
                       diag, frame, glyph, ladder, measure, tim3, timebase,
                       trigger, ui)

`frame.c` is still unreferenced by the image, so the size is unchanged and
only the identity byte moved.  It becomes live with the TRGO/freeze work.

Nothing was flashed; the board still runs v6.

## 2026-11-03 (later) - waveform column decimation and y mapping

`app/waveform.{c,h}` is the last pure layer before real drawing:

* `wave_decimate(samples, n, cols, mins, maxs, cap)` buckets the window into
  `cols` columns using `start = i*n/cols`, `end = (i+1)*n/cols` and keeps the
  **min and max of each slice**, because that is what makes a one-sample
  spike survive on screen.  Sampling one point per column or averaging would
  lose it.  Slices are always non-empty: when `n < cols` an empty slice is
  clamped to the nearest existing sample, so a short window repeats columns
  instead of reading past the buffer.  `cap` bounds the writes and the return
  value is the number of columns actually written.
* `wave_y(count, range, geom)` maps counts to pixel rows with the panel's
  downward y axis (higher count -> higher on the screen).  Out-of-range
  counts clamp to the grid edge; `v_max <= v_min` or a NULL range returns the
  grid centre instead of dividing by zero; a geometry shorter than two rows
  returns `y_top`.
* No HAL and no floating point: the renderer only has to draw a vertical bar
  from `wave_y(max)` to `wave_y(min)` per column.

Tests (`tests/test_waveform.c`) cover: even 100/10 bucketing, the short
window (4 samples into 8 columns), cap truncation, the spike surviving in
column 1, every rejection path, and the y mapping at both rails, near the
centre (within one pixel of 107), outside the range, on a flat range, with a
NULL range and with a 1-pixel geometry.

    build/dso183.bin   12040 bytes -> 12 pages
    vector table       SP 0x20005000, reset 0x08002779
    build sequence     0x51 (81) -> panel must read "B=4453424D S=51"
    md5                ceffd68f40206a62708b90d28240f5fd
    flash / RAM        12040 B / 61440 B (19 %), 3672 B / 20480 B (17 %)
    make test          13/13 suites pass

`waveform.c` is still unreferenced, so the size did not move; only the
identity byte did.  It becomes live with the M7 renderer.

Nothing was flashed; the board still runs v6.

## 2026-11-03 (later) - calibration maths (application, not storage)

`app/cal.{c,h}` is the last link between counts and a number a user can
believe, and it is deliberately separate from the flash store (M8a still
owns reading/writing 0x0800F800):

    uV = offset_uv + (raw * gain_q16) >> 16

* `cal_fit()` derives that pair from two measured points, so the bench
  procedure is "apply a known voltage twice" instead of guessing a divider
  ratio.  Both raw values must be 0..4095 and distinct; the slope may be
  negative (an inverting front end is legitimate); a gain or offset that
  cannot fit an int32 Q16 is refused rather than stored, because such a
  value would saturate every reading.
* `cal_raw_to_uv()` saturates at +/-2 V.  Wrapping would turn a fault into a
  plausible-looking voltage, which is the worst possible display failure.
* `cal_uv_to_mv()` rounds toward zero for the readout; it is not used by any
  further arithmetic.
* `cal_identity` (0 offset, 1 uV/count) is the explicit "never calibrated"
  state, so the screen shows raw counts scaled 1:1 instead of inventing
  accuracy.

Tests (`tests/test_cal.c`): exact identity mapping, a 1 uV/count fit, a
10 uV/count fit with a -1000 uV offset, an inverting fit, argument order
independence, all fit rejections (zero span, raw > 4095, NULL output,
overflowing slope) with `*out` proven untouched, both saturation rails, and
the microvolt/millivolt rounding at the boundaries.

    build/dso183.bin   12040 bytes -> 12 pages
    vector table       SP 0x20005000, reset 0x08002779
    build sequence     0xFE (254) -> panel must read "B=4453424D S=FE"
    md5                5b1b3a3347b02f1d3461661b0c661e91
    flash / RAM        12040 B / 61440 B (19 %), 3672 B / 20480 B (17 %)
    make test          14/14 suites pass

Still unreferenced, so the size did not move - but the pure layer is now
complete: acquire (tim3_hw) -> place/copy (acq_window) -> measure ->
calibrate -> decimate/map (waveform) -> draw.  The build sequence is at
0xFE and wraps to 0x00 on the next rebuild; that is harmless (it is an
identity byte, and 0x00 still differs from every older image) but worth
noticing when reading the panel.

Nothing was flashed; the board still runs v6.

## 2026-11-03 (later) - renderer seam

`app/render.{c,h}` closes the pure pipeline.  `render_columns()` takes the
sample window, the count range, the grid geometry and a column offset, and
emits one vertical bar per column through a `render_vline_fn` callback
(vline, not pixel: the display driver owns timing and the bus, this module
owns geometry).

* `mins`/`maxs` scratch is supplied by the caller, so the RAM budget stays
  visible at the call site instead of hiding in a static buffer.
* A NULL draw callback returns 0 rather than dereferencing it - a missing
  hook must be a silent no-op, never a crash on a board with a watchdog.
* If `wave_y()` ever returns the pair the wrong way round (a future range
  change), the bar is swapped rather than inverted, so nothing can be drawn
  outside the grid.

Tests (`tests/test_render.c`) use a recording sink: four columns from x = 10
in order, every bar inside the grid and `y_top <= y_bottom`, column 0 ending
on the bottom row (199) and column 3 starting at the top (16), more columns
than samples (each still drawn), a NULL range collapsing every bar onto row
108, and all parameter rejections leaving the sink untouched.

    build/dso183.bin   12040 bytes -> 12 pages
    vector table       SP 0x20005000, reset 0x08002779
    build sequence     0x6C (108) -> panel must read "B=4453424D S=6C"
    md5                adfa74beb4561d3379e9a6c80e258ed4
    flash / RAM        12040 B / 61440 B (19 %), 3672 B / 20480 B (17 %)
    make test          15/15 suites pass

The pure layer is now complete end to end:

    tim3 (pace) -> acq_window (place/copy) -> measure (stats)
      -> cal (counts to uV) -> waveform (decimate/map) -> render (bars)

Every stage is host-tested and none of them is referenced by the image yet,
which is why the size has not moved since 12,040 B.  The next change that
wires them in (acq.c slaving ADC1 to TIM3 TRGO, freezing DMA on trigger and
calling frame_build/render_columns) is hardware-visible and needs the bench.

Nothing was flashed; the board still runs v6.

## 2026-11-03 (audit pass 3) - read the whole tree for reasons it would not work

Scope: every source file in `app/`, `drivers/display/`, `startup/`, the
linker script, the Makefile flags and the three flash tools.  Purpose: find
what would stop the flashed image from working, not to add features.  Method:
read the code and the vendor evidence, then prove each suspicion against the
artefact (vector bytes, map file, register values), and only then annotate.

**Findings that were real, and are now fixed**

1. *P1 - acquisition timeout could never be honoured.* `acq_cycle()` waits
   with `HAL_Delay()` and feeds nothing; `SysTick_Handler` deliberately does
   not feed the IWDG either (so a hung main loop still resets).  With
   `ACQ_MAX_WAIT_MS = 60000` any timeout above ~10 s was guaranteed to be cut
   short by a watchdog reset, and the header comment claimed the opposite.
   Now `ACQ_MAX_WAIT_MS = 5000`, with
   `_Static_assert(ACQ_MAX_WAIT_MS < ACQ_IWDG_TIMEOUT_MS)` so the relationship
   cannot drift again.  5 s is also below the worst-case IWDG period: 40 kHz
   nominal LSI gives 10 s, but the F1 LSI spreads 30..60 kHz, i.e. 6.7 s to
   13.3 s, so 5 s is safe at the fast end and the slow end only lengthens it.
2. *P2 - conversion-rate comments were wrong in two directions.* With
   SMP = 7.5 cycles a conversion is 20 ADC cycles = 1.875 us at 10.67 MHz,
   i.e. ~530 kS/s; the comments still said 42 kS/s (239.5 cycles) and one said
   15.2 us.  All corrected, including the aperture claim (0.70 us).
3. *P2 - trigger index is not exact.* `acq_isr_adc()` reads `CNDTR` at ISR
   entry, so the latched position lags the true crossing by the
   ADC-to-interrupt latency.  The comment now says so, names it open item
   U-04b, and says to measure it with a known square before trusting
   sub-sample placement.  Pretending it was exact would have produced a
   mysterious constant time offset on every capture.
4. *P2 - the wait helpers only work after `HAL_Init()`.* Their deadline comes
   from `HAL_GetTick()`; a future pre-HAL_Init caller would spin forever.
   Documented at the top of `acq.c` rather than changed, because the current
   call path is correct and a silent "just use a counter" rewrite would lose
   the SysTick-based bound.
5. *P3 - display pin-mode comment contradicted the code.* The vendor's
   `GPIOB->CRH` is `0x88333433` (PB12/PB13 outputs); this driver writes
   `0x88883433` (PB12..PB15 inputs).  The difference is deliberate - an
   unknown net must not be energised - and is now documented instead of
   looking like a typo.  PB8/PB9/PB10/PB11 and all of GPIOC match the vendor.

**Suspicions that turned out to be fine (and why)**

* *Reset loop before the watchdog is reconfigured.* IWDG registers survive a
  system reset, and v6 leaves a ~8 ms period, so a freshly programmed image
  could reset before reaching `board_iwdg_init()`.  It cannot: `main()`
  configures the 10 s IWDG at line 156 and only then touches the display
  (which is where the long `HAL_Delay()` calls live).  The whole init chain
  before that point is register writes with no delay.
* *The reset pulsing the display is never driven.* `GPIOB->CRH` nibble for
  PB9 is `3` (push-pull output), so the pulse is real; an earlier note
  claiming otherwise was reading the nibbles in the wrong order.
* *PCLK2 left unconfigured.* `clock_config()` omits PCLK2 from `ClockType`,
  but `HAL_RCC_ClockConfig()` clears PPRE2 in its CFGR mask and writes the
  divider back anyway, so PCLK2 = 64 MHz and both the ADC /6 and the USART1
  BRR agree with the code's assumptions.
* *USART1 receive pin driven.* PA10 is configured as an input with pull-up,
  never an output, so a second driver on the console line cannot be fought.
* *Vector table gaps.* DMA1_Channel1, ADC1_2, TIM3 and USART1 all occupy the
  correct slots; the two that are implemented are wired, and TIM3/USART1
  fall to `Default_Handler`, which is acceptable because neither interrupt is
  enabled yet.
* *DMA disable order.* `dma_start()` clears `CCR` before polling `EN` (fixed
  earlier), so the "always times out when already running" trap is gone.

**Still open (needs the bench, or a deliberate design decision)**

* `trig_window_start()` and `acq_win_place()` are two placement functions.
  The first clamps and never wraps; the second wraps with modulo and is what
  `frame_build()` uses.  A late trigger therefore produces a valid but
  trigger-less window if someone wires the wrong one.  Reconcile before the
  M5 integration: `acq_win_place()` is the intended single source.
* `acq_cycle()` reads the circular buffer while DMA keeps writing it, so a
  frame can contain samples from either side of the read.  Documented as
  M5-acceptable and to be replaced by the frozen-window path (stop DMA1
  channel 1, `frame_build()`, restart) together with the TRGO switch.
* Nothing in the image exercises the new pure layer yet, so a flash today
  verifies the diagnostic screen only - not acquisition, timing or drawing.

**Verification state.** `make test` 16/16 suites; `make check` (build + tests
+ size + `tools/check_docs.py`) passes; image 12,040 B over 12 pages, seq
0x9A, md5 `b67e06c797933901b22a71c4676a6d24`, reset `0x08002779`.  Nothing
was flashed.

## 2026-11-03 - bench contact: ST-Link and UART verified (read-only)

The instrument was connected (ST-LINK/V2 `12010C1C8418303030303032`, UART on an
FT232R at `/dev/ttyUSB0`).  Nothing was flashed, no reset, no write: every
read below is a plain memory read through the AHB-AP while the vendor
firmware kept running.

**Probe.** `DBGMCU_IDCODE = 0x20036410` (STM32F103 medium density, rev A),
`CPUID = 0x412FC231` (Cortex-M3 r2p1).  The vector table at `0x08000000`
reads SP `0x20005000`, reset `0x0800177D` - i.e. the board is running the v6
image, not ours, exactly as expected.

**The v6 watchdog is as dangerous as the analysis said.**
`IWDG_PR = 0x000009C4`, `IWDG_RLR = 0x00000004` -> ~8 ms nominal.  The freeze
in `write_page.py` (`DBGMCU_CR` bit 8, `DBG_IWDG_STOP`, verified by read-back
before halting) is therefore mandatory, not defensive: an erase takes tens of
milliseconds.

**The vendor runs from HSE, not HSI.** `RCC_CR = 0x03038083` (HSE and PLL
locked), `RCC_CFGR = 0x001D400A` (SW = PLL, PLLSRC = HSE, APB1 /2, PCLK2 /1)
-> PCLK2 = 72 MHz, APB1 = 36 MHz.  Our firmware reconfigures to HSI/2 x 16 =
64 MHz, which is a deliberate, independent choice; it changes nothing about
the 115200 console because the HAL derives BRR from the real PCLK2.

**UART.** `USART1_CR1 = 0x0000202C` (UE, TE, RE, RXNEIE all set),
`BRR = 0x271` (625) -> 72 MHz / 625 = 115200 exactly, `SR` with TXE and TC
set, no ORE/FE observed.  Our adapter opens 8N1 at 115200 and receives
silence: the vendor console does not greet on a bare CR/LF, and its RX
interrupt drains `DR` immediately, so error flags cannot be caught after the
fact.  Silence on v6 is therefore expected and proves nothing either way; the
first real UART evidence will be our own `DSO183 DIAG OK` banner.

**ADC configuration read live - the ladder map was wrong.**
`ADC1_CR2 = 0x0098F101` -> ADON=1, DMA=1, CONT=0, EXTTRIG=1 with EXTSEL=4
(TIM2_CC2): the vendor paces one conversion per external trigger from TIM2,
not TIM3.  That matters only as evidence about the vendor; our TIM3 TRGO
choice does not collide with anything.
`ADC1_SQR3 = 0x00000002` -> the regular group converts **channel 2 (PA2)**,
which confirms `ACQ_SIGNAL_CHANNEL = 2`.
`ADC1_CR1 = 0x00800342` -> SCAN=1, AWDEN=1, AWDCH=2: the analog watchdog also
monitors channel 2, same as our design.
`ADC1_SMPR2 = 0` -> the vendor samples at the 1.5-cycle minimum.
`ADC1_JSQR = 0x002394C0` with JL=2, which decodes to **JSQ1 = 0, JSQ2 = 6,
JSQ3 = 5** and JSQ4 = 7 (unused, because JL=2 means three ranks).  The ladder
channels are therefore **0 (SEN1), 6 (SEN2), 5 (CPL)** - the earlier read of
the same word had slid the ranks by one and produced 6/5/7.  Fixed in
`app/ladder.h`, `tests/test_ladder.c`, the `main.c` comment and both README
rows; the raw values stay on screen, so a wrong rank order is still visible.

    make test    16/16 suites pass
    image        12,040 B / 12 pages, seq 0x1D (29), md5
                 1b27119ef0df303e5e8e9bfdc1528fde, reset 0x08002779
    make check   green (docs re-verified against the rebuilt image)

Still nothing flashed.  What is now verified from real hardware: the chip,
the probe path, the v6 watchdog hazard, the vendor clock tree, the console
baud, the signal channel, the ADC pacing source, and the ladder channel map.
What is still open: whether our TX physically reaches PA10 (needs a firmware
that answers), the display read path, and everything that depends on our own
image running.

## 2026-11-03 (later) - ADC paced by TIM3 TRGO, not SWSTART

`acq.c` no longer free-runs: `ADC1->CR2` is programmed with
`EXTTRIG=1`, `EXTSEL=101` and `CONT` **clear**, so one TIM3 update event
makes TRGO pace exactly one conversion. Two corrections came with it:

* **EXTSEL 101 is TIM3_TRGO** (RM0008 Table 79: 100 = TIM2_CC2,
  101 = TIM3_TRGO, 110 = TIM4_CC4). An earlier note in `acq.h` said 110 for
  TIM3, which is TIM4_CC4 - a timer this firmware never starts, so the ADC
  would have sat silent while every register read back "configured". The
  constant now lives in `ACQ_EXTSEL_TIM3_TRGO` with the table quoted above it.
* **The bench already told us the vendor's pacing source**: live ADC1_CR2
  reads 0x0098F101, i.e. `EXTTRIG=1`, `EXTSEL=4` = TIM2_CC2, `CONT=0`. So
  the vendor samples on TIM2 and our choice of TIM3 cannot collide with it.

New in `acq.h`/`acq.c`:

    #define ACQ_EXTSEL_TIM3_TRGO  5U    /* 101 */
    #define ACQ_DEFAULT_TIMEBASE  9U    /* 1 ms/div until told otherwise */
    #define ACQ_START_MS         20U
    enum acq_status acq_set_timebase(uint8_t index);

`acq_set_timebase()` builds the PSC/ARR pair through `tim3_config_for()` (the
host-tested module from the previous step), stops the timer, restarts it with
the new period and records the index. It returns `ACQ_PARAM` for an index
outside 0..26 and `ACQ_NOT_INITED` before `acq_init()`, so a caller cannot
silently get the default rate.

`acq_init()` now starts the timer instead of issuing `SWSTART`, and then
**proves** conversions started: `wait_cndtr_changed()` polls `DMA1_CNDTR`
against a 20 ms deadline and returns an error if it never leaves
`ACQ_SAMPLES`. A running timer with a dead ADC, or a trigger source the ADC
does not accept, both look perfectly configured on a register dump; the DMA
counter does not lie. Every failure path now parks the channel through
`dma_stop_and_park()` (disable + bounded ack) instead of leaving it armed.

The now-unused `wait_bit_set()` helper was deleted (the `STRT` wait was its
last caller), so the file still builds without an unused-function warning.

    build/dso183.bin   12040 bytes -> 12 pages
    vector table       SP 0x20005000, reset 0x08002779
    build sequence     0x8B (139) -> panel must read "B=4453424D S=8B"
    md5                45780837c6c818bef89f564db27ce935
    flash / RAM        12040 B / 61440 B (19 %), 3672 B / 20480 B (17 %)
    make test          16/16 suites pass

`acq.c` is still unreferenced by `main()`, so the image size barely moved and
the new pacing path is not yet exercised on hardware. That is the next step
and it needs a flash.

Nothing was flashed; the board still runs v6.

## 2026-11-03 (later) - the acquisition engine goes live (bench build)

Until now every new module was dead weight: `main()` never called it, so
`--gc-sections` dropped it and the image proved nothing.  This step makes the
path live end to end and prints what it sees on the console, because there is
no waveform renderer yet:

    main() -> acq_init() -> acq_set_timebase(index 9, 1 ms/div)
           -> acq_cycle(1000 ms) -> acq_samples() + gate.trigger_index
           -> frame_build(ring, 1024, trig, 256, 64, dst, 256, &frame)
           -> bench_format_stats() / bench_format_samples() -> USART1

* `app/bench.{c,h}` (pure, host-tested): `bench_format_stats()` emits one line
  `acq ok <cycles> to <timeouts> err <errors> | n .. min .. max .. vpp ..
  mean .. rms ..`; `bench_format_samples()` emits `smp a b c ..`.  Both
  NUL-terminate on every path, report truncation as an error instead of
  cutting a number in half, and never print an unterminated buffer.
* `app/main.c`: `acq_bench_init()` starts the engine and records
  `STATUS_ERR_ACQ` (new bit 0x10, mask widened to 0x1F) on failure without
  stopping the diagnostic screen or the console; `acq_bench_poll()` runs one
  cycle per loop, counts timeouts and hardware errors, re-initialises once
  after an error, parks the engine if that fails, and prints the first
  frame's first eight samples once.
* Timing: the 1000 ms cycle wait plus the 500 ms loop delay keeps the IWDG
  feed interval around 1.5 s against a 10 s period (6.7 s worst case with an
  LSI at 60 kHz), so a dead input yields `to` counters, not a reset.
* Deliberate consequence: acquisition takes ADC1's ownership token for its
  lifetime, so `input_scan()` now returns invalid for every ladder channel
  and screen rows 5..7 show `3` / `FFFF`.  That is the ownership guard, not a
  dead front panel; the ladder moves to injected ranks in M6.

**Build sequence widened from 8 to 16 bits.**  `BUILD_SEQ` was
`cksum % 256`, and this build genuinely collided with the previous one at
29 (verified by recomputing cksum).  With the panel row printing `%02X` that
would have made "is the right image running?" unanswerable.  `diag.c` now
prints `S=%04lX` (`B=4453424D S=3786`), `BUILD_SEQ` is `cksum % 65536`,
`tests/test_diag.c` and `tools/check_docs.py` expect four digits.

    build/dso183.bin   15140 bytes -> 15 pages (0x08000000..0x08003BFF)
    vector table       SP 0x20005000, reset 0x080031E9
    build sequence     0x3786 (14214)
    md5                a35cde601822f204a054b5494323c275
    flash / RAM        15140 B / 61440 B (24 %), 6256 B / 20480 B (30 %)
    make test          17/17 suites (bench added)
    make check         OK: FLASH_HANDOVER.md matches the built image

Nothing was flashed.  The board still runs v6; this is the first image whose
behaviour is worth a bench session.

## 2026-11-03 (bench) - acquisition live on hardware: first working image

The bench build now runs on the instrument. Boot log (115200 8N1,
`/tmp/opencode/boot5.log`):

    22.626  DSO183 DIAG OK
    24.628  acq ok
    25.029  diag screen ok
    25.029  alive
    26.030  acq ok 1 to 1 err 0 | n 256 min 975 max 979 vpp 4 mean 977 rms 977
    26.030  smp 976 977 978 978 978 978 979 978
    ...

SWD state while it runs: `status_init_errors` 0, `status_acq_hw_errors` 0,
`TIM3_CR1` 1, `TIM3_ARR` 1999, `DMA_CNDTR` 0 (a full circular lap),
`ADC_CR2` 0x00180101 (ADON, DMA, EXTTRIG, EXTSEL = 100).

Three defects had to be fixed to get here, and all three were real:

1. **IWDG register offsets swapped** (`board_iwdg.c`): the header mapped
   RLR to +0x04 and PR to +0x08, the reverse of RM0008. Every build wrote
   the prescaler index into RLR and the 3125 reload value into PR, which
   keeps only three bits - programming (RLR=5, PR=/128) = 19.2 ms instead of
   10 s. The board reset before `diag screen ok` could be printed. Proven by
   writing 0xABC to +0x04 and 3 to +0x08 while halted: +4 read back 4
   (3-bit field), +8 read back 3 (12-bit field).
2. **`s_inited` was set after `acq_set_timebase()`** (`acq.c`):
   `acq_set_timebase()` refuses with `ACQ_NOT_INITED` unless the engine is
   marked initialised, but `acq_init()` only set the flag at its end - after
   calling it. Every boot took the failure path (DMA parked, TIM3 never
   clocked). The flag is now set before the call and cleared again on either
   later failure.
3. **EXTSEL was wrong twice** (`acq.h`): first 110 and then 101, both of
   which are TIM4_CC4, not TIM3_TRGO. The ADC waited forever on a timer that
   never fires, so the DMA count never moved. The correct value is **4**
   (100 = TIM3_TRGO), which is also what the vendor image uses.

Also corrected on the way: the inverted `tim3_hw_start()` return check
(0 = success, was treated as failure), and the IWDG shadow-register
handshake (write PR, wait PVU, write RLR, wait RVU - the old code wrote both
back to back, so one write was silently dropped even after the offsets were
fixed).

**What the readouts mean right now.** The input is a quiet DC level of about
977 counts (~0.79 V), below the 2048-count trigger level, so every cycle ends
via the timeout path: `to` equals the cycle count while `err` stays 0 and the
window is still valid. That is the AUTO-mode fallback working as designed.
Feeding a signal that crosses 2048 counts should make `to` stop climbing.

The ladder rows read as invalid while acquisition runs (the ADC1 ownership
token working as intended); the ladder moves to injected ranks in M6.

    build/dso183.bin   15180 bytes -> 15 pages
    vector table       SP 0x20005000, reset 0x08003211
    build sequence     0x8899 (34969) -> panel reads "B=4453424D S=8899"
    md5                8add76ef81657acb0ae9f5fd3041d358
    make test          17/17 suites

## 2026-11-03 (bench) - MADCTL 0x20 -> 0x68: the mirror is gone

The first flashed image drew correctly mirrored: every line read right-to-left,
rows in the right order.  A camera snapshot flipped horizontally was perfectly
readable, which identifies the fault as MX (column mirror), not MY and not a
90-degree rotation - the glyphs themselves were always correct.

The driver's own header comment was the smoking gun: line 94 already recorded
`MADCTL: the vendor writes 0x68 (MX|MV|BGR) at 0x08001d64`, while the init
table sent `0x36, 1, 0x20` (MV only).  The vendor constant had been copied
without its MX bit.

    ili9341_init():   0x36, 1, 0x20   ->   0x36, 1, 0x68

Verified on hardware: two snapshots after flashing show upright,
left-to-right text with the rows in the correct order.  `status_build_seq`
read over SWD is 12284 (0x2FFC), matching FLASH_HANDOVER.md.

**Still open: the display READ path.**  Row 3 shows `ID 0000 M 08`, and
`status_display_id` reads 0xD3000000 - the first byte captured over the bus is
the command byte 0xD3 itself.  So `lcd_read_cmd()` latches the command
shift-register content before the panel drives the bus, and every following
byte is shifted by one; MADCTL reads back 0x08 for the same reason.  The fix
is a dummy read and a D/C-to-/RD settling gap, which is the next step.

    build/dso183.bin   15180 bytes -> 15 pages
    vector table       SP 0x20005000, reset 0x08003211
    build sequence     0x2FFC (12284)
    md5                64ba7c7ab0d087d32247691bf3f8c3a9
    make test          17/17 suites pass

## 2026-11-03 (bench) - display read path fixed: the command echo byte

The diag row had been showing `ID 0000 M 08` since the first flash. The bus
itself was healthy - proved without flashing anything by bit-banging the read
sequence from the debugger with the core halted (`/tmp/opencode/lcd_read_test.py`):

    0x0B -> 0B 68 00        MADCTL really is 0x68 (the mirror fix took)
    0x04 -> 04 85 85 52     controller ID is 0x858552, not 0x00934108
    0x09 -> 09 B4 53 04     display status
    0x0A -> 0A 9C 00        power mode
    0x0C -> 0C 05 00        pixel format

**Every command returns its own command byte first**, then the data. The
driver was treating that echo as the first data byte, so every value was
shifted one byte left: `0x0B 0x68` became `0x0B08` (`M 08`) and the ID
became zero.

Fix: `lcd_read_cmd(cmd, count)` now clocks out and discards one dummy byte
before reading `count` real bytes; `ili9341_read_id()` uses `0x04` with three
bytes (0xD3 answers only zeros on this panel) and `ili9341_read_madctl()`
uses `0x0B` with one byte. Diag row 3 prints the raw measured values
(`ID %06lX M %02lX`).

Verified on hardware after one flash (S=0CAF): panel row 3 reads
`ID 858552 M 68`, SWD `status_display_id` = 0x00858552, `ERR 00`, loop and
heartbeat advancing, text upright and left-to-right.

An important correction for anyone reading older notes: this board does NOT
carry an ILI9341. It answers 0x858552 (a compatible controller) and accepts
the 9341 init sequence; the ID expectation in every older document was wrong.

    build/dso183.bin   15192 bytes -> 15 pages
    vector table       SP 0x20005000, reset 0x08003225
    build sequence     0x0CAF (3247) -> panel reads "B=4453424D S=0CAF"
    md5                3cab849173148a84ff9c5244ba44b6be
    flash / RAM        15192 B / 61440 B (24 %), 6256 B / 20480 B (30 %)
    make test          17/17 suites pass

## 2026-11-03 (bench) - M7: first live trace on the panel

The image now draws instead of only reporting: `frame_build()` feeds
`render_columns()`, whose vertical-line callback is `ili9341_fill_rect()`.
The plot area is cleared, the 10x8 grid is redrawn, and one bar per screen
column is plotted from the min/max of the samples that fall into it, so a
single-sample spike still survives.

Hardware evidence (image seq 0xE500, 16 pages):

    UART   acq ok 29..33 to <same> err 0 | n 256 min 971..977 vpp 3..6 ...
    panel  grid + trace in the upper-middle band, ~1.5 s per update

Two honest observations from the first photo:

* The trace looks dashed, and that is arithmetic, not a rendering bug: the
  window is 5 counts tall, padded to ~14, mapped onto 184 px, so one count
  is ~13 px and ordinary ADC dither of 1..2 counts moves a column by 13..26
  px.  A proper volts/div scale (min/max over a longer window, or a fixed
  range per sensitivity setting) is the real fix.
* Full clear + redraw per frame can tear if a photo lands mid-draw.  A
  double-buffer or a rectangle-only repaint is the later fix; at 1.5 s per
  frame it is cosmetic.

Also confirmed in the same run: every cycle ends in the AUTO timeout path
(`to` equals `cycles`) because the input sits near count 973, far below the
2048 trigger level - the window is still valid, which is exactly what the
AUTO fallback is for.

    build/dso183.bin   15884 bytes -> 16 pages (0x08000000..0x08003FFF)
    vector table       SP 0x20005000, reset 0x080034D1
    build sequence     0xE500 (58624) -> header shows "S=E500"
    md5                3fe1d35156375e1a3711c772ee503613
    flash / RAM        15884 B / 61440 B (26 %), 7536 B / 20480 B (36 %)
    make test          17/17 suites pass

Nothing beyond this image has been flashed; the settings page has never been
addressed, and the v6 restore path remains verified.

## 2026-11-03 (bench) - M7 clean trace: fixed scale and column bridging

The first live trace was drawn but dotted.  Two separate causes, both fixed
in `app/main.c`:

* **Auto-range was the amplifier.**  `scope_draw()` derived the display range
  from `stats.min`/`stats.max` plus 4 counts of padding, so a 5-count window
  was stretched over the 184 px grid - about 13 px per ADC count.  One count
  of ordinary dither threw a column 13 px and two counts 26 px, which is the
  scatter that looked like a dashed line.  The range is now **fixed at full
  scale (0..4095)**, where one count is 0.045 px, so the same signal draws as
  a thin line.  This is a placeholder for a real volts/div control (M7).
* **Columns were not joined.**  Even at full scale, adjacent columns can land
  more than a pixel apart.  `scope_vline()` now remembers the previous
  column's row (`s_scope_last_y` / `s_scope_has_last`, reset at the start of
  each frame) and extends the bar to include it, so the trace is continuous
  rather than a row of dashes.

Verified on hardware (one flash, 16 pages, all read back matching):
`status_build_seq` = 0xA1CA, `status_display_id` = 0x858552 (read path still
good), `status_init_errors` = 0, `status_acq_hw_errors` = 0, DHCSR 0x01010001
(core running).  The camera shows a single unbroken line across the panel,
sitting in the upper third for a ~973-count DC input, with a slight upward
slope that is real drift rather than a drawing artefact.

Reminder for the next debug session: `status_build_seq` moved to
0x20000054 in this build.  Resolve symbols from the current ELF every time -
a stale address reads as garbage, which once looked like a wrong image.

    build/dso183.bin   15896 bytes -> 16 pages
    vector table       SP 0x20005000, reset 0x080034DD
    build sequence     0xA1CA (41418) -> header must read "S=A1CA"
    md5                b1e83c9e68edf72c456db178e55f872d
    flash / RAM        15896 B / 61440 B (26 %), 7536 B / 20480 B (36 %)
    make test          17/17 suites pass

Nothing beyond this image was flashed; the settings page has never been
addressed, and the verified v6 restore path is unchanged.

## 2026-11-03 (bench) - trigger marker and scale label

Two additions to the scope view, both confirmed on the panel:

* `scope_draw()` draws a **dashed level marker** at
  `wave_y(ACQ_TRIGGER_LEVEL, &range, &geom)` - 6 px on, 6 px off, 1 px high -
  *before* the trace, so it uses exactly the same mapping as the waveform and
  cannot drift apart from it.  With the fixed 0..4095 range the 2048 trigger
  level sits at the vertical centre of the grid, which is what the snapshot
  shows.
* A bottom bar label `0.41V/d 1ms/d` is printed once, under the grid.  Full
  scale 4095 counts over 8 divisions is 0.41 V/div at a 3.3 V reference; the
  timebase is the index armed by `acq_set_timebase(ACQ_DEFAULT_TIMEBASE)`
  (9 = 1 ms/div).  Both numbers are placeholders until volts/div becomes
  operator-controlled, but a wrong label is worse than no label, so they
  are derived from the constants rather than typed in blind.

The first build emitted two `-Wsign-compare` warnings from
`(UI_W - x) > 6U ? 6U : (UI_W - x)`; the dash width is now computed through
an explicit `uint16_t left`, and the tree builds without project warnings.

Hardware verification: one snapshot shows the continuous green trace, the
dashed marker above it at the trigger level, and the legible bottom label,
with the grid behind.  SWD after the flash: DHCSR `0x01010001` (running),
16 pages written and verified, settings page untouched.

    build/dso183.bin   15980 bytes -> 16 pages
    vector table       SP 0x20005000, reset 0x08003521
    build sequence     0x7534 (30004) -> panel reads "B=4453424D S=7534"
    md5                9a51e2d03fbf414f6189b751fc2fda11
    flash / RAM        15980 B / 61440 B (26 %), 7536 B / 20480 B (36 %)
    make test          17/17 suites pass

## 2026-11-03 (bench) - front-panel switches live: volts/div follows SEN1/SEN2

The switches had appeared dead since M7. Three independent defects were
behind it, and each one hid the next:

1. **The display and the ladder were only refreshed on the first frame.**
   `acq_bench_poll()` had `ladder_refresh(); scope_draw(&f);` inside
   `if (s_acq_cycles == 1U)`, so after the first capture the panel showed a
   frozen image and the switches could not possibly change anything. The
   header block now keeps its own one-shot guard; the refresh and the trace
   run every cycle.
2. **ADC1's injected group never converts on this board.** `JSQR` was
   correctly programmed to the vendor value `0x002394C0` (JSQ1=ch0 SEN1,
   JSQ2=ch6 SEN2, JSQ3=ch5 CPL) and read back correctly, but `JSWSTART`
   never set `JEOC` and `JDR1..3` stayed zero, even with the regular group
   stopped. The vendor's injected-rank trick is therefore unusable here and
   the ladder is read through the **regular** group during a short pause:
   `acq_pause()` stops TIM3, parks DMA and repoints the trigger,
   `input_read_channel()` reads the three channels, `acq_resume()` restores
   the signal channel, its sample time and the timer trigger, then restarts
   DMA and TIM3.
3. **A bare `SWSTART` is ignored while `EXTTRIG = 1`.** On the F1 the
   regular trigger source comes from `EXTSEL` whenever `EXTTRIG` is set, so
   the ladder's software start did nothing and every read returned
   `0xFFFF` (`ladder 65535 65535 65535`). Clearing `EXTTRIG` did not help
   either. The working sequence, measured over SWD, is `EXTSEL = 111`
   (SWSTART) with `EXTTRIG = 1`: the next `SWSTART` sets `EOC` immediately
   and `DR` returns a live count. `acq_pause()` now repoints `EXTSEL` to
   `ACQ_EXTSEL_SWSTART`; `acq_resume()` puts `EXTSEL` back to
   `ACQ_EXTSEL_TIM3_TRGO`.

Hardware evidence after the fix (image S=B2D9):

    acq ok 7 to 7 err 0 | n 256 min 1122 max 1149 vpp 27 mean 1140 rms 1140
    ladder 2711 2129 3582 combo 11 cpl 0 span 32
    ladder 2710 2126 3563 combo 11 cpl 0 span 32

Decoded: SEN1=2, SEN2=1, CPL=2 -> combo 4+3*2+1 = 11 -> span 32 counts,
and the panel's bottom-left label reads **`3mV/d 1ms/d`**, i.e.
32 x 3300/(4095 x 8) = 3.2 mV/div. Acquisition stays healthy through every
pause (`err 0`, stable min/max), so the pause/read/restore seam costs nothing
visible in the trace.

Build identity: 16,740 B / 17 pages, SP 0x20005000, reset 0x080037D9,
sequence 0xB2D9 (45785), md5 d6ab3fdb550ead1301638e8790474ffa, 17/17 host
suites. Nothing else was flashed; the settings page has never been addressed.

## 2026-11-03 (bench) - AC coupling from the CPL switch

`app/waveform.c` gained the pure helper used by the scope view:

    uint16_t wave_remove_mean(uint16_t *samples, uint16_t n);

It computes the window mean with a 32-bit sum (`(sum + n/2)/n`) and rewrites
each sample as `2048 + sample - mean`, clamped to 0..4095, returning the mean
it removed (0 for NULL/empty).  Mutating the caller's buffer is deliberate:
`main()` already renders a private copy in `s_frame`, so the ring buffer is
untouched and the next frame starts from raw data.

`app/main.c` wires it to the front panel: `ladder_refresh()` stores the
decoded coupling in `s_coupling` (0 = DC, 1 = AC, 2 = GND) and `scope_draw()`
calls `wave_remove_mean()` only when it is 1, immediately before decimation.
DC therefore renders exactly as before; GND is not implemented yet (it needs
a sample clamp, not a mean removal, so pretending it works would be a lie).

Tests (`tests/test_waveform.c`) check mean 3000 for {2900,2950,3000,3050,3100}
with the mapped values 1948/2048/2148 and both rejection paths.  The first
patch appended that block *after* main()'s early `printf` + `return`, so it
never ran and the suite passed vacuously; the premature exit was removed and
the assertions now execute.

On hardware (S=67D1, 16,856 B / 17 pages, reset 0x0800384D): the CPL switch
currently reports `cpl 0`, and the UART shows the unchanged DC behaviour

    acq ok 5 to 5 err 0 | n 256 min 1152 max 1169 vpp 17 mean 1162 rms 1161
    ladder 2714 2128 3564 combo 11 cpl 0 span 32

so the AC branch is correctly inactive in DC.  Flipping CPL to AC should
centre the trace and change the `cpl` field to 1.

Nothing was flashed beyond this image; the settings page has never been
addressed and the v6 restore path remains verified.

## 2026-11-03 (bench) - continuity indicator

`app/continuity.{c,h}` answers one question: is the probe shorted to ground?
It is pure logic, so the thresholds and the hysteresis are host-tested:

    #define CONTINUITY_ON_COUNTS   60U     /* ~50 mV at a 3.3 V reference */
    #define CONTINUITY_OFF_COUNTS 120U     /* ~100 mV                     */
    uint8_t continuity_update(uint8_t prev, uint16_t level,
                              uint16_t on_counts, uint16_t off_counts);

* `level` is the highest sample of the current window, so a short that pulls
  the whole window to noise around zero counts as connected (a single high
  sample does not).
* Two thresholds give hysteresis: below 60 counts -> ON, above 120 -> OFF,
  in between -> unchanged.  With `on >= off` the function degenerates to a
  single threshold instead of behaving randomly.
* There is **no buzzer on this board** (no beep/piezo/buzzer reference exists
  in the vendor analysis), so the result is reported visually, not audibly:
  the bottom label gains a ` CONT` suffix, and the console line carries
  `cont 0|1`:

      ladder 2727 2120 3563 combo 11 cpl 0 span 32 cont 0

`tests/test_continuity.c` covers below-on (0, 60), at/above-off (120, 4000),
the middle band keeping the previous state in both directions, and the
degenerate threshold case.

Hardware (2026-11-03): the flashed image reports `cont 0` with the input at
~1180 counts, i.e. correctly idle; shorting the probe to ground is expected
to flip it to `cont 1` and add `CONT` to the panel label.

    build/dso183.bin   17000 bytes -> 17 pages
    vector table       SP 0x20005000, reset 0x080038C9
    build sequence     0x3648 (13896) -> panel must read "B=4453424D S=3648"
    md5                19eccfa2f656a43318bd831a37441152
    flash / RAM        17000 B / 61440 B (28 %), 7544 B / 20480 B (36 %)
    make test          18/18 suites pass

Nothing was flashed beyond this image; the settings page was never addressed
and the v6 restore path is unchanged.

## 2026-11-04 (bench) - trace centred on the frame mean; CPL is not on the ADC ladder

Two findings, one fix.

**CPL cannot be read.** A one-flash diagnostic printed all eight ADC channels
every frame. Flipping the CPL switch to AC (which demonstrably re-routes the
analogue input - the user saw the AC-coupling capacitor charging as a "square
wave for a second") changes no channel: ch5 sits at ~3460..3570 in both
positions, ch1 drifts ~80 counts, everything else is flat or railed. So the
AC/DC switch is a purely analogue switch on this board; the vendor's
`JSQ3 = ch5` describes a channel that does not carry its position here. The
software AC branch (`wave_remove_mean()` gated on `cpl == 1`) is therefore
unreachable and will be removed rather than left as a promise.

**Why the trace never centred.** The vertical window was built around a fixed
centre of 2048 counts while this front end idles near 1195 counts. At a
32-count span that put the trace below the bottom of the grid. `scope_draw()`
now centres the window on the frame's own mean (`centre = f->stats.mean`,
`lo = centre - span/2`, clamped), so the trace is centred whatever the input's
DC level - the display acts as an auto-zero without touching the data.

    build/dso183.bin   17084 bytes -> 17 pages
    vector table       SP 0x20005000, reset 0x08003901
    build sequence     0xC519 (50457) -> panel reads "B=4453424D S=C519"
    md5                d86a276850b7fc9e00e0293748f43360
    flash / RAM        17084 B / 61440 B (28 %), 7544 B / 20480 B (36 %)
    make test          18/18 suites pass

Camera check after the flash: the trace sits mid-grid at `3mV/d 1ms/d` and
shows real structure instead of a line pinned to the edge.

Still open: the trigger level is fixed at 2048 while the signal idles near
1195, so every frame takes the AUTO timeout path (`to` == `cycles`); the level
needs to be settable (and it should default near the measured mean). Then
HOLD, GND coupling and timebase selection.

## 2026-11-04 (later) - PC link specified (protocol v0, not wired yet)

The DSO183's USB socket is power-only, so a PC utility has to talk over the
console UART (USART1, PA9 TX / PA10 RX, 115200 8N1, 3.3 V adapter) or over
SWD.  `app/link.{c,h}` implements the framing end of that, pure and
host-tested:

    offset  size  field
    0       2     preamble A5 5A
    2       1     type (0x01 = sample frame)
    3       1     sequence, wraps
    4       2     sample count n, little endian
    6       2n    samples, uint16 little endian
    6+2n    2     CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over
                  type..samples, little endian

`link_encode_frame()` refuses NULL pointers, n == 0, n > 1024 and a
destination smaller than 8 + 2n, and writes nothing on failure.
`link_crc16()` returns 0x29B1 for "123456789".

Status: spec + codec done, **not wired into the image**.  The running
firmware still prints text lines only.  Wiring it (emit one binary frame per
acquisition when a host sends an enable command, plus a tiny Python decoder
that writes CSV) is a later feature; the text lines stay for debugging.

    running image  17,028 B / 17 pages, seq 0xCC02 (52226),
                   md5 67f89eb72e93ac1dbdcd7f38d517b3fb, reset 0x080038CD
    host suites    19 (link added)

## 2026-11-04 (later) - PC link protocol v1: full host control

The link is no longer just a frame format: a host can drive the instrument.
Both directions use the same envelope and the same CRC-16/CCITT-FALSE code.

    command  A5 5A | 0x02 | seq | op | arg_lo | arg_hi | crc_lo | crc_hi
    reply    A5 5A | 0x03 | seq | op | status | val_lo | val_hi | crc_lo | crc_hi

All fields little-endian; the CRC covers bytes 2..7, i.e. everything except
the preamble and the CRC itself.  Replies echo `seq` and `op` so a host can
match them without ambiguity.

Opcodes (`app/link_cmd.h`):

    NOP            0x00   no effect, replies OK (link check)
    SET_TIMEBASE   0x01   arg 0..26 -> acq_set_timebase()
    SET_TRIGGER    0x02   arg 0..4095 -> acq_set_trigger_level()
    SET_SPAN       0x03   arg 16..4095 counts -> display span
    RUN            0x04   leave HOLD, acquisition continues
    STOP           0x05   HOLD: freeze the displayed frame
    GET_STATUS     0x06   replies value = current op/timebase index
    STREAM         0x07   arg 0/1 -> binary frame bursts instead of text

`link_cmd_parse()` rejects a short buffer, a bad preamble, the wrong packet
type, an unknown opcode (even with a valid CRC) and a CRC mismatch, and
leaves the caller's structure untouched.  `link_cmd_arg_valid()` is the
single place the argument policy lives, so the host and the firmware cannot
disagree about what is legal.

Tests (`tests/test_link_cmd.c`): byte-exact encoding of SET_TIMEBASE seq 7
arg 9, round trip, short buffer, bad preamble, corrupted CRC, NULL in/out,
unknown opcode with valid CRC, exact-fit vs one-short capacity, reply round
trip (status OK, value 0x1234), truncated replies, and the argument
boundaries for every opcode.  `make test` is 20/20.

Still to wire (device side): USART1 receive path -> link_cmd_parse() ->
dispatch -> link_rep_encode(), plus emitting link_encode_frame() bursts while
STREAM is on.  Until that lands the running image is unchanged:
17,028 B / 17 pages, seq 0xCC02 (52226), md5 67f89eb72e93ac1dbdcd7f38d517b3fb,
reset 0x080038CD.

## 2026-11-04 (bench) - PC link v1 verified on hardware

The device side is now complete and measured.  Commands are ten-byte
packets `A5 5A | 0x02 | seq | op | arg_lo | arg_hi | 0x00 | crc_lo | crc_hi`
(CRC-16/CCITT-FALSE over bytes 2..7); replies are
`A5 5A | 0x03 | seq | op | status | val_lo | val_hi | crc_lo | crc_hi`.
Verified live over /dev/ttyUSB0 at 115200 8N1: GET_STATUS returned
`a5 5a 03 20 06 00 01 00 ec dc` (value 1 = RUN), SET_SPAN 128 returned
`a5 5a 03 30 03 00 00 00 c2 57`, and STREAM(1) produced **53 valid 520-byte
`A5 5A 01` frames in 3 s (~17.7 frames/s, 28,021 bytes, every CRC valid)**,
close to the 115200 ceiling of ~22 frames/s.  STREAM(0) returns the throttled
text console.

Defects fixed to get there:
* commands were ignored because the host test sent nine-byte packets while the
  firmware expects ten (three payload bytes, CRC over six);
* `console_write()` used a 20 ms `HAL_UART_Transmit` timeout, but a 520-byte
  frame takes ~45 ms, so the first frame truncated and the old code then
  disabled the console permanently - now 100 ms, and a timeout no longer kills
  the console;
* RX was polled once per loop while TX blocked for milliseconds, losing bytes -
  now a USART1 RX interrupt with a 64-byte ring (`board_uart_rx_start`,
  `board_uart_rx_isr`, `board_uart_get_byte`) plus `USART1_IRQHandler`;
* text statistics printed every loop once the trigger fired - throttled to
  two lines per second;
* while STREAM is on, the ladder pause and the 320-column redraw are skipped
  and the loop delay is dropped from 500 ms to 0, which took streaming from
  ~1.3 to ~17.7 frames/s.

    image   18232 B / 18 pages   seq 0xC10E (49422)
    md5     c52b4f241f77dbd01208fcc8efc50520
    reset   0x08003D65, SP 0x20005000, RAM 8152 B / 39 %
    make test 21/21 suites; make check green
    settings page 0x0800F800.. never addressed; v6 restore via --page-count 62

## 2026-11-04 (bench) - link v1 smoke test, open RUN/STOP question

A host smoke test over /dev/ttyUSB0 (1.6 s read window per command) got a
reply for every command - STATUS, STOP, RUN, TIMEBASE 12 - confirming the
ten-byte command path and the reply path. Two lessons:

* window length matters: with 0.35 s every command looked unanswered because
  the main loop period is ~500 ms and the reply can arrive one command late;
  at 1.6 s every command is answered.  The earlier "NO REPLY" round was a
  host timing artefact.
* the reply VALUE for RUN/STOP is currently stale (link_host_exec leaves it
  untouched), so `STOP` reporting value 0x0001 and `STATUS` after it also
  reporting 0x0001 cannot be read as proof that run was cleared.  GET_STATUS
  is the authority; RUN/STOP should echo the new run bit.

OPEN ITEM: settle whether STOP clears and RUN sets the run flag by reading
STATUS with >= 1 s between commands (expect 1 -> 0 -> 1), and make RUN/STOP
replies carry the resulting state instead of the previous payload.

Image unchanged in this step: 18,232 B / 18 pages, seq 0xC10E (49422),
md5 c52b4f241f77dbd01208fcc8efc50520, reset 0x08003D65, 21/21 host suites.

## 2026-11-04 (bench) - RUN/STOP control verified over the PC link

The link control path is now proven end to end on hardware, with >= 1.3 s
between commands so the ~500 ms main loop cannot deliver a reply late:

    GET_STATUS -> op 06 status 0 value 1     (running)
    STOP       -> op 05 status 0 value 0     (reply echoes: stopped)
    GET_STATUS -> op 06 status 0 value 0
    RUN        -> op 04 status 0 value 1     (reply echoes: running)
    GET_STATUS -> op 06 status 0 value 1
    SET_TIMEBASE 12 -> op 01 status 0 value 0

`link_host_exec()` zeroes `*value` on entry, so every reply is deterministic;
the RUN case now sets `*value = 1U` after a successful `set_run(1)` so the
reply describes the resulting state rather than a leftover.  STOP needs no
change: zero already means "stopped".

STREAM replies still carry 0 - its `ok = ...` assignment spans two lines, so
the earlier patch only touched RUN.  Cosmetically worth fixing (the reply
should echo 1/0), but GET_STATUS is authoritative in the meantime.

Host-side lessons: read at least 1 s per command (the loop period is 500 ms
plus a 45 ms frame while streaming), and resynchronise on the `A5 5A` tag
because text and binary share the port.

    build/dso183.bin   18248 bytes -> 18 pages
    vector table       SP 0x20005000, reset 0x08003D75
    build sequence     0x6706 (26374) -> panel must read "B=4453424D S=6706"
    md5                76741873592d685000d81fe9ca419b60
    flash / RAM        18248 B / 61440 B (29 %), 8152 B / 20480 B (39 %)
    make test          21/21 suites pass

Nothing beyond this image was flashed; the settings page
0x0800F800..0x0800FFFF has never been addressed and the v6 restore path
remains verified (`--page-count 62`).

## 2026-11-04 (bench) - STREAM echo verified; guard target added

The backlog sweep closed two items and left two that need hardware decisions.

**STREAM echo.** `app/link_host.c` now sets the reply value for a successful
STREAM command to the resulting state, so the reply is self-describing instead
of always carrying the zero that `*value` is initialised to.  Verified live:
`STREAM 1` -> `a5 5a 03 01 07 00 01 00 bd 08` (value 1) and 41 frames in 2 s
(~20.5 fps, essentially the 115200 ceiling of ~22 fps); `STREAM 0` ->
`a5 5a 03 02 07 00 00 00 5e d5` (value 0).

**Guard target.** `tools/guard.py` + `make guard` perform static checks that do
not need the board: settings-page addresses (0x0800F800 / 0x0800FC00) must not
appear in `app/` or `drivers/`, `tools/*.py` must not invoke `st-flash write`,
`st-flash erase` or `--mass-erase`, `app/` must not contain an unbounded loop
outside the main superloop, and display code must not clobber the upper byte of
`GPIOB->ODR`.  The first version reported five false positives (its own pattern
text, docstrings that mention st-flash, and `main.c`'s legitimate superloop);
the rewritten version reports `guard: 0 issue(s)`.

Still open, both needing a hardware answer: a physical timebase/trigger-level
control (currently protocol-only, so a PC is required to change them) and GND
coupling for CPL position 2.  The AC/DC switch proved to be analogue-only -
no ADC channel moves with it - so GND is probably the same kind of hardware
function and would need a measured node before firmware can act on it.

    build/dso183.bin   18132 bytes -> 18 pages
    vector table       SP 0x20005000, reset 0x08003D01
    build sequence     0x7FC1 (32705)
    md5                978b9a6450213c64b644faba04e34f12
    flash / RAM        18132 B / 61440 B (29 %), 7512 B / 20480 B (36 %)
    make test          21/21 suites pass
    make guard         guard: 0 issue(s)

Nothing was flashed beyond this image; the settings page was never addressed.

## 2026-11-04 (bench) - scale label padded; switch mapping closed by measurement

**Ghost character fixed.** The bottom-left scale label is repainted only when
the span or continuity state changes, and the new string was written over the
old one without clearing it: after the span moved from 4095 to 32 counts the
panel showed `3mV/d 1ms/dd`, i.e. the tail of the previous `412mV/d 1ms/d`
survived. `scope_draw()` now pads the label with spaces up to the buffer
width (`strlen` + loop, `#include <string.h>`) before `ili9341_print()`, so a
shorter label overwrites the whole line. Snapshot after the fix: the text
ends at `1ms/d` with blank panel behind it.

**The front-panel switches are hardware-only.** Three independent measurements
agree:

* 150 s capture while every slide switch was moved through all three
  positions, holding each >= 3 s: the `adc 0..7` line never changed by more
  than 25 counts on any channel (ch0 ~2735, ch1 ~2126, ch2 ~1204 signal,
  ch3/ch4 4095, ch5 ~3571, ch6 ~2121, ch7 ~2116).
* 120 s SWD poll of GPIOA/B/C IDR with the core halted (IWDG frozen by
  DBGMCU bit 8): zero bit changes - baseline `A=DF00 B=0700 C=4000`
  throughout.
* The injected ADC group was re-tested with the *correct* bits: JSWSTART is
  CR2 bit 21 (not 22), and with `JEXTSEL=111 | JEXTTRIG` the group does
  complete (JEOC sets, SR 0x1D) but **JDR1..JDR3 read zero**. The injected
  data path is unusable on this clone, so the pause/read/restore route through
  the regular group stays the only working method - and it too sees nothing,
  because the switches are not wired to the ADC at all.

Consequence: the gain range and the AC/DC/GND coupling are switched in the
analogue front end, not reported to the MCU. The `3mV/d` on screen is
therefore the default span, not a decoded switch position, and GND coupling
(third CPL position) cannot be commanded in firmware. Timebase remains
protocol-only (`SET_TIMEBASE`) because the vendor selected it in its GUI;
trigger level auto-follows the frame mean so the trace stays locked, with
`SET_TRIGGER` as the manual override.

    build/dso183.bin   18156 bytes -> 18 pages
    vector table       SP 0x20005000, reset 0x08003D19
    build sequence     0xE012 (57362) -> panel reads "B=4453424D S=E012"
    md5                69ac753cf8e441beb6bb47713766b41c
    flash / RAM        18156 B / 61440 B (30 %), 7512 B / 20480 B (36 %)
    make test          21/21 suites, make guard 0 issues, make check green

Nothing beyond this image was flashed; settings page never addressed; the v6
recovery path (`--page-count 62`) remains verified.

## 2026-11-04 (bench) - front-panel switches decoded; range/gain scale the display

The three front-panel switches are 3-position resistor ladders on ADC1
channels 5, 6 and 7, which each settle near one of three levels
(~3577 high, ~2112 middle, ~0 low).  An earlier "the switches are invisible"
conclusion was wrong twice over: `input_init()` had put PA0..PA7 in analogue
mode (so the digital IDR path read zero), and the first ADC sweeps sampled at
moments when no switch was moving.

Implementation:

* `input_init()` is back to `GPIOA->CRL = 0` (PA0..PA7 analogue, PA2 signal).
* `ladder_refresh()` reads ch5 (coupling), ch6 (range) and ch7 (gain) inside
  the existing acquisition pause, maps each raw value with
  `sw_level()` (< 600 -> 0, < 3000 -> 1, else 2) and computes
  `mv = range_mv[r] / gain_x[g]` from `range_mv {10, 100, 1000}` mV/div and
  `gain_x {1, 2, 5}`.  The resulting span (`8 * mv * 4095 / 3300`, clamped to
  16..4095) feeds the existing window and label machinery, so the panel and
  the measurements now follow the hardware instead of a fixed default.

Verified on the panel: header `B=4453424D S=A7A6`, grid, centred trace and a
bottom label reading **`100mV/d 1ms/d`** - exactly the 0.1 V range at gain
x1, which proves the range and gain channels decode correctly.  The coupling
channel is read but does not affect volts/division, so its assignment is not
yet proven and does not affect the scaling.

    build/dso183.bin   18048 bytes -> 18 pages
    vector table       SP 0x20005000, reset 0x08003CF1
    build sequence     0xA7A6 (42918) -> panel must read "B=4453424D S=A7A6"
    md5                c1732c689b9bb3581feb35a741f05221
    flash / RAM        18048 B / 61440 B (29 %), 7512 B / 20480 B (36 %)
    make test          21/21 suites

Nothing beyond this image was flashed; the settings page 0x0800F800..0x0800FFFF
has never been addressed, and the v6 restore path remains verified.

## 2026-11-04 (later) - Phase 1: acquisition integrity (immutable snapshot)

The acquisition engine used to hand out a pointer into the live DMA ring:
`acq_cycle()` returned `s_buf` while DMA1 channel 1 kept overwriting it, so
the frame builder and the measurements could read a window that changed
under them.  It also called `acq_set_trigger_level(f.stats.mean)` every
frame, silently overriding the configured trigger level.

Changes:

* `app/acq.c` - `acq_cycle(timeout_ms, post_samples, gate)` now freezes the
  acquisition before anyone reads it: after the AWD event (or the bounded
  trigger timeout) it waits for the requested post-trigger samples, stops
  TIM3, stops and parks DMA1 channel 1, copies the 1024-sample ring into a
  private `s_snap` buffer, records the freeze write position, and restarts
  pacing.  `acq_samples()` returns the immutable copy.  Trigger polling is a
  tight loop with `HAL_GetTick()` deadlines (no fixed `HAL_Delay(2)`), and a
  post-fill deadline of `ACQ_POST_FILL_MS` bounds the wait.  Timer restart
  failure returns `ACQ_HW_ERROR` and leaves DMA stopped.
* `app/acq_window.{c,h}` - `acq_win_place_tail()` (window ending at the
  freeze position, for untriggered/AUTO frames) and `acq_ring_advance()`
  (wrap-safe forward distance used by the post-fill wait).
* `app/frame.{c,h}` - `frame_build_tail()` composes the newest-window path;
  the measurement/copy tail is shared with `frame_build()`.
* `app/main.c` - untriggered frames use `frame_build_tail(..., gate.write_index, ...)`
  instead of re-reading the live buffer; the per-frame trigger-level
  override is gone, so the configured level is respected.

Tests: `tests/test_acq_window.c` covers tail placement, wrap and the ring
advance; `tests/test_frame.c` covers the tail frame and its truncation/error
paths.  `make test` passes 21/21 suites; `make` and `make guard` pass.

Known limitation until Phase 2: the default trigger level is 2048 counts and
this front end rests near 1195, so most frames take the bounded timeout and
fall back to the AUTO tail window.  That path still shows fresh data, but
AUTO/NORM/SINGLE are not yet applied by `acq_cycle()`, and the menu's trigger
level is not yet passed to the acquisition engine.

Mapping note: the bench entry above lists ch5 as coupling; the later
105-sample per-switch sweep corrected this to **ch5 = gain, ch6 = range,
ch7 = coupling**, which is what `main.c` now decodes (the stale comment was
also fixed).

    build/dso183.bin   19924 bytes -> 20 pages
    vector table       SP 0x20005000, reset 0x08004321
    build sequence     0xF861 (63585) -> panel must read "B=4453424D S=F861"
    md5                eaae97330444207360f436536b262b45
    flash / RAM        19924 B / 61440 B (32 %), 9600 B / 20480 B (46 %)
    make test          21/21 suites

Nothing was flashed in this phase; the settings page 0x0800F800..0x0800FFFF
was not addressed, and the v6 restore path remains unchanged.

## 2026-11-04 (later) - Phase 2: trigger modes (AUTO/NORM/SINGLE)

Changes:

* `app/trigger.{c,h}` - new pure `trig_fsm_action()` so all mode policy lives
  in one host-testable place: AUTO captures on a trigger and falls back to
  the newest window after a timeout; NORM captures only on a trigger and
  otherwise holds the previous frame; SINGLE captures one armed trigger and
  then holds.  `trig_fsm_take()` delegates to it, keeping its old semantics.
* `app/acq.{c,h}` - `acq_cycle()` now applies the selected mode: a hold
  returns immediately with `gate.fresh == 0` and leaves the previous snapshot
  untouched, while AUTO/NORM/SINGLE captures replace it and set
  `gate.fresh == 1`.  `acq_set_trigger_mode()` validates the mode and arms a
  new SINGLE selection; `acq_trigger_arm()` re-arms a completed SINGLE.
  After one SINGLE capture, further `acq_cycle()` calls park without touching
  the DMA/TIM3 path.
* `app/main.c` - held gates are not counted, re-measured or redrawn; the
  menu trigger mode is applied after `acq_init()` (menu init runs first).
  `app/menu.c` now routes trigger-mode changes into
  `acq_set_trigger_mode()` and arms SINGLE on entry.
* The AWD remains the hardware trigger event; no slope filter was added
  because there is no slope UI.  The user trigger level is never overridden.

Tests: `trig_fsm_action()` covers AUTO/NORM/SINGLE captures, fallbacks,
holds, disarming and NULL policy checks; the existing scan/hysteresis
tests still pass unchanged.  `make test` 21/21, `make check` green.

    build/dso183.bin   20244 bytes -> 20 pages
    vector table       SP 0x20005000, reset 0x08004461
    build sequence     0x88D6 (35030) -> panel must read "B=4453424D S=88D6"
    md5                10e3fccf84b3697de751ada9564a38c3
    flash / RAM        20244 B / 61440 B (32 %), 9600 B / 20480 B (46 %)
    make test          21/21 suites

Outstanding: the default level (2048) sits well above the front-end rest
level (~1195), so NORM will typically hold until the trigger level is
adjusted from the menu.  Nothing was flashed.

## 2026-11-04 (later) - Phase 3: user-settings persistence

Changes:

* `app/settings.{h,c}` - NEW module persisting instrument user settings to
  the flash page immediately after the calibration page (0x0800F900), so
  user preferences are separate from instrument calibration.
* `app/flash_store.{h,c}` - generalised to support writing/reading at any
  address: `flash_store_write_at(addr, data, len)` / `flash_store_read_at(addr, out, len)`.
  Existing `flash_store_write` / `flash_store_read` remain as thin wrappers at
  the calibration page (0x0800F800).
* `app/main.c` - `acq_bench_init()` now loads settings via `settings_load()`,
  then applies timebase, trigger mode/level, and run/stream state to the
  acquisition engine and menu.
* `app/menu.{h,c}` - menu now persists user settings on button release and on
  each parameter change (timebase, trigger mode, trigger level).  Added
  `run` and `stream` fields to the menu struct.
* `tests/test_settings.c` - NEW host tests for settings pack/unpack/defaults.

New build identity (after Phase 3 changes):

    build/dso183.bin   20900 bytes -> 21 pages
    vector table       SP 0x20005000, reset 0x080046C9
    build sequence     0x6AEF (27375) -> panel must read "B=4453424D S=6AEF"
    md5                29471f8bbef56e295d2f4dbc6a5fefe2
    flash / RAM        20900 B / 61440 B (34 %), 9688 B / 20480 B (47 %)
    make test          22/22 suites

Outstanding: settings load/save use a flash page at 0x0800F900; flash write
operations were NOT executed during this phase - the firmware was built,
host-tested and doc-checked only.  Nothing was flashed.

## 2026-11-04 (later) - Phase 4: calibration menu and live calibration

Changes:

* `app/cal.{h,c}` - added live (mutable) calibration accessors
  `cal_active()` / `cal_active_set()`; the live pair defaults to the
  identity calibration and is loaded from the calibration store at boot,
  swapped by the calibration menu.
* `app/menu.{h,c}` - new parameter pages `MENU_CAL_OFFSET` / `MENU_CAL_GAIN`
  added to the depth-based menu state machine: short press walks deeper,
  long hold >1500 ms walks back, hold >200 ms steps the parameter.  Offset
  steps +8 uV per hold step; gain (Q16) steps +1024 per hold step; both go
  through the calibration store and become the live cal immediately.
* `app/main.c` - `acq_bench_init()` applies the persisted settings
  unconditionally once the engine is ready (removed a stale `s_acq_ok` gate
  that skipped the apply path) and loads the persisted instrument
  calibration into the live cal.
* `app/main.c` `scope_draw` - the Vpp readout is calibrated: raw max/min
  counts map through the live cal to uV, /1000 for mV, then scale by the user
  span for the panel readout.
* `app/menu.h` - `cal_gain_q16` corrected from overflowing `uint16_t` to
  `int32_t` (Q16).
* `app/settings.h` - added `settings_pack` / `settings_unpack` prototypes.

Final build identity (end of Phase 4):

    build/dso183.bin   20900 bytes -> 21 pages
    vector table       SP 0x20005000, reset 0x080046C9
    build sequence     0x6AEF (27375) -> panel must read "B=4453424D S=6AEF"
    md5                29471f8bbef56e295d2f4dbc6a5fefe2
    flash / RAM        20900 B / 61440 B (34 %), 9688 B / 20480 B (47 %)
    make test          22/22 suites

Outstanding: calibration-menu flash writes go through the calibration store
page (0x0800F800); no flash operations were executed during this phase - the
firmware was built, host-tested and doc-checked only.  Nothing was flashed.

## 2026-11-04 (later) - Phase 5: full regression run and final audit

Phase 5 was the full regression run after Phase 4: `make check` green,
`make test` 21/21 suites, `make guard` 0 issues; nothing flashed; Phase 1/2
acquisition behaviour untouched.

Final audit pass closed the last remaining compiler warnings:

* `app/flash_store.c` - removed local `FLASH_KEY1/2`, `FLASH_CR_*`,
  `FLASH_SR_*` redefinitions; the STM32F1 CMSIS device header
  (`stm32f103xb.h`) already provides the same RM0008 values, so the
  redefinition warnings are gone and the flash policy code now uses the HAL
  definitions directly.
* `app/main.c` `ladder_refresh` - removed the write-only `cpl` variable
  (written from ADC ch7, never read; the coupling decode was not part of the
  implemented behaviour).
* `app/main.c` Vpp readout - enlarged the `snprintf` display buffer to 64
  bytes, clearing `-Wformat-truncation` on the `%lu` directives.

Final clean verification (2026-11-04, end of Phase 5):

    build/dso183.bin   20900 bytes -> 21 pages
    vector table       SP 0x20005000, reset 0x080046C9
    build sequence     0x6AEF (27375) -> panel must read "B=4453424D S=6AEF"
    md5                29471f8bbef56e295d2f4dbc6a5fefe2
    flash / RAM        20900 B / 61440 B (34 %), 9688 B / 20480 B (47 %)
    make test          21/21 suites

Outstanding: hardware validation of the calibration menu is still pending
(no flash performed during any phase).  Nothing was flashed.

## 2026-11-05 - full audit against the stock firmware (read-only)

Scope: the whole tree (app/, tests/, linker/, tools/), the stock v6 image
(`../projects/dso183/firmware/dso183.bin`, `artifacts/patch/dso183_patched_full_v6.bin`)
and the reverse-engineering corpus (`../dso183-investigation/`).  Nothing was
built into the image path, flashed or modified in `app/` - this pass only
audited code and updated documentation.

Deliverable: **`docs/AUDIT_2026-11-05.md`** - capability comparison,
defect list with severities and file:line evidence, requirement-coverage
table (N/H/F/U/D/CAL from `CUSTOM_FIRMWARE_REQUIREMENTS.md`), hardware
limits, and an ordered remainder list.

Defects found (not yet fixed - see the audit for evidence and fixes):

* **P0-1 settings and calibration share one flash page.** `SETTINGS_ADDR`
  is `0x0800F900`, 256 bytes inside the 1 KiB calibration page at
  `0x0800F800`, so `settings_save()` erases the calibration record and
  `cal_store_write()` erases the settings record.  The linker `SETTINGS`
  region (`0x0800F800`, 4 KiB) also runs 2 KiB past the end of the 64 KiB
  part while leaving the two free pages at `0x0800F000`/`0x0800F400`
  unmapped.  Fix: settings page `0x0800F400`, region from `0x0800F000`.
* **P0-2 the Vpp readout mixes units.** `div10 = vpp_mv * 80 / s_span`
  divides millivolts by ADC counts; with the identity calibration the
  panel therefore always shows `Vpp 0.0d`.
* **P0-3 the trigger level can only be raised** (+64 per step, capped at
  4095).  The default 2048 is above the front-end idle level (~1195), so
  NORM/SINGLE never trigger and the user cannot lower the level.
* **P1-1 menu long-hold bounces** (it clears `button_down` while the button
  is still down, so the next poll sees a fresh press and goes deeper) and
  the menu row is painted at y=20..39 inside the plot area, so it is
  flickered by frame redraws and can stay on screen in HOLD.
* **P1-2 the superloop sleeps 500 ms** when not streaming: ~2 Hz menu,
  acquisition and link response, and presses shorter than a poll are lost.
* **P1-3 the bottom scale label hardcodes `1 ms/d`** and the trigger marker
  is drawn at the constant `ACQ_TRIGGER_LEVEL` instead of the live level.
* **P1-4 persistence is magic-only** (no CRC, version or journal); a torn
  write silently degrades to defaults, and every menu step erases the page.
* **P1-5 the five fastest timebases** (1-20 us/div) are REPT-only in the
  timebase table but `acq.c`/`main.c` accept them as single-shot.
* **P1-6 user-facing parity gaps** versus stock: Vrms/Freq/Duty, AV+/AV-,
  Cycle/AllDisp, Save/Load WF, Clr Offset and Default restore, the "Set CPL"
  prompt, a Running/Stopped label, Board info/CFW version, Display 1/2 and
  the 9-step per-sensitivity calibration flow.

Documentation changes in this pass:

* `README.md` - status paragraph and milestone table replaced with the
  current state (M0-M8a, Phases 1-5, PC link) plus a "what is left" list.
* `FLASH_HANDOVER.md` - storage rows corrected: calibration page
  `0x0800F800..0x0800FBFF`, the settings record (currently inside that page,
  known defect) and the never-written device record `0x0800FC00..0x0800FFFF`;
  recovery text no longer conflates the two.
* `agent.md` - M4 status line replaced, flash-layout rules corrected, the
  settings/calibration page collision recorded, `main.c` description updated.
* `CHANGES_SUMMARY.md`, `IMPLEMENTATION_COMPLETE.md`,
  `DOCUMENTATION_UPDATE_SUMMARY.md` - marked as historical snapshots.

Build identity is unchanged by this pass (documentation only):

    build/dso183.bin   20900 bytes -> 21 pages
    vector table       SP 0x20005000, reset 0x080046C9
    build sequence     0x6AEF (27375) -> panel must read "B=4453424D S=6AEF"
    md5                29471f8bbef56e295d2f4dbc6a5fefe2
    flash / RAM        20900 B / 61440 B (34 %), 9688 B / 20480 B (47 %)
    make test          21/21 suites

Verdict: a working, unusually well-verified scope core - not yet a product.
The three P0 defects are small, localised fixes that should land before any
bench session that involves the menu or a persisted setting.

## 2026-11-05 (later) - audit remediation

Every P0 and the practical P1 defects from `docs/AUDIT_2026-11-05.md` were fixed in
source; nothing was flashed.

Storage:

* `app/flash_store.h` - layout is now settings slot A `0x0800F000`, slot B
  `0x0800F400`, calibration `0x0800F800`, device record `0x0800FC00` (never written);
  the linker `SETTINGS` region starts at `0x0800F000`.
* `app/settings.{h,c}` - 16-byte record: magic, version, sequence, CRC-16/CCITT-FALSE,
  then timebase/mode/level/run/stream.  Saves alternate slots and always keep the
  newest valid record; load picks the newest valid slot by signed sequence comparison.
* `app/cal_store.c` - 16-byte CRC record for the calibration pair.
* `app/crc16.{h,c}` - new CRC-16/CCITT-FALSE helper (check value 0x29B1).

UI / acquisition:

* `app/menu.{h,c}` - rewritten for one PA8 button: 1 kHz debounced sampler in
  `SysTick`, queued taps, double-taps (one step back) and long-holds (next page);
  six pages including calibration offset/gain; bidirectional steps with clamps;
  persistence debounced to 800 ms idle; menu strip at y=214, cleared when closed,
  20 s auto-close.
* `app/stm32f1xx_it.c` - `SysTick_Handler` calls `menu_button_tick()` (still no
  watchdog feed).
* `app/acq.{h,c}` - `acq_set_timebase()` rejects the five equivalent-time-only
  entries (`ACQ_PARAM`); getters `acq_timebase_get()`, `acq_trigger_level_get()`,
  `acq_trigger_mode_get()`.
* `app/main.c` - Vpp is a counts ratio (`vpp * 80 / span`); the scale label uses
  `timebase_label()`; the marker uses the live trigger level; persisted settings are
  clamped; AUTO uses a 250 ms trigger window; the loop delay is 1 ms instead of 500 ms.

Tests:

* `tests/test_crc16.c` (new), `tests/test_flash_layout.c` (new), `tests/test_settings.c`
  rewritten for the CRC record and slot order.  24/24 host suites pass.

Documentation: README, FLASH_HANDOVER, agent.md and the audit document now describe
the fixed layout and the new build.

New build identity:

    build/dso183.bin   22068 bytes -> 22 pages
    vector table       SP 0x20005000, reset 0x08004B69
    build sequence     0xE4EB (58603) -> panel must read "B=4453424D S=E4EB"
    md5                61db40e382e24eb5d69c04c1b3056023
    flash / RAM        22068 B / 61440 B (35 %), 9712 B / 20480 B (47 %)
    make test          24/24 suites

Outstanding: stock feature parity (Vrms/frequency/duty, save/load waveform,
average/cycle, clear-offset/defaults, per-sensitivity calibration), a PC link host
tool, and bench validation of the new menu timing/flash slots/Vpp readout.  Nothing
was flashed.

### Re-audit of the remediation (2026-11-05, later)

Re-reading the changed code found two real defects in the first remediation pass:

* `acq_init()` (called again after a hardware error) reset the watchdog trigger band to
  the compile-time default and the timer to the default timebase while the getters kept
  reporting the user values, so the marker and hardware disagreed after a recovery.  It
  now preserves `s_trig_level`/`s_timebase` and re-arms SINGLE.
* Menu events were queued without timestamps, so a main-loop stall longer than the
  300 ms double-tap window could turn two separate taps into a phantom double-tap.  Each
  event now carries its SysTick timestamp.

Still open (documented in `docs/AUDIT_2026-11-05.md` §8.1): calibration is inert
(no per-sensitivity offset applied), RUN/STOP/STREAM set over the link is not persisted,
`cal_store_write` ignores a write failure, the link still advertises timebase 0..4,
`SET_SPAN` is overwritten by `ladder_refresh`, full-frame redraw flicker, and no host
test for `menu.c`/`acq.c`.

Build identity after the re-audit:

    build/dso183.bin   22068 bytes -> 22 pages
    vector table       SP 0x20005000, reset 0x08004B69
    build sequence     0xE4EB (58603) -> panel must read "B=4453424D S=E4EB"
    md5                61db40e382e24eb5d69c04c1b3056023
    flash / RAM        22068 B / 61440 B (35 %), 9712 B / 20480 B (47 %)
    make test          24/24 suites

## 2026-11-05 (later still) - calibration table and link/persistence consistency

Closes the three items that §8.1 of `docs/AUDIT_2026-11-05.md` had left open.

Changes:

* `app/cal_table.{h,c}` - NEW.  Nine per-sensitivity zero levels (`range * 3 + gain`)
  plus a presence mask, packed into 20 bytes.  The mask decides presence, so a
  calibrated zero of 0 counts is legal; an entry above 4095 is dropped on load.
* `app/cal_store.{h,c}` - calibration record v2: 40 bytes, `CAL2` + version + CRC-16
  over the whole record, base `offset_uv`/`gain_q16` and the zero table together.
  `cal_store_write()` returns the flash status so a failed save is retried, and
  `cal_store_read()` refreshes the `.noinit` warm copy so the pair and the table always
  come from the same record.
* `app/runstate.{h,c}` - NEW.  One source of truth for RUN/STREAM, replacing the
  separate `s_run`/`s_stream` copies in `main.c` and the cached `run`/`stream` fields in
  the menu, which could overwrite a PC-link RUN/STOP with a stale value.
* `app/menu.{h,c}` - `CAL ZERO` page: tap captures the current frame mean as the zero
  for the active combo, double tap clears it, the strip reports the stored count.  Values
  only mark the record dirty when they actually change, so holding at a clamp limit no
  longer queues redundant erases.
* `app/main.c` - the trace centres on the stored zero for the active combo (frame mean
  otherwise), each fresh non-streamed frame is offered to the menu, `ladder_refresh()`
  keeps the last good decode on a failed read and only recomputes when the switches
  actually moved (so a host `SET_SPAN` survives), and RUN/STREAM go through `runstate`.
* `app/link_cmd.c` - `SET_TIMEBASE` validates against `TIMEBASE_FIRST_SINGLE_SHOT`, so
  the five equivalent-time-only entries are rejected instead of accepted and ignored.
* Tests: NEW `tests/test_cal_table.c` (7 cases) and `tests/test_runstate.c`;
  `tests/test_link_cmd.c` and `tests/test_flash_layout.c` extended.

Still open: the base offset/gain pair is stored but does not yet scale the volts/div
label or Vpp (CAL-03 partly met - the per-combo zero is real, absolute scaling is not);
full-frame redraw flicker; no host tests for `menu.c`/`acq.c`/HAL modules;
`cal_store_write_table()` unused; the product-parity layer (Vrms/frequency/duty,
save/load waveform, average/cycle, clear-offset/defaults).

Build identity:

    build/dso183.bin   23084 bytes -> 23 pages
    vector table       SP 0x20005000, reset 0x08004F3D
    build sequence     0x2575 (9589) -> panel must read "B=4453424D S=2575"
    md5                f1a6868e5febcba0e1ffa70ce10b793d
    flash / RAM        23084 B / 61440 B (37 %), 9744 B / 20480 B (47 %)
    make test          26/26 suites

## 2026-11-05 - remote-control verification and flash preparation

Checked the PC link end to end before preparing the image for flashing.  Framing,
CRC, parser, dispatcher and streaming were intact, but the range policy was not
actually applied: `link_cmd_arg_valid()` was only called from the unit tests.

Fixed:

* `app/link_host.c` now validates the argument for a known opcode before dispatch
  and answers `LINK_ST_BAD_ARG` (value 0) without calling a hook.  This closes a real
  protocol bug: `SET_TIMEBASE` was cast from 16 bits to `uint8_t`, so 261 (`0x0105`)
  wrapped to index 5 and was applied with OK.  A trigger level above 4095, a span
  outside 16..4095 and a stream argument other than 0/1 are now refused instead of
  being clamped or normalised.  Unknown opcodes still answer `LINK_ST_UNKNOWN`.
* `app/main.c` suppresses the `alive` keepalive while streaming, so sample frames are
  no longer interleaved with ASCII text.
* `app/link_cmd.h` comment corrected to `TIMEBASE_FIRST_SINGLE_SHOT..26`.
* NEW `tests/test_link_e2e.c`: host encode -> device parse -> dispatch -> device reply
  encode -> host parse, for every opcode, including the 261 wrap case and a corrupted
  CRC.  `tests/test_link_host.c` updated for the enforced policy.
* Flash preparation docs: `FLASH_HANDOVER.md` page counts corrected to the current
  23-page image (0x08000000..0x08005BFF), the stale "settings record at 0x0800F900"
  note removed, and the recovery range changed from `--page-count 62` (which wrote the
  settings slots at F000/F400) to `--page-count 60` (app area only, 0x08000000..
  0x0800EFFF).  `tools/flash/write_image.py` now protects the whole 4 KiB storage
  region (F000/F400 settings, F800 calibration, FC00 device record) instead of only
  F800..FFFF, and its messages name the actual owners.

Verification:

    build/dso183.bin   23352 bytes -> 23 pages
    vector table       SP 0x20005000, reset 0x08005049
    build sequence     0xE54A (58698) -> panel must read "B=4453424D S=E54A"
    md5                5158cae814ecf5f78b564da9f01c93a1
    flash / RAM        23352 B / 61440 B (37 %), 9744 B / 20480 B (47 %)
    make check         RC=0, all artefact checks OK
    make test          27/27 suites
    tools/guard.py     0 issues
    write_image.py     dry run: 23 pages listed, storage untouched

Nothing was flashed.

## 2026-11-05 - host PC-link test system + live bench run

Built the host-side test system on the in-firmware PC link, so the verified image can
be exercised from the bench without a host GUI:

* NEW `tools/remote_proto.py` - pure codec (CRC-16/CCITT-FALSE, sample frame / command /
  reply encode+parse, argument policy) + offline self-checks.
* NEW `tools/remote_client.py` - pyserial transport on the console UART + reply/frame
  scanners that skip inter-frame console noise (`alive\r\n`), auto port detect.
* NEW `tools/remote_tests.py` - suites: codec self-checks, control policy (offline
  FakeClient), serial transport, streaming frames, bad-CRC resilience.
* NEW `tools/remote_run.py` - CLI: offline by default, `--live` for the bench.
* Makefile: `make remote-test` (offline), `make remote-live` (bench).

Live bench run (device currently running the pre-hardening image):

* 60 results: transport OK, reply latency ~0.02 s, bad-CRC command dropped + link
  recovered, STREAM+RUN frames captured (3 frames, seq contiguous, CRC valid).
* 6 failures - all correspond to defects fixed in the 2026-11-05 hardening and still
  present in the currently flashed image: SET_TIMEBASE range/wrap, SET_TRIGGER clamp,
  STREAM normalisation, unknown-op reply policy.

Outstanding: measurement/signal-level streaming tests need a user-connected test signal;
`make remote-live` re-run after the new image is flashed (expect 60/60 green then).
