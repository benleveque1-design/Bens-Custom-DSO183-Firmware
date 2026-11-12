/* Continuity detection - pure logic, host-testable.
 *
 * A short to ground pulls the input to ~0 counts, so "connected" is simply
 * "the whole window sits below a low threshold".  The two thresholds give
 * hysteresis: an open input may drift a little and must not chatter.
 *
 * There is no documented buzzer on this board (no beep/ piezo references in
 * the vendor analysis), so the result is shown on the panel and reported on
 * the console instead of being audible.
 */
#ifndef CONTINUITY_H
#define CONTINUITY_H

#include <stdint.h>

/* Defaults: ~50 mV to close, ~100 mV to open (3.3 V reference, 12-bit). */
#define CONTINUITY_ON_COUNTS   60U
#define CONTINUITY_OFF_COUNTS  120U

/* Feed the window maximum (the highest sample in the frame).  Returns the
 * new state: 1 = continuity, 0 = open.  `prev` is kept while the level sits
 * between the two thresholds. */
uint8_t continuity_update(uint8_t prev, uint16_t level,
                          uint16_t on_counts, uint16_t off_counts);

#endif /* CONTINUITY_H */
