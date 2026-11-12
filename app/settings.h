#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

/* Persisted user settings record.
 *
 * Stored as a 16-byte record in one of two independent 1 KiB flash pages
 * (SETTINGS_SLOT_A / SETTINGS_SLOT_B).  A save always writes the page that
 * does NOT hold the newest valid record, so a torn write can only destroy
 * the copy being replaced: the previous valid record survives and is used
 * on the next boot.
 *
 * Record layout (little endian):
 *   0..3   magic 'STA1'
 *   4      version (1)
 *   5      sequence number (wraps at 255; newer = signed difference > 0)
 *   6..7   CRC-16/CCITT-FALSE over all 16 bytes with these two zeroed
 *   8      timebase index
 *   9      trigger mode
 *   10..11 trigger level (ADC counts)
 *   12     run flag
 *   13     stream flag
 *   14     trigger_slope (0 rise,1 fall,2 auto) - valid when marker set
 *   15     marker (0x5A => byte 14 is trigger_slope; legacy 0 => auto)
 */
#define SETTINGS_MAGIC    0x53544154UL   /* 'STA1' */
#define SETTINGS_VERSION  1U
#define SETTINGS_REC_SIZE 16U
#define SETTINGS_MARKER   0x5AU
#define SETTINGS_SLOPE_AUTO 2U

struct settings {
    uint32_t magic;
    uint8_t  timebase_idx;
    uint8_t  trigger_mode;
    uint16_t trigger_level;
    uint8_t  run;
    uint8_t  stream;
    uint8_t  trigger_slope; /* 0 rise,1 fall,2 auto */
    uint8_t  marker;        /* persisted marker (in-memory canonical 0x5A) */
};

/* Load settings from the newest valid slot.  Returns 0 when a valid record
 * was found, 1 when none existed (the caller gets defaults). */
uint8_t settings_load(struct settings *out);

/* Save settings to the older slot (never erasing the newest valid record).
 * Returns 0 on success, 1 on a flash failure or when compiled for the host. */
uint8_t settings_save(const struct settings *s);

/* Serialise `s` into a 16-byte record (sequence number 0, CRC included). */
void settings_pack(const struct settings *s, uint8_t out[SETTINGS_REC_SIZE]);

/* Validate and decode a 16-byte record.  Returns 0 on success, 1 when the
 * magic, version or CRC is wrong. */
uint8_t settings_unpack(const uint8_t in[SETTINGS_REC_SIZE], struct settings *out);

/* Defaults for a cold boot / missing record. */
void settings_defaults(struct settings *out);

/* Sequence comparison used to pick the newest slot: returns 1 when `a` is
 * newer than `b` using 8-bit wrap-around. */
uint8_t settings_slot_newer(uint8_t a, uint8_t b);

#endif /* SETTINGS_H */
