/* Edge anchoring policy for a hardware-latched trigger (B4.36).
 *
 * The ADC analog watchdog is a LEVEL detector: it latches on the first
 * conversion outside a one-sided band.  The index it reports is therefore
 * only guaranteed to be a sample on the trigger side of the threshold - it
 * is NOT guaranteed to be the edge sample.  Treating that index as the
 * display anchor is what produced the 50 us/div rising-edge failure: the
 * frame was built 95 samples before a mid-plateau latch, so the requested
 * edge was not in the window at all and the trace showed the *opposite*
 * transition 190 samples late.
 *
 * This module turns "the hardware said something happened near here" into
 * "this ring index is a real edge of the requested slope":
 *
 *   1. LOCAL pass - scan [latch - local_back, latch + local_fwd] for the
 *      crossing nearest the latch.  A healthy latch finds the edge at
 *      distance 0..2 and nothing changes.
 *   2. RECOVERY pass - only when the local pass found nothing: scan further
 *      back, still ending at the latch, and take the crossing nearest the
 *      latch (i.e. the most recent real edge before it).  This is what a
 *      late/mid-plateau latch needs.  The caller bounds `recover_back` from
 *      the measured capture span so the recovered edge is always
 *      publishable (`acq_win_span_ok`).
 *
 * Both passes use the same Schmitt (rearm + cross) state machine as
 * trig_scan(), so mid-band noise cannot create a phantom edge.  Pure logic,
 * no hardware: host tests drive it with synthetic rings.
 */
#ifndef ACQ_ANCHOR_H
#define ACQ_ANCHOR_H

#include <stdint.h>

#include "trigger.h"

/* Local window around the hardware latch, in samples.  Kept at the historic
 * values so a correct latch behaves exactly as before. */
#define ACQ_ANCHOR_LOCAL_BACK   128U
#define ACQ_ANCHOR_LOCAL_FWD     48U

/* Hard ceiling on the recovery search.  One period of the slowest signal a
 * single-shot range can meaningfully show is bounded by the ring; 512 samples
 * covers half of the 1024-sample ring, i.e. a full half-cycle at the fastest
 * timebase (640 samples/ms at 50 us/div).  The caller may pass a smaller
 * bound derived from the measured span; it is never allowed to exceed this. */
#define ACQ_ANCHOR_RECOVER_MAX  512U

struct acq_anchor_cfg {
    struct trig_cfg scan;      /* level, hysteresis, requested slope          */
    uint16_t local_back;       /* local pass: samples before the latch        */
    uint16_t local_fwd;        /* local pass: exclusive forward extent (the
                                * scan covers latch - local_back through
                                * latch + local_fwd - 1)                      */
    uint16_t recover_back;     /* total back bound for the recovery pass; 0 or
                                * <= local_back disables recovery (ETS)       */
};

struct acq_anchor {
    uint32_t index;            /* ring index of the chosen edge               */
    int32_t  offset;           /* signed offset from the latch (negative =
                                * the edge is before the latch)               */
    uint8_t  found;            /* 1 = a requested-slope edge was found        */
    uint8_t  recovered;        /* 1 = found only by the recovery pass         */
};

/* Pick the anchor on `ring` (size `ring_size`) for a hardware latch at
 * `latch`.  Returns 1 when `out` holds a usable edge, 0 when no crossing of
 * the requested slope exists in the searched region (out->found is 0).
 * `out` is untouched on invalid arguments. */
uint8_t acq_anchor_pick(const uint16_t *ring, uint16_t ring_size, uint16_t latch,
                        const struct acq_anchor_cfg *cfg, struct acq_anchor *out);

/* Effective ring advance from the chosen anchor to the frozen write
 * position, given the span measured from the latch:
 *
 *   effective = clamp(span - offset, 0, 0xFFFF)
 *
 * A recovered edge (offset < 0) gives a LARGER effective span, because the
 * anchor is older than the latch; the caller must re-check it with
 * acq_win_span_ok().  Pure arithmetic, exposed so the publication policy is
 * host-testable. */
uint16_t acq_anchor_effective_span(uint16_t latch_span, int32_t offset);

#endif /* ACQ_ANCHOR_H */
