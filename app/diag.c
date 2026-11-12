#include "diag.h"

#include <stdio.h>
#include <string.h>

/* Every row is blank-padded to DIAG_COLS-1 chars (snprintf already appends
 * the NUL), so a refreshed value overwrites the stale one completely -
 * shorter numbers cannot leave ghost digits on the panel. */
static void row_fixed(char *dst)
{
    size_t len = strlen(dst);

    while (len < (DIAG_COLS - 1U)) {
        dst[len++] = ' ';
    }
    dst[len] = '\0';
}

void diag_row(char *dst, uint8_t row, const struct diag_state *st)
{
    if (dst == NULL) { return; }

    if (st == NULL) {
        snprintf(dst, DIAG_COLS, "%-*s", (int)(DIAG_COLS - 1U), "");
        return;
    }

    switch (row) {
    case 0U:
        snprintf(dst, DIAG_COLS, "B=%08lX S=%04lX",
                 (unsigned long)st->build_magic, (unsigned long)(st->build_seq & 0xFFFFU));
        break;
    case 1U:
        snprintf(dst, DIAG_COLS, "BOOT %lu",
                 (unsigned long)st->boot_count);
        break;
    case 2U:
        snprintf(dst, DIAG_COLS, "LOOP %06lu T %06lu",
                 (unsigned long)(st->main_loops % 1000000UL),
                 (unsigned long)(st->heartbeat % 1000000UL));
        break;
    case 3U:
        /* Raw RDDID (0x04) and MADCTL (0x0B).  This board's controller
         * reports 0x858552, not an ILI9341's 0x00934108, so the row shows the
         * measured value instead of decoding it into a fixed part number. */
        snprintf(dst, DIAG_COLS, "ID %06lX M %02lX",
                 (unsigned long)(st->display_id & 0xFFFFFFUL),
                 (unsigned long)(st->madctl & 0xFFUL));
        break;
    case 4U:
        snprintf(dst, DIAG_COLS, "PA0-15 %04lX",
                 (unsigned long)(st->pa_idr & 0xFFFFU));
        break;
    case 5U:
    case 6U:
    {
        /* Four raw ADC counts per row: "A0-3 0123 0456 0789 0ABC" - exactly
         * 26 characters, so the slide-switch ladder is readable straight
         * off the panel: the channel that moves when a switch is flipped is
         * the ladder input (open question U-02a). */
        uint8_t base = (row == 5U) ? 0U : 4U;

        snprintf(dst, DIAG_COLS, "A%d-%d %04lX %04lX %04lX %04lX",
                 (int)base, (int)(base + 3U),
                 (unsigned long)st->adc[base + 0U],
                 (unsigned long)st->adc[base + 1U],
                 (unsigned long)st->adc[base + 2U],
                 (unsigned long)st->adc[base + 3U]);
        break;
    }
    case 7U:
        /* Decoded slide-switch positions: "SW 1 0 2" (SEN1 SEN2 CPL). */
        snprintf(dst, DIAG_COLS, "SW %u %u %u",
                 (unsigned)st->sw_pos[0],
                 (unsigned)st->sw_pos[1],
                 (unsigned)st->sw_pos[2]);
        break;
    case 8U:
        /* Sensitivity combination + coupling: "CMB 7 CPL 1".
         * combo 0 / coupling 255 mean "no valid reading". */
        snprintf(dst, DIAG_COLS, "CMB %u CPL %u",
                 (unsigned)st->combo,
                 (unsigned)st->coupling);
        break;
    case 9U:
        /* Initialisation failure flags: "ERR 04" (04 = console UART).
         * 00 means every init step succeeded. */
        snprintf(dst, DIAG_COLS, "ERR %02lX",
                 (unsigned long)(st->init_errors & 0xFFU));
        break;
    default:
        dst[0] = '\0';
        break;
    }
    row_fixed(dst);
}

uint32_t diag_rows(char rows[DIAG_ROWS][DIAG_COLS], const struct diag_state *st)
{
    uint32_t written = 0U;
    uint8_t r;

    if (rows == NULL) { return 0U; }
    for (r = 0U; r < DIAG_ROWS; r++) {
        uint32_t len = 0U;

        diag_row(rows[r], r, st);
        while ((len < DIAG_COLS) && (rows[r][len] != '\0')) {
            len++;
        }
        written += len;
    }
    return written;
}

/* The row layout is a straight vertical stack of fixed-height text cells. */
uint16_t diag_row_x(uint8_t row)
{
    (void)row;
    return (uint16_t)DIAG_X0;
}

uint16_t diag_row_y(uint8_t row)
{
    return (uint16_t)(DIAG_Y0 + ((uint32_t)row * (uint32_t)DIAG_ROW_STEP));
}

uint8_t diag_row_fits(uint8_t row)
{
    uint32_t w = (uint32_t)(DIAG_COLS - 1U) * (uint32_t)DIAG_CELL_W
               * (uint32_t)DIAG_TEXT_SCALE;
    uint32_t h = (uint32_t)DIAG_CELL_H * (uint32_t)DIAG_TEXT_SCALE;

    if ((uint32_t)diag_row_x(row) + w > (uint32_t)DIAG_PANEL_W) {
        return 0U;
    }
    if ((uint32_t)diag_row_y(row) + h > (uint32_t)DIAG_PANEL_H) {
        return 0U;
    }
    return 1U;
}
