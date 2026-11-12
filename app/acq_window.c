#include "acq_window.h"

#include <stddef.h>

uint8_t acq_win_place(uint16_t buf_size, uint32_t trigger_index,
                      uint16_t window, uint16_t pre, struct acq_win *out)
{
    uint32_t trig;

    if ((out == NULL) || (buf_size == 0U)) {
        return 1U;
    }
    if ((window < 2U) || (window > buf_size) || (pre >= window)) {
        return 1U;
    }

    /* The DMA write position is a free-running counter: reduce it once,
     * then subtract the pre-trigger share with an unsigned wrap. */
    trig = trigger_index % (uint32_t)buf_size;

    out->start = (uint16_t)((trig + (uint32_t)buf_size - (uint32_t)pre)
                            % (uint32_t)buf_size);
    out->length = window;
    out->trigger_pos = pre;
    return 0U;
}

uint8_t acq_win_place_tail(uint16_t buf_size, uint32_t write_pos,
                           uint16_t window, struct acq_win *out)
{
    if ((out == NULL) || (buf_size == 0U)) {
        return 1U;
    }
    if ((window < 2U) || (window > buf_size)) {
        return 1U;
    }

    /* write_pos is a free-running counter: reduce once, then step back a
     * whole window with an unsigned wrap (the sample before index 0 is the
     * last one the DMA wrote). */
    out->start = (uint16_t)(((write_pos % (uint32_t)buf_size)
                             + (uint32_t)buf_size - (uint32_t)window)
                            % (uint32_t)buf_size);
    out->length = window;
    out->trigger_pos = 0U;          /* no trigger in a tail window */
    return 0U;
}

uint16_t acq_ring_advance(uint16_t prev, uint16_t pos, uint16_t size)
{
    if ((size == 0U) || (prev >= size) || (pos >= size)) {
        return 0U;
    }
    return (uint16_t)(((uint32_t)pos + (uint32_t)size - (uint32_t)prev)
                      % (uint32_t)size);
}

uint16_t acq_win_copy(const uint16_t *buf, uint16_t buf_size,
                      const struct acq_win *w, uint16_t *dst, uint16_t cap)
{
    uint16_t n;
    uint16_t i;
    uint16_t idx;

    if ((buf == NULL) || (w == NULL) || (dst == NULL) || (buf_size == 0U)) {
        return 0U;
    }
    if ((w->length == 0U) || (w->start >= buf_size)) {
        return 0U;
    }

    n = (w->length < cap) ? w->length : cap;
    idx = w->start;
    for (i = 0U; i < n; i++) {
        dst[i] = buf[idx];
        idx++;
        if (idx >= buf_size) {
            idx = 0U;                  /* one wrap is enough for any window */
        }
    }
    return n;
}
