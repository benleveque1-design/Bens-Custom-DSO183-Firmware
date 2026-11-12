#define _POSIX_C_SOURCE 200809L
/* Host unit tests for the diagnostic screen content (app/diag.c). */
#include "diag.h"

#include <stdio.h>
#include <string.h>

#define FAIL(msg) do { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); return 1; } while (0)
#define CHECK(cond) do { if (!(cond)) { FAIL(#cond); } } while (0)

static int all_fixed_width(const char rows[DIAG_ROWS][DIAG_COLS])
{
    for (uint8_t r = 0U; r < DIAG_ROWS; r++) {
        size_t len = strnlen(rows[r], DIAG_COLS);

        if (len != DIAG_COLS - 1U) {
            printf("row %u len %zu\n", r, len);
            return 0;
        }
    }
    return 1;
}

static int strip_blanks(const char *src, char *dst)
{
    size_t o = 0U;

    for (size_t i = 0U; src[i] != '\0'; i++) {
        if (src[i] != ' ') { dst[o++] = src[i]; }
    }
    dst[o] = '\0';
    return (o > 0U);
}

static int test_rows(void)
{
    static char rows[DIAG_ROWS][DIAG_COLS];
    const struct diag_state st = {
        .build_magic = 0x4453424DU, .build_seq = 0x4EU,
        .boot_count = 1U, .main_loops = 123456U, .heartbeat = 789U,
        /* RAW bus reads, echo byte already discarded by the driver: 0x04
         * answers 0x85,0x85,0x52 on this panel, 0x0B answers 0x68. */
        .display_id = 0x00858552U, .madctl = 0x00000068U, .pa_idr = 0x0010U,
        .adc = { 0x0000U, 0x0123U, 0x0456U, 0x0789U,
                 0x0ABCU, 0xFFFFU, 0x1111U, 0x2222U },
        .sw_pos = { 1U, 0U, 2U }, .combo = 7U, .coupling = 0U,
        .init_errors = 0x04U,
    };
    char tight[DIAG_COLS];

    CHECK(diag_rows(rows, &st) > 0U);
    CHECK(all_fixed_width(rows));

    CHECK(strip_blanks(rows[0], tight) && strcmp(tight, "B=4453424DS=004E") == 0);
    CHECK(strip_blanks(rows[1], tight) && strcmp(tight, "BOOT1") == 0);
    CHECK(strip_blanks(rows[2], tight) && strcmp(tight, "LOOP123456T000789") == 0);
    CHECK(strip_blanks(rows[3], tight) && strcmp(tight, "ID858552M68") == 0);
    CHECK(strip_blanks(rows[4], tight) && strcmp(tight, "PA0-150010") == 0);
    CHECK(strip_blanks(rows[5], tight) &&
          strcmp(tight, "A0-30000012304560789") == 0);
    CHECK(strip_blanks(rows[6], tight) &&
          strcmp(tight, "A4-70ABCFFFF11112222") == 0);
    CHECK(strip_blanks(rows[7], tight) && strcmp(tight, "SW102") == 0);
    CHECK(strip_blanks(rows[8], tight) && strcmp(tight, "CMB7CPL0") == 0);
    CHECK(strip_blanks(rows[9], tight) && strcmp(tight, "ERR04") == 0);
    CHECK(strip_blanks(rows[DIAG_ROWS - 1U], tight) && strcmp(tight, "ERR04") == 0);
    return 0;
}

/* A failed bus read must LOOK failed: 0xFF or 0x00 everywhere must never
 * be mistaken for a valid controller ID. */
static int test_failed_read_is_visible(void)
{
    static char rows[DIAG_ROWS][DIAG_COLS];
    const struct diag_state st = {
        .display_id = 0xFFFFFFFFU, .madctl = 0x00000000U,
    };
    char tight[DIAG_COLS];

    diag_rows(rows, &st);
    CHECK(strip_blanks(rows[3], tight) && strcmp(tight, "IDFFFFFFM00") == 0);
    return 0;
}

static int test_row_single(void)
{
    static char rows[DIAG_ROWS][DIAG_COLS];
    const struct diag_state st = { .build_magic = 1U, .build_seq = 2U,
                                   .display_id = 0x00858552U, .madctl = 0x00000068U };
    char tight[DIAG_COLS];

    diag_row(rows[3], 3U, &st);
    CHECK(strip_blanks(rows[3], tight) && strcmp(tight, "ID858552M68") == 0);

    diag_row(tight, 99U, &st);                 /* out of range: blank      */
    CHECK(strip_blanks(tight, rows[0]) == 0);
    return 0;
}

static int test_null(void)
{
    CHECK(diag_rows((char (*)[DIAG_COLS])NULL, NULL) == 0U);
    return 0;
}

static int test_layout(void)
{
    /* Every row must sit fully inside the 320x240 panel. */
    for (uint8_t r = 0U; r < DIAG_ROWS; r++) {
        CHECK(diag_row_fits(r) == 1U);
    }
    CHECK(diag_row_x(0U) == DIAG_X0);
    CHECK(diag_row_y(0U) == DIAG_Y0);
    CHECK(diag_row_y(1U) == DIAG_Y0 + DIAG_ROW_STEP);
    /* the text block must not run past the bottom of the panel */
    CHECK((uint32_t)diag_row_y(DIAG_ROWS - 1U)
          + (uint32_t)DIAG_CELL_H * DIAG_TEXT_SCALE <= DIAG_PANEL_H);
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_rows();
    failures += test_failed_read_is_visible();
    failures += test_row_single();
    failures += test_null();
    failures += test_layout();

    if (failures == 0) {
        printf("diag: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
