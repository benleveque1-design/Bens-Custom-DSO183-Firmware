/* Calibration persistence: .noinit RAM for warm-reset survival (fast path)
 * plus the reserved calibration flash page for cold-boot survival (the
 * authoritative store).  On write both are updated; on read the flash page
 * wins because it is the one that survives a power cycle.
 *
 * The flash record is 40 bytes with a version and a CRC-16:
 *   0..3   magic 'CAL2'
 *   4      version (2)
 *   5      reserved (0)
 *   6..7   CRC-16/CCITT-FALSE over all 40 bytes with these two zeroed
 *   8..11  offset_uv (int32, LE)     base calibration
 *   12..15 gain_q16  (int32, LE)
 *   16..35 zero table (9 x uint16 + uint16 present mask, see cal_table.h)
 *   36..39 reserved (0)
 * A torn or corrupted write therefore fails the CRC and falls back to the
 * .noinit copy (warm reset) or the identity calibration (cold boot), instead
 * of loading half-written values.
 */
#include "cal_store.h"
#include "cal.h"
#include "cal_table.h"
#include "flash_store.h"
#include "crc16.h"

#include <stddef.h>
#include <string.h>

#define CAL_STORE_MAGIC 0x43414C32UL   /* 'CAL2' */
#define CAL_STORE_VERSION 2U
#define CAL_REC_SIZE CAL_STORE_REC_SIZE
#define CAL_CRC_OFF 6U
#define CAL_OFFSET_OFF 8U
#define CAL_GAIN_OFF 12U
#define CAL_TABLE_OFF 16U

/* Identity base calibration: 1 count = 1 uV, no offset. */
#define CAL_IDENTITY_GAIN 65536L

struct cal_state {
    int32_t offset_uv;
    int32_t gain_q16;
    struct cal_table tab;
    uint32_t magic;
};

/* .noinit keeps the last written values across a warm reset.  The magic word
 * is the only thing that tells a real copy from whatever RAM powered up with,
 * so every accessor goes through live_state(). */
static struct cal_state s_cal
    __attribute__((section(".noinit"), used));

static void state_identity(struct cal_state *st)
{
    st->offset_uv = 0;
    st->gain_q16  = CAL_IDENTITY_GAIN;
    cal_table_init(&st->tab);
    st->magic     = CAL_STORE_MAGIC;
}

static struct cal_state *live_state(void)
{
    if (s_cal.magic != CAL_STORE_MAGIC) {
        state_identity(&s_cal);
    }
    return &s_cal;
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static void pack_state(const struct cal_state *st, uint8_t out[CAL_REC_SIZE])
{
    uint16_t crc;
    uint8_t  tab[CAL_TABLE_BYTES];

    memset(out, 0, CAL_REC_SIZE);
    put_u32(&out[0], CAL_STORE_MAGIC);
    out[4] = CAL_STORE_VERSION;
    put_u32(&out[CAL_OFFSET_OFF], (uint32_t)st->offset_uv);
    put_u32(&out[CAL_GAIN_OFF], (uint32_t)st->gain_q16);

    if (cal_table_pack(&st->tab, tab) == CAL_TABLE_BYTES) {
        memcpy(&out[CAL_TABLE_OFF], tab, sizeof tab);
    }
    /* A table that could not be packed stays all-zero (no entry present),
     * which the display reads as "uncalibrated" - never as a valid zero. */

    crc = crc16_ccitt(out, CAL_REC_SIZE);
    out[CAL_CRC_OFF] = (uint8_t)(crc & 0xFFU);
    out[CAL_CRC_OFF + 1U] = (uint8_t)((crc >> 8) & 0xFFU);
}

static uint8_t unpack_state(const uint8_t in[CAL_REC_SIZE], struct cal_state *out)
{
    uint8_t  tmp[CAL_REC_SIZE];
    uint16_t crc;
    uint8_t  tab[CAL_TABLE_BYTES];

    if (get_u32(&in[0]) != CAL_STORE_MAGIC) {
        return 1U;
    }
    if (in[4] != CAL_STORE_VERSION) {
        return 1U;
    }
    memcpy(tmp, in, sizeof tmp);
    tmp[CAL_CRC_OFF] = 0U;
    tmp[CAL_CRC_OFF + 1U] = 0U;
    crc = crc16_ccitt(tmp, sizeof tmp);
    if (crc != get_u16(&in[CAL_CRC_OFF])) {
        return 1U;
    }

    out->offset_uv = (int32_t)get_u32(&in[CAL_OFFSET_OFF]);
    out->gain_q16  = (int32_t)get_u32(&in[CAL_GAIN_OFF]);
    memcpy(tab, &in[CAL_TABLE_OFF], sizeof tab);
    if (cal_table_unpack(tab, &out->tab) != 0U) {
        cal_table_init(&out->tab);
    }
    out->magic = CAL_STORE_MAGIC;
    return 0U;
}

void cal_store_init(void)
{
    uint8_t page[CAL_REC_SIZE];
    struct cal_state from_flash;

    if (flash_store_read(page, sizeof page) == 0U) {
        if (unpack_state(page, &from_flash) == 0U) {
            s_cal = from_flash;
            return;
        }
    }
    (void)live_state();   /* keep a valid warm copy, else install identity */
}

uint8_t cal_store_read(struct cal *out)
{
    uint8_t page[CAL_REC_SIZE];
    struct cal_state from_flash;

    if (out == NULL) {
        return 1U;
    }
    /* Flash is authoritative: it survives a cold boot that .noinit does not. */
    if (flash_store_read(page, sizeof page) == 0U) {
        if (unpack_state(page, &from_flash) == 0U) {
            /* Refresh the warm copy as well, so the base pair and the zero
             * table always come from the same record. */
            s_cal = from_flash;
            *out = (struct cal){ from_flash.offset_uv, from_flash.gain_q16 };
            return 0U;
        }
    }
    if (live_state()->magic != CAL_STORE_MAGIC) {
        return 1U;   /* uncalibrated / cold */
    }
    out->offset_uv = s_cal.offset_uv;
    out->gain_q16  = s_cal.gain_q16;
    return 0U;
}

uint8_t cal_store_write(const struct cal *c)
{
    struct cal_state *st = live_state();
    uint8_t rec[CAL_REC_SIZE];

    /* Always refresh the warm-reset copy so a warm reset still finds the
     * last value if the flash write is interrupted. */
    if (c != NULL) {
        st->offset_uv = c->offset_uv;
        st->gain_q16  = c->gain_q16;
    }
    st->magic = CAL_STORE_MAGIC;

    pack_state(st, rec);
    return flash_store_write(rec, sizeof rec);
}

uint8_t cal_store_write_table(void)
{
    return cal_store_write(NULL);
}

const struct cal_table *cal_table_active(void)
{
    return &live_state()->tab;
}

struct cal_table *cal_table_mutable(void)
{
    return &live_state()->tab;
}