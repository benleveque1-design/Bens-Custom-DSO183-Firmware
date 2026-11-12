#include "ladder.h"

#include <stddef.h>

uint8_t ladder_map(uint16_t raw)
{
    if (raw > LADDER_THRESH_HI) {
        return 2u;
    }
    if (raw <= LADDER_THRESH_LO) {
        return 0u;
    }
    return 1u;
}

uint8_t ladder_combo(uint8_t sen1, uint8_t sen2)
{
    if (sen1 > 2u || sen2 > 2u) {
        return 0u;
    }
    return (uint8_t)(LADDER_COMBO_MIN + 3u * sen1 + sen2);
}

uint8_t ladder_coupling(uint8_t cpl_pos)
{
    if (cpl_pos > 2u) {
        return 0u;
    }
    return (uint8_t)(2u - cpl_pos);
}

int ladder_cal_index(uint8_t combo)
{
    if (combo < LADDER_COMBO_MIN || combo > LADDER_COMBO_MAX) {
        return -1;
    }
    return (int)combo - LADDER_COMBO_MIN;
}

void ladder_positions(const uint16_t raw[3], uint8_t pos[3])
{
    uint8_t i;

    if (raw == NULL || pos == NULL) {
        return;
    }
    for (i = 0U; i < 3U; i++) {
        if (raw[i] == LADDER_RAW_INVALID) {
            pos[i] = 3U;               /* no reading */
        } else {
            pos[i] = ladder_map(raw[i]);
        }
    }
}

uint8_t ladder_decode(uint16_t sen1_raw, uint16_t sen2_raw, uint16_t cpl_raw,
                       uint8_t *combo_out, uint8_t *coupling_out)
{
    uint16_t raw[3];
    uint8_t  pos[3];

    raw[0] = sen1_raw;
    raw[1] = sen2_raw;
    raw[2] = cpl_raw;
    ladder_positions(raw, pos);

    if (pos[0] > 2U || pos[1] > 2U || pos[2] > 2U) {
        if (combo_out != NULL)     { combo_out[0] = 0U; }
        if (coupling_out != NULL)  { coupling_out[0] = 0xFFU; }
        return 0U;
    }
    if (combo_out != NULL)     { combo_out[0] = ladder_combo(pos[0], pos[1]); }
    if (coupling_out != NULL)  { coupling_out[0] = ladder_coupling(pos[2]); }
    return 1U;
}
