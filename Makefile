# DSO183 custom firmware - portable build (GNU Make + arm-none-eabi-gcc)
#
# Works unchanged on the development host and on the ST-Link host (both have
# arm-none-eabi-gcc; neither needs PlatformIO).  Host-side unit tests build with
# the native compiler and need no HAL.
#
#   make            build the firmware image (build/dso183.bin, .hex, .elf)
#   make size       print the flash/RAM budget report
#   make test       build and run the host unit tests
#   make clean      remove build artefacts
#
TARGET  := dso183
BUILD   := build

# ---- toolchain -------------------------------------------------------------
PREFIX  ?= arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size
HOSTCC  ?= cc

# ---- sources ---------------------------------------------------------------
HAL_DIR   := drivers/STM32F1xx_HAL_Driver
CMSIS_DIR := drivers/CMSIS/Core
CMSIS_DEV := drivers/CMSIS/Device/ST/STM32F1xx

C_SOURCES := \
  $(wildcard app/*.c) \
  $(wildcard drivers/display/*.c) \
  fonts/glcdfont.c \
  fonts/glyph.c \
  $(wildcard $(HAL_DIR)/Src/*.c) \
  $(CMSIS_DEV)/system_stm32f1xx.c

ASM_SOURCES := startup/startup_stm32f103c8.s

INCLUDES := -Iapp -I$(HAL_DIR)/Inc -I$(CMSIS_DIR) -I$(CMSIS_DEV)/Include \
           -Idrivers/display -Ifonts -I$(BUILD)

# Build sequence: a checksum of the application sources, so every distinct
# source state gets its own identity byte (shown on the diagnostic screen as
# "S=xx" and checked over SWD after a flash).  It is written into a generated
# header, so changing the sources really does rebuild status.c.
# The digest must cover EVERY source that can change the image, and the
# generated header must be rebuilt when any of them changes - otherwise the
# "S=" byte on the screen would not identify the flashed image.
DIGEST_FILES := $(wildcard app/*.c) $(wildcard app/*.h) \
                $(wildcard drivers/display/*.c) $(wildcard drivers/display/*.h) \
                $(wildcard fonts/*.c) $(wildcard fonts/*.h)
BUILD_SEQ := $(shell cat $(DIGEST_FILES) 2>/dev/null | cksum | awk '{print $$1 % 65536}')

DEFINES := -DSTM32F103xB -DUSE_HAL_DRIVER

# ---- flags -----------------------------------------------------------------
# -ffunction-sections + --gc-sections keeps the image inside the 60 KiB budget
# reserved for code (the last 4 KiB page belongs to the calibration store).
MCUFLAGS := -mcpu=cortex-m3 -mthumb

CFLAGS := $(MCUFLAGS) $(DEFINES) $(INCLUDES) -Os -g3 -std=c11 \
          -Wall -Wextra -Wno-unused-parameter \
          -ffunction-sections -fdata-sections -fno-common \
          -MMD -MP

ASFLAGS := $(MCUFLAGS) -x assembler-with-cpp -g3

LDSCRIPT := linker/STM32F103C8_64k_20k.ld
LDFLAGS  := $(MCUFLAGS) -T$(LDSCRIPT) -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map \
            -specs=nano.specs -specs=nosys.specs

OBJECTS := $(addprefix $(BUILD)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
OBJECTS += $(addprefix $(BUILD)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

# ---- host test build -------------------------------------------------------
# every tests/test_*.c is its own binary (each has a main()); the pure-logic
# production modules it exercises link alongside it.
TEST_LIBS     := app/ladder.c app/acq_stats.c app/diag.c app/trigger.c app/timebase.c \
                 app/tim3.c app/ui.c \
                 app/acq_window.c app/adc_owner.c app/measure.c app/frame.c app/waveform.c app/bench.c app/cal.c app/render.c app/continuity.c app/link.c app/link_cmd.c app/link_host.c fonts/glyph.c fonts/glcdfont.c app/settings.c app/crc16.c app/cal_table.c app/runstate.c app/wave_store.c app/ets_phase.c app/ets_epoch.c app/ets_source.c
TEST_BINARIES := $(patsubst tests/%.c,$(BUILD)/%,$(wildcard tests/test_*.c))

# ---- rules -----------------------------------------------------------------
.PHONY: all size test clean flash-dry

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).bin $(BUILD)/$(TARGET).hex size

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/build_seq.h: $(DIGEST_FILES) | $(BUILD)
	@printf '/* generated - do not edit */\n#define STATUS_BUILD_SEQ %s\n' "$(BUILD_SEQ)" > $@

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) -c $(CFLAGS) $< -o $@

