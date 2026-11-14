#include "link_cmd.h"

#include <stddef.h>
#include "link.h"
#include "timebase.h"

/* Shared body: both packets are 10 bytes with the CRC over bytes 2..7. */
static uint32_t encode_packet(uint8_t *dst, uint32_t cap, uint8_t type,
                              uint8_t seq, uint8_t op, uint8_t b5,
                              uint8_t b6, uint8_t b7)
{
    uint16_t crc;

    if ((dst == NULL) || (cap < LINK_CMD_LEN)) {
        return 0U;
    }
    dst[0] = LINK_CMD_SOF0;
    dst[1] = LINK_CMD_SOF1;
    dst[2] = type;
    dst[3] = seq;
    dst[4] = op;
    dst[5] = b5;
    dst[6] = b6;
    dst[7] = b7;
    crc = link_crc16(&dst[2], 6U);
    dst[8] = (uint8_t)(crc & 0xFFU);
    dst[9] = (uint8_t)(crc >> 8);
    return LINK_CMD_LEN;
}

/* Shared parser: validates envelope + CRC, then hands back bytes 3..7. */
static uint8_t parse_packet(const uint8_t *src, uint32_t len, uint8_t type,
                            uint8_t *seq, uint8_t *op, uint8_t *b5,
                            uint8_t *b6, uint8_t *b7)
{
    uint16_t crc;
    uint16_t want;

    if ((src == NULL) || (len < LINK_CMD_LEN)) {
        return 0U;
    }
    if ((src[0] != LINK_CMD_SOF0) || (src[1] != LINK_CMD_SOF1)
        || (src[2] != type)) {
        return 0U;
    }
    want = (uint16_t)((uint16_t)src[8] | ((uint16_t)src[9] << 8));
    crc  = link_crc16(&src[2], 6U);
    if (crc != want) {
        return 0U;
    }
    if (seq != NULL) { *seq = src[3]; }
    if (op  != NULL) { *op  = src[4]; }
    if (b5  != NULL) { *b5  = src[5]; }
    if (b6  != NULL) { *b6  = src[6]; }
    if (b7  != NULL) { *b7  = src[7]; }
    return 1U;
}

uint32_t link_cmd_encode(uint8_t *dst, uint32_t cap, uint8_t seq,
                         uint8_t op, uint16_t arg)
{
    if (op >= LINK_OP_COUNT) {
        return 0U;
    }
    return encode_packet(dst, cap, LINK_CMD_TYPE_CMD, seq, op,
                         (uint8_t)(arg & 0xFFU), (uint8_t)(arg >> 8), 0U);
}

uint8_t link_cmd_parse(const uint8_t *src, uint32_t len, struct link_cmd *out)
{
    uint8_t seq = 0U;
    uint8_t op  = 0U;
    uint8_t lo  = 0U;
    uint8_t hi  = 0U;

    if (out == NULL) {
        return 0U;
    }
    if (parse_packet(src, len, LINK_CMD_TYPE_CMD, &seq, &op, &lo, &hi, NULL) == 0U) {
        return 0U;
    }
    if (op >= LINK_OP_COUNT) {
        return 0U;
    }
    out->seq = seq;
    out->op  = op;
    out->arg = (uint16_t)((uint16_t)lo | ((uint16_t)hi << 8));
    return 1U;
}

uint32_t link_rep_encode(uint8_t *dst, uint32_t cap, uint8_t seq,
                         uint8_t op, uint8_t status, uint16_t value)
{
    if (op >= LINK_OP_COUNT) {
        return 0U;
    }
    return encode_packet(dst, cap, LINK_CMD_TYPE_REP, seq, op, status,
                         (uint8_t)(value & 0xFFU), (uint8_t)(value >> 8));
}

uint8_t link_rep_parse(const uint8_t *src, uint32_t len, struct link_cmd *out,
                       uint8_t *status, uint16_t *value)
{
    uint8_t seq = 0U;
    uint8_t op  = 0U;
    uint8_t st  = 0U;
    uint8_t lo  = 0U;
    uint8_t hi  = 0U;

    if ((out == NULL) || (status == NULL) || (value == NULL)) {
        return 0U;
    }
    if (parse_packet(src, len, LINK_CMD_TYPE_REP, &seq, &op, &st, &lo, &hi) == 0U) {
        return 0U;
    }
    if (op >= LINK_OP_COUNT) {
        return 0U;
    }
    out->seq = seq;
    out->op  = op;
    out->arg = 0U;
    *status  = st;
    *value   = (uint16_t)((uint16_t)lo | ((uint16_t)hi << 8));
    return 1U;
}

uint8_t link_cmd_arg_valid(uint8_t op, uint16_t arg)
{
    switch (op) {
    case LINK_OP_SET_TIMEBASE:
        /* Indices 0..4 are equivalent-time (ETS); engine handles them. */
        return (arg < TIMEBASE_COUNT) ? 1U : 0U;
    case LINK_OP_SET_TRIGGER:
        return (arg <= 4095U) ? 1U : 0U;   /* 12-bit ADC counts              */
    case LINK_OP_SET_SPAN:
        return ((arg >= 16U) && (arg <= 4095U)) ? 1U : 0U;
    case LINK_OP_NOP:
    case LINK_OP_RUN:
    case LINK_OP_STOP:
    case LINK_OP_GET_STATUS:
        return 1U;
    case LINK_OP_STREAM:
        return (arg <= 1U) ? 1U : 0U;
    case LINK_OP_SET_TRIG_MODE:
        return (arg <= 2U) ? 1U : 0U;      /* AUTO/NORM/SINGLE */
    case LINK_OP_GET_VALUE:
        return (arg < (uint16_t)GETV_COUNT) ? 1U : 0U;
    case LINK_OP_SET_TRIG_SLOPE:
        return (arg <= 2U) ? 1U : 0U;      /* rising/falling/auto */
    case LINK_OP_CAL_SET_OFFSET_MV:
        return 1U;                         /* int16 bit pattern in u16 */
    case LINK_OP_CAL_SET_GAIN_X1000:
        return ((arg >= 100U) && (arg <= 10000U)) ? 1U : 0U; /* 0.1x..10x */
    case LINK_OP_CAL_SAVE:
    case LINK_OP_CAL_RESET:
        return 1U;
    case LINK_OP_WAVE_SAVE:
    case LINK_OP_WAVE_RECALL:
        return (arg <= 1U) ? 1U : 0U;      /* slot 0/1 */
    case LINK_OP_SET_AVG:
        return ((arg == 1U) || (arg == 2U) || (arg == 4U) || (arg == 8U)) ? 1U : 0U;
    default:
        return 0U;
    }
}
