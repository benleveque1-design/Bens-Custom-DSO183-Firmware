/* CRC-16/CCITT-FALSE - one implementation for every persisted record.
 *
 * poly 0x1021, init 0xFFFF, MSB first, no reflection, no final XOR.
 * Check value: crc16_ccitt("123456789", 9) == 0x29B1.
 *
 * Pure and host-testable: the settings and calibration records both use it
 * so a torn or corrupted flash write is rejected instead of silently
 * becoming a valid-looking record.
 */
#ifndef CRC16_H
#define CRC16_H

#include <stdint.h>

uint16_t crc16_ccitt(const uint8_t *data, uint32_t len);

#endif /* CRC16_H */
