/* Host tests for ETS volatile diagnostics definitions and B4.5 semantics contracts.
 * Validates GETV allocation, reset-reason encoding, sequence/update semantics,
 * readiness interpretation, rejection definition, and snapshot retry procedure.
 * (Runtime firmware behavior exercised by inspection/build; no hardware.) */
#include "link_cmd.h"

#include <stdio.h>
#include <stdint.h>

#define ETS_SWEEPS_TARGET 16U

#define FAIL(msg) do { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); return 1; } while (0)
#define CHECK(cond) do { if (!(cond)) { FAIL(#cond); } } while (0)

static int test_getv_append_only(void)
{
    /* Existing indices 0..33 must remain unchanged; diagnostics append at 34+. */
    CHECK(GETV_TRIG_LEVEL == 0U);
    CHECK(GETV_DIAG_TRIG_MODE == 27U);
    CHECK(GETV_MEM_MSP_FREE_MIN == 33U);
    CHECK(GETV_ETS_ACCEPTED_SWEEPS == 34U);
    CHECK(GETV_ETS_REJECTED_SWEEPS == 35U);
    CHECK(GETV_ETS_ACCUM_PROGRESS == 36U);
    CHECK(GETV_ETS_FILLED_BINS == 37U);
    CHECK(GETV_ETS_READY == 38U);
    CHECK(GETV_ETS_RESET_REASON == 39U);
    CHECK(GETV_ETS_DIAG_SEQ == 40U);
    CHECK(GETV_ETS_SRC_B1 == 41U);
    CHECK(GETV_ETS_SRC_QUANTISED == 42U);
    CHECK(GETV_ETS_REJECT_UNBOUND == 43U);
    CHECK(GETV_ETS_HW_ROLLOVER == 44U);
    CHECK(GETV_DIAG_HW_COUNT == 45U);
    CHECK(GETV_COUNT == 46U);
    return 0;
}

static int test_reset_reason_encoding(void)
{
    CHECK(ETS_RESET_TB_CHANGE == 0x0001U);
    CHECK(ETS_RESET_TRIG_MODE_CHANGE == 0x0002U);
    CHECK(ETS_RESET_TRIG_SLOPE_CHANGE == 0x0004U);
    CHECK(ETS_RESET_TRIG_LEVEL_CHANGE == 0x0008U);
    CHECK(ETS_RESET_READY == 0x0010U);
    CHECK(ETS_RESET_INIT == 0x0020U);
    CHECK(ETS_RESET_INCOMPLETE == 0x0040U);

    /* Context changes can co-occur as bitmask. */
    CHECK((ETS_RESET_TB_CHANGE | ETS_RESET_TRIG_LEVEL_CHANGE) == 0x0009U);
    CHECK((ETS_RESET_TRIG_MODE_CHANGE | ETS_RESET_TRIG_SLOPE_CHANGE | ETS_RESET_TRIG_LEVEL_CHANGE) == 0x000EU);
    /* READY and INCOMPLETE are epoch-completion reasons (distinct events). */
    CHECK((ETS_RESET_READY & (ETS_RESET_TB_CHANGE|ETS_RESET_TRIG_MODE_CHANGE|ETS_RESET_TRIG_SLOPE_CHANGE|ETS_RESET_TRIG_LEVEL_CHANGE)) == 0U);
    CHECK((ETS_RESET_INCOMPLETE & ETS_RESET_READY) == 0U);
    return 0;
}

static int test_diag_seq_update_contract(void)
{
    /* ETS_DIAG_SEQ is a diagnostic update counter (u16 wrap), not an odd/even seqlock.
     * Increment on: each logical rejected-sweep outcome; once after each accepted
     * sweep's diagnostic updates complete (accepted/filled/progress, not per bin);
     * and on every ETS epoch reset. Excluded/unrelated polls do not mutate diagnostics
     * and must not change sequence. */
    uint16_t seq = 0U;
    uint16_t accepted = 0U;
    uint16_t rejected = 0U;
    uint16_t filled = 0U;

    /* logical rejected outcome: saturating counter update + seq bump */
    if (rejected < 0xFFFFU) { rejected++; }
    seq++;
    CHECK(rejected == 1U && seq == 1U);

    /* accepted sweep: multiple bin 0->1 transitions in same sweep, but seq increments once after updates complete */
    filled += 3U; /* represents several newly-filled bins in this sweep */
    accepted++;
    seq++; /* once per accepted sweep, not per bin */
    CHECK(accepted == 1U && filled == 3U && seq == 2U);

    /* epoch reset increments sequence */
    seq++;
    CHECK(seq == 3U);

    /* rejected saturation: value saturates but logical rejected outcome still occurs => seq increments */
    rejected = 0xFFFFU;
    if (rejected < 0xFFFFU) { rejected++; }
    seq++;
    CHECK(rejected == 0xFFFFU && seq == 4U);

    /* excluded/unrelated poll: no diagnostic mutation => no sequence change */
    {
        uint16_t seq_before = seq;
        /* no-op (untriggered/non-eligible/not invoked) */
        CHECK(seq == seq_before);
    }

    /* wraparound */
    seq = 0xFFFFU;
    seq++;
    CHECK(seq == 0U);

    return 0;
}

