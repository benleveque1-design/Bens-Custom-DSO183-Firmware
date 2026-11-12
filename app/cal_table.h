/* Per-sensitivity zero-offset table - pure logic, host-testable.
 *
 * The front end has a DC offset that differs for each of the nine
 * range x gain combinations the slide switches can select (3 ranges x
 * 3 gains).  The vendor firmware keeps a nine-entry table of those zero
 * levels and applies the matching entry on every switch change; without it
 * a trace is only ever centred on its own mean, so a DC input cannot be
 * seen and "zero volts" is wherever the amplifier happens to sit today.
 *
 * This module holds that table: an absolute zero level in 12-bit ADC
 * counts per combo, plus a bitmask saying which entries have actually been
 * calibrated.  An absent entry is NOT the same as a zero of 0 counts - the
 * display falls back to mean-centring for it, which is the behaviour every
 * build before this table existed had.
 *
 * Index order matches the ladder decode: combo = range * 3 + gain, both
 * levels 0..2 (0 = the lowest sensitivity/first range position).
 *
 * No HAL, no floating point, no flash: persistence lives in cal_store.
 */
#ifndef CAL_TABLE_H
#define CAL_TABLE_H

#include <stdint.h>

#define CAL_COMBO_COUNT  9
#define CAL_TABLE_BYTES  20U   /* 9 x uint16 zero + uint16 present mask */

struct cal_table {
    uint16_t zero[CAL_COMBO_COUNT];   /* zero level in ADC counts, 0..4095 */
    uint16_t present;                 /* bit n = zero[n] is calibrated     */
};

/* All entries absent. */
void cal_table_init(struct cal_table *t);

/* 1 when `idx` has been calibrated, 0 otherwise (including out-of-range). */
uint8_t cal_table_present(const struct cal_table *t, uint8_t idx);

/* The zero level for `idx`, or 0 when absent/out of range.  Callers that
 * need to distinguish "absent" must use cal_table_present() as well. */
uint16_t cal_table_zero(const struct cal_table *t, uint8_t idx);

/* Record `raw` (clamped to 0..4095) as the zero level for `idx`.
 * Returns 0 on success, 1 for a NULL table or an out-of-range index. */
uint8_t cal_table_set(struct cal_table *t, uint8_t idx, uint16_t raw);

/* Forget one entry / all entries.  Returns 0 on success, 1 for a bad index
 * (cal_table_clear_all only fails on a NULL table). */
uint8_t cal_table_clear(struct cal_table *t, uint8_t idx);
uint8_t cal_table_clear_all(struct cal_table *t);

/* Serialise to CAL_TABLE_BYTES little-endian bytes (zero[0..8] then the
 * mask).  Returns the number of bytes written, 0 on a NULL argument. */
uint32_t cal_table_pack(const struct cal_table *t, uint8_t *out);

/* Parse CAL_TABLE_BYTES bytes.  Entries whose zero is above 4095 are
 * dropped (and their present bit cleared) rather than trusted: a value
 * that large cannot be an ADC reading.  Returns 0 on success, 1 on a NULL
 * argument. */
uint8_t cal_table_unpack(const uint8_t *in, struct cal_table *t);

#endif /* CAL_TABLE_H */