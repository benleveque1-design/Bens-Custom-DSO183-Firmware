/* Acquisition window placement - pure logic, host-testable.
 *
 * acq.c fills a CIRCULAR DMA buffer and latches the write position when the
 * analog watchdog fires.  What the display needs is a LINEAR window with the
 * trigger at a known column, so the two operations are separated here:
 *
 *   acq_win_place()  - arithmetic only: which circular index starts the
 *                      window, where the trigger sits inside it;
 *   acq_win_copy()   - bounded copy out of the circular buffer, handling the
 *                      wrap, into a caller-provided linear array.
 *
 * Neither function touches hardware: M6 can therefore choose when to stop
 * the DMA (freeze) without this module caring how it was done.
 *
 * The trigger is placed `pre` samples from the left edge, so `pre` is the
 * pre-trigger share of the window (pre = window / 2 centres it; pre = 0 puts
 * the trigger on the first column, the way the vendor's TIM4_CCR1 offset is
 * normally set up).
 */
#ifndef ACQ_WINDOW_H
#define ACQ_WINDOW_H

#include <stdint.h>

struct acq_win {
    uint16_t start;        /* first sample, circular index                 */
    uint16_t length;       /* samples in the window                        */
    uint16_t trigger_pos;  /* offset of the trigger sample inside it       */
};

/* Compute the window that ends `window - pre` samples after the trigger.
 *
 * buf_size       circular buffer length, 1..65535
 * trigger_index  buffer write position at the trigger (any u32; reduced mod
 *                buf_size, so a free-running DMA counter needs no masking)
 * window         samples wanted, 2..buf_size
 * pre            samples before the trigger, 0..window-1
 *
 * Returns 0 on success, 1 for any invalid argument (out is untouched).
 */
uint8_t acq_win_place(uint16_t buf_size, uint32_t trigger_index,
                      uint16_t window, uint16_t pre, struct acq_win *out);

/* Place the window so it ENDS at `write_pos`: the most recent sample is
 * write_pos-1 and the window is the `window` samples before it.  This is the
 * AUTO fallback placement used when no trigger fired - it must show the
 * newest data, not a slice anchored on a trigger that does not exist.
 * write_pos is reduced modulo buf_size, like acq_win_place's trigger_index.
 * Returns 0 on success, 1 for any invalid argument (out is untouched). */
uint8_t acq_win_place_tail(uint16_t buf_size, uint32_t write_pos,
                           uint16_t window, struct acq_win *out);

/* Circular distance from `prev` to `pos`: how many samples the write
 * position advanced, modulo size.  Used by the acquisition engine to wait
 * for a known number of post-trigger samples without losing a whole lap.
 * Returns 0 when either position is out of range or size is 0. */
uint16_t acq_ring_advance(uint16_t prev, uint16_t pos, uint16_t size);

/* Copy the window out of the circular buffer into dst (cap entries).
 * Returns the number of samples copied: min(length, cap), 0 on bad args. */
uint16_t acq_win_copy(const uint16_t *buf, uint16_t buf_size,
                      const struct acq_win *w, uint16_t *dst, uint16_t cap);

#endif /* ACQ_WINDOW_H */
