/* CRC-16/CCITT-FALSE - see crc16.h.  Integer only, no tables, so it does
 * not add a 512-byte rodata table to the image. */
#include "crc16.h"

#include <stddef.h>

uint16_t crc16_ccitt(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFU;
    uint32_t i;
    uint8_t  bit;

    if (data == NULL) {
        return 0xFFFFU;
    }
    for (i = 0U; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((uint16_t)(crc << 1) ^ 0x1021U);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}