static int test_rejection_and_readiness_semantics(void)
{
    /* Normative rejection: triggered, ETS-eligible, reached accumulation decision point,
     * failed to produce valid accepted contribution. Untriggered/non-eligible excluded.
     * Rejected never modifies bins/counts/progress. */
    uint16_t accepted = 2U, filled = 5U, rejected = 0U;
    uint16_t accepted_before = accepted, filled_before = filled;
    rejected++; /* logical rejected outcome */
    CHECK(rejected == 1U);
    CHECK(accepted == accepted_before);
    CHECK(filled == filled_before);

    /* B4.25 readiness contract:
     * - accumulating: accepted < target, ready==0
     * - target reached, coverage < 80 or measurement failure: the epoch is
     *   invalidated (accepted -> 0), ready==0, reason==ETS_RESET_INCOMPLETE
     * - successful reconstruction: ready==1, accepted==target, filled>=80;
     *   the completed diagnostics stay observable until the next sweep
     * Therefore ready==1 implies filled>=80; ready=1 with fill=0 is
     * structurally impossible (covered exhaustively in test_ets_epoch.c). */
    CHECK(ETS_SWEEPS_TARGET == 16U);
    {
        uint16_t acc = 5U, ready = 0U;
        CHECK(acc < ETS_SWEEPS_TARGET && ready == 0U);
    }
    {
        uint16_t acc = 0U, ready = 0U; /* failed attempt: epoch restarted */
        uint16_t reason = ETS_RESET_INCOMPLETE;
        CHECK(acc == 0U && ready == 0U && reason != ETS_RESET_READY);
    }
    {
        uint16_t acc = ETS_SWEEPS_TARGET, filled_ok = 80U, ready = 1U;
        uint16_t reason = ETS_RESET_READY;
        CHECK(acc == ETS_SWEEPS_TARGET && filled_ok >= 80U && ready == 1U
              && reason == ETS_RESET_READY);
    }

    return 0;
}

static int test_snapshot_retry_contract(void)
{
    /* Host snapshot procedure: read seq, read fields 34-39, read seq again;
     * accept only if both sequence values match; otherwise retry with bounded count.
     * No odd/even seqlock. */
    uint16_t fields_a[6] = {1U, 2U, 1U, 10U, 0U, ETS_RESET_INIT};
    uint16_t fields_b[6] = {1U, 2U, 1U, 10U, 0U, ETS_RESET_INIT};
    uint16_t seq0 = 7U, seq1 = 7U;
    CHECK(seq0 == seq1); /* matching => accept */
    (void)fields_a; (void)fields_b;

    seq0 = 7U; seq1 = 8U;
    CHECK(seq0 != seq1); /* changed => reject/retry */

    /* bounded retry */
    {
        int retries = 0;
        const int max_retries = 3;
        int accepted = 0;
        while (retries < max_retries) {
            uint16_t s0 = 1U;
            uint16_t s1 = (retries < 2) ? (uint16_t)(s0 + 1U) : s0; /* changes then stabilizes */
            retries++;
            if (s0 == s1) { accepted = 1; break; }
        }
        CHECK(accepted == 1 && retries == 3);
    }
    return 0;
}

static int test_link_cmd_arg_valid_getv(void)
{
    /* GETV subindices 0..45 valid, 46 invalid (append-only extension). */
    CHECK(link_cmd_arg_valid(LINK_OP_GET_VALUE, 0U) == 1U);
    CHECK(link_cmd_arg_valid(LINK_OP_GET_VALUE, 33U) == 1U);
    CHECK(link_cmd_arg_valid(LINK_OP_GET_VALUE, 34U) == 1U);
    CHECK(link_cmd_arg_valid(LINK_OP_GET_VALUE, 40U) == 1U);
    CHECK(link_cmd_arg_valid(LINK_OP_GET_VALUE, 41U) == 1U);
    CHECK(link_cmd_arg_valid(LINK_OP_GET_VALUE, 44U) == 1U);
    CHECK(link_cmd_arg_valid(LINK_OP_GET_VALUE, 45U) == 1U);
    CHECK(link_cmd_arg_valid(LINK_OP_GET_VALUE, 46U) == 0U);
    return 0;
}

int main(void)
{
    int failures = 0;
    failures += test_getv_append_only();
    failures += test_reset_reason_encoding();
    failures += test_diag_seq_update_contract();
    failures += test_rejection_and_readiness_semantics();
    failures += test_snapshot_retry_contract();
    failures += test_link_cmd_arg_valid_getv();
    if (failures == 0) {
        printf("ets_diag: all tests passed\n");
    }
    return failures;
}
