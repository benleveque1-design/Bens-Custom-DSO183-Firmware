/* Host unit tests for settings persistence packing (app/settings.c).
 *
 * The on-flash record is 16 bytes with a version, a sequence number and a
 * CRC-16; these tests cover the codec, the corruption paths and the
 * slot-selection order.  Real flash I/O is not exercised here.
 */
#include "settings.h"
#include "flash_store.h"
#include "acq.h"
#include "trigger.h"
#include "timebase.h"

#include <stdio.h>
#include <string.h>

#define FAIL(msg) do { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); return 1; } while (0)
#define CHECK(cond) do { if (!(cond)) { FAIL(#cond); } } while (0)

static int test_struct_size(void)
{
    CHECK(sizeof(struct settings) == 12U);
    CHECK(SETTINGS_REC_SIZE == 16U);
    CHECK(SETTINGS_REC_SIZE <= SETTINGS_SLOT_SIZE);
    return 0;
}

static int test_pack_unpack(void)
{
    struct settings original = {
        .magic = SETTINGS_MAGIC,
        .timebase_idx = 12U,
        .trigger_mode = TRIG_MODE_NORM,
        .trigger_level = 3072U,
        .run = 0U,
        .stream = 1U,
        .trigger_slope = 1U,
        .marker = 0U
    };
    uint8_t packed[SETTINGS_REC_SIZE];
    struct settings unpacked;

    settings_pack(&original, packed);
    CHECK(settings_unpack(packed, &unpacked) == 0U);
    CHECK(unpacked.magic == original.magic);
    CHECK(unpacked.timebase_idx == original.timebase_idx);
    CHECK(unpacked.trigger_mode == original.trigger_mode);
    CHECK(unpacked.trigger_level == original.trigger_level);
    CHECK(unpacked.run == original.run);
    CHECK(unpacked.stream == original.stream);
    CHECK(unpacked.trigger_slope == 1U);
    CHECK(unpacked.marker == SETTINGS_MARKER);
    return 0;
}

static int test_rejects_corruption(void)
{
    struct settings s;
    uint8_t packed[SETTINGS_REC_SIZE];
    uint8_t bad[SETTINGS_REC_SIZE];

    settings_defaults(&s);
    settings_pack(&s, packed);

    /* Payload bit flip must fail the CRC. */
    memcpy(bad, packed, sizeof bad);
    bad[10] ^= 0x01U;
    CHECK(settings_unpack(bad, &s) == 1U);

    /* CRC field flip must fail too. */
    memcpy(bad, packed, sizeof bad);
    bad[6] ^= 0xFFU;
    CHECK(settings_unpack(bad, &s) == 1U);

    /* Wrong magic is rejected before the CRC. */
    memcpy(bad, packed, sizeof bad);
    bad[0] = 0x00U;
    CHECK(settings_unpack(bad, &s) == 1U);

    /* Wrong version is rejected. */
    memcpy(bad, packed, sizeof bad);
    bad[4] = (uint8_t)(SETTINGS_VERSION + 1U);
    CHECK(settings_unpack(bad, &s) == 1U);

    /* An all-0xFF page (erased flash) is not a record. */
    memset(bad, 0xFF, sizeof bad);
    CHECK(settings_unpack(bad, &s) == 1U);
    return 0;
}

static int test_null_arguments(void)
{
    struct settings s;
    uint8_t empty[SETTINGS_REC_SIZE] = { 0U };

    settings_defaults(&s);
    CHECK(settings_unpack(NULL, &s) == 1U);
    CHECK(settings_unpack(empty, NULL) == 1U);
    return 0;
}

static int test_slot_order(void)
{
    /* Signed 8-bit sequence comparison: 1 is newer than 0, 0 is newer than
     * 255 after wrap, and equal is not newer. */
    CHECK(settings_slot_newer(1U, 0U) == 1U);
    CHECK(settings_slot_newer(0U, 1U) == 0U);
    CHECK(settings_slot_newer(1U, 1U) == 0U);
    CHECK(settings_slot_newer(0U, 255U) == 1U);
    CHECK(settings_slot_newer(255U, 0U) == 0U);
    CHECK(settings_slot_newer(128U, 127U) == 1U);
    CHECK(settings_slot_newer(127U, 128U) == 0U);
    return 0;
}

static int test_defaults(void)
{
    struct settings s;

    settings_defaults(&s);
    CHECK(s.magic == SETTINGS_MAGIC);
    CHECK(s.timebase_idx == ACQ_DEFAULT_TIMEBASE);
    CHECK(s.trigger_mode == TRIG_MODE_AUTO);
    CHECK(s.trigger_level == ACQ_TRIGGER_LEVEL);
    CHECK(s.run == 1U);
    CHECK(s.stream == 0U);
    CHECK(s.trigger_slope == SETTINGS_SLOPE_AUTO);
    CHECK(s.marker == SETTINGS_MARKER);
    return 0;
}

int main(void)
{
    if (test_struct_size() != 0) { return 1; }
    if (test_pack_unpack() != 0) { return 1; }
    if (test_rejects_corruption() != 0) { return 1; }
    if (test_null_arguments() != 0) { return 1; }
    if (test_slot_order() != 0) { return 1; }
    if (test_defaults() != 0) { return 1; }
    puts("All settings tests passed");
    return 0;
}
