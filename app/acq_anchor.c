#include "acq_anchor.h"

#include <stddef.h>

/* Nearest simple-threshold crossing, used only as the LOCAL-pass fallback the
 * pre-B4.36 firmware already had (strict Schmitt can fail when level +/- hyst
 * lies outside the waveform).  The recovery pass deliberately does not use
 * it: a late latch must be anchored on a hysteresis-qualified edge, not on a
 * single noisy sample. */
static int32_t scan_simple_ring_nearest(const uint16_t *ring, uint16_t ring_size,
                                        uint16_t start, uint32_t n,
                                        uint32_t center,
                                        const struct trig_cfg *cfg)
{
    uint32_t best_dist = 0xFFFFFFFFUL;
    uint32_t best = 0U;
    uint32_t i;

    if ((ring == NULL) || (cfg == NULL) || (ring_size < 2U) || (n < 2U)) {
        return -1;
    }
    if (center >= n) {
        center = n - 1U;
    }
    for (i = 1U; i < n; i++) {
        uint16_t p = ring[((uint32_t)start + i - 1U) % (uint32_t)ring_size];
        uint16_t c = ring[((uint32_t)start + i) % (uint32_t)ring_size];
        uint8_t cross = (cfg->slope == TRIG_FALLING)
                      ? (((p > cfg->level) && (c <= cfg->level)) ? 1U : 0U)
                      : (((p < cfg->level) && (c >= cfg->level)) ? 1U : 0U);

        if (cross != 0U) {
            uint32_t dist = (i >= center) ? (i - center) : (center - i);

            if (dist < best_dist) {
                best_dist = dist;
                best = i;
            }
        }
    }
    return (best_dist == 0xFFFFFFFFUL) ? -1 : (int32_t)best;
}

uint8_t acq_anchor_pick(const uint16_t *ring, uint16_t ring_size, uint16_t latch,
                        const struct acq_anchor_cfg *cfg, struct acq_anchor *out)
{
    uint32_t back;
    uint32_t fwd;
    uint32_t n;
    uint32_t center;
    uint16_t start;
    int32_t  edge;

    if ((ring == NULL) || (cfg == NULL) || (out == NULL)
        || (ring_size < 2U) || (latch >= ring_size)) {
        return 0U;
    }

    back = cfg->local_back;
    fwd  = cfg->local_fwd;

    /* LOCAL pass: keep the historic behaviour for a healthy latch. */
    if ((back + fwd + 1U) > (uint32_t)ring_size) {
        /* Never examine a sample twice: shrink the window, keeping the
         * forward part (it covers the DMA/ISR lag). */
        uint32_t room = (uint32_t)ring_size - 1U;

        if (fwd >= room) {
            fwd = room - 1U;
            back = 1U;
        } else {
            back = room - fwd;
        }
    }
    n = back + fwd;
    if (n < 2U) {
        return 0U;
    }
    center = back;
    start = (uint16_t)(((uint32_t)latch + (uint32_t)ring_size - back)
                       % (uint32_t)ring_size);
    edge = trig_scan_ring_nearest(ring, ring_size, start, n, center, &cfg->scan);
    if (edge < 0) {
        /* Behaviour parity with the pre-B4.36 refinement: a level near a
         * signal extreme can defeat the Schmitt rearm inside this window. */
        edge = scan_simple_ring_nearest(ring, ring_size, start, n, center,
                                        &cfg->scan);
    }
    if (edge >= 0) {
        out->index = ((uint32_t)start + (uint32_t)edge) % (uint32_t)ring_size;
        out->offset = (int32_t)edge - (int32_t)center;
        out->found = 1U;
        out->recovered = 0U;
        return 1U;
    }

    /* RECOVERY pass: a late latch sits on the plateau, so the true edge is
     * behind the local window.  Search the bounded region that still ends at
     * the latch and take the crossing nearest to it. */
    if (cfg->recover_back <= cfg->local_back) {
        out->found = 0U;
        out->recovered = 0U;
        return 0U;
    }
    back = cfg->recover_back;
    if (back > ACQ_ANCHOR_RECOVER_MAX) {
        back = ACQ_ANCHOR_RECOVER_MAX;
    }
    fwd = cfg->local_fwd;
    if ((back + fwd + 1U) > (uint32_t)ring_size) {
        uint32_t room = (uint32_t)ring_size - 1U;

        if (fwd >= room) {
            fwd = room - 1U;
            back = 1U;
        } else {
            back = room - fwd;
        }
    }
    n = back + fwd;
    if (n < 2U) {
        out->found = 0U;
        out->recovered = 0U;
        return 0U;
    }
    center = back;
    start = (uint16_t)(((uint32_t)latch + (uint32_t)ring_size - back)
                       % (uint32_t)ring_size);
    edge = trig_scan_ring_nearest(ring, ring_size, start, n, center, &cfg->scan);
    if (edge < 0) {
        out->found = 0U;
        out->recovered = 0U;
        return 0U;
    }
    out->index = ((uint32_t)start + (uint32_t)edge) % (uint32_t)ring_size;
    out->offset = (int32_t)edge - (int32_t)center;
    out->found = 1U;
    out->recovered = 1U;
    return 1U;
}

uint16_t acq_anchor_effective_span(uint16_t latch_span, int32_t offset)
{
    int32_t eff = (int32_t)latch_span - offset;

    if (eff < 0) {
        return 0U;
    }
    if (eff > 0xFFFF) {
        return 0xFFFFU;
    }
    return (uint16_t)eff;
}
