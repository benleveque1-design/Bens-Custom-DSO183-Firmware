#include "tim3.h"
#include "timebase.h"

#include <stddef.h>

uint8_t tim3_config_for(uint8_t index, struct tim3_config *out)
{
    const struct timebase_step *st;

    if (out == NULL) {
        return 1U;
    }
    st = timebase_entry(index);
    if (st == NULL) {
        return 1U;
    }

    out->psc = st->tick_psc;
    out->arr = st->tick_arr;
    return 0U;
}

uint32_t tim3_counts(const struct tim3_config *cfg)
{
    if (cfg == NULL) {
        return 0U;
    }
    return ((uint32_t)cfg->psc + 1U) * ((uint32_t)cfg->arr + 1U);
}
