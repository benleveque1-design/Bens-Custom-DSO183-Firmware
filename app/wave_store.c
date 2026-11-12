/* Persistent waveform save/recall - two 1 KiB slots in USERDATA flash.
 * Header (16 B, LE) + samples (u16 LE). CRC-16/CCITT-FALSE over the whole
 * record with the CRC field zeroed. */
#include "wave_store.h"

#include "crc16.h"
#include "flash_store.h"

#include <string.h>

#ifdef HOST_MODE
/* Weak stubs so host tests that don't exercise wave_store still link.
 * A test that mocks flash provides strong definitions (overriding these). */
__attribute__((weak)) uint8_t flash_store_read_at(uint32_t addr, uint8_t *out,
                                                  uint32_t len)
{
    (void)addr; (void)out; (void)len; return 1U;
}
__attribute__((weak)) uint8_t flash_store_write_at(uint32_t addr,
                                                   const uint8_t *data,
                                                   uint32_t len)
{
    (void)addr; (void)data; (void)len; return 1U;
}
#endif

#define HDR_MAGIC_OFF  0U
#define HDR_VER_OFF    4U
#define HDR_SLOT_OFF   5U
#define HDR_LEN_OFF    6U
#define HDR_CRC_OFF    8U
#define REC_MAX (WAVE_STORE_HDR_SIZE + WAVE_STORE_MAX_SAMPLES * 2U)

/* Single shared scratch buffer for save/load/valid/validate.
 *
 * Lifetime/ownership: wave_store_* are synchronous, main-thread (menu/PC link)
 * operations, not reentrant and not called from ISR context.  No function calls
 * another wave_store_* while holding live data in the scratch buffer, so one
 * REC_MAX buffer is sufficient.  validate_rec() computes CRC in place by
 * temporarily zeroing the record's CRC field and restoring it. */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint32_t)(v >> 24);
}

static uint8_t s_scratch[REC_MAX];

static uint32_t slot_addr(uint8_t slot)
{
    return (slot == 0U) ? WAVE_SLOT_0 : WAVE_SLOT_1;
}

static uint8_t validate_rec(uint8_t *buf, uint32_t buflen, uint8_t slot,
                            uint16_t *out_len)
{
    uint16_t len, crc, want;
    uint32_t total;
    uint8_t crc0, crc1;

    if (buflen < WAVE_STORE_HDR_SIZE) { return 0U; }
    if (rd32(&buf[HDR_MAGIC_OFF]) != WAVE_STORE_MAGIC) { return 0U; }
    if (buf[HDR_VER_OFF] != WAVE_STORE_VERSION) { return 0U; }
    if (buf[HDR_SLOT_OFF] != slot) { return 0U; }
    len = rd16(&buf[HDR_LEN_OFF]);
    if ((len == 0U) || (len > WAVE_STORE_MAX_SAMPLES)) { return 0U; }
    total = (uint32_t)WAVE_STORE_HDR_SIZE + (uint32_t)len * 2U;
    if (total > buflen || total > REC_MAX) { return 0U; }
    want = rd16(&buf[HDR_CRC_OFF]);
    crc0 = buf[HDR_CRC_OFF];
    crc1 = buf[HDR_CRC_OFF + 1U];
    buf[HDR_CRC_OFF] = 0U;
    buf[HDR_CRC_OFF + 1U] = 0U;
    crc = crc16_ccitt(buf, total);
    buf[HDR_CRC_OFF] = crc0;
    buf[HDR_CRC_OFF + 1U] = crc1;
    if (crc != want) { return 0U; }
    if (out_len != NULL) { *out_len = len; }
    return 1U;
}

uint8_t wave_store_save(uint8_t slot, const uint16_t *samples, uint16_t len)
{
    uint16_t i;
    uint32_t total;

    if ((slot > 1U) || (samples == NULL) || (len == 0U) || (len > WAVE_STORE_MAX_SAMPLES)) {
        return 1U;
    }
    total = (uint32_t)WAVE_STORE_HDR_SIZE + (uint32_t)len * 2U;
    memset(s_scratch, 0xFF, total);
    wr32(&s_scratch[HDR_MAGIC_OFF], WAVE_STORE_MAGIC);
    s_scratch[HDR_VER_OFF] = WAVE_STORE_VERSION;
    s_scratch[HDR_SLOT_OFF] = slot;
    wr16(&s_scratch[HDR_LEN_OFF], len);
    wr16(&s_scratch[HDR_CRC_OFF], 0U);
    for (i = 0U; i < len; i++) {
        wr16(&s_scratch[WAVE_STORE_HDR_SIZE + (uint32_t)i * 2U], samples[i]);
    }
    wr16(&s_scratch[HDR_CRC_OFF], crc16_ccitt(s_scratch, total));
    return (flash_store_write_at(slot_addr(slot), s_scratch, total) == 0U) ? 0U : 1U;
}

uint8_t wave_store_load(uint8_t slot, uint16_t *samples, uint16_t max, uint16_t *out_len)
{
    uint16_t len, i;

    if ((slot > 1U) || (samples == NULL) || (max == 0U)) { return 1U; }
    if (flash_store_read_at(slot_addr(slot), s_scratch, REC_MAX) != 0U) { return 1U; }
    if (validate_rec(s_scratch, REC_MAX, slot, &len) == 0U) { return 1U; }
    if (len > max) { len = max; }
    for (i = 0U; i < len; i++) {
        samples[i] = rd16(&s_scratch[WAVE_STORE_HDR_SIZE + (uint32_t)i * 2U]);
    }
    if (out_len != NULL) { *out_len = len; }
    return 0U;
}

uint8_t wave_store_valid(uint8_t slot)
{
    if (slot > 1U) { return 0U; }
    if (flash_store_read_at(slot_addr(slot), s_scratch, REC_MAX) != 0U) { return 0U; }
    return validate_rec(s_scratch, REC_MAX, slot, NULL);
}
