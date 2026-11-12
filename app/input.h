/* Front-panel input reader - hardware thin layer over ADC1.
 *
 * The DSO183 reads its three slide switches (CPL, SEN1, SEN2) from a
 * resistive ladder on ADC1 inputs (analysis/m1_frontpanel_storage.md F-01:
 * raw thresholds 0x600 / 0xA00, decoded by app/ladder.c).  The vendor
 * sequence pins the channels down: SEN1 = ch6/PA6, SEN2 = ch5/PA5,
 * CPL = ch7/PA7 (see app/ladder.h).  This module still exposes a one-shot
 * reader for every channel 0..7 so the diagnostic screen can show the RAW
 * value of all of them, and so a wiring change remains visible rather than
 * being hidden by a hard-coded guess.
 *
 * Deliberate constraints:
 *   - ADC1 only (the only ADC the board has, per the vendor analysis);
 *   - channels 0..7 only: PA8..PA15 are not free (PA9/PA10 are the console
 *     UART, PA13/PA14 are SWD);
 *   - every wait is bounded - a stuck ADC must never hang the UI;
 *   - no DMA, no continuous conversion, no watchdog-triggered path: this is a
 *     poll-once-per-refresh reader, so it cannot disturb the M5 acquisition
 *     engine's DMA1 channel 1 usage of ADC1 (that engine initialises the ADC
 *     registers itself when it starts).
 */
#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

/* Channels this reader is allowed to touch (PA0..PA7). */
#define INPUT_ADC_FIRST_CHANNEL 0U
#define INPUT_ADC_LAST_CHANNEL  7U
#define INPUT_ADC_CHANNEL_COUNT  (INPUT_ADC_LAST_CHANNEL - INPUT_ADC_FIRST_CHANNEL + 1U)

/* Value stored for a channel whose conversion failed. */
#define INPUT_ADC_INVALID 0xFFFFU

/* Bring ADC1 up (clock, prescaler, analog pins, self-calibration).
 * Idempotent: safe to call more than once.
 * Returns 1 when the ADC is ready, 0 when it refused to power up or
 * calibrate (the caller records the failure - the screen must show it). */
uint8_t input_init(void);

/* One conversion on ADC1 channel `ch` (0..7).
 * Returns 0 and stores the raw 12-bit count in *out, or -1 on a bad
 * argument / a conversion that did not complete in time. */
int input_read_channel(uint8_t ch, uint16_t *out);

/* Read every channel 0..7 into dst[0..7]; failed channels get
 * INPUT_ADC_INVALID.  dst must hold INPUT_ADC_CHANNEL_COUNT entries. */
void input_scan(uint16_t *dst);

#endif /* INPUT_H */
