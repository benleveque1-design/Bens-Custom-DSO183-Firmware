#include "ets_epoch.h"

#include <stddef.h>
#include <string.h>

/* Clear the accumulation and epoch-scoped diagnostics.  The context fields,
 * reset_reason and the diagnostic sequence are deliberately left alone: the
 * caller decides what the new epoch's reason is and whether that is a logical
 * diagnostic event (see ets_epoch_reset / ets_epoch_note_failure). */
static void epoch_clear(struct ets_epoch *e)
{
    memset(e->sum, 0, sizeof e->sum);
    memset(e->cnt, 0, sizeof e->cnt);
    e->sweeps   = 0U;
    e->rejected = 0U;
    e->filled   = 0U;
    e->src_b1         = 0U;
    e->src_quantised  = 0U;
    e->reject_unbound = 0U;
    e->hw_rollover    = 0U;
    e->ready    = 0U;
    e->complete = 0U;
}

void ets_epoch_reset(struct ets_epoch *e, const struct ets_epoch_context *ctx,
                     uint16_t reason)
{
    if (e == NULL) {
        return;
    }
    epoch_clear(e);
    if (ctx != NULL) {
        e->tb         = ctx->tb;
        e->trig_mode  = ctx->trig_mode;
        e->trig_slope = ctx->trig_slope;
        e->trig_level = ctx->trig_level;
    }
    e->reset_reason = reason;
    e->diag_seq++;                 /* epoch reset is a diagnostic outcome */
}

uint8_t ets_epoch_context_differs(const struct ets_epoch *e,
                                  const struct ets_epoch_context *ctx,
                                  uint16_t *reason_out)
{
    uint16_t reason = 0U;

    if ((e == NULL) || (ctx == NULL)) {
        if (reason_out != NULL) { *reason_out = 0U; }
        return 0U;
    }
    if (e->tb != ctx->tb)                     { reason |= ETS_RESET_TB_CHANGE; }
    if (e->trig_mode != ctx->trig_mode)       { reason |= ETS_RESET_TRIG_MODE_CHANGE; }
    if (e->trig_slope != ctx->trig_slope)     { reason |= ETS_RESET_TRIG_SLOPE_CHANGE; }
    if (e->trig_level != ctx->trig_level)     { reason |= ETS_RESET_TRIG_LEVEL_CHANGE; }
    if (reason_out != NULL) { *reason_out = reason; }
    return (reason != 0U) ? 1U : 0U;
}

void ets_epoch_start_sweep(struct ets_epoch *e)
{
    if (e == NULL) {
        return;
    }
    if (e->complete != 0U) {
        /* The completed epoch was observable until this sweep.  Start the
         * next one; reset_reason keeps the last outcome (ETS_RESET_READY)
         * until the next epoch end or context change. */
        epoch_clear(e);
    }
}

void ets_epoch_add(struct ets_epoch *e, uint16_t bin, uint16_t value)
{
    if ((e == NULL) || (bin >= ETS_EPOCH_WINDOW)) {
        return;
    }
    if (e->cnt[bin] == 0xFFFFU) {
        return;                    /* saturate instead of wrapping a bin */
    }
    if (e->cnt[bin] == 0U) {
        e->filled++;
    }
    e->sum[bin] += (uint32_t)value;
    e->cnt[bin]++;
}

void ets_epoch_note_accepted(struct ets_epoch *e)
{
    if (e == NULL) {
        return;
    }
    /* Bounded by construction: reconstruct is mandatory at the target and
     * every completed/failed attempt restarts the epoch.  The guard makes a
     * caller bug observable as a stall at the target, never as a wrap. */
    if (e->sweeps < ETS_EPOCH_SWEEPS_TARGET) {
        e->sweeps++;
    }
    e->diag_seq++;
}

void ets_epoch_note_rejected(struct ets_epoch *e)
{
    if (e == NULL) {
        return;
    }
    if (e->rejected < 0xFFFFU) {
        e->rejected++;
    }
    e->diag_seq++;
}

void ets_epoch_note_source(struct ets_epoch *e, enum ets_phase_source source)
{
    if (e == NULL) {
        return;
    }
    if (source == ETS_SOURCE_B1) {
        if (e->src_b1 < 0xFFFFU) {
            e->src_b1++;
        }
    } else if (source == ETS_SOURCE_QUANTISED) {
        if (e->src_quantised < 0xFFFFU) {
            e->src_quantised++;
        }
    }
    /* Rejected sources are accounted by ets_epoch_note_rejected(). */
}

