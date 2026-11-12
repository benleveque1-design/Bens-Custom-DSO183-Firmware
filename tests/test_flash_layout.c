/* Host unit tests for the flash storage layout (app/flash_store.h).
 *
 * The audit found the settings record sharing the calibration page, which
 * made the two records erase each other.  These assertions pin the fixed
 * layout: two independent 1 KiB settings slots, then the calibration page,
 * then the vendor device record, all inside the last 4 KiB of the part and
 * all 1 KiB-page aligned.
 */
#include "flash_store.h"
#include "settings.h"
#include "cal_store.h"

#include <stdio.h>

#define FAIL(msg) do { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); return 1; } while (0)
#define CHECK(cond) do { if (!(cond)) { FAIL(#cond); } } while (0)

#define PAGE_SIZE  1024UL
#define FLASH_END  0x08010000UL

static int test_alignment(void)
{
    CHECK((SETTINGS_SLOT_A % PAGE_SIZE) == 0U);
    CHECK((SETTINGS_SLOT_B % PAGE_SIZE) == 0U);
    CHECK((FLASH_STORE_ADDR % PAGE_SIZE) == 0U);
    CHECK((FLASH_DEVICE_ADDR % PAGE_SIZE) == 0U);
    return 0;
}

static int test_no_overlap(void)
{
    /* Settings slots are separate pages and do not reach the calibration
     * page; calibration does not reach the device record. */
    CHECK(SETTINGS_SLOT_B == (SETTINGS_SLOT_A + PAGE_SIZE));
    CHECK(FLASH_STORE_ADDR >= (SETTINGS_SLOT_B + PAGE_SIZE));
    CHECK(FLASH_DEVICE_ADDR == (FLASH_STORE_ADDR + FLASH_STORE_SIZE));
    CHECK(SETTINGS_REC_SIZE <= SETTINGS_SLOT_SIZE);
    CHECK(CAL_STORE_REC_SIZE <= FLASH_STORE_SIZE);
    CHECK(CAL_STORE_REC_SIZE <= PAGE_SIZE);
    CHECK(FLASH_STORE_SIZE == PAGE_SIZE);
    return 0;
}

static int test_inside_part(void)
{
    /* The vendor device record is the last 1 KiB page of the 64 KiB part. */
    CHECK(FLASH_DEVICE_ADDR >= 0x0800FC00U);
    CHECK((FLASH_DEVICE_ADDR + PAGE_SIZE) <= FLASH_END);
    CHECK((SETTINGS_SLOT_A + (3U * PAGE_SIZE)) <= FLASH_END);
    return 0;
}

static int test_device_record_untouched(void)
{
    /* Nothing may place a settings or calibration record in the vendor page. */
    CHECK(FLASH_DEVICE_ADDR != SETTINGS_SLOT_A);
    CHECK(FLASH_DEVICE_ADDR != SETTINGS_SLOT_B);
    CHECK(FLASH_DEVICE_ADDR != FLASH_STORE_ADDR);
    return 0;
}

int main(void)
{
    if (test_alignment() != 0) { return 1; }
    if (test_no_overlap() != 0) { return 1; }
    if (test_inside_part() != 0) { return 1; }
    if (test_device_record_untouched() != 0) { return 1; }
    puts("All flash layout tests passed");
    return 0;
}
