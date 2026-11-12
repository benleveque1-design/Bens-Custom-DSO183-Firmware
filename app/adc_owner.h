/* ADC1 ownership - pure state machine, host-testable.
 *
 * ADC1 has one regular conversion group, and two subsystems want it:
 *   - app/acq.c   : continuous DMA conversion of the signal channel;
 *   - app/input.c : one-shot reads of the front-panel ladder channels.
 *
 * They cannot run at the same time: input_read_channel() rewrites SQR3, so a
 * poll in the middle of an acquisition would retarget the DMA stream at a
 * ladder channel and quietly produce nonsense in both.  The firmware
 * therefore pauses the acquisition (acq_pause() releases the token), scans
 * the ladder from the regular group, and resumes (acq_resume() re-acquires
 * the token).  A scan attempted without that pause is refused with "busy"
 * rather than silently corrupting the acquisition.
 *
 * Deliberately no HAL and no allocation - the same file runs in the host
 * tests and on the target.
 */
#ifndef ADC_OWNER_H
#define ADC_OWNER_H

#include <stdint.h>

enum adc_owner {
    ADC_OWNER_NONE  = 0U,   /* free                                       */
    ADC_OWNER_ACQ   = 1U,   /* continuous acquisition (DMA)               */
    ADC_OWNER_INPUT = 2U,   /* one-shot ladder read                       */
    ADC_OWNER_COUNT = 3U
};

/* Take the ADC for `who`.  Returns 0 on success, 1 if another owner holds it
 * or `who` is not a valid owner value.  Re-taking it as its current owner is
 * idempotent (returns 0, changes nothing): the token does NOT count
 * references, so one release frees it. */
uint8_t adc_owner_acquire(uint8_t who);

/* Give the ADC back.  Returns 0 on success; 1 if `who` is not the current
 * owner (a release by a non-owner must never clear someone else's token). */
uint8_t adc_owner_release(uint8_t who);

/* Current owner (ADC_OWNER_NONE when free). */
uint8_t adc_owner_get(void);

/* Boot/reset helper: back to ADC_OWNER_NONE. */
void adc_owner_reset(void);

#endif /* ADC_OWNER_H */
