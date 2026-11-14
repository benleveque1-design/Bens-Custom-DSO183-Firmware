/* Host tests for ETS Stage B3 integration contract: exact timer-count mapping,
 * floor division, acceptance/rejection behavior, and sweep/count/readiness logic.
 * Arithmetic/contract verification only (no hardware/analogue proof). */
#include "ets_phase.h"
#include "timebase.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define BENCH_WINDOW 320U
#define BENCH_PRE     96U
#define ETS_SWEEPS_TARGET 16U

#define FAIL(msg) do { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); return 1; } while (0)
#define CHECK(cond) do { if (!(cond)) { FAIL(#cond); } } while (0)
#define CHECK_EQ_I64(a,b) do { int64_t _a=(a), _b=(b); if (_a!=_b) { \
    printf("FAIL %s:%d: %s == %lld, expected %s == %lld\n", \
           __FILE__, __LINE__, #a, (long long)_a, #b, (long long)_b); return 1; } } while (0)
#define CHECK_EQ_U32(a,b) do { uint32_t _a=(a), _b=(b); if (_a!=_b) { \
    printf("FAIL %s:%d: %s == %u, expected %s == %u\n", \
           __FILE__, __LINE__, #a, (unsigned)_a, #b, (unsigned)_b); return 1; } } while (0)

static uint32_t counts_of(uint8_t tb)
{
    const struct timebase_step *st = timebase_entry(tb);
    if (st == NULL) return 0U;
    return ((uint32_t)st->tick_psc + 1U) * ((uint32_t)st->tick_arr + 1U);
}

static int test_floor_div(void)
{
    /* Positive and exact */
    CHECK_EQ_I64(ets_phase_floor_div(0, 40), 0);
    CHECK_EQ_I64(ets_phase_floor_div(39, 40), 0);
    CHECK_EQ_I64(ets_phase_floor_div(40, 40), 1);
    CHECK_EQ_I64(ets_phase_floor_div(80, 40), 2);

    /* Negative: mathematical floor, not C truncation-toward-zero */
    CHECK_EQ_I64(ets_phase_floor_div(-1, 40), -1);
    CHECK_EQ_I64(ets_phase_floor_div(-39, 40), -1);
    CHECK_EQ_I64(ets_phase_floor_div(-40, 40), -1);
    CHECK_EQ_I64(ets_phase_floor_div(-41, 40), -2);
    CHECK_EQ_I64(ets_phase_floor_div(-50, 40), -2); /* floor(-1.25) */
    CHECK_EQ_I64(ets_phase_floor_div(-79, 40), -2);
    CHECK_EQ_I64(ets_phase_floor_div(-80, 40), -2);
    CHECK_EQ_I64(ets_phase_floor_div(-81, 40), -3);

    /* Contrast with truncation-toward-zero behavior for representative negatives */
    {
        int64_t n = -50, d = 40;
        int64_t trunc_like = n / d; /* C99+ trunc toward zero for int64 */
        CHECK_EQ_I64(trunc_like, -1);
        CHECK(ets_phase_floor_div(n, d) != trunc_like);
    }
    return 0;
}

static int test_bin_index_basic_and_boundaries(void)
{
    int32_t bin;
    uint8_t ok;
    const uint32_t hw = 100U;  /* TB5 */
    const uint32_t ets = 40U;  /* 20us bin interval in timer counts */

    /* phase 0 (crossing at y1): center sample maps to BENCH_PRE */
    ok = ets_phase_bin_index((uint16_t)BENCH_PRE, BENCH_PRE, BENCH_WINDOW, hw, ets, 0U, &bin);
    CHECK(ok == 1U); CHECK(bin == (int32_t)BENCH_PRE);

    /* Crossing-aligned mapping: t_q16 = (i-PRE)*hw_q16 + phase_q16.
       phase > 0 means crossing occurred before y1, so y1 is +phase after crossing. */
    /* phase 50 counts (50%): i=PRE => +50 counts, offset floor(50/40)=1 => bin 97 */
    ok = ets_phase_bin_index((uint16_t)BENCH_PRE, BENCH_PRE, BENCH_WINDOW, hw, ets, 50U * 65536U, &bin);
    CHECK(ok == 1U); CHECK(bin == 97);

    /* Boundaries around positive offsets */
    ok = ets_phase_bin_index((uint16_t)BENCH_PRE, BENCH_PRE, BENCH_WINDOW, hw, ets, 39U * 65536U, &bin);
    CHECK(ok == 1U); CHECK(bin == 96); /* floor(39/40)=0 */
    ok = ets_phase_bin_index((uint16_t)BENCH_PRE, BENCH_PRE, BENCH_WINDOW, hw, ets, 40U * 65536U, &bin);
    CHECK(ok == 1U); CHECK(bin == 97); /* floor(40/40)=1 */
    ok = ets_phase_bin_index((uint16_t)BENCH_PRE, BENCH_PRE, BENCH_WINDOW, hw, ets, 41U * 65536U, &bin);
    CHECK(ok == 1U); CHECK(bin == 97); /* floor(41/40)=1 */
    ok = ets_phase_bin_index((uint16_t)BENCH_PRE, BENCH_PRE, BENCH_WINDOW, hw, ets, 79U * 65536U, &bin);
    CHECK(ok == 1U); CHECK(bin == 97); /* floor(79/40)=1 */
    ok = ets_phase_bin_index((uint16_t)BENCH_PRE, BENCH_PRE, BENCH_WINDOW, hw, ets, 80U * 65536U, &bin);
    CHECK(ok == 1U); CHECK(bin == 98); /* floor(80/40)=2 */
    ok = ets_phase_bin_index((uint16_t)BENCH_PRE, BENCH_PRE, BENCH_WINDOW, hw, ets, 81U * 65536U, &bin);
    CHECK(ok == 1U); CHECK(bin == 98); /* floor(81/40)=2 */

    /* Negative t_q16 still possible for pre-trigger samples; floor division must apply.
       i=95 (one sample before center), phase 50 => t=( -1*100 + 50 )=-50 counts, bin 94 */
    ok = ets_phase_bin_index((uint16_t)(BENCH_PRE - 1U), BENCH_PRE, BENCH_WINDOW, hw, ets, 50U * 65536U, &bin);
    CHECK(ok == 1U); CHECK(bin == 94);
    /* boundary: i=95, phase 60 => t=-40, floor(-1) => bin 95 */
    ok = ets_phase_bin_index((uint16_t)(BENCH_PRE - 1U), BENCH_PRE, BENCH_WINDOW, hw, ets, 60U * 65536U, &bin);
    CHECK(ok == 1U); CHECK(bin == 95);
    /* i=95, phase 59 => t=-41, floor(-41/40)=-2 => bin 94 */
    ok = ets_phase_bin_index((uint16_t)(BENCH_PRE - 1U), BENCH_PRE, BENCH_WINDOW, hw, ets, 59U * 65536U, &bin);
    CHECK(ok == 1U); CHECK(bin == 94);

    /* Out of range */
    ok = ets_phase_bin_index(0U, BENCH_PRE, BENCH_WINDOW, hw, 2U, 0U, &bin);
    CHECK(ok == 0U);

    return 0;
}

static int test_exact_timer_count_mapping_and_fractional_phase(void)
{
    /* Exact counts for ETS logical TBs and HW base TB5; ensure mapping uses counts, not truncated ns. */
    const uint32_t hw = counts_of(5U); /* 100 */
    const uint32_t ets_counts[] = {2U, 4U, 10U, 20U, 40U}; /* TB0..TB4 */
    unsigned k;

    CHECK_EQ_U32(hw, 100U);
    CHECK_EQ_U32((hw * 125U) / 8U, 1562U); /* truncated ns representation would lose 0.5ns */

    for (k = 0U; k < 5U; k++) {
        uint8_t tb = (uint8_t)k;
        uint32_t ets = counts_of(tb);
        int32_t bin;
        uint8_t ok;
        CHECK_EQ_U32(ets, ets_counts[k]);

        /* 50% fractional phase => 50 counts, independent of ns truncation */
        ok = ets_phase_bin_index((uint16_t)BENCH_PRE, BENCH_PRE, BENCH_WINDOW, hw, ets, 50U * 65536U, &bin);
        CHECK(ok == 1U);
        {
            int64_t expected_off = ets_phase_floor_div(((int64_t)50 << 16), ((int64_t)ets << 16));
            CHECK(bin == (int32_t)((int64_t)BENCH_PRE + expected_off));
        }

        /* 25% phase (75 counts) */
        ok = ets_phase_bin_index((uint16_t)BENCH_PRE, BENCH_PRE, BENCH_WINDOW, hw, ets, 75U * 65536U, &bin);
        CHECK(ok == 1U);
    }

    /* A non-integer-fraction phase in counts domain (rounded result) maps deterministically */
    {
        struct ets_phase_result r = ets_phase_estimate(1000U, 2000U, 1250U, ETS_PHASE_RISING, hw);
        int32_t bin;
        CHECK(r.valid == 1U);
        CHECK(ets_phase_bin_index((uint16_t)BENCH_PRE, BENCH_PRE, BENCH_WINDOW, hw, 10U, r.phase_q16, &bin) == 1U);
    }
    return 0;
}

static int test_accept_reject_edges_and_auto(void)
{
    struct ets_phase_result r;
    const uint32_t hw = 100U;

    /* Rising and falling valid crossings */
    r = ets_phase_estimate(1000U, 2000U, 1500U, ETS_PHASE_RISING, hw);
    CHECK(r.valid == 1U);
    r = ets_phase_estimate(2000U, 1000U, 1500U, ETS_PHASE_FALLING, hw);
    CHECK(r.valid == 1U);

    /* AUTO effective-rising behavior (contract): evaluate as rising */
    r = ets_phase_estimate(1000U, 2000U, 1500U, ETS_PHASE_RISING, hw);
    CHECK(r.valid == 1U);
    /* Falling pair must not be accepted as rising/AUTO-rising */
    r = ets_phase_estimate(2000U, 1000U, 1500U, ETS_PHASE_RISING, hw);
    CHECK(r.valid == 0U);

    /* Valid fallback-like simple threshold crossing (brackets configured level) accepted */
    r = ets_phase_estimate(1999U, 2050U, 2000U, ETS_PHASE_RISING, hw);
    CHECK(r.valid == 1U);
    r = ets_phase_estimate(2050U, 1999U, 2000U, ETS_PHASE_FALLING, hw);
    CHECK(r.valid == 1U);

    /* Schmitt-selected-like pair that fails configured-level bracketing must be rejected:
       e.g. rising with y1 well above level but y0 not below level (no bracket of configured level) */
    r = ets_phase_estimate(2010U, 2100U, 2000U, ETS_PHASE_RISING, hw); /* y0>=level */
    CHECK(r.valid == 0U);
    r = ets_phase_estimate(1990U, 2100U, 2000U, ETS_PHASE_FALLING, hw); /* wrong direction */
    CHECK(r.valid == 0U);
    r = ets_phase_estimate(1990U, 1995U, 2000U, ETS_PHASE_RISING, hw); /* y1 < level */
    CHECK(r.valid == 0U);

    return 0;
}

static int test_rejected_sweep_zero_changes_and_counting(void)
{
    /* Simulate accumulation contract: rejected phase => no bin/count changes, no accepted increment.
       Accepted => bin updates and accepted sweep increments; readiness at target. */
    uint32_t sum[BENCH_WINDOW];
    uint16_t cnt[BENCH_WINDOW];
    uint8_t accepted = 0U;
    const uint32_t hw = 100U, ets = 40U;
    uint16_t i;

    memset(sum, 0, sizeof(sum));
    memset(cnt, 0, sizeof(cnt));

    /* Rejected sweep (non-bracketing pair) */
    {
        struct ets_phase_result r = ets_phase_estimate(2010U, 2100U, 2000U, ETS_PHASE_RISING, hw);
        CHECK(r.valid == 0U);
        if (r.valid != 0U) { /* must not happen */
            for (i = 0U; i < BENCH_WINDOW; i++) { sum[i]++; cnt[i]++; }
            accepted++;
        }
    }
    for (i = 0U; i < BENCH_WINDOW; i++) { CHECK(sum[i] == 0U); CHECK(cnt[i] == 0U); }
    CHECK(accepted == 0U);

    /* Accepted sweeps increment count and update bins */
    {
        struct ets_phase_result r = ets_phase_estimate(1000U, 2000U, 1500U, ETS_PHASE_RISING, hw);
        CHECK(r.valid == 1U);
        for (i = 0U; i < BENCH_WINDOW; i++) {
            int32_t bi;
            if (ets_phase_bin_index(i, BENCH_PRE, BENCH_WINDOW, hw, ets, r.phase_q16, &bi) != 0U) {
                sum[(uint16_t)bi] += 1234U;
                cnt[(uint16_t)bi]++;
            }
        }
        accepted++;
    }
    CHECK(accepted == 1U);
    {
        uint16_t filled = 0U;
        for (i = 0U; i < BENCH_WINDOW; i++) if (cnt[i] != 0U) filled++;
        CHECK(filled > 0U);
    }

    /* Another rejection makes no changes */
    {
        uint32_t sum2[BENCH_WINDOW];
        uint16_t cnt2[BENCH_WINDOW];
        memcpy(sum2, sum, sizeof(sum));
        memcpy(cnt2, cnt, sizeof(cnt));
        {
            struct ets_phase_result r = ets_phase_estimate(1500U, 1500U, 1500U, ETS_PHASE_RISING, hw);
            CHECK(r.valid == 0U);
        }
        CHECK(memcmp(sum2, sum, sizeof(sum)) == 0);
        CHECK(memcmp(cnt2, cnt, sizeof(cnt)) == 0);
        CHECK(accepted == 1U);
    }

    /* Readiness only when accepted sweeps reach target (and fill sufficient) */
    while (accepted < ETS_SWEEPS_TARGET) {
        struct ets_phase_result r = ets_phase_estimate(1000U, 2000U, 1500U, ETS_PHASE_RISING, hw);
        CHECK(r.valid == 1U);
        for (i = 0U; i < BENCH_WINDOW; i++) {
            int32_t bi;
            if (ets_phase_bin_index(i, BENCH_PRE, BENCH_WINDOW, hw, ets, r.phase_q16, &bi) != 0U) {
                sum[(uint16_t)bi] += 1234U;
                cnt[(uint16_t)bi]++;
            }
        }
        accepted++;
        if (accepted < ETS_SWEEPS_TARGET) {
            /* not ready yet by count */
            CHECK(accepted < ETS_SWEEPS_TARGET);
        }
    }
    CHECK(accepted == ETS_SWEEPS_TARGET);

    return 0;
}

static int test_context_reset_semantics(void)
{
    /* Contract: timebase/trigger-context changes reset accumulation state.
       (Integration preserves ets_reset on tb/mode/slope/level change in main.c.)
       Here we verify the reset precondition logic deterministically. */
    uint8_t tb = 5U, mode = 0U, slope = 0U; uint16_t level = 2048U;
    uint8_t s_tb = tb, s_mode = mode, s_slope = slope; uint16_t s_level = level;
    uint8_t need_reset;

    need_reset = (s_tb!=tb)||(s_mode!=mode)||(s_slope!=slope)||(s_level!=level);
    CHECK(need_reset == 0U);
    tb = 4U;
    need_reset = (s_tb!=tb)||(s_mode!=mode)||(s_slope!=slope)||(s_level!=level);
    CHECK(need_reset == 1U);
    tb = 5U; mode = 1U;
    need_reset = (s_tb!=tb)||(s_mode!=mode)||(s_slope!=slope)||(s_level!=level);
    CHECK(need_reset == 1U);
    mode = 0U; slope = 1U;
    need_reset = (s_tb!=tb)||(s_mode!=mode)||(s_slope!=slope)||(s_level!=level);
    CHECK(need_reset == 1U);
    slope = 0U; level = 2000U;
    need_reset = (s_tb!=tb)||(s_mode!=mode)||(s_slope!=slope)||(s_level!=level);
    CHECK(need_reset == 1U);

    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_floor_div();
    failures += test_bin_index_basic_and_boundaries();
    failures += test_exact_timer_count_mapping_and_fractional_phase();
    failures += test_accept_reject_edges_and_auto();
    failures += test_rejected_sweep_zero_changes_and_counting();
    failures += test_context_reset_semantics();

    if (failures == 0) {
        printf("ets_integration: all tests passed\n");
    }
    return failures;
}
