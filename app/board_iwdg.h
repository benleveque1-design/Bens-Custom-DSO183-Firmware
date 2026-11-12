/* Minimal independent-watchdog driver.
 *
 * The vendored HAL in this tree has no IWDG module, and we deliberately do not
 * depend on one: the watchdog is the last line of defence and must be obvious.
 *
 * Why this matters on this instrument (all Verified):
 *   - the option bytes read USER = 0x5A, i.e. WDG_SW = 1: the IWDG can only be
 *     started by software.  The vendor firmware configures it and never starts
 *     it, which is why a freeze on a stock unit is permanent.
 *   - the v6 patch starts it with RLR = 4 and PR = 0x9C4 (/64) => an ~8 ms
 *     window.  That is short enough that a debugger halt resets the board.
 *     The custom firmware uses a configurable, generous timeout instead.
 */
#ifndef BOARD_IWDG_H
#define BOARD_IWDG_H

#include <stdint.h>

/* Start the IWDG with a timeout of `timeout_ms` milliseconds.
 * The LSI is nominally 40 kHz (RM0008) with a 30..60 kHz tolerance, so the
 * requested value is computed with the nominal figure; the real timeout can be
 * 1.5x shorter or 0.67x longer. */
void board_iwdg_init(uint32_t timeout_ms);

/* Reload the counter (call at least every timeout/2). */
void board_iwdg_feed(void);

#endif /* BOARD_IWDG_H */
