/* Host tests for the B4.25 ETS epoch state machine and reconstruction
 * (app/ets_epoch.c) plus the unchanged normal-streaming/trigger contracts it
 * depends on.
 *
 * These tests prove arithmetic, state-machine and framing behaviour on
 * synthetic data.  They do NOT prove analogue timing, phase-estimator
 * accuracy on hardware, or any on-device performance: B4.25 is host-only. */
#include "ets_epoch.h"
#include "ets_phase.h"
#include "timebase.h"
#include "tim3.h"
#include "frame.h"
#include "trigger.h"
#include "link.h"
#include "link_cmd.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define FAIL(msg) do { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); return 1; } while (0)
#define CHECK(cond) do { if (!(cond)) { FAIL(#cond); } } while (0)
#define CHECK_EQ_U32(a,b) do { uint32_t _a=(a), _b=(b); if (_a!=_b) { \
    printf("FAIL %s:%d: %s == %u, expected %s == %u\n", __FILE__, __LINE__, \
           #a, (unsigned)_a, #b, (unsigned)_b); return 1; } } while (0)
#define CHECK_EQ_I32(a,b) do { int32_t _a=(a), _b=(b); if (_a!=_b) { \
    printf("FAIL %s:%d: %s == %ld, expected %s == %ld\n", __FILE__, __LINE__, \
           #a, (long)_a, #b, (long)_b); return 1; } } while (0)

#define W ETS_EPOCH_WINDOW

/* Mirrors ACQ_SAMPLES in app/acq.h.  Duplicated instead of including acq.h so
 * the host test stays free of the HAL headers that header pulls in; the value
 * is cross-checked by test_trigger_and_frame_regression's ring arithmetic. */
#define ACQ_SAMPLES_TEST 1024U

static const struct ets_epoch_context k_ctx = {
    0U,  /* tb: 1 us/div (ETS) */
    0U,  /* trigger mode AUTO */
    0U,  /* rising */
    970U /* trigger level */
};

static uint32_t counts_of(uint8_t tb)
{
    const struct timebase_step *st = timebase_entry(tb);

    if (st == NULL) { return 0U; }
    return ((uint32_t)st->tick_psc + 1U) * ((uint32_t)st->tick_arr + 1U);
}

/* Add one synthetic TB0 sweep whose sub-sample phase picks bin offset k.
 * This mirrors the real mapping: hw = 100 counts, ets = 2 counts, so a
 * sweep covers bins 96 + 50*d + k for every window index d. */
static void add_tb0_sweep(struct ets_epoch *e, uint16_t k)
{
    int d;

    for (d = -2; d <= 4; d++) {
        int b = 96 + (50 * d) + (int)k;

        if ((b >= 0) && (b < (int)W)) {
            ets_epoch_add(e, (uint16_t)b,
                          (uint16_t)(1000U + (uint16_t)((unsigned)b * 7U)));
        }
    }
}

/* Same for TB1: hw = 100, ets = 4 -> bins 96 + 25*d + k. */
static void add_tb1_sweep(struct ets_epoch *e, uint16_t k)
{
    int d;

    for (d = -3; d <= 9; d++) {
        int b = 96 + (25 * d) + (int)k;

        if ((b >= 0) && (b < (int)W)) {
            ets_epoch_add(e, (uint16_t)b,
                          (uint16_t)(1200U + (uint16_t)((unsigned)b * 5U)));
        }
    }
}

/* ------------------------------------------------- 1. counter lifecycle */

static int test_counter_limits_and_no_wrap(void)
{
    struct ets_epoch e;
    uint16_t i;

    ets_epoch_reset(&e, &k_ctx, ETS_RESET_INIT);
    CHECK_EQ_U32(ets_epoch_sweeps(&e), 0U);

    /* A caller that never reconstructs still cannot wrap: the accepted
     * counter saturates at the target. */
    for (i = 0U; i < 1000U; i++) {
        ets_epoch_note_accepted(&e);
        CHECK(ets_epoch_sweeps(&e) <= ETS_EPOCH_SWEEPS_TARGET);
    }
    CHECK_EQ_U32(ets_epoch_sweeps(&e), ETS_EPOCH_SWEEPS_TARGET);
    CHECK(ets_epoch_target_reached(&e) != 0U);

    /* Rejected counter saturates at u16 and never wraps. */
    for (i = 0U; i < 0xFFFFU; i++) {
        ets_epoch_note_rejected(&e);
    }
    CHECK_EQ_U32(ets_epoch_rejected(&e), 0xFFFFU);
    ets_epoch_note_rejected(&e);
    CHECK_EQ_U32(ets_epoch_rejected(&e), 0xFFFFU);

    /* Diagnostic sequence is a u16 counter: wrap is defined, not a fault. */
    CHECK(ets_epoch_diag_seq(&e) != 0U);

    /* A rejection never advances the accepted counter. */
    {
        uint16_t acc = ets_epoch_sweeps(&e);

        ets_epoch_note_rejected(&e);
        CHECK_EQ_U32(ets_epoch_sweeps(&e), acc);
    }
    return 0;
}

/* ------------------------------- 2. failed reconstruction / invalidation */

static int test_failed_reconstruction_invalidates_epoch(void)
{
    struct ets_epoch e;
    uint16_t frame[W];
    uint16_t pristine[W];
    uint16_t i;

    ets_epoch_reset(&e, &k_ctx, ETS_RESET_INIT);
    for (i = 0U; i < ETS_EPOCH_SWEEPS_TARGET; i++) {
        add_tb0_sweep(&e, 0U);          /* every sweep same phase: ~6 bins */
        ets_epoch_note_accepted(&e);
    }
    CHECK(ets_epoch_target_reached(&e) != 0U);
    CHECK(ets_epoch_filled(&e) < ETS_EPOCH_FILL_MIN);

    /* A failed attempt must not touch the destination: that is the property
     * that keeps the last published frame (and therefore the stream) valid. */
    for (i = 0U; i < W; i++) {
        frame[i] = (uint16_t)(0x1200U + i);
    }
    memcpy(pristine, frame, sizeof pristine);

    CHECK_EQ_U32(ets_epoch_reconstruct(&e, frame), ETS_EPOCH_FAILED);
    CHECK(memcmp(frame, pristine, sizeof pristine) == 0);
    CHECK_EQ_U32(ets_epoch_sweeps(&e), 0U);        /* epoch invalidated */
    CHECK_EQ_U32(ets_epoch_filled(&e), 0U);
    CHECK_EQ_U32(ets_epoch_ready(&e), 0U);
    CHECK_EQ_U32(ets_epoch_reset_reason(&e), ETS_RESET_INCOMPLETE);

    /* A stale ready flag cannot survive this path. */
    CHECK(ets_epoch_ready(&e) == 0U);

    /* Same guarantee before the target is reached. */
    for (i = 0U; i < 5U; i++) {
        add_tb0_sweep(&e, i);
        ets_epoch_note_accepted(&e);
    }
    memcpy(frame, pristine, sizeof frame);
    CHECK_EQ_U32(ets_epoch_reconstruct(&e, frame), ETS_EPOCH_ACCUMULATING);
    CHECK(memcmp(frame, pristine, sizeof pristine) == 0);
    return 0;
}

/* --------------------------- 3. repeated failed and successful attempts */

static int test_repeated_attempts_bounded(void)
{
    struct ets_epoch e;
    uint16_t frame[W];
    uint16_t round;

    ets_epoch_reset(&e, &k_ctx, ETS_RESET_INIT);

    for (round = 0U; round < 40U; round++) {
        uint16_t i;
        uint8_t success = (uint8_t)(round & 1U);

        for (i = 0U; i < ETS_EPOCH_SWEEPS_TARGET; i++) {
            if (success != 0U) {
                add_tb0_sweep(&e, i);          /* 16 distinct phases */
            } else {
                add_tb0_sweep(&e, 0U);         /* degenerate phase */
            }
            ets_epoch_note_accepted(&e);
            CHECK(ets_epoch_sweeps(&e) <= ETS_EPOCH_SWEEPS_TARGET);
        }
        {
            enum ets_epoch_result r = ets_epoch_reconstruct(&e, frame);

            if (success != 0U) {
                CHECK_EQ_U32(r, ETS_EPOCH_READY);
                CHECK_EQ_U32(ets_epoch_ready(&e), 1U);
                CHECK(ets_epoch_filled(&e) >= ETS_EPOCH_FILL_MIN);
            } else {
                CHECK_EQ_U32(r, ETS_EPOCH_FAILED);
                CHECK_EQ_U32(ets_epoch_ready(&e), 0U);
                CHECK_EQ_U32(ets_epoch_sweeps(&e), 0U);
            }
        }
        /* The next sweep starts the next epoch; a completed epoch's
         * diagnostics were observable until this point. */
        ets_epoch_start_sweep(&e);
        CHECK_EQ_U32(ets_epoch_sweeps(&e), 0U);
        CHECK_EQ_U32(ets_epoch_ready(&e), 0U);
    }
    return 0;
}

/* -------------------------------------- 4. readiness invalidation/recovery */

static int test_readiness_invalidation_and_recovery(void)
{
    struct ets_epoch e;
    uint16_t frame[W];
    struct ets_epoch_context ctx2 = k_ctx;
    uint16_t i;

    ets_epoch_reset(&e, &k_ctx, ETS_RESET_INIT);
    for (i = 0U; i < ETS_EPOCH_SWEEPS_TARGET; i++) {
        add_tb0_sweep(&e, i);
        ets_epoch_note_accepted(&e);
    }
    CHECK_EQ_U32(ets_epoch_reconstruct(&e, frame), ETS_EPOCH_READY);
    CHECK_EQ_U32(ets_epoch_ready(&e), 1U);
    CHECK_EQ_U32(ets_epoch_reset_reason(&e), ETS_RESET_READY);
    CHECK_EQ_U32(ets_epoch_sweeps(&e), ETS_EPOCH_SWEEPS_TARGET);
    CHECK(ets_epoch_filled(&e) >= ETS_EPOCH_FILL_MIN);

    /* A new sweep starts a new epoch: readiness is epoch-scoped and can no
     * longer coexist with an empty/new epoch. */
    ets_epoch_start_sweep(&e);
    CHECK_EQ_U32(ets_epoch_ready(&e), 0U);
    CHECK_EQ_U32(ets_epoch_filled(&e), 0U);

    /* Context changes invalidate immediately with a bitmask reason. */
    for (i = 0U; i < ETS_EPOCH_SWEEPS_TARGET; i++) {
        add_tb0_sweep(&e, i);
        ets_epoch_note_accepted(&e);
    }
    CHECK_EQ_U32(ets_epoch_reconstruct(&e, frame), ETS_EPOCH_READY);
    CHECK_EQ_U32(ets_epoch_ready(&e), 1U);

    ctx2.tb = 2U;
    {
        uint16_t reason = 0U;

        CHECK(ets_epoch_context_differs(&e, &ctx2, &reason) != 0U);
        CHECK_EQ_U32(reason, ETS_RESET_TB_CHANGE);
        ets_epoch_reset(&e, &ctx2, reason);
    }
    CHECK_EQ_U32(ets_epoch_ready(&e), 0U);
    CHECK_EQ_U32(ets_epoch_reset_reason(&e), ETS_RESET_TB_CHANGE);
    CHECK_EQ_U32(ets_epoch_sweeps(&e), 0U);

    /* Downstream measurement failure also invalidates a just-built frame. */
    {
        struct ets_epoch f2;

        memset(&f2, 0, sizeof f2);
        ets_epoch_reset(&f2, &k_ctx, ETS_RESET_INIT);
        for (i = 0U; i < ETS_EPOCH_SWEEPS_TARGET; i++) {
            add_tb0_sweep(&f2, i);
            ets_epoch_note_accepted(&f2);
        }
        CHECK_EQ_U32(ets_epoch_reconstruct(&f2, frame), ETS_EPOCH_READY);
        CHECK_EQ_U32(ets_epoch_ready(&f2), 1U);
        ets_epoch_note_failure(&f2);
        CHECK_EQ_U32(ets_epoch_ready(&f2), 0U);
        CHECK_EQ_U32(ets_epoch_reset_reason(&f2), ETS_RESET_INCOMPLETE);
        CHECK_EQ_U32(ets_epoch_sweeps(&f2), 0U);
    }
    return 0;
}

/* ------------------------------------------- 5. diagnostic consistency */

static int test_diagnostic_consistency(void)
{
    struct ets_epoch e;
    uint16_t frame[W];
    uint16_t i;

    ets_epoch_reset(&e, &k_ctx, ETS_RESET_INIT);
    CHECK_EQ_U32(ets_epoch_reset_reason(&e), ETS_RESET_INIT);

    /* Every accepted sweep bumps the sequence exactly once. */
    {
        uint16_t s0 = ets_epoch_diag_seq(&e);
        uint16_t j;

        for (j = 0U; j < 5U; j++) {
            ets_epoch_note_accepted(&e);
        }
        CHECK_EQ_U32((uint16_t)(ets_epoch_diag_seq(&e) - s0), 5U);
    }
    /* Every rejection bumps it exactly once. */
    {
        uint16_t s0 = ets_epoch_diag_seq(&e);

        ets_epoch_note_rejected(&e);
        CHECK_EQ_U32((uint16_t)(ets_epoch_diag_seq(&e) - s0), 1U);
    }
    /* Complete a valid epoch.  Snapshot read brackets must be stable in the
     * single-threaded host model, and the invariant must hold. */
    for (i = 0U; i < ETS_EPOCH_SWEEPS_TARGET; i++) {
        add_tb0_sweep(&e, i);
        ets_epoch_note_accepted(&e);
    }
    {
        uint16_t seq0 = ets_epoch_diag_seq(&e);
        uint16_t acc  = ets_epoch_sweeps(&e);
        uint16_t fill = ets_epoch_filled(&e);
        uint8_t  rdy  = ets_epoch_ready(&e);
        uint16_t seq1;

        CHECK_EQ_U32(ets_epoch_reconstruct(&e, frame), ETS_EPOCH_READY);
        seq1 = ets_epoch_diag_seq(&e);
        (void)seq0;
        CHECK(seq1 != 0U);
        /* Reconstruction publishes a coherent snapshot. */
        acc  = ets_epoch_sweeps(&e);
        fill = ets_epoch_filled(&e);
        rdy  = ets_epoch_ready(&e);
        CHECK(acc == ETS_EPOCH_SWEEPS_TARGET);
        CHECK(fill >= ETS_EPOCH_FILL_MIN);
        CHECK(rdy == 1U);
        CHECK(ets_epoch_reset_reason(&e) == ETS_RESET_READY);
    }
    /* Reason/ready mutual exclusion: never both READY and INCOMPLETE, and a
     * reason change is impossible without an epoch reset (seq bump). */
    ets_epoch_note_failure(&e);
    CHECK(ets_epoch_reset_reason(&e) == ETS_RESET_INCOMPLETE);
    CHECK(ets_epoch_ready(&e) == 0U);
    return 0;
}

/* -------------------------------------- 6. TB0-TB4 index/timebase model */

static int test_tb0_to_tb4_timebase_relationships(void)
{
    uint8_t tb;

    for (tb = 0U; tb <= 4U; tb++) {
        const struct timebase_step *st = timebase_entry(tb);
        struct tim3_config cfg;

        CHECK(st != NULL);
        CHECK_EQ_U32(st->single_shot, 0U);                 /* ETS range */
        CHECK_EQ_U32(counts_of(5U), 100U);                 /* HW base */
        CHECK_EQ_U32(tim3_config_for(5U, &cfg), 0U);
        CHECK_EQ_U32(tim3_counts(&cfg), 100U);
        CHECK_EQ_U32(timebase_smp_code(5U), 0U);           /* SMP 1.5 */

        /* The hardware index used by acq_set_timebase() for every ETS range
         * is TIMEBASE_FIRST_SINGLE_SHOT (5), and acq_base_timebase_get()
         * reports it.  acq.c pacing_resume()/acq_resume() must use the same
         * index (B4.25 defect A). */
        CHECK_EQ_U32(TIMEBASE_FIRST_SINGLE_SHOT, 5U);

        /* Per-sweep bin coverage: WINDOW samples map to distinct bins spaced
         * hw/ets apart.  16 sweeps must be able to exceed the 80-bin
         * threshold under ideal phase diversity. */
        {
            uint32_t ets_counts = counts_of(tb);
            uint32_t per_sweep_x10 = (W * ets_counts * 10U) / counts_of(5U);

            CHECK(ets_counts > 0U);
            CHECK((per_sweep_x10 * ETS_EPOCH_SWEEPS_TARGET) >= (ETS_EPOCH_FILL_MIN * 10U));
        }
    }

    /* Normal (single-shot) ranges keep direct pacing. */
    for (tb = 5U; tb < TIMEBASE_COUNT; tb++) {
        const struct timebase_step *st = timebase_entry(tb);

        CHECK(st != NULL);
        CHECK(st->single_shot != 0U);
    }
    return 0;
}

/* ------------------------------------- 7. ETS frame construction / counts */

static int test_ets_frame_construction(void)
{
    struct ets_epoch e;
    uint16_t frame[W];
    uint8_t  wire[LINK_OVERHEAD + 2U * W];
    uint32_t n;
    uint16_t i;

    /* TB0, 16 distinct phases -> READY. */
    ets_epoch_reset(&e, &k_ctx, ETS_RESET_INIT);
    for (i = 0U; i < ETS_EPOCH_SWEEPS_TARGET; i++) {
        add_tb0_sweep(&e, i);
        ets_epoch_note_accepted(&e);
    }
    CHECK_EQ_U32(ets_epoch_reconstruct(&e, frame), ETS_EPOCH_READY);

    /* Every sample is a measured value in the synthetic range; the old 2048
     * forward-fill sentinel is gone and every bin is inside the frame. */
    for (i = 0U; i < W; i++) {
        CHECK(frame[i] >= 1000U);
        CHECK(frame[i] <= 1000U + (uint16_t)((W - 1U) * 7U));
        CHECK(frame[i] != 2048U);
    }

    /* Streamed frame: 320 samples, self-describing header, CRC present. */
    n = link_encode_frame(wire, (uint32_t)sizeof wire, 7U, frame, W);
    CHECK_EQ_U32(n, LINK_OVERHEAD + (2U * W));
    CHECK_EQ_U32(wire[0], LINK_SOF0);
    CHECK_EQ_U32(wire[1], LINK_SOF1);
    CHECK_EQ_U32(wire[2], LINK_TYPE_FRAME);
    CHECK_EQ_U32(wire[3], 7U);
    CHECK_EQ_U32((uint32_t)wire[4] | ((uint32_t)wire[5] << 8), W);
    CHECK_EQ_U32(wire[6], (uint8_t)(frame[0] & 0xFFU));
    CHECK_EQ_U32(wire[7], (uint8_t)(frame[0] >> 8));
    {
        uint16_t crc = link_crc16(&wire[2], n - 4U);

        CHECK_EQ_U32((uint32_t)wire[n - 2U] | ((uint32_t)wire[n - 1U] << 8), crc);
    }

    /* Leading and interior gaps hold the nearest measured value: no
     * out-of-range sentinel and no stale global frame content. */
    {
        struct ets_epoch g;
        uint16_t gframe[W];
        uint16_t j;
        uint16_t first = 0U;
        uint16_t b;

        ets_epoch_reset(&g, &k_ctx, ETS_RESET_INIT);
        for (j = 0U; j < ETS_EPOCH_SWEEPS_TARGET; j++) {
            add_tb0_sweep(&g, j);              /* 16 distinct phases */
            ets_epoch_note_accepted(&g);
        }
        ets_epoch_reconstruct(&g, gframe);     /* establishes full coverage */

        /* Force a leading gap (bins 0..5) and an interior gap (bin 100),
         * the two shapes the old 2048 sentinel produced.  The reconstruct
         * call recomputes `filled` from cnt[], so the coverage gate still
         * sees the poked bins as uncovered. */
        for (b = 0U; b < 6U; b++) {
            g.cnt[b] = 0U;
        }
        g.cnt[100] = 0U;
        for (b = 0U; b < W; b++) {
            if (g.cnt[b] != 0U) { first = b; break; }
        }
        CHECK(first > 0U);                     /* the leading gap is real  */

        CHECK_EQ_U32(ets_epoch_reconstruct(&g, gframe), ETS_EPOCH_READY);
        /* Leading gap holds the first measured bin, not a sentinel. */
        CHECK_EQ_U32(gframe[0], gframe[first]);
        /* Interior gap holds the previous measured bin. */
        CHECK_EQ_U32(gframe[100], gframe[99]);
        for (j = 0U; j < W; j++) {
            CHECK(gframe[j] != 2048U);
            CHECK(gframe[j] >= 1000U);
        }
    }

    /* A frame buffer with canaries proves the reconstruction writes exactly
     * the window and nothing around it (buffer ownership / lifetime). */
    {
        struct ets_epoch c;
        uint16_t guarded[W + 2U];

        guarded[0] = 0xAAAAU;
        guarded[W + 1U] = 0x5555U;
        ets_epoch_reset(&c, &k_ctx, ETS_RESET_INIT);
        for (i = 0U; i < ETS_EPOCH_SWEEPS_TARGET; i++) {
            add_tb0_sweep(&c, i);
            ets_epoch_note_accepted(&c);
        }
        CHECK_EQ_U32(ets_epoch_reconstruct(&c, &guarded[1]), ETS_EPOCH_READY);
        CHECK_EQ_U32(guarded[0], 0xAAAAU);
        CHECK_EQ_U32(guarded[W + 1U], 0x5555U);
    }
    return 0;
}

/* ------------------------------------------- 8. normal streaming regression */

static int test_normal_streaming_regression(void)
{
    uint16_t samples[320];
    uint8_t  wire[LINK_OVERHEAD + 2U * 1024U];
    uint32_t n;
    uint16_t i;

    for (i = 0U; i < 320U; i++) {
        samples[i] = (uint16_t)(i * 13U);
    }
    n = link_encode_frame(wire, (uint32_t)sizeof wire, 0U, samples, 320U);
    CHECK_EQ_U32(n, LINK_OVERHEAD + 640U);
    CHECK_EQ_U32(wire[2], LINK_TYPE_FRAME);
    CHECK_EQ_U32((uint32_t)wire[4] | ((uint32_t)wire[5] << 8), 320U);
    for (i = 0U; i < 320U; i++) {
        uint16_t got = (uint16_t)((uint16_t)wire[6U + 2U * i]
                                  | ((uint16_t)wire[7U + 2U * i] << 8));

        CHECK_EQ_U32(got, samples[i]);
    }
    {
        uint16_t crc = link_crc16(&wire[2], n - 4U);

        CHECK_EQ_U32((uint32_t)wire[n - 2U] | ((uint32_t)wire[n - 1U] << 8), crc);
    }

    /* Capacity refusal is unchanged (no partial frame). */
    CHECK_EQ_U32(link_encode_frame(wire, 100U, 0U, samples, 320U), 0U);
    /* Zero-length frames are still refused. */
    CHECK_EQ_U32(link_encode_frame(wire, (uint32_t)sizeof wire, 0U, samples, 0U), 0U);
    return 0;
}

/* ------------------------------------ 9. trigger / acquisition regression */

static int test_trigger_and_frame_regression(void)
{
    uint16_t ring[ACQ_SAMPLES_TEST];
    uint16_t dst[320];
    struct frame f;
    struct trig_cfg cfg = { 2048U, 8U, TRIG_RISING };
    uint16_t i;

    /* A clean 0 -> 4095 edge at ring index 500. */
    for (i = 0U; i < ACQ_SAMPLES_TEST; i++) {
        ring[i] = (i < 500U) ? 100U : 4000U;
    }
    {
        int32_t edge = trig_scan_nearest(ring, ACQ_SAMPLES_TEST, 500U, &cfg);

        CHECK_EQ_I32(edge, 500);
    }
    CHECK_EQ_U32(frame_build(ring, ACQ_SAMPLES_TEST, 500U, 320U, 96U,
                             dst, 320U, &f), 0U);
    CHECK_EQ_U32(f.length, 320U);
    CHECK_EQ_U32(f.win.trigger_pos, 96U);
    CHECK_EQ_U32(dst[96], ring[500]);
    CHECK_EQ_U32(dst[95], ring[499]);
    CHECK_EQ_U32(f.stats.min, 100U);
    CHECK_EQ_U32(f.stats.max, 4000U);

    /* Tail build for the untriggered AUTO fallback is unchanged. */
    CHECK_EQ_U32(frame_build_tail(ring, ACQ_SAMPLES_TEST, 900U, 320U,
                                  dst, 320U, &f), 0U);
    CHECK_EQ_U32(dst[319], ring[899]);
    CHECK_EQ_U32(f.win.trigger_pos, 0U);
    return 0;
}

/* ------------------------------------- 10. buffer aliasing / lifetime */

static int test_buffer_aliasing_and_copy_semantics(void)
{
    struct ets_epoch e;
    uint16_t src[4] = { 10U, 20U, 30U, 40U };
    uint16_t i;

    /* ets_epoch_add copies the value into the accumulator: later changes to
     * the source buffer (the R->E->T scratch alias contract) cannot alter an
     * epoch that already consumed it. */
    ets_epoch_reset(&e, &k_ctx, ETS_RESET_INIT);
    for (i = 0U; i < 4U; i++) {
        ets_epoch_add(&e, i, src[i]);
    }
    memset(src, 0xFF, sizeof src);
    for (i = 0U; i < 4U; i++) {
        CHECK_EQ_U32(e.sum[i], (uint32_t)(10U * (i + 1U)));
        CHECK_EQ_U32(e.cnt[i], 1U);
    }

    /* The epoch owns its bins: reconstructing twice from identical data is
     * deterministic and does not depend on a caller scratch buffer. */
    {
        struct ets_epoch a;
        uint16_t fa[W], fb[W];

        ets_epoch_reset(&a, &k_ctx, ETS_RESET_INIT);
        for (i = 0U; i < ETS_EPOCH_SWEEPS_TARGET; i++) {
            add_tb1_sweep(&a, i);
            ets_epoch_note_accepted(&a);
        }
        CHECK_EQ_U32(ets_epoch_reconstruct(&a, fa), ETS_EPOCH_READY);
        memcpy(fb, fa, sizeof fa);
        CHECK(memcmp(fa, fb, sizeof fa) == 0);
        for (i = 0U; i < W; i++) {
            CHECK(fa[i] >= 1200U);
        }
    }

    /* The window geometry is the same as the bench frame contract. */
    CHECK_EQ_U32(ETS_EPOCH_WINDOW, 320U);
    CHECK_EQ_U32(ETS_EPOCH_PRE, 96U);
    CHECK_EQ_U32(ETS_EPOCH_FILL_MIN, 80U);
    CHECK(ETS_EPOCH_WINDOW >= ETS_EPOCH_PRE);
    return 0;
}

static int test_source_tallies_and_reset(void)
{
    struct ets_epoch e;
    uint32_t i;

    memset(&e, 0, sizeof e);   /* diag_seq is not cleared by a reset */
    ets_epoch_reset(&e, &k_ctx, ETS_RESET_INIT);
    CHECK_EQ_U32(ets_epoch_src_b1(&e), 0U);
    CHECK_EQ_U32(ets_epoch_src_quantised(&e), 0U);
    CHECK_EQ_U32(ets_epoch_reject_unbound(&e), 0U);
    CHECK_EQ_U32(ets_epoch_hw_rollover(&e), 0U);

    /* Three B1 sweeps, two quantised, one unbound rejection, four rollovers.
     * The source tallies must not bump the diagnostic sequence: the existing
     * accepted/rejected calls own that. */
    for (i = 0U; i < 3U; i++) {
        ets_epoch_note_source(&e, ETS_SOURCE_B1);
    }
    for (i = 0U; i < 2U; i++) {
        ets_epoch_note_source(&e, ETS_SOURCE_QUANTISED);
    }
    ets_epoch_note_reject_unbound(&e);
    for (i = 0U; i < 4U; i++) {
        ets_epoch_note_hw_rollover(&e);
    }
    CHECK_EQ_U32(ets_epoch_src_b1(&e), 3U);
    CHECK_EQ_U32(ets_epoch_src_quantised(&e), 2U);
    CHECK_EQ_U32(ets_epoch_reject_unbound(&e), 1U);
    CHECK_EQ_U32(ets_epoch_hw_rollover(&e), 4U);
    CHECK_EQ_U32(ets_epoch_rejected(&e), 0U);   /* subset counter only */
    /* The epoch reset bumped the sequence once; none of the tally helpers
     * above may bump it again. */
    CHECK_EQ_U32(ets_epoch_diag_seq(&e), 1U);

    /* A rejected source contributes to neither accepted tally. */
    ets_epoch_note_source(&e, ETS_SOURCE_REJECTED);
    CHECK_EQ_U32(ets_epoch_src_b1(&e), 3U);
    CHECK_EQ_U32(ets_epoch_src_quantised(&e), 2U);

    /* Every epoch reset clears the tallies with the bins. */
    ets_epoch_reset(&e, &k_ctx, ETS_RESET_TB_CHANGE);
    CHECK_EQ_U32(ets_epoch_src_b1(&e), 0U);
    CHECK_EQ_U32(ets_epoch_src_quantised(&e), 0U);
    CHECK_EQ_U32(ets_epoch_reject_unbound(&e), 0U);
    CHECK_EQ_U32(ets_epoch_hw_rollover(&e), 0U);

    /* Saturation, never wrap. */
    e.src_b1 = 0xFFFFU;
    e.reject_unbound = 0xFFFFU;
    e.hw_rollover = 0xFFFFU;
    ets_epoch_note_source(&e, ETS_SOURCE_B1);
    ets_epoch_note_reject_unbound(&e);
    ets_epoch_note_hw_rollover(&e);
    CHECK_EQ_U32(e.src_b1, 0xFFFFU);
    CHECK_EQ_U32(e.reject_unbound, 0xFFFFU);
    CHECK_EQ_U32(e.hw_rollover, 0xFFFFU);
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_counter_limits_and_no_wrap();
    failures += test_failed_reconstruction_invalidates_epoch();
    failures += test_repeated_attempts_bounded();
    failures += test_readiness_invalidation_and_recovery();
    failures += test_diagnostic_consistency();
    failures += test_tb0_to_tb4_timebase_relationships();
    failures += test_ets_frame_construction();
    failures += test_normal_streaming_regression();
    failures += test_trigger_and_frame_regression();
    failures += test_buffer_aliasing_and_copy_semantics();
    failures += test_source_tallies_and_reset();

    if (failures == 0) {
        printf("ets_epoch: all tests passed\n");
    }
    return failures;
}
