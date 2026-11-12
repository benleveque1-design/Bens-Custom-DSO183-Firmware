/* Diagnostic screen - pure content layer, host-testable.
 *
 * Replaces the coloured-bar bring-up screen with text rows that prove the
 * firmware is running and that the display bus works in BOTH directions:
 *   row 0  build identity (magic + sequence, bumped per build);
 *   row 1  boot counter (1 on a cold boot, grows on each warm reset);
 *   row 2  main-loop counter + SysTick heartbeat (advancing = alive);
 *   row 3  display controller ID and MADCTL read back over the bus
 *          (9341 / 20 expected; anything else means the read path failed);
 *   row 4  raw PA0..15 input register (input evidence, unconfigured pins);
 *   rows 5,6 raw ADC1 counts of channels 0..3 and 4..7 - the front-panel
 *          slide-switch ladder shows up here as the channel that moves;
 *   row 7  decoded switch positions SEN1/SEN2/CPL (0..2, 3 = no reading);
 *   row 8  decoded sensitivity combination (4..12) and coupling (0..2);
 *   row 9  error register (display init errors + acquisition HW errors).
 *
 * No HAL dependency: formatting only.  Rows are fixed width so the
 * firmware can overwrite values in place without ghosting.
 */
#ifndef DIAG_H
#define DIAG_H

#include <stdint.h>

#define DIAG_ROWS 10U
#define DIAG_COLS 27U          /* incl. NUL: 26 chars x 6 px x scale 2 = 312 px */

/* Panel geometry and text metrics (ILI9341 in the MADCTL 0x20 orientation:
 * the logical screen is 320 x 240).  The layout lives here, not in main.c,
 * so the "does the text fit on the panel" question is host-testable. */
#define DIAG_PANEL_W   320U
#define DIAG_PANEL_H   240U
#define DIAG_TEXT_SCALE 2U
#define DIAG_CELL_W    6U        /* glcdfont cell width  */
#define DIAG_CELL_H    8U        /* glcdfont cell height */
#define DIAG_X0        4U
#define DIAG_Y0        8U
#define DIAG_ROW_STEP  16U       /* cell height at scale 2 */

/* Number of ADC channels the screen reports (PA0..PA7). */
#define DIAG_ADC_CHANNELS 8U

struct diag_state {
    uint32_t build_magic;
    uint32_t build_seq;
    uint32_t boot_count;
    uint32_t main_loops;
    uint32_t heartbeat;
    uint32_t display_id;       /* RAW bus read of 0xD3 (see below)      */
    uint32_t madctl;           /* RAW bus read of 0x0B (see below)      */
    uint32_t pa_idr;           /* raw GPIOA input register              */
    uint16_t adc[DIAG_ADC_CHANNELS];  /* raw ADC1 counts, 0xFFFF = failed */
    uint8_t  sw_pos[3];        /* SEN1, SEN2, CPL positions (0..2, 3=bad)  */
    uint8_t  combo;            /* sensitivity combination 4..12 (0=bad)    */
    uint8_t  coupling;         /* coupling 0..2 (0xFF = bad)               */
    uint32_t init_errors;      /* STATUS_ERR_* bit field (0 = all good)    */
};

/* The driver's read returns the FIRST byte read in the MOST significant
 * position (id = (id << 8) | byte).  The ILI9341 answers 0xD3 with
 * [0x00, 0x93, 0x41, 0x08] and 0x0B with [0x20, ...], so the interesting
 * bytes are:
 *   manufacturer = (raw >> 16) & 0xFF  -> 0x93
 *   device       = (raw >>  8) & 0xFF  -> 0x41
 *   madctl byte  = (raw >> 24) & 0xFF  -> 0x20
 * (The manufacturer/device shifts are robust to whether the panel sends a
 * leading dummy byte or not.) */
#define DIAG_ID_MANUFACTURER(raw) (((raw) >> 16) & 0xFFU)
#define DIAG_ID_DEVICE(raw)       (((raw) >>  8) & 0xFFU)
#define DIAG_MADCTL_BYTE(raw)     (((raw) >> 24) & 0xFFU)

/* Fill all DIAG_ROWS row buffers (each DIAG_COLS incl. NUL).  Every row is
 * blank-padded to a FIXED width so overwriting never leaves ghost digits.
 * Returns the number of characters written across all rows (diagnostics
 * only, the caller does not branch on it). */
uint32_t diag_rows(char rows[DIAG_ROWS][DIAG_COLS],
                   const struct diag_state *st);

/* Same, for a single row (row index validated, unused rows blank). */
void diag_row(char *dst, uint8_t row, const struct diag_state *st);

/* Screen placement of a row (pixels, top-left corner of the text cell). */
uint16_t diag_row_x(uint8_t row);
uint16_t diag_row_y(uint8_t row);

/* 1 when the whole row fits inside the panel (host-checked, so the layout
 * can never silently drift off-screen). */
uint8_t diag_row_fits(uint8_t row);

#endif /* DIAG_H */
