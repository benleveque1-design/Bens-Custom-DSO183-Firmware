#ifndef WAVE_STORE_H
#define WAVE_STORE_H

#include <stdint.h>

/* Persistent waveform snapshot in USERDATA flash (WAVE_SLOT_0/1).
 * 16-byte header + up to WAVE_STORE_MAX_SAMPLES u16 samples. */

#define WAVE_STORE_MAGIC       0x57415631UL /* 'WAV1' */
#define WAVE_STORE_VERSION     1U
#define WAVE_STORE_HDR_SIZE    16U
#define WAVE_STORE_MAX_SAMPLES 320U   /* one 10-division display window */

/* Save samples[0..len-1] to slot (0 or 1). Returns 0 on success. */
uint8_t wave_store_save(uint8_t slot, const uint16_t *samples, uint16_t len);

/* Load from slot into samples (max entries). Sets *out_len. Returns 0 on success. */
uint8_t wave_store_load(uint8_t slot, uint16_t *samples, uint16_t max, uint16_t *out_len);

/* 1 if slot contains a valid record. */
uint8_t wave_store_valid(uint8_t slot);

#endif /* WAVE_STORE_H */
