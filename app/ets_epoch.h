/* ETS epoch state machine and equivalent-time reconstruction - pure logic.
 *
 * One ETS epoch is a bounded sequence of accepted triggered sweeps whose
 * phase-mapped samples are averaged into BENCH_WINDOW bins.  This module owns
 * the counter lifecycle, the readiness state and the reconstruction policy;
 * it touches no hardware and no acquisition buffer, so every transition is
 * host-testable.  The caller (app/main.c) owns the phase estimator, the
 * acquisition ring and the destination frame.
 *
 * Contract (B4.25):
 *   - `sweeps` counts ACCEPTED sweeps only and saturates at ETS_EPOCH_SWEEPS_TARGET.
 *     It can never wrap: reaching the target makes ets_epoch_reconstruct()
 *     mandatory, and every completed or failed attempt starts a fresh epoch.
 *   - A rejected sweep never changes bins and never advances `sweeps`; it
 *     bumps `rejected` (saturating) and the diagnostic sequence.
 *   - `ready` is epoch-scoped: it becomes 1 only when a reconstruction
 *     completed with enough coverage, and is cleared as soon as a new epoch
 *     starts accumulating or the context changes.  Therefore
 *     ready == 1 implies sweeps == target and filled >= ETS_EPOCH_FILL_MIN;
 *     the stale "ready while empty" state is structurally impossible.
 *   - `reset_reason` records why the current epoch was started: the context
 *     change bits, ETS_RESET_INIT at boot, ETS_RESET_READY when the previous
 *     epoch reconstructed successfully, or ETS_RESET_INCOMPLETE when the
 *     previous attempt reached the target but produced no valid frame.
 *   - Reconstruction never invents out-of-range samples.  Uncovered bins
 *     hold the nearest measured bin value (leading gap: first measured bin;
 *     trailing/interior gap: previous bin).  The old 2048 sentinel is gone.
 *
 * After a successful reconstruction the completed epoch's diagnostics stay
 * observable (sweeps == target, filled == coverage) until the next accepted
 * or rejected sweep starts the next epoch; that keeps "ready" and "filled"
 * mutually consistent for a poll.
 */
#ifndef ETS_EPOCH_H
#define ETS_EPOCH_H

#include <stdint.h>

#include "link_cmd.h"
#include "ets_source.h"

#define ETS_EPOCH_WINDOW         320U
#define ETS_EPOCH_PRE             96U
#define ETS_EPOCH_SWEEPS_TARGET   16U
#define ETS_EPOCH_FILL_MIN        (ETS_EPOCH_WINDOW / 4U)   /* 80 bins */

enum ets_epoch_result {
    ETS_EPOCH_ACCUMULATING = 0U,  /* fewer than target sweeps accepted     */
    ETS_EPOCH_READY        = 1U,  /* frame reconstructed with enough cover */
    ETS_EPOCH_FAILED       = 2U   /* target reached, coverage too low      */
};

struct ets_epoch_context {
    uint8_t  tb;
    uint8_t  trig_mode;
    uint8_t  trig_slope;
    uint16_t trig_level;
};

struct ets_epoch {
    uint32_t sum[ETS_EPOCH_WINDOW];
    uint16_t cnt[ETS_EPOCH_WINDOW];   /* samples per bin (u16 saturating)   */
    uint16_t sweeps;                  /* accepted, 0..ETS_EPOCH_SWEEPS_TARGET */
    uint16_t rejected;                /* saturating                         */
    uint16_t filled;                  /* bins with cnt > 0, 0..WINDOW       */
    uint16_t src_b1;                  /* accepted sweeps placed by B1       */
    uint16_t src_quantised;           /* accepted sweeps placed at midpoint */
    uint16_t reject_unbound;          /* rejected: capture not bound        */
    uint16_t hw_rollover;             /* bound captures with a timer wrap   */
    uint16_t reset_reason;            /* ETS_RESET_* of the current epoch   */
    uint16_t diag_seq;                /* diagnostic update counter (u16)    */
    uint8_t  tb;
    uint8_t  trig_mode;
    uint8_t  trig_slope;
    uint8_t  ready;                   /* 1 only for a completed valid frame */
    uint8_t  complete;                /* previous epoch reconstructed OK    */
    uint16_t trig_level;
};

