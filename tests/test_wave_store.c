/* Host tests for persistent waveform store (app/wave_store.c).
 * Provides a mock flash backing store (strong symbols override HOST_MODE
 * weak stubs in wave_store.c). */
#include "wave_store.h"
#include "flash_store.h"
#include "crc16.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

/* ---- mock flash (2 x 1 KiB slots) ------------------------------------ */
static uint8_t g_flash[2048];
static uint8_t g_force_read_fail;
static uint8_t g_force_write_fail;

static int slot_of(uint32_t addr)
{
    if (addr == WAVE_SLOT_0) return 0;
    if (addr == WAVE_SLOT_1) return 1;
    return -1;
}

uint8_t flash_store_read_at(uint32_t addr, uint8_t *out, uint32_t len)
{
    int s = slot_of(addr);
    if (g_force_read_fail || s < 0 || out == NULL || len > 1024U) return 1U;
    memcpy(out, &g_flash[s * 1024], len);
    return 0U;
}

uint8_t flash_store_write_at(uint32_t addr, const uint8_t *data, uint32_t len)
{
    int s = slot_of(addr);
    if (g_force_write_fail || s < 0 || data == NULL || len > 1024U) return 1U;
    memcpy(&g_flash[s * 1024], data, len);
    if (len < 1024U) memset(&g_flash[s * 1024 + len], 0xFF, 1024U - len);
    return 0U;
}

static void flash_erase_all(void)
{
    memset(g_flash, 0xFF, sizeof(g_flash));
    g_force_read_fail = 0U;
    g_force_write_fail = 0U;
}

static void fill_samples(uint16_t *s, uint16_t n, uint16_t base)
{
    uint16_t i;
    for (i = 0U; i < n; i++) s[i] = (uint16_t)(base + i * 3U);
}

static void test_round_trip(void)
{
    uint16_t in[256], out[256];
    uint16_t len = 0U;
    flash_erase_all();
    fill_samples(in, 256U, 100U);
    CHECK(wave_store_save(0U, in, 256U) == 0U);
    CHECK(wave_store_valid(0U) == 1U);
    memset(out, 0, sizeof(out));
    CHECK(wave_store_load(0U, out, 256U, &len) == 0U);
    CHECK(len == 256U);
    CHECK(memcmp(in, out, sizeof(in)) == 0);
}

static void test_both_slots_independent(void)
{
    uint16_t a[64], b[128], oa[128], ob[128];
    uint16_t la = 0U, lb = 0U;
    flash_erase_all();
    fill_samples(a, 64U, 1U);
    fill_samples(b, 128U, 500U);
    CHECK(wave_store_save(0U, a, 64U) == 0U);
    CHECK(wave_store_save(1U, b, 128U) == 0U);
    CHECK(wave_store_load(0U, oa, 128U, &la) == 0U);
    CHECK(wave_store_load(1U, ob, 128U, &lb) == 0U);
    CHECK(la == 64U && lb == 128U);
    CHECK(memcmp(a, oa, 64U * 2U) == 0);
    CHECK(memcmp(b, ob, 128U * 2U) == 0);
}

static void test_valid_then_load_reuse(void)
{
    uint16_t in[32], out[32];
    uint16_t len = 0U;
    flash_erase_all();
    fill_samples(in, 32U, 7U);
    CHECK(wave_store_save(1U, in, 32U) == 0U);
    CHECK(wave_store_valid(1U) == 1U);
    CHECK(wave_store_load(1U, out, 32U, &len) == 0U);
    CHECK(len == 32U && memcmp(in, out, 64U) == 0);
}

static void test_repeated_ops(void)
{
    uint16_t in[16], out[16];
    uint16_t len;
    int i;
    flash_erase_all();
    for (i = 0; i < 5; i++) {
        fill_samples(in, 16U, (uint16_t)(i * 10));
        CHECK(wave_store_save(0U, in, 16U) == 0U);
        CHECK(wave_store_valid(0U) == 1U);
        CHECK(wave_store_load(0U, out, 16U, &len) == 0U);
        CHECK(len == 16U && memcmp(in, out, 32U) == 0);
    }
}

