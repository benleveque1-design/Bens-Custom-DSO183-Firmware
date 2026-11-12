/* TIM3 pacing timer - pure configuration layer (host-testable).
 *
 * TIM3 is the acquisition clock: one update event per sample interval, and
 * TRGO (CR2.MMS = 010 "update") is what the ADC must use as its external
 * trigger in M6.  The (PSC, ARR) pairs are baked in app/timebase.c; this
 * layer only hands them out and keeps the register width contract in one
 * place, so the hardware commit (app/tim3_hw.c) stays a handful of writes
 * with no arithmetic in it.
 *
 * Clock: TIM3 runs at 64 MHz (2 x PCLK1, see timebase.h), i.e. one count is
 * 15.625 ns.
 */
#ifndef TIM3_H
#define TIM3_H

#include <stdint.h>

/* Register-ready values.  PSC and ARR are 16-bit on the F1, so a larger
 * count must be produced by a different split - which the baked table
 * already guarantees for all 27 ranges. */
struct tim3_config {
    uint16_t psc;
    uint16_t arr;
};

/* Fill *out with the pacing values for a timebase index.
 * Returns 0 on success, 1 for a NULL pointer or an index >= TIMEBASE_COUNT.
 * On failure *out is left untouched: a bad index must not silently become
 * range 0 and pace the acquisition at the wrong rate. */
uint8_t tim3_config_for(uint8_t index, struct tim3_config *out);

/* Total counts per update period, (psc+1)*(arr+1).  0 for a NULL pointer. */
uint32_t tim3_counts(const struct tim3_config *cfg);

#endif /* TIM3_H */