void ets_epoch_note_reject_unbound(struct ets_epoch *e)
{
    if ((e != NULL) && (e->reject_unbound < 0xFFFFU)) {
        e->reject_unbound++;
    }
}

void ets_epoch_note_hw_rollover(struct ets_epoch *e)
{
    if ((e != NULL) && (e->hw_rollover < 0xFFFFU)) {
        e->hw_rollover++;
    }
}

uint8_t ets_epoch_target_reached(const struct ets_epoch *e)
{
    return ((e != NULL) && (e->sweeps >= ETS_EPOCH_SWEEPS_TARGET)) ? 1U : 0U;
}

enum ets_epoch_result ets_epoch_reconstruct(struct ets_epoch *e,
                                            uint16_t *frame)
{
    uint16_t i;
    uint16_t first = 0U;
    uint16_t filled = 0U;

    if ((e == NULL) || (frame == NULL)) {
        return ETS_EPOCH_FAILED;
    }
    if (e->sweeps < ETS_EPOCH_SWEEPS_TARGET) {
        return ETS_EPOCH_ACCUMULATING;
    }

    /* Coverage first: a failed attempt must not write a single byte into the
     * caller's frame, so the last published frame survives intact. */
    for (i = 0U; i < ETS_EPOCH_WINDOW; i++) {
        if (e->cnt[i] != 0U) {
            if (filled == 0U) {
                first = i;
            }
            filled++;
        }
    }
    e->filled = filled;

    if (filled < ETS_EPOCH_FILL_MIN) {
        /* Not enough of the equivalent-time window was visited: no valid
         * frame.  Invalidate the epoch instead of accumulating past the
         * target (the B4.24 overflow/wrap path). */
        epoch_clear(e);
        e->reset_reason = ETS_RESET_INCOMPLETE;
        e->diag_seq++;
        return ETS_EPOCH_FAILED;
    }

    /* Coverage >= fill minimum, so `first` is a measured bin.  Hold the
     * nearest measured value across uncovered bins: leading gap takes the
     * first measured bin, interior/trailing gaps take the previous one.
     * Every emitted sample is therefore a measured mean, never a synthetic
     * sentinel (the old 2048 forward-fill). */
    for (i = first; i < ETS_EPOCH_WINDOW; i++) {
        frame[i] = (e->cnt[i] != 0U)
                 ? (uint16_t)(e->sum[i] / (uint32_t)e->cnt[i])
                 : frame[i - 1U];
    }
    for (i = 0U; i < first; i++) {
        frame[i] = frame[first];
    }

    e->ready = 1U;
    e->complete = 1U;
    e->reset_reason = ETS_RESET_READY;
    e->diag_seq++;
    return ETS_EPOCH_READY;
}

void ets_epoch_note_failure(struct ets_epoch *e)
{
    if (e == NULL) {
        return;
    }
    epoch_clear(e);
    e->reset_reason = ETS_RESET_INCOMPLETE;
    e->diag_seq++;
}

uint16_t ets_epoch_sweeps(const struct ets_epoch *e)
{
    return (e != NULL) ? e->sweeps : 0U;
}

uint16_t ets_epoch_rejected(const struct ets_epoch *e)
{
    return (e != NULL) ? e->rejected : 0U;
}

uint16_t ets_epoch_filled(const struct ets_epoch *e)
{
    return (e != NULL) ? e->filled : 0U;
}

uint16_t ets_epoch_src_b1(const struct ets_epoch *e)
{
    return (e != NULL) ? e->src_b1 : 0U;
}

uint16_t ets_epoch_src_quantised(const struct ets_epoch *e)
{
    return (e != NULL) ? e->src_quantised : 0U;
}

uint16_t ets_epoch_reject_unbound(const struct ets_epoch *e)
{
    return (e != NULL) ? e->reject_unbound : 0U;
}

uint16_t ets_epoch_hw_rollover(const struct ets_epoch *e)
{
    return (e != NULL) ? e->hw_rollover : 0U;
}

uint16_t ets_epoch_reset_reason(const struct ets_epoch *e)
{
    return (e != NULL) ? e->reset_reason : 0U;
}

uint16_t ets_epoch_diag_seq(const struct ets_epoch *e)
{
    return (e != NULL) ? e->diag_seq : 0U;
}

uint8_t ets_epoch_ready(const struct ets_epoch *e)
{
    return ((e != NULL) && (e->ready != 0U)) ? 1U : 0U;
}
