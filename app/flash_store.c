/* Flash store for the reserved parameter page of the instrument.
 *
 * Register-level, no HAL: the HAL's flash helpers are built on top of the
 * same registers and carry no extra behaviour we need, but this file is the
 * one place that touches FLASH->CR/SR/KEYR, so the policy (unlock, wait,
 * verify, lock) lives in one spot and cannot drift.
 *
 * The page is 1 KiB; the parameter record is 12 bytes, so a full page
 * erase precedes every write.  Bounded waits only: a stuck flash controller
 * leaves the page erased and returns an error, never hangs.
 *
 * Deliberately NOT called from the acquisition or render path: it is a
 * configuration-time operation, and it takes ~10 ms per page.
 */
#include "flash_store.h"

#include "board.h"

#include <stdint.h>
#include <string.h>

/* STM32F1 flash controller keys (RM0008 3.3.7).  Both must be written, in
 * order, to unlock FLASH->CR; any other value re-locks immediately.
 *
 * The controller key / status / control bit macros are provided by the
 * STM32F1 CMSIS device header (stm32f103xb.h already defines FLASH_KEY1 /
 * FLASH_KEY2 / FLASH_CR_* / FLASH_SR_* with the same RM0008 bit values), so
 * they are used from there and must NOT be redefined here. */

/* Bounded spin budget: a page erase is ~10 ms at 64 MHz, so 100k spins is
 * ~1.5 ms of CPU and still 6x head-room; a dead controller must not hang
 * the boot path. */
#define FLASH_SPINS 1000000U

static uint8_t wait_bsy_clear(void)
{
    uint32_t spins = FLASH_SPINS;

    while ((FLASH->SR & FLASH_SR_BSY) != 0U) {
        if (spins == 0U) {
            return 1U;                     /* controller never went idle */
        }
        spins--;
    }
    return 0U;
}

static uint8_t flash_unlock(void)
{
    FLASH->KEYR = FLASH_KEY1;
    FLASH->KEYR = FLASH_KEY2;
    if ((FLASH->CR & FLASH_CR_LOCK) == 0U) {
        return 0U;                         /* unlocked */
    }
    /* Try the two keys once more: a spurious lock is a known F1 quirk. */
    FLASH->KEYR = FLASH_KEY1;
    FLASH->KEYR = FLASH_KEY2;
    return (uint8_t)((FLASH->CR & FLASH_CR_LOCK) != 0U);
}

static void flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

/* --- generic flash-write-at-any-address (used by cal_store and settings_store) --- */

static uint8_t flash_write_at(uint32_t addr, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint8_t rc;

    if ((data == NULL) || (len == 0U) || (len > FLASH_STORE_SIZE)) {
        return 1U;
    }

    rc = flash_unlock();
    if (rc != 0U) {
        return 1U;                         /* could not unlock the controller */
    }

    /* Any programming error from a previous run lingers in SR until the
     * controller is idle, so clear it before starting this page. */
    (void)wait_bsy_clear();
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_PGERR;

    /* Erase the whole page: PER + AR + STRT, then wait for EOP. */
    FLASH->CR |= FLASH_CR_PER;
    FLASH->AR  = addr;
    FLASH->CR |= FLASH_CR_STRT;
    if (wait_bsy_clear() != 0U) {
        FLASH->CR &= ~FLASH_CR_PER;
        flash_lock();
        return 1U;
    }
    if ((FLASH->SR & FLASH_SR_EOP) == 0U) {
        FLASH->CR &= ~FLASH_CR_PER;
        flash_lock();
        return 1U;
    }
    FLASH->SR = FLASH_SR_EOP;
    FLASH->CR &= ~FLASH_CR_PER;

    /* Program the caller's bytes, halfword by halfword.  The F1 programs
     * 16 bits at a time; the last halfword is padded with 0xFF if the
     * caller's length is odd, so the erased tail is never left half-written. */
    FLASH->CR |= FLASH_CR_PG;
    for (i = 0U; i < len; i += 2U) {
        uint16_t hw = (uint16_t)data[i];
        if ((i + 1U) < len) {
            hw = (uint16_t)(hw | ((uint16_t)data[i + 1] << 8));
        } else {
            hw = (uint16_t)(hw | 0xFF00U);  /* pad the final halfword */
        }
        *(volatile uint16_t *)(addr + i) = hw;
        if (wait_bsy_clear() != 0U) {
            FLASH->CR &= ~FLASH_CR_PG;
            flash_lock();
            return 1U;
        }
        if ((FLASH->SR & FLASH_SR_PGERR) != 0U) {
            FLASH->SR = FLASH_SR_PGERR;
            FLASH->CR &= ~FLASH_CR_PG;
            flash_lock();
            return 1U;
        }
    }
    FLASH->CR &= ~FLASH_CR_PG;

    /* Verify by reading back: a programming failure that did not set PGERR
     * still leaves the page wrong, and the whole point of this store is that
     * a bad value is worse than no value. */
    for (i = 0U; i < len; i++) {
        if (*(volatile const uint8_t *)(addr + i) != data[i]) {
            flash_lock();
            return 1U;
        }
    }

    flash_lock();
    return 0U;
}

/* Calibration page wrappers — remain at FLASH_STORE_ADDR */
uint8_t flash_store_write(const uint8_t *data, uint32_t len)
{
    return flash_write_at(FLASH_STORE_ADDR, data, len);
}

uint8_t flash_store_read(uint8_t *out, uint32_t len)
{
    return flash_store_read_at(FLASH_STORE_ADDR, out, len);
}

uint8_t flash_store_write_at(uint32_t addr, const uint8_t *data, uint32_t len)
{
    return flash_write_at(addr, data, len);
}

uint8_t flash_store_read_at(uint32_t addr, uint8_t *out, uint32_t len)
{
    uint32_t i;

    if ((out == NULL) || (len == 0U) || (len > FLASH_STORE_SIZE)) {
        return 1U;
    }

    for (i = 0U; i < len; i++) {
        out[i] = *(volatile const uint8_t *)(addr + i);
    }
    return 0U;
}