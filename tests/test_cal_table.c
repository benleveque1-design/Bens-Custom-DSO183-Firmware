/* Host unit tests for the per-sensitivity zero table (app/cal_table.c).
 *
 * The table holds nine ADC counts (range * 3 + gain) plus a present mask.
 * An absent entry must read back as zero AND be distinguishable from a
 * calibrated zero of 0 counts; serialisation keeps both, drops impossible
 * readings, and rejects NULL arguments.
 */
#include "cal_table.h"

#include <stdio.h>
#include <string.h>

#define FAIL(msg) do { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); return 1; } while (0)
#define CHECK(cond) do { if (!(cond)) { FAIL(#cond); } } while (0)

static int test_init_absent(void)
{
    struct cal_table t;
    uint8_t i;

    memset(&t, 0xAA, sizeof t);
    cal_table_init(&t);
    CHECK(t.present == 0U);
    for (i = 0U; i < CAL_COMBO_COUNT; i++) {
        CHECK(t.zero[i] == 0U);
        CHECK(cal_table_present(&t, i) == 0U);
        CHECK(cal_table_zero(&t, i) == 0U);
    }
    return 0;
}

static int test_set_present(void)
{
    struct cal_table t;

    cal_table_init(&t);
    CHECK(cal_table_set(&t, 4U, 1195U) == 0U);
    CHECK(cal_table_present(&t, 4U) == 1U);
    CHECK(cal_table_zero(&t, 4U) == 1195U);
    CHECK(cal_table_present(&t, 3U) == 0U);
    CHECK(cal_table_zero(&t, 3U) == 0U);

    /* A calibrated zero of 0 counts is legitimately present. */
    CHECK(cal_table_set(&t, 0U, 0U) == 0U);
    CHECK(cal_table_present(&t, 0U) == 1U);
    CHECK(cal_table_zero(&t, 0U) == 0U);
    return 0;
}

static int test_set_clamps(void)
{
    struct cal_table t;

    cal_table_init(&t);
    CHECK(cal_table_set(&t, 8U, 5000U) == 0U);
    CHECK(cal_table_zero(&t, 8U) == 4095U);
    CHECK(cal_table_set(&t, 8U, 4095U) == 0U);
    CHECK(cal_table_zero(&t, 8U) == 4095U);
    return 0;
}

static int test_bad_args(void)
{
    struct cal_table t;
    uint8_t  buf[CAL_TABLE_BYTES];

    cal_table_init(&t);
    CHECK(cal_table_set(&t, CAL_COMBO_COUNT, 100U) == 1U);
    CHECK(cal_table_clear(&t, CAL_COMBO_COUNT) == 1U);
    CHECK(cal_table_set(NULL, 0U, 100U) == 1U);
    CHECK(cal_table_clear(NULL, 0U) == 1U);
    CHECK(cal_table_clear_all(NULL) == 1U);
    CHECK(cal_table_present(NULL, 0U) == 0U);
    CHECK(cal_table_zero(NULL, 0U) == 0U);
    CHECK(cal_table_pack(NULL, buf) == 0U);
    CHECK(cal_table_pack(&t, NULL) == 0U);
    CHECK(cal_table_unpack(NULL, &t) == 1U);
    CHECK(cal_table_unpack(buf, NULL) == 1U);
    return 0;
}

static int test_clear(void)
{
    struct cal_table t;

    cal_table_init(&t);
    (void)cal_table_set(&t, 1U, 700U);
    (void)cal_table_set(&t, 7U, 800U);
    CHECK(cal_table_clear(&t, 1U) == 0U);
    CHECK(cal_table_present(&t, 1U) == 0U);
    CHECK(cal_table_zero(&t, 1U) == 0U);
    CHECK(cal_table_present(&t, 7U) == 1U);   /* neighbours untouched */

    CHECK(cal_table_clear_all(&t) == 0U);
    CHECK(t.present == 0U);
    CHECK(cal_table_present(&t, 7U) == 0U);
    return 0;
}

static int test_pack_unpack(void)
{
    struct cal_table t;
    struct cal_table back;
    uint8_t  buf[CAL_TABLE_BYTES];

    cal_table_init(&t);
    (void)cal_table_set(&t, 0U, 0U);          /* present, zero counts */
    (void)cal_table_set(&t, 2U, 1234U);
    (void)cal_table_set(&t, 8U, 4095U);

    memset(buf, 0, sizeof buf);
    CHECK(cal_table_pack(&t, buf) == CAL_TABLE_BYTES);

    memset(&back, 0xAA, sizeof back);
    CHECK(cal_table_unpack(buf, &back) == 0U);
    CHECK(back.present == t.present);
    CHECK(memcmp(back.zero, t.zero, sizeof t.zero) == 0);
    CHECK(cal_table_present(&back, 0U) == 1U);
    CHECK(cal_table_zero(&back, 0U) == 0U);
    CHECK(cal_table_zero(&back, 2U) == 1234U);
    CHECK(cal_table_zero(&back, 8U) == 4095U);
    CHECK(cal_table_present(&back, 1U) == 0U);
    return 0;
}

static int test_unpack_drops_impossible(void)
{
    struct cal_table t;
    uint8_t  buf[CAL_TABLE_BYTES];

    memset(buf, 0, sizeof buf);
    /* zero[0] = 0xFFFF (impossible), zero[1] = 0x1000 (4096, impossible),
     * zero[2] = 4095 (legal), mask claims 0..3 present plus junk bits. */
    buf[0] = 0xFFU; buf[1] = 0xFFU;
    buf[2] = 0x00U; buf[3] = 0x10U;
    buf[4] = 0xFFU; buf[5] = 0x0FU;
    buf[CAL_COMBO_COUNT * 2U]      = 0xFFU;
    buf[CAL_COMBO_COUNT * 2U + 1U] = 0xFFU;

    memset(&t, 0xAA, sizeof t);
    CHECK(cal_table_unpack(buf, &t) == 0U);
    CHECK(cal_table_present(&t, 0U) == 0U);
    CHECK(cal_table_present(&t, 1U) == 0U);
    CHECK(cal_table_present(&t, 2U) == 1U);
    CHECK(cal_table_zero(&t, 2U) == 4095U);
    CHECK(cal_table_present(&t, 3U) == 1U);   /* claimed, zero stays 0 */
    CHECK(cal_table_zero(&t, 3U) == 0U);
    /* Mask is truncated to nine bits: nothing above combo 8 exists. */
    CHECK((t.present & (uint16_t)~0x01FFU) == 0U);
    return 0;
}

int main(void)
{
    if (test_init_absent()) return 1;
    if (test_set_present()) return 1;
    if (test_set_clamps()) return 1;
    if (test_bad_args()) return 1;
    if (test_clear()) return 1;
    if (test_pack_unpack()) return 1;
    if (test_unpack_drops_impossible()) return 1;
    printf("All cal_table tests passed\n");
    return 0;
}