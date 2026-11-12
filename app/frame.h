/* Frame assembly - one call from "ring buffer + trigger position" to
 * "linear window + measurements", pure and host-testable.
 *
 * This is the seam the hardware path will use: the acquisition engine owns
 * the DMA ring and the trigger index, this module owns the arithmetic that
 * turns them into something the renderer and the readouts can use.  Keeping
 * it hardware-free means the wrap, the pre-trigger split and the statistics
 * are all testable without a board, and the future freeze/restart code only
 * has to hand over the same three numbers.
 */
#ifndef FRAME_H
#define FRAME_H

#include <stdint.h>

#include "acq_window.h"
#include "measure.h"

struct frame {
    struct acq_win       win;     /* where the window sits in the ring      */
    struct measure_stats stats;   /* over the samples actually copied       */
    uint16_t             length;  /* samples in the destination buffer      */
};

/* Build one frame.
 *
 *   ring        circular sample buffer (ACQ_SAMPLES entries when live)
 *   ring_size   its length in samples
 *   trig_index  the free-running DMA counter value at the trigger
 *   window      wanted window length (<= ring_size)
 *   pre         samples before the trigger (0 .. window-1)
 *   dst         destination, at least `cap` samples
 *   cap         destination capacity
 *
 * Returns 0 on success, 1 on a parameter error (nothing is written beyond
 * `dst[0]`; `out` is left with length 0), and 2 when the window was valid
 * but the destination was too small - the statistics then describe the
 * truncated window.  Truncation is reported rather than hidden because a
 * short frame looks perfectly plausible on screen otherwise.
 */
uint8_t frame_build(const uint16_t *ring, uint16_t ring_size,
                    uint32_t trig_index, uint16_t window, uint16_t pre,
                    uint16_t *dst, uint16_t cap, struct frame *out);

/* Build the untriggered AUTO-fallback frame: the `window` samples ending at
 * `write_index` (the DMA write position at freeze), i.e. the newest data.
 *
 * Same return codes and same `out` contract as frame_build, but no trigger
 * is implied: out->win.trigger_pos is 0.  Callers use this when the gate
 * reports triggered == 0; placing an untriggered window as if a trigger
 * existed would show a discontinuity mid-screen. */
uint8_t frame_build_tail(const uint16_t *ring, uint16_t ring_size,
                         uint32_t write_index, uint16_t window,
                         uint16_t *dst, uint16_t cap, struct frame *out);

#endif /* FRAME_H */
