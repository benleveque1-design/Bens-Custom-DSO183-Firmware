#include "cal_table.h"

#include <stddef.h>

#define CAL_COUNT_MAX 4095U

void cal_table_init(struct cal_table *t)
{
    uint8_t i;

    if (t == NULL) {
        return;
    }
    for (i = 0U; i < CAL_COMBO_COUNT; i++) {
        t->zero[i] = 0U;
    }
    t->present = 0U;
}

uint8_t cal_table_present(const struct cal_table *t, uint8_t idx)
{
    if ((t == NULL) || (idx >= CAL_COMBO_COUNT)) {
        return 0U;
    }
    return (uint8_t)(((t->present >> idx) & 1U) != 0U);
}

uint16_t cal_table_zero(const struct cal_table *t, uint8_t idx)
{
    if ((t == NULL) || (idx >= CAL_COMBO_COUNT)
        || (cal_table_present(t, idx) == 0U)) {
        return 0U;
    }
    return t->zero[idx];
}

uint8_t cal_table_set(struct cal_table *t, uint8_t idx, uint16_t raw)
{
    if ((t == NULL) || (idx >= CAL_COMBO_COUNT)) {
        return 1U;
    }
    if (raw > CAL_COUNT_MAX) {
        raw = CAL_COUNT_MAX;
    }
    t->zero[idx] = raw;
    t->present = (uint16_t)(t->present | (uint16_t)(1U << idx));
    return 0U;
}

uint8_t cal_table_clear(struct cal_table *t, uint8_t idx)
{
    if ((t == NULL) || (idx >= CAL_COMBO_COUNT)) {
        return 1U;
    }
    t->zero[idx] = 0U;
    t->present = (uint16_t)(t->present & (uint16_t)~(1U << idx));
    return 0U;
}

uint8_t cal_table_clear_all(struct cal_table *t)
{
    if (t == NULL) {
        return 1U;
    }
    cal_table_init(t);
    return 0U;
}

uint32_t cal_table_pack(const struct cal_table *t, uint8_t *out)
{
    uint32_t i;

    if ((t == NULL) || (out == NULL)) {
        return 0U;
    }
    for (i = 0U; i < CAL_COMBO_COUNT; i++) {
        out[i * 2U]      = (uint8_t)(t->zero[i] & 0xFFU);
        out[i * 2U + 1U] = (uint8_t)(t->zero[i] >> 8);
    }
    out[CAL_COMBO_COUNT * 2U]      = (uint8_t)(t->present & 0xFFU);
    out[CAL_COMBO_COUNT * 2U + 1U] = (uint8_t)(t->present >> 8);
    return CAL_TABLE_BYTES;
}

uint8_t cal_table_unpack(const uint8_t *in, struct cal_table *t)
{
    uint8_t  i;
    uint16_t mask;

    if ((in == NULL) || (t == NULL)) {
        return 1U;
    }
    /* The stored mask decides presence: an entry can legitimately hold 0
     * counts as its calibrated zero (a grounded input at the bottom of the
     * scale), so the zeros alone cannot tell absent from present. */
    mask = (uint16_t)((uint16_t)in[CAL_COMBO_COUNT * 2U]
                    | ((uint16_t)in[CAL_COMBO_COUNT * 2U + 1U] << 8));
    mask = (uint16_t)(mask & ((1U << CAL_COMBO_COUNT) - 1U));

    for (i = 0U; i < CAL_COMBO_COUNT; i++) {
        uint16_t z = (uint16_t)((uint16_t)in[i * 2U]
                              | ((uint16_t)in[i * 2U + 1U] << 8));

        if (z > CAL_COUNT_MAX) {
            /* Impossible reading: drop the entry rather than map the trace
             * to a level that cannot exist. */
            t->zero[i] = 0U;
            mask = (uint16_t)(mask & (uint16_t)~(1U << i));
        } else {
            t->zero[i] = z;
        }
    }
    t->present = mask;
    return 0U;
}