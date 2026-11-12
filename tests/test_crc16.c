/* Host unit tests for the CRC-16/CCITT-FALSE helper (app/crc16.c).
 *
 * This is the integrity primitive behind the settings and calibration
 * records, so its exact parameters matter: polynomial 0x1021, init 0xFFFF,
 * no reflection, no final XOR.  The check value for "123456789" is 0x29B1.
 */
#include "crc16.h"

#include <stdio.h>

#define FAIL(msg) do { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); return 1; } while (0)
#define CHECK(cond) do { if (!(cond)) { FAIL(#cond); } } while (0)

static int test_check_value(void)
{
    static const uint8_t text[] = "123456789";

    CHECK(crc16_ccitt(text, 9U) == 0x29B1U);
    return 0;
}

static int test_empty_and_null(void)
{
    static const uint8_t byte = 0x00U;

    CHECK(crc16_ccitt(NULL, 0U) == 0xFFFFU);
    CHECK(crc16_ccitt(&byte, 0U) == 0xFFFFU);
    CHECK(crc16_ccitt(NULL, 4U) == 0xFFFFU);
    return 0;
}

static int test_detects_single_bit(void)
{
    uint8_t a[4] = { 0x01U, 0x02U, 0x03U, 0x04U };
    uint8_t b[4] = { 0x01U, 0x02U, 0x03U, 0x05U };

    CHECK(crc16_ccitt(a, 4U) != crc16_ccitt(b, 4U));
    return 0;
}

int main(void)
{
    if (test_check_value() != 0) { return 1; }
    if (test_empty_and_null() != 0) { return 1; }
    if (test_detects_single_bit() != 0) { return 1; }
    puts("All crc16 tests passed");
    return 0;
}