/* Start an epoch from scratch with this context and reason.  Clears bins,
 * counters, readiness and the completion latch; bumps the diagnostic
 * sequence once (an epoch reset is a logical diagnostic outcome). */
void ets_epoch_reset(struct ets_epoch *e, const struct ets_epoch_context *ctx,
                     uint16_t reason);

/* 1 when the context differs from the epoch's context; writes the OR of the
 * ETS_RESET_* change bits to *reason_out (0 when identical). */
uint8_t ets_epoch_context_differs(const struct ets_epoch *e,
                                  const struct ets_epoch_context *ctx,
                                  uint16_t *reason_out);

/* Called once per processed triggered sweep, before adding samples.  If the
 * previous epoch completed, start a fresh epoch now (the completed
 * diagnostics were observable until this point).  No effect otherwise. */
void ets_epoch_start_sweep(struct ets_epoch *e);

/* Add one mapped sample to a bin: sum/cnt/filled update.  `bin` must be in
 * [0, ETS_EPOCH_WINDOW); out-of-range bins are ignored defensively. */
void ets_epoch_add(struct ets_epoch *e, uint16_t bin, uint16_t value);

/* Account for an accepted sweep (after its bins were added): increments
 * `sweeps` (never above the target) and the diagnostic sequence. */
void ets_epoch_note_accepted(struct ets_epoch *e);

/* Account for a rejected ETS-eligible sweep: saturating `rejected` plus one
 * diagnostic-sequence increment.  Bins and `sweeps` are untouched. */
void ets_epoch_note_rejected(struct ets_epoch *e);

/* Record which phase source placed an accepted sweep.  Called once per
 * accepted sweep, before ets_epoch_note_accepted(); it does not touch the
 * diagnostic sequence (the accepted/rejected call owns that increment), so
 * the "one seq bump per logical outcome" rule is unchanged. */
void ets_epoch_note_source(struct ets_epoch *e, enum ets_phase_source source);

/* Record why a sweep was rejected because its hardware capture could not be
 * bound to it.  Subset of `rejected`; saturating; no additional seq bump. */
void ets_epoch_note_reject_unbound(struct ets_epoch *e);

/* Record a bound hardware capture whose TIM3 count shows at least one wrap
 * past the conversion-complete offset.  Diagnostic only; saturating; no
 * additional seq bump. */
void ets_epoch_note_hw_rollover(struct ets_epoch *e);

/* 1 when `sweeps` has reached the target and reconstruction must be tried. */
uint8_t ets_epoch_target_reached(const struct ets_epoch *e);

/* Reconstruct `frame[0..ETS_EPOCH_WINDOW)` from the accumulated bins.
 * Returns ACCUMULATING before the target (frame untouched).  At the target:
 *   - READY:  the frame is written, ready=1, reset_reason=ETS_RESET_READY,
 *             bins left in place so the completed diagnostics stay coherent
 *             until the next sweep;
 *   - FAILED: coverage < fill minimum, so the frame is left BYTE-IDENTICAL
 *             (the caller's last published frame survives) and the epoch is
 *             restarted with ETS_RESET_INCOMPLETE (ready=0, counters zeroed).
 * `frame` must have room for ETS_EPOCH_WINDOW samples. */
enum ets_epoch_result ets_epoch_reconstruct(struct ets_epoch *e,
                                            uint16_t *frame);

/* Reconstruction produced a frame but downstream measurement failed:
 * invalidate that epoch exactly like insufficient coverage. */
void ets_epoch_note_failure(struct ets_epoch *e);

/* Diagnostic accessors (read-only). */
uint16_t ets_epoch_sweeps(const struct ets_epoch *e);
uint16_t ets_epoch_rejected(const struct ets_epoch *e);
uint16_t ets_epoch_filled(const struct ets_epoch *e);
uint16_t ets_epoch_src_b1(const struct ets_epoch *e);
uint16_t ets_epoch_src_quantised(const struct ets_epoch *e);
uint16_t ets_epoch_reject_unbound(const struct ets_epoch *e);
uint16_t ets_epoch_hw_rollover(const struct ets_epoch *e);
uint16_t ets_epoch_reset_reason(const struct ets_epoch *e);
uint16_t ets_epoch_diag_seq(const struct ets_epoch *e);
uint8_t  ets_epoch_ready(const struct ets_epoch *e);

#endif /* ETS_EPOCH_H */
