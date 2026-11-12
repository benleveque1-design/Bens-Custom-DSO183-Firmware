/* Calibration persistence: the reserved calibration flash page plus a
 * .noinit warm-reset copy of the same data.
 *
 * The store holds two things that belong together because they are both
 * per-instrument, both edited from the calibration menu, and both change
 * rarely enough that one page erase per edit is acceptable:
 *
 *   - the base linear calibration (`struct cal`, offset + Q16 gain) used to
 *     turn counts into microvolts;
 *   - the nine-entry per-sensitivity zero table (`struct cal_table`) that the
 *     display uses to put a known 0 V input on the centre line.
 *
 * Reads prefer the flash page because it survives a power cycle; a warm
 * reset before the flash write completes still finds the .noinit copy.
 */
#ifndef CAL_STORE_H
#define CAL_STORE_H

#include <stdint.h>

#include "cal.h"
#include "cal_table.h"

/* The whole persisted record (base pair + zero table) must fit one page. */
#define CAL_STORE_REC_SIZE 40U

/* Load the persisted calibration into the .noinit cache.  Falls back to a
 * valid warm-reset copy, then to the identity calibration.  Call once at
 * boot, before cal_store_read(). */
void cal_store_init(void);

/* Read the base calibration.  Returns 0 when a calibration exists, 1 when
 * the instrument is uncalibrated (out then receives the identity pair). */
uint8_t cal_store_read(struct cal *out);

/* Persist the base calibration plus the live zero table.  Returns 0 on
 * success, 1 when the flash write failed (the .noinit copy is still
 * updated, so a warm reset keeps the new values). */
uint8_t cal_store_write(const struct cal *c);

/* Persist the live base calibration plus the live zero table - used after a
 * zero-table edit, where the base pair has not changed.  Same return. */
uint8_t cal_store_write_table(void);

/* The live zero table (never NULL).  Read-only view for the display. */
const struct cal_table *cal_table_active(void);

/* Mutable view for the calibration menu.  Editing it does NOT persist;
 * call cal_store_write_table() when the edit is final. */
struct cal_table *cal_table_mutable(void);

#endif /* CAL_STORE_H */