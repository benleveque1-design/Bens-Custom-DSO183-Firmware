#include "link.h"

#include <stddef.h>

uint16_t link_crc16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFU;
    uint32_t i;
    uint8_t  b;

    if (data == NULL) {
        return 0U;
    }
    for (i = 0U; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (b = 0U; b < 8U; b++) {
            crc = ((crc & 0x8000U) != 0U)
                ? (uint16_t)((uint16_t)(crc << 1) ^ 0x1021U)
                : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint32_t link_encode_frame(uint8_t *dst, uint32_t cap, uint8_t seq,
                           const uint16_t *samples, uint16_t n)
{
    uint32_t need;
    uint32_t p = 6U;
    uint32_t i;
    uint16_t crc;

    if ((dst == NULL) || (samples == NULL)) {
        return 0U;
    }
    if ((n == 0U) || (n > LINK_MAX_SAMPLES)) {
        return 0U;
    }
    need = LINK_OVERHEAD + (2U * (uint32_t)n);
    if (cap < need) {
        return 0U;                     /* nothing written on failure */
    }

    dst[0] = LINK_SOF0;
    dst[1] = LINK_SOF1;
    dst[2] = LINK_TYPE_FRAME;
    dst[3] = seq;
    dst[4] = (uint8_t)(n & 0xFFU);
    dst[5] = (uint8_t)(n >> 8);

    for (i = 0U; i < n; i++) {
        dst[p++] = (uint8_t)(samples[i] & 0xFFU);
        dst[p++] = (uint8_t)(samples[i] >> 8);
    }

    crc = link_crc16(&dst[2], p - 2U);
    dst[p++] = (uint8_t)(crc & 0xFFU);
    dst[p++] = (uint8_t)(crc >> 8);
    return p;
}
