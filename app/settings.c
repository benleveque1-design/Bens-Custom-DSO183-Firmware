/* User-settings persistence - two alternating flash slots with CRC.
 *
 * The previous version stored a 12-byte record behind a magic word only, in
 * a "slot" that was actually inside the calibration page.  That had two
 * consequences: a settings save erased the calibration record (and vice
 * versa), and a torn write silently fell back to defaults with no way to
 * tell corruption from an empty device.
 *
 * The record now carries a version and a CRC-16, and it alternates between
 * two independent 1 KiB pages.  Save always targets the page that does not
 * hold the newest valid record, so the last good settings survive a power
 * cut during a save.  Load picks the valid record with the newest sequence
 * number (8-bit wrap-aware).
 */
#include "settings.h"
#include "flash_store.h"
#include "crc16.h"
#include "acq.h"
#include "trigger.h"
#include "timebase.h"

#include <stddef.h>
#include <string.h>

#ifdef HOST_MODE
#define HOST_MODE_ACTIVE 1
#else
#define HOST_MODE_ACTIVE 0
#endif

#define REC_MAGIC_OFF    0U
#define REC_VERSION_OFF  4U
#define REC_SEQ_OFF      5U
#define REC_CRC_OFF      6U
#define REC_TIMEBASE_OFF 8U
#define REC_MODE_OFF     9U
#define REC_LEVEL_OFF    10U
#define REC_RUN_OFF      12U
#define REC_STREAM_OFF   13U
#define REC_SLOPE_OFF    14U
#define REC_MARKER_OFF   15U

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static uint16_t record_crc(const uint8_t rec[SETTINGS_REC_SIZE])
{
    uint8_t tmp[SETTINGS_REC_SIZE];

    memcpy(tmp, rec, sizeof tmp);
    tmp[REC_CRC_OFF] = 0U;
    tmp[REC_CRC_OFF + 1U] = 0U;
    return crc16_ccitt(tmp, sizeof tmp);
}

static void record_build(const struct settings *s, uint8_t seq,
                         uint8_t out[SETTINGS_REC_SIZE])
{
    uint16_t crc;

    memset(out, 0, SETTINGS_REC_SIZE);
    put_u32(&out[REC_MAGIC_OFF], SETTINGS_MAGIC);
    out[REC_VERSION_OFF] = SETTINGS_VERSION;
    out[REC_SEQ_OFF]     = seq;
    out[REC_TIMEBASE_OFF] = s->timebase_idx;
    out[REC_MODE_OFF]     = s->trigger_mode;
    put_u16(&out[REC_LEVEL_OFF], s->trigger_level);
    out[REC_RUN_OFF]      = s->run;
    out[REC_STREAM_OFF]   = s->stream;
    {
        uint8_t slope = s->trigger_slope;
        if (slope > 2U) { slope = SETTINGS_SLOPE_AUTO; }
        out[REC_SLOPE_OFF]  = slope;
        out[REC_MARKER_OFF] = SETTINGS_MARKER;
    }

    crc = record_crc(out);
    out[REC_CRC_OFF]      = (uint8_t)(crc & 0xFFU);
    out[REC_CRC_OFF + 1U] = (uint8_t)((crc >> 8) & 0xFFU);
}

static uint8_t record_valid(const uint8_t rec[SETTINGS_REC_SIZE])
{
    if (get_u32(&rec[REC_MAGIC_OFF]) != SETTINGS_MAGIC) {
        return 0U;
    }
    if (rec[REC_VERSION_OFF] != SETTINGS_VERSION) {
        return 0U;
    }
    return (record_crc(rec) == get_u16(&rec[REC_CRC_OFF])) ? 1U : 0U;
}

static void record_decode(const uint8_t rec[SETTINGS_REC_SIZE],
                          struct settings *out)
{
    out->magic         = SETTINGS_MAGIC;
    out->timebase_idx  = rec[REC_TIMEBASE_OFF];
    out->trigger_mode  = rec[REC_MODE_OFF];
    out->trigger_level = get_u16(&rec[REC_LEVEL_OFF]);
    out->run           = rec[REC_RUN_OFF];
    out->stream        = rec[REC_STREAM_OFF];
    if ((rec[REC_MARKER_OFF] == SETTINGS_MARKER) && (rec[REC_SLOPE_OFF] <= 2U)) {
        out->trigger_slope = rec[REC_SLOPE_OFF];
    } else {
        out->trigger_slope = SETTINGS_SLOPE_AUTO; /* legacy/default */
    }
    out->marker = SETTINGS_MARKER;
}