# status.c carries the build identity, so it depends on the generated
# sequence header.
$(BUILD)/status.o: $(BUILD)/build_seq.h

$(BUILD)/%.o: %.s | $(BUILD)
	$(CC) -c $(ASFLAGS) $< -o $@

$(BUILD)/%: tests/%.c $(TEST_LIBS) | $(BUILD)
	@$(HOSTCC) -std=c11 -Wall -Wextra -Wno-unused-parameter -O1 \
	    -DHOST_MODE -Iapp -Ifonts $(TEST_LIBS) $< -o $@ -lm -MMD -MP

$(BUILD)/$(TARGET).elf: $(OBJECTS) $(LDSCRIPT) | $(BUILD)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

# Header dependencies.  Without these, editing a HEADER (app/ladder.h,
# app/acq.h, app/diag.h, ...) did NOT rebuild the objects that include it,
# so the flashed image silently kept the OLD header contents (found during
# the 2026-10-28 audit: the ladder channel map and the signal channel were
# still the pre-fix values inside the .bin).  -MMD -MP writes the .d files,
# the -include below feeds them back to make.
-include $(wildcard $(BUILD)/*.d)

size: $(BUILD)/$(TARGET).elf
	@$(SIZE) $<
	@sh tools/size_report.sh $(BUILD)/$(TARGET).elf

test: $(TEST_BINARIES)
	@for t in $(TEST_BINARIES); do ./$$t || exit 1; done

# Host PC-link test system (pure protocol + control policy, no hardware).
# Live bench suites (transport + streaming) live behind `make remote-live`.
remote-test:
	python3 tools/remote_run.py

remote-live:
	python3 tools/remote_run.py --live

# One command that answers "is this tree internally consistent?": builds the
# image, runs every host test, checks the flash/RAM budget and verifies that
# FLASH_HANDOVER.md still describes the artefact that was just built.
check: $(BUILD)/$(TARGET).bin test size
	@python3 tools/check_docs.py

clean:
	rm -rf $(BUILD)

# Printing the flash layout is a dry run only - it never writes to the target.
flash-dry: $(BUILD)/$(TARGET).bin
	@echo "dry run: would program $(BUILD)/$(TARGET).bin at 0x08000000"
	@echo "NOTE: the real write path is tools/flash/write_page.py (pyocd), and it"
	@echo "      refuses to run without AUTHORIZED=1.  st-flash write is NOT used:"
	@echo "      on the STM32F1 path it mass-erases the chip."

# UI
# (app/ui.c is already picked up by the app/*.c wildcard above - listing it
# twice would only duplicate the source list.)

# Host helpers for the offline preview.
#   dump_diag  - prints the diagnostic rows as text
#   panel-render - paints the whole panel with the PRODUCTION modules
#                  (app/diag.c + fonts/glyph.c) into build/panel.ppm
.PHONY: dump_diag panel-render diag-preview
dump_diag: | $(BUILD)
	@$(HOSTCC) -std=c11 -Wall -Wextra -Wno-unused-parameter -O1 \
	    -Iapp -Idrivers/display -Ifonts -Itools -I$(BUILD) -DHAVE_BUILD_SEQ \
	    tools/dump_diag.c app/diag.c -o $(BUILD)/dump_diag

panel-render: | $(BUILD)
	@$(HOSTCC) -std=c11 -Wall -Wextra -Wno-unused-parameter -O1 \
	    -Iapp -Idrivers/display -Ifonts -Itools -I$(BUILD) -DHAVE_BUILD_SEQ \
	    tools/panel_render.c app/diag.c fonts/glyph.c fonts/glcdfont.c \
	    -o $(BUILD)/panel_render
	@$(BUILD)/panel_render $(BUILD)/panel.ppm

diag-preview: panel-render
	@python3 tools/render_diag.py --ppm $(BUILD)/panel.ppm \
	    --out $(BUILD)/diag_preview.png

# static guard rails (no hardware)
guard:
	python3 tools/guard.py