static void test_corrupted_crc(void)
{
    uint16_t in[8], out[8];
    uint16_t len = 0U;
    flash_erase_all();
    fill_samples(in, 8U, 0U);
    CHECK(wave_store_save(0U, in, 8U) == 0U);
    g_flash[20] ^= 0x5AU; /* corrupt sample data -> CRC mismatch */
    CHECK(wave_store_valid(0U) == 0U);
    CHECK(wave_store_load(0U, out, 8U, &len) != 0U);
}

static void test_bad_header(void)
{
    uint16_t in[8], out[8];
    uint16_t len = 0U;
    flash_erase_all();
    fill_samples(in, 8U, 0U);
    CHECK(wave_store_save(0U, in, 8U) == 0U);
    g_flash[0] = 0x00U; /* magic */
    CHECK(wave_store_valid(0U) == 0U);
    CHECK(wave_store_load(0U, out, 8U, &len) != 0U);

    CHECK(wave_store_save(0U, in, 8U) == 0U);
    g_flash[4] = 0xFFU; /* version */
    CHECK(wave_store_valid(0U) == 0U);

    CHECK(wave_store_save(0U, in, 8U) == 0U);
    g_flash[5] = 1U; /* slot field mismatch for slot 0 */
    CHECK(wave_store_valid(0U) == 0U);

    CHECK(wave_store_save(0U, in, 8U) == 0U);
    g_flash[6] = 0U; g_flash[7] = 0U; /* len 0 */
    CHECK(wave_store_valid(0U) == 0U);
}

static void test_invalid_args(void)
{
    uint16_t in[8], out[8];
    uint16_t len = 0U;
    fill_samples(in, 8U, 0U);
    CHECK(wave_store_save(2U, in, 8U) != 0U);
    CHECK(wave_store_save(0U, NULL, 8U) != 0U);
    CHECK(wave_store_save(0U, in, 0U) != 0U);
    CHECK(wave_store_save(0U, in, WAVE_STORE_MAX_SAMPLES + 1U) != 0U);
    CHECK(wave_store_load(2U, out, 8U, &len) != 0U);
    CHECK(wave_store_load(0U, NULL, 8U, &len) != 0U);
    CHECK(wave_store_load(0U, out, 0U, &len) != 0U);
    CHECK(wave_store_valid(2U) == 0U);
}

static void test_empty_flash(void)
{
    uint16_t out[8];
    uint16_t len = 0U;
    flash_erase_all();
    CHECK(wave_store_valid(0U) == 0U);
    CHECK(wave_store_valid(1U) == 0U);
    CHECK(wave_store_load(0U, out, 8U, &len) != 0U);
}

static void test_io_failures(void)
{
    uint16_t in[8], out[8];
    uint16_t len = 0U;
    flash_erase_all();
    fill_samples(in, 8U, 0U);
    g_force_write_fail = 1U;
    CHECK(wave_store_save(0U, in, 8U) != 0U);
    g_force_write_fail = 0U;
    CHECK(wave_store_save(0U, in, 8U) == 0U);
    g_force_read_fail = 1U;
    CHECK(wave_store_valid(0U) == 0U);
    CHECK(wave_store_load(0U, out, 8U, &len) != 0U);
}

static void test_load_truncates_to_max(void)
{
    uint16_t in[64], out[16];
    uint16_t len = 0U;
    flash_erase_all();
    fill_samples(in, 64U, 3U);
    CHECK(wave_store_save(0U, in, 64U) == 0U);
    memset(out, 0xAA, sizeof(out));
    CHECK(wave_store_load(0U, out, 16U, &len) == 0U);
    CHECK(len == 16U);
    CHECK(memcmp(in, out, 32U) == 0);
}

int main(void)
{
    test_round_trip();
    test_both_slots_independent();
    test_valid_then_load_reuse();
    test_repeated_ops();
    test_corrupted_crc();
    test_bad_header();
    test_invalid_args();
    test_empty_flash();
    test_io_failures();
    test_load_truncates_to_max();
    if (failures == 0) {
        printf("wave_store: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
