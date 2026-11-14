/* Host unit tests for isolated ETS sub-sample phase estimator (app/ets_phase.c).
 * Stage B1: arithmetic and input-validation behavior only (no hardware/analogue proof).
 */
#include "ets_phase.h"
#include "timebase.h"

#include <stdio.h>
#include <stdint.h>

#define FAIL(msg) do { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); return 1; } while (0)
#define CHECK(cond) do { if (!(cond)) { FAIL(#cond); } } while (0)
#define CHECK_EQ_U32(a,b) do { uint32_t _a=(a), _b=(b); if (_a!=_b) { \
    printf("FAIL %s:%d: %s == %u (0x%08x), expected %s == %u (0x%08x)\n", \
           __FILE__, __LINE__, #a, (unsigned)_a, (unsigned)_a, #b, (unsigned)_b, (unsigned)_b); return 1; } } while (0)

/* Helper: expected phase_q16 = round(numer/denom * hw_counts * 65536) normalized < hw_counts<<16 */
static uint32_t expected_phase_q16(uint32_t numer, uint32_t denom, uint32_t hw_counts)
{
    uint64_t phase = (((uint64_t)numer * (uint64_t)hw_counts) << 16) + ((uint64_t)denom / 2U);
    phase /= (uint64_t)denom;
    {
        uint64_t limit = ((uint64_t)hw_counts << 16);
        if (phase >= limit) {
            phase = (phase == limit) ? 0U : limit; /* should not happen for valid cases */
        }
    }
    return (uint32_t)phase;
}

static int test_rising_fractions(void)
{
    /* Pair chosen so f=(L-y0)/(y1-y0) hits exact fractions; phase=(1-f)*hw_counts.
     * Use hw_counts=100 (TB5 exact interval). */
    const uint32_t hw = 100U;
    struct ets_phase_result r;

    /* f=0.25 (25% from y0 to y1): y0=1000, y1=2000, L=1250 => numer(y1-L)=750, denom=1000 */
    r = ets_phase_estimate(1000U, 2000U, 1250U, ETS_PHASE_RISING, hw);
    CHECK(r.valid == 1U);
    CHECK_EQ_U32(r.phase_q16, expected_phase_q16(750U, 1000U, hw)); /* 75.0 counts */

    /* f=0.50 (50%): L=1500 => numer=500 */
    r = ets_phase_estimate(1000U, 2000U, 1500U, ETS_PHASE_RISING, hw);
    CHECK(r.valid == 1U);
    CHECK_EQ_U32(r.phase_q16, expected_phase_q16(500U, 1000U, hw)); /* 50.0 counts */

    /* f=0.75 (75%): L=1750 => numer=250 */
    r = ets_phase_estimate(1000U, 2000U, 1750U, ETS_PHASE_RISING, hw);
    CHECK(r.valid == 1U);
    CHECK_EQ_U32(r.phase_q16, expected_phase_q16(250U, 1000U, hw)); /* 25.0 counts */

    /* near 100% (f close to 1): L=1999 => numer=1, denom=1000, phase small */
    r = ets_phase_estimate(1000U, 2000U, 1999U, ETS_PHASE_RISING, hw);
    CHECK(r.valid == 1U);
    CHECK_EQ_U32(r.phase_q16, expected_phase_q16(1U, 1000U, hw));

    /* near 0% fraction from y0 side but accepted bracket (f just above 0): L=1001 => numer=999 */
    r = ets_phase_estimate(1000U, 2000U, 1001U, ETS_PHASE_RISING, hw);
    CHECK(r.valid == 1U);
    CHECK_EQ_U32(r.phase_q16, expected_phase_q16(999U, 1000U, hw));

    return 0;
}

static int test_falling_fractions(void)
{
    const uint32_t hw = 100U;
    struct ets_phase_result r;

    /* Falling: y0>level>=y1, f=(level-y0)/(y1-y0)=(y0-level)/(y0-y1), numer(level-y1) */
    /* f=0.25: y0=2000,y1=1000,L=1750 => numer=750, denom=1000 */
    r = ets_phase_estimate(2000U, 1000U, 1750U, ETS_PHASE_FALLING, hw);
    CHECK(r.valid == 1U);
    CHECK_EQ_U32(r.phase_q16, expected_phase_q16(750U, 1000U, hw));

    /* f=0.50: L=1500 */
    r = ets_phase_estimate(2000U, 1000U, 1500U, ETS_PHASE_FALLING, hw);
    CHECK(r.valid == 1U);
    CHECK_EQ_U32(r.phase_q16, expected_phase_q16(500U, 1000U, hw));

    /* f=0.75: L=1250 */
    r = ets_phase_estimate(2000U, 1000U, 1250U, ETS_PHASE_FALLING, hw);
    CHECK(r.valid == 1U);
    CHECK_EQ_U32(r.phase_q16, expected_phase_q16(250U, 1000U, hw));

    /* near 100% (f close to 1): L=1001 => numer=1 */
    r = ets_phase_estimate(2000U, 1000U, 1001U, ETS_PHASE_FALLING, hw);
    CHECK(r.valid == 1U);
    CHECK_EQ_U32(r.phase_q16, expected_phase_q16(1U, 1000U, hw));

    /* near 0%: L=1999 => numer=999 */
    r = ets_phase_estimate(2000U, 1000U, 1999U, ETS_PHASE_FALLING, hw);
    CHECK(r.valid == 1U);
    CHECK_EQ_U32(r.phase_q16, expected_phase_q16(999U, 1000U, hw));

    return 0;
}

static int test_boundary_equality(void)
{
    const uint32_t hw = 100U;
    struct ets_phase_result r;

    /* y1 == level: crossing at current sample => phase 0 */
    r = ets_phase_estimate(1000U, 2048U, 2048U, ETS_PHASE_RISING, hw);
    CHECK(r.valid == 1U);
    CHECK_EQ_U32(r.phase_q16, 0U);

    r = ets_phase_estimate(3000U, 2048U, 2048U, ETS_PHASE_FALLING, hw);
    CHECK(r.valid == 1U);
    CHECK_EQ_U32(r.phase_q16, 0U);

    /* y0 == level: not accepted as demonstrable bracket start for this sample */
    r = ets_phase_estimate(2048U, 3000U, 2048U, ETS_PHASE_RISING, hw);
    CHECK(r.valid == 0U);
    r = ets_phase_estimate(2048U, 1000U, 2048U, ETS_PHASE_FALLING, hw);
    CHECK(r.valid == 0U);

    return 0;
}

static int test_equal_flat_and_wrong_direction(void)
{
    const uint32_t hw = 100U;
    struct ets_phase_result r;

    /* equal/flat samples */
    r = ets_phase_estimate(2000U, 2000U, 2000U, ETS_PHASE_RISING, hw);
    CHECK(r.valid == 0U);
    r = ets_phase_estimate(1500U, 1500U, 1000U, ETS_PHASE_FALLING, hw);
    CHECK(r.valid == 0U);

    /* wrong-direction pairs */
    r = ets_phase_estimate(2000U, 1000U, 1500U, ETS_PHASE_RISING, hw); /* falling pair */
    CHECK(r.valid == 0U);
    r = ets_phase_estimate(1000U, 2000U, 1500U, ETS_PHASE_FALLING, hw); /* rising pair */
    CHECK(r.valid == 0U);

    /* non-bracketing in requested direction */
    r = ets_phase_estimate(1000U, 1200U, 1500U, ETS_PHASE_RISING, hw); /* both below */
    CHECK(r.valid == 0U);
    r = ets_phase_estimate(1800U, 2000U, 1500U, ETS_PHASE_RISING, hw); /* both above */
    CHECK(r.valid == 0U);
    r = ets_phase_estimate(1800U, 1600U, 1500U, ETS_PHASE_FALLING, hw); /* both above */
    CHECK(r.valid == 0U);
    r = ets_phase_estimate(1200U, 1000U, 1500U, ETS_PHASE_FALLING, hw); /* both below */
    CHECK(r.valid == 0U);

    return 0;
}

static int test_out_of_range_and_invalid_inputs(void)
{
    struct ets_phase_result r;

    /* out-of-range trigger level for the pair (no bracket) */
    r = ets_phase_estimate(1000U, 2000U, 500U, ETS_PHASE_RISING, 100U);
    CHECK(r.valid == 0U);
    r = ets_phase_estimate(1000U, 2000U, 2500U, ETS_PHASE_RISING, 100U);
    CHECK(r.valid == 0U);
    r = ets_phase_estimate(2000U, 1000U, 500U, ETS_PHASE_FALLING, 100U);
    CHECK(r.valid == 0U);
    r = ets_phase_estimate(2000U, 1000U, 2500U, ETS_PHASE_FALLING, 100U);
    CHECK(r.valid == 0U);

    /* invalid/unrepresentable inputs: zero interval, bad edge */
    r = ets_phase_estimate(1000U, 2000U, 1500U, ETS_PHASE_RISING, 0U);
    CHECK(r.valid == 0U);
    r = ets_phase_estimate(1000U, 2000U, 1500U, (enum ets_phase_edge)99U, 100U);
    CHECK(r.valid == 0U);

    /* ADC-domain extremes still compute correctly when bracket is valid */
    r = ets_phase_estimate(0U, 4095U, 2048U, ETS_PHASE_RISING, 100U);
    CHECK(r.valid == 1U);
    CHECK_EQ_U32(r.phase_q16, expected_phase_q16(2047U, 4095U, 100U));
    r = ets_phase_estimate(4095U, 0U, 2048U, ETS_PHASE_FALLING, 100U);
    CHECK(r.valid == 1U);
    CHECK_EQ_U32(r.phase_q16, expected_phase_q16(2048U, 4095U, 100U));

    return 0;
}

static int test_near_zero_denom_and_conditioning(void)
{
    /* Smallest non-zero integer denom is 1 count: (y1-y0)==1.
     * Bracket requires y0 < level <= y1, so only y1==level works => phase 0. */
    struct ets_phase_result r = ets_phase_estimate(2047U, 2048U, 2048U, ETS_PHASE_RISING, 100U);
    CHECK(r.valid == 1U);
    CHECK_EQ_U32(r.phase_q16, 0U);

    r = ets_phase_estimate(2048U, 2047U, 2047U, ETS_PHASE_FALLING, 100U);
    CHECK(r.valid == 1U);
    CHECK_EQ_U32(r.phase_q16, 0U);

    /* denom==1 with level not at y1 is impossible under valid bracket; ensure rejection */
    r = ets_phase_estimate(2047U, 2048U, 2047U, ETS_PHASE_RISING, 100U); /* y0==level rejected */
    CHECK(r.valid == 0U);

    /* Poorly conditioned in value (large hw, close levels) still yields exact integer result */
    r = ets_phase_estimate(1000U, 1002U, 1001U, ETS_PHASE_RISING, 100U);
    CHECK(r.valid == 1U);
    CHECK_EQ_U32(r.phase_q16, expected_phase_q16(1U, 2U, 100U)); /* 50 counts */

    return 0;
}

static int test_phase_normalization(void)
{
    /* Valid results must be in [0, hw_interval_counts<<16). */
    struct ets_phase_result r;
    uint32_t hw = 100U;
    uint64_t limit = ((uint64_t)hw << 16);

    r = ets_phase_estimate(1000U, 2000U, 1999U, ETS_PHASE_RISING, hw);
    CHECK(r.valid == 1U);
    CHECK(r.phase_q16 < (uint32_t)limit);

    r = ets_phase_estimate(1000U, 2000U, 2000U, ETS_PHASE_RISING, hw); /* boundary => 0 */
    CHECK(r.valid == 1U);
    CHECK(r.phase_q16 == 0U);
    CHECK(r.phase_q16 < (uint32_t)limit);

    /* Across various hw intervals */
    {
        const uint32_t hws[] = {2U, 4U, 10U, 20U, 40U, 100U, 1000U, 100000U};
        unsigned i;
        for (i = 0U; i < sizeof(hws)/sizeof(hws[0]); i++) {
            uint64_t lim = ((uint64_t)hws[i] << 16);
            r = ets_phase_estimate(0U, 1000U, 1U, ETS_PHASE_RISING, hws[i]);
            CHECK(r.valid == 1U);
            CHECK((uint64_t)r.phase_q16 < lim);
            r = ets_phase_estimate(0U, 1000U, 1000U, ETS_PHASE_RISING, hws[i]);
            CHECK(r.valid == 1U);
            CHECK(r.phase_q16 == 0U);
        }
    }
    return 0;
}

static int test_exact_timer_count_intervals_and_no_ns_rounding(void)
{
    /* Exact TIM3 timer-count intervals for all ETS-related timebases (counts = (PSC+1)*(ARR+1)). */
    const uint32_t expected_counts[TIMEBASE_COUNT] = {
        2U, 4U, 10U, 20U, 40U, 100U,        /* 1,2,5,10,20,50 us */
        200U, 400U, 1000U, 2000U, 4000U, 10000U,
        20000U, 40000U, 100000U, 200000U, 400000U, 1000000U,
        2000000U, 4000000U, 10000000U, 20000000U, 40000000U, 100000000U
    };
    uint8_t i;
    for (i = 0U; i < TIMEBASE_COUNT; i++) {
        const struct timebase_step *st = timebase_entry(i);
        uint32_t counts;
        CHECK(st != NULL);
        counts = ((uint32_t)st->tick_psc + 1U) * ((uint32_t)st->tick_arr + 1U);
        CHECK_EQ_U32(counts, expected_counts[i]);
    }

    /* TB5 (HW base for ETS) is exactly 100 counts = 1562.5 ns, but (counts*125)/8 == 1562 ns truncated.
     * Estimator must operate on exact counts and not depend on truncated ns. */
    {
        const uint32_t hw_counts = 100U; /* TB5 exact */
        uint32_t ns_trunc = (hw_counts * 125U) / 8U; /* 1562 */
        struct ets_phase_result r;

        CHECK_EQ_U32(hw_counts, expected_counts[5]);
        CHECK_EQ_U32(ns_trunc, 1562U); /* demonstrates truncation that must NOT drive estimator */

        /* 50% crossing: exact phase is 50 counts (781.25 ns), represented as 50*65536 Q16. */
        r = ets_phase_estimate(1000U, 2000U, 1500U, ETS_PHASE_RISING, hw_counts);
        CHECK(r.valid == 1U);
        CHECK_EQ_U32(r.phase_q16, 50U * 65536U);

        /* If interval were naively taken as 1562 "ns units" at 1ns/count scale, 50% would be 781 (wrong domain).
         * More directly: mapping via integer ns (1562) loses 0.5 ns per interval vs 1562.5; over one interval
         * half a timer count (0.5 * 65536 = 32768 Q16-count units) of precision is discarded. */
        {
            uint32_t phase_ns_trunc_domain = (uint32_t)((50ULL * (uint64_t)ns_trunc) / 100ULL); /* 781 */
            CHECK(phase_ns_trunc_domain == 781U);
            /* Exact phase in ns-equivalent fixed relation is 781.25ns => 78125/100; not equal to 781. */
            CHECK((r.phase_q16 * 125U) % (8U * 65536U / 1U) != 0U || r.phase_q16 == 50U * 65536U);
            /* A rounded-ns interval representation (1562 instead of 1562.5) cannot express 50.0 exact counts as 781.25ns;
             * the estimator's count-domain result must remain 50*65536 regardless of ns truncation. */
            CHECK(r.phase_q16 != (uint32_t)(((uint64_t)phase_ns_trunc_domain * 65536ULL * 8ULL) / 125ULL));
        }

        /* Repeat for a non-half case to ensure no silent dependency on ns truncation path. */
        r = ets_phase_estimate(1000U, 2000U, 1250U, ETS_PHASE_RISING, hw_counts); /* 75 counts */
        CHECK(r.valid == 1U);
        CHECK_EQ_U32(r.phase_q16, 75U * 65536U);
    }

    /* All ETS logical intervals (1,2,5,10,20 us => 2,4,10,20,40 counts) produce exact Q16 results. */
    {
        const uint32_t ets_counts[] = {2U, 4U, 10U, 20U, 40U};
        unsigned k;
        for (k = 0U; k < 5U; k++) {
            struct ets_phase_result r = ets_phase_estimate(0U, 100U, 50U, ETS_PHASE_RISING, ets_counts[k]);
            CHECK(r.valid == 1U);
            CHECK_EQ_U32(r.phase_q16, (ets_counts[k] * 65536U) / 2U); /* 50% */
        }
    }

    return 0;
}

static int test_negative_time_bin_arithmetic_reference(void)
{
    /* Standalone crossing-aligned mapping arithmetic check.
     * Model: t_counts_q16 = (i - BENCH_PRE)*hw_counts_q16 + phase_q16 (signed 64-bit),
     * where phase_q16=(1-f)*hw is time from interpolated crossing to sample y1=i (crossing earlier => +phase).
     * Floor-like division is required for negative offsets (e.g. pre-trigger samples); C truncation toward zero is biased. */
    const int32_t bench_pre = 96;
    const uint32_t hw_counts = 100U; /* TB5 */
    const uint32_t ets_counts_tb4 = 40U; /* 20us logical bin interval in timer counts */
    int64_t hw_q16 = ((int64_t)hw_counts << 16);
    int64_t ets_q16 = ((int64_t)ets_counts_tb4 << 16);

    /* i=96 (y1 at center), phase 50% => 50 counts after crossing: t = +50 counts, bin offset floor(1.25)=1 */
    {
        int32_t i = 96;
        uint32_t phase_q16 = 50U * 65536U;
        int64_t t_q16 = (int64_t)(i - bench_pre) * hw_q16 + (int64_t)phase_q16; /* +50<<16 */
        CHECK(t_q16 / ets_q16 == 1);
        CHECK((int64_t)bench_pre + (t_q16 / ets_q16) == 97);
    }

    /* Negative offset case (pre-trigger sample): i=95, phase 50 => t = -50 counts, bin offset floor(-1.25)=-2 */
    {
        int32_t i = 95;
        uint32_t phase_q16 = 50U * 65536U;
        int64_t t_q16 = (int64_t)(i - bench_pre) * hw_q16 + (int64_t)phase_q16; /* -50<<16 */
        int64_t numer = t_q16;
        int64_t bin_off_floor;
        if (numer >= 0) bin_off_floor = numer / ets_q16;
        else bin_off_floor = -(((-numer) + ets_q16 - 1) / ets_q16);
        CHECK(bin_off_floor == -2); /* floor(-1.25) */
        /* C truncation toward zero would give -1 (biased) */
        {
            int64_t bin_off_trunc = numer / ets_q16;
            CHECK(bin_off_trunc == -1);
            CHECK(bin_off_floor != bin_off_trunc);
        }
        CHECK((int64_t)bench_pre + bin_off_floor == 94);
    }

    /* non-negative case with larger offset */
    {
        uint32_t phase_q16 = 0U;
        int64_t t_q16 = (int64_t)(97 - bench_pre) * hw_q16 + (int64_t)phase_q16; /* +100 counts */
        CHECK(t_q16 / ets_q16 == 2);
    }

    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_rising_fractions();
    failures += test_falling_fractions();
    failures += test_boundary_equality();
    failures += test_equal_flat_and_wrong_direction();
    failures += test_out_of_range_and_invalid_inputs();
    failures += test_near_zero_denom_and_conditioning();
    failures += test_phase_normalization();
    failures += test_exact_timer_count_intervals_and_no_ns_rounding();
    failures += test_negative_time_bin_arithmetic_reference();

    if (failures == 0) {
        printf("ets_phase: all tests passed\n");
    }
    return failures;
}
