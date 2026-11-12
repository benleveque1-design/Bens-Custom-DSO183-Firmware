/* Host link protocol v0 - framing for a future PC utility.
 *
 * The DSO183's own USB connector is power-only (no data pair routed to the
 * MCU), so the PC link has to go through the console UART on PA9/PA10 with a
 * 3.3 V USB-serial adapter, or over SWD for slow, read-only inspection.
 *
 * This module is pure framing: it turns one sample window into a self
 * describing packet that a host can resynchronise on after a glitch, so the
 * protocol can be specified, tested and versioned before any host tool is
 * written.  Little-endian, 16-bit samples (the ADC is 12-bit).
 *
 *   offset  size  field
 *   0       2     preamble 0xA5 0x5A
 *   2       1     type       0x01 = frame
 *   3       1     sequence   wraps 0..255, lets the host spot drops
 *   4       2     sample count n (LE)
 *   6       2*n   samples, uint16 LE, 0..4095
 *   6+2n    2     CRC-16/CCITT-FALSE over type..samples
 */
#ifndef LINK_H
#define LINK_H

#include <stdint.h>

#define LINK_SOF0            0xA5U
#define LINK_SOF1            0x5AU
#define LINK_TYPE_FRAME      0x01U

/* 2 preamble + 1 type + 1 seq + 2 count + 2 CRC */
#define LINK_OVERHEAD        8U
#define LINK_MAX_SAMPLES     1024U

/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no xorout.
 * The standard check value for "123456789" is 0x29B1. */
uint16_t link_crc16(const uint8_t *data, uint32_t len);

/* Encode one frame into dst.  Returns the number of bytes written, or 0 when
 * the arguments cannot produce a valid packet (NULL pointers, n == 0,
 * n > LINK_MAX_SAMPLES, or cap < LINK_OVERHEAD + 2*n).  Nothing is written on
 * a 0 return. */
uint32_t link_encode_frame(uint8_t *dst, uint32_t cap, uint8_t seq,
                           const uint16_t *samples, uint16_t n);

#endif /* LINK_H */