void settings_pack(const struct settings *s, uint8_t out[SETTINGS_REC_SIZE])
{
    if ((s == NULL) || (out == NULL)) {
        return;
    }
    record_build(s, 0U, out);
}

uint8_t settings_unpack(const uint8_t in[SETTINGS_REC_SIZE],
                        struct settings *out)
{
    if ((in == NULL) || (out == NULL) || (record_valid(in) == 0U)) {
        return 1U;
    }
    record_decode(in, out);
    return 0U;
}

void settings_defaults(struct settings *out)
{
    if (out == NULL) {
        return;
    }
    out->magic         = SETTINGS_MAGIC;
    out->timebase_idx  = ACQ_DEFAULT_TIMEBASE;
    out->trigger_mode  = (uint8_t)TRIG_MODE_AUTO;
    out->trigger_level = ACQ_TRIGGER_LEVEL;
    out->run           = 1U;
    out->stream        = 0U;
    out->trigger_slope = SETTINGS_SLOPE_AUTO;
    out->marker        = SETTINGS_MARKER;
}

uint8_t settings_slot_newer(uint8_t a, uint8_t b)
{
    return ((int8_t)(uint8_t)(a - b) > 0) ? 1U : 0U;
}

#if HOST_MODE_ACTIVE == 0
/* The newest sequence number this firmware has seen, so a save always
 * increments it even when both slots are readable. */
static uint8_t s_seq;
#endif

uint8_t settings_load(struct settings *out)
{
    if (out == NULL) {
        return 1U;
    }
    settings_defaults(out);

#if HOST_MODE_ACTIVE == 0
    {
        uint8_t a[SETTINGS_REC_SIZE];
        uint8_t b[SETTINGS_REC_SIZE];
        uint8_t va;
        uint8_t vb;

        va = (flash_store_read_at(SETTINGS_SLOT_A, a, sizeof a) == 0U)
           && (record_valid(a) != 0U);
        vb = (flash_store_read_at(SETTINGS_SLOT_B, b, sizeof b) == 0U)
           && (record_valid(b) != 0U);

        if ((va == 0U) && (vb == 0U)) {
            return 1U;
        }
        if ((va != 0U) && (vb != 0U)) {
            if (settings_slot_newer(a[REC_SEQ_OFF], b[REC_SEQ_OFF]) != 0U) {
                record_decode(a, out);
                s_seq = a[REC_SEQ_OFF];
            } else {
                record_decode(b, out);
                s_seq = b[REC_SEQ_OFF];
            }
        } else if (va != 0U) {
            record_decode(a, out);
            s_seq = a[REC_SEQ_OFF];
        } else {
            record_decode(b, out);
            s_seq = b[REC_SEQ_OFF];
        }
        return 0U;
    }
#else
    return 1U;
#endif
}

uint8_t settings_save(const struct settings *s)
{
    if (s == NULL) {
        return 1U;
    }

#if HOST_MODE_ACTIVE == 0
    {
        uint8_t a[SETTINGS_REC_SIZE];
        uint8_t b[SETTINGS_REC_SIZE];
        uint8_t va;
        uint8_t vb;
        uint8_t rec[SETTINGS_REC_SIZE];
        uint32_t target;
        uint8_t seq;

        va = (flash_store_read_at(SETTINGS_SLOT_A, a, sizeof a) == 0U)
           && (record_valid(a) != 0U);
        vb = (flash_store_read_at(SETTINGS_SLOT_B, b, sizeof b) == 0U)
           && (record_valid(b) != 0U);

        if ((va != 0U) && (vb != 0U)) {
            if (settings_slot_newer(a[REC_SEQ_OFF], b[REC_SEQ_OFF]) != 0U) {
                seq = a[REC_SEQ_OFF];
                target = SETTINGS_SLOT_B;   /* replace the older copy */
            } else {
                seq = b[REC_SEQ_OFF];
                target = SETTINGS_SLOT_A;
            }
        } else if (va != 0U) {
            seq = a[REC_SEQ_OFF];
            target = SETTINGS_SLOT_B;
        } else if (vb != 0U) {
            seq = b[REC_SEQ_OFF];
            target = SETTINGS_SLOT_A;
        } else {
            seq = 0U;
            target = SETTINGS_SLOT_A;
        }

        seq = (uint8_t)(seq + 1U);
        record_build(s, seq, rec);
        if (flash_store_write_at(target, rec, sizeof rec) != 0U) {
            return 1U;
        }
        s_seq = seq;
        return 0U;
    }
#else
    return 1U;
#endif
}
