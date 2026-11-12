#include "timebase.h"

#include <stddef.h>

/* Table generated from the DSO183's published timebase list (user-supplied):
 *   1us,2us,5us,10us,20us,50us,0.1ms,0.2ms,0.5ms,1ms,2ms,5ms,10ms,20ms,
 *   50ms,0.1s,0.2s,0.5s,1s,2s,5s,10s,20s,50s
 * Capped at 50 s/div: 100 s+ windows make the synchronous capture/UI
 * impractically slow (minutes per cycle at 10 div/window).
 * Index 0 = fastest (1 us/div), 26 = slowest (500 s/div) - matching the
 * investigation notes ('fast path for timebases <= 12', 'slow range 18..21')
 * and the on-screen order.  Each entry's (PSC, ARR) reproduces
 * div_us/(32 samples) to one timer count at the 64 MHz TIM3 tick
 * (15.625 ns/count; APB1 is 32 MHz and the F1 timer clock doubles when the
 * APB1 prescaler is not 1).  single_shot follows the ADC conversion-time
 * floor (1312.5 ns = 84 counts): the five fastest steps need equivalent-time
 * (REPT) sampling, which the acquisition engine does not implement yet. */
static const struct timebase_step s_table[TIMEBASE_COUNT] = {
    {         1u,     0u,     1u, 0},  /* idx 0: 1us    REPT */
    {         2u,     0u,     3u, 0},  /* idx 1: 2us    REPT */
    {         5u,     0u,     9u, 0},  /* idx 2: 5us    REPT */
    {        10u,     0u,    19u, 0},  /* idx 3: 10us   REPT */
    {        20u,     0u,    39u, 0},  /* idx 4: 20us   REPT */
    {        50u,     0u,    99u, 1},  /* idx 5: 50us   first SS */
    {       100u,     0u,   199u, 1},  /* idx 6: 0.1ms */
    {       200u,     0u,   399u, 1},  /* idx 7: 0.2ms */
    {       500u,     0u,   999u, 1},  /* idx 8: 0.5ms */
    {      1000u,     0u,  1999u, 1},  /* idx 9: 1ms */
    {      2000u,     0u,  3999u, 1},  /* idx 10: 2ms */
    {      5000u,     0u,  9999u, 1},  /* idx 11: 5ms */
    {     10000u,     0u, 19999u, 1},  /* idx 12: 10ms */
    {     20000u,     0u, 39999u, 1},  /* idx 13: 20ms */
    {     50000u,     1u, 49999u, 1},  /* idx 14: 50ms */
    {    100000u,     3u, 49999u, 1},  /* idx 15: 0.1s */
    {    200000u,     7u, 49999u, 1},  /* idx 16: 0.2s */
    {    500000u,    15u, 62499u, 1},  /* idx 17: 0.5s */
    {   1000000u,    31u, 62499u, 1},  /* idx 18: 1s */
    {   2000000u,    63u, 62499u, 1},  /* idx 19: 2s */
    {   5000000u,   159u, 62499u, 1},  /* idx 20: 5s */
    {  10000000u,   319u, 62499u, 1},  /* idx 21: 10s */
    {  20000000u,   639u, 62499u, 1},  /* idx 22: 20s */
    {  50000000u,  1599u, 62499u, 1},  /* idx 23: 50s */
};

_Static_assert(TIMEBASE_COUNT == 24U, "timebase capped at 50 s/div (0..23)");
_Static_assert(TIMEBASE_TIM3_HZ == (2U * TIMEBASE_APB1_HZ),
               "F1 timer clock is 2x PCLK1 when the APB1 prescaler is not 1");
_Static_assert(TIMEBASE_MIN_SS_COUNTS == (TIMEBASE_CONV_CYCLES
                                         * TIMEBASE_TIM3_HZ
                                         / TIMEBASE_ADC_CLK_HZ),
               "min single-shot counts must match the ADC conversion time");
_Static_assert((TIMEBASE_TIM3_HZ / TIMEBASE_ADC_CLK_HZ)
               == TIMEBASE_ADC_CYCLES_PER_TICK,
               "ADC clock must be TIM3/6 (PCLK2/6 with PCLK2 = HCLK)");

const struct timebase_step *timebase_entry(uint8_t index)
{
    if (index >= TIMEBASE_COUNT) {
        return NULL;
    }
    return &s_table[index];
}

uint32_t timebase_window_us(uint8_t index)
{
    const struct timebase_step *st = timebase_entry(index);

    if (st == NULL) {
        return 0U;
    }
    if (st->div_us > (UINT32_MAX / (uint32_t)TIMEBASE_DIVS)) {
        return UINT32_MAX;              /* 5 s..500 s windows saturate */
    }
    return st->div_us * (uint32_t)TIMEBASE_DIVS;
}

uint32_t timebase_tick_ns(uint8_t index)
{
    const struct timebase_step *st = timebase_entry(index);
    uint32_t counts;

    if (st == NULL) {
        return 0U;
    }
    counts = ((uint32_t)st->tick_psc + 1U) * ((uint32_t)st->tick_arr + 1U);
    /* counts x 15.625 = counts x 125 / 8 ns (64 MHz TIM3).  Saturate
     * instead of overflowing (15.625 s max tick > 4.2e9 ns). */
    if (counts > (UINT32_MAX / 125U)) {
        return UINT32_MAX;
    }
    return (counts * 125U) / 8U;
}

/* ------------------------------------------------- ADC sample-time policy */

/* SMPR code -> sampling time in HALF ADC clocks (1.5, 7.5, 13.5 ... 239.5). */
static const uint16_t k_smp_cycles_x2[8] = {
    3U, 15U, 27U, 57U, 83U, 111U, 143U, 479U
};

uint16_t timebase_smp_cycles_x2(uint8_t code)
{
    if (code > 7U) {
        return 0U;
    }
    return k_smp_cycles_x2[code];
}

/* Does a conversion with this SMP code finish inside `counts` TIM3 ticks? */
static uint8_t smp_fits(uint32_t counts, uint8_t code)
{
    /* (sampling + 12.5) ADC clocks, converted to TIM3 counts:
     * half_clocks_of_whole_conversion * TIM3-tick-per-ADC-clock / 2. */
    uint32_t total_x2 = (uint32_t)k_smp_cycles_x2[code] + 25U;  /* + 12.5 */

    return ((total_x2 * TIMEBASE_ADC_CYCLES_PER_TICK / 2U) <= counts) ? 1U : 0U;
}

uint8_t timebase_smp_code(uint8_t index)
{
    const struct timebase_step *st = timebase_entry(index);
    uint32_t counts;

    if ((st == NULL) || (st->single_shot == 0U)) {
        return 0U;      /* not paced by TIM3: shortest aperture */
    }
    counts = ((uint32_t)st->tick_psc + 1U) * ((uint32_t)st->tick_arr + 1U);
    if (smp_fits(counts, TIMEBASE_SMP_BASELINE) != 0U) {
        return TIMEBASE_SMP_BASELINE;
    }
    return 0U;
}

const char *timebase_label(uint8_t index)
{
    static const char *const s_labels[TIMEBASE_COUNT] = {
        "1us",  "2us",  "5us",  "10us", "20us", "50us",
        "0.1ms","0.2ms","0.5ms","1ms",  "2ms",  "5ms",
        "10ms", "20ms", "50ms", "0.1s", "0.2s", "0.5s",
        "1s",   "2s",   "5s",   "10s",  "20s",  "50s"
    };

    if (index >= TIMEBASE_COUNT) {
        return "?";
    }
    return s_labels[index];
}
