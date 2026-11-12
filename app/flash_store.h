#ifndef FLASH_STORE_H
#define FLASH_STORE_H

#include <stdint.h>

/* Flash storage / user-data map (1 KiB pages, STM32F103C8).
 *
 *   0x0800E800  waveform slot 0  (user data, preserved across image writes)
 *   0x0800EC00  waveform slot 1  (user data, preserved across image writes)
 *   0x0800F000  settings slot A  (written alternately with slot B)
 *   0x0800F400  settings slot B
 *   0x0800F800  calibration record
 *   0x0800FC00  vendor device record - NEVER written
 *
 * USERDATA_BASE..0x08010000 is reserved user data: the safe image flasher
 * refuses app images that extend into it, so waveform/settings/cal/vendor data
 * survives firmware updates. Waveform slots are written only by firmware
 * (flash_store_write_at) or explicit tooling, never by a normal image flash.
 */
#define USERDATA_BASE     0x0800E800UL
#define WAVE_SLOT_0       0x0800E800UL
#define WAVE_SLOT_1       0x0800EC00UL
#define WAVE_SLOT_SIZE    1024U

#define SETTINGS_SLOT_A   0x0800F000UL
#define SETTINGS_SLOT_B   0x0800F400UL
#define SETTINGS_SLOT_SIZE 1024U

/* Calibration page (cal_store.c). */
#define FLASH_STORE_ADDR  0x0800F800UL
#define FLASH_STORE_SIZE  1024U

/* Vendor device record page - read-only, never passed to a write function. */
#define FLASH_DEVICE_ADDR 0x0800FC00UL

/* Erase the calibration page and write `len` bytes of `data` to it.
 * Returns 0 on success, 1 on a controller/erase/program/verify failure.
 * `len` must be <= FLASH_STORE_SIZE and the caller must keep the buffer
 * valid until this returns. */
uint8_t flash_store_write(const uint8_t *data, uint32_t len);

/* Read `len` bytes from the calibration page into `out`. */
uint8_t flash_store_read(uint8_t *out, uint32_t len);

/* Erase the page at `addr` (1 KiB aligned) and write `len` bytes of `data`
 * to it.  Returns 0 on success, 1 on a controller/erase/program/verify
 * failure.  `len` must be <= FLASH_STORE_SIZE. */
uint8_t flash_store_write_at(uint32_t addr, const uint8_t *data, uint32_t len);

/* Read `len` bytes from `addr` into `out`. */
uint8_t flash_store_read_at(uint32_t addr, uint8_t *out, uint32_t len);

#endif /* FLASH_STORE_H */
