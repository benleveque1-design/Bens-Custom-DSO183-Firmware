#include "frame.h"

#include <stddef.h>

/* Shared tail: copy the placed window and measure it.  Both entry points
 * differ only in HOW the window is placed, so the copy/measure/report
 * policy lives in one place. */
static uint8_t frame_finish(const uint16_t *ring, uint16_t ring_size,
                            uint16_t window, uint16_t *dst, uint16_t cap,
                            struct frame *out)
{
    uint16_t copied;

    copied = acq_win_copy(ring, ring_size, &out->win, dst, cap);
    if (copied == 0U) {
        return 1U;
    }
    out->length = copied;

    if (measure_compute(dst, copied, &out->stats) != 0U) {
        out->length = 0U;
        return 1U;
    }
    return (copied < window) ? 2U : 0U;
}

uint8_t frame_build(const uint16_t *ring, uint16_t ring_size,
                    uint32_t trig_index, uint16_t window, uint16_t pre,
                    uint16_t *dst, uint16_t cap, struct frame *out)
{
    if (out == NULL) {
        return 1U;
    }
    out->length = 0U;
    out->stats.n = 0U;

    if ((ring == NULL) || (dst == NULL) || (cap == 0U)) {
        return 1U;
    }
    if (acq_win_place(ring_size, trig_index, window, pre, &out->win) != 0U) {
        return 1U;
    }
    return frame_finish(ring, ring_size, window, dst, cap, out);
}

uint8_t frame_build_tail(const uint16_t *ring, uint16_t ring_size,
                         uint32_t write_index, uint16_t window,
                         uint16_t *dst, uint16_t cap, struct frame *out)
{
    if (out == NULL) {
        return 1U;
    }
    out->length = 0U;
    out->stats.n = 0U;

    if ((ring == NULL) || (dst == NULL) || (cap == 0U)) {
        return 1U;
    }
    if (acq_win_place_tail(ring_size, write_index, window,
                           &out->win) != 0U) {
        return 1U;
    }
    return frame_finish(ring, ring_size, window, dst, cap, out);
}
