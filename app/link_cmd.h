/* PC link - host command layer (protocol v1), pure and host-testable.
 *
 * The scope must be fully controllable from a PC, not just able to stream
 * samples.  Frames travel device -> PC as LINK_TYPE_FRAME (see link.h); this
 * module adds the other direction, a fixed-size command, plus a fixed-size
 * reply so the host can acknowledge every setting.
 *
 * Both packets keep the same envelope as the sample frames:
 *
 *   command  A5 5A | 0x02 | seq | op | arg_lo | arg_hi | crc_lo | crc_hi
 *   reply    A5 5A | 0x03 | seq | op | status | val_lo | val_hi | crc_lo | crc_hi
 *
 * the CRC is byte 2 up to the last byte before the CRC (CRC-16/CCITT-FALSE,
 * the same function the sample frames use), stored little-endian.
 *
 * Nothing here touches hardware: the parser validates and the caller decides
 * what a command means.  That keeps the wire format testable without a board
 * and leaves policy (clamping, whether a knob may move during a sweep) in
 * one place in the firmware.
 */
#ifndef LINK_CMD_H
#define LINK_CMD_H

#include <stdint.h>

#define LINK_CMD_SOF0      0xA5U
#define LINK_CMD_SOF1      0x5AU
#define LINK_CMD_TYPE_CMD  0x02U
#define LINK_CMD_TYPE_REP  0x03U
#define LINK_CMD_LEN       10U

/* Status byte in a reply. */
#define LINK_ST_OK         0x00U
#define LINK_ST_BAD_ARG    0x01U   /* argument out of range for the opcode  */
#define LINK_ST_BUSY       0x02U   /* acquisition could not accept it yet   */
#define LINK_ST_UNKNOWN    0x03U   /* opcode not implemented                */

/* Opcodes.  Arguments are uint16 little-endian on the wire. */
enum link_op {
    LINK_OP_NOP          = 0x00U,  /* arg ignored                           */
    LINK_OP_SET_TIMEBASE = 0x01U,  /* arg = index TIMEBASE_FIRST_SINGLE_SHOT..(TIMEBASE_COUNT-1) */
    LINK_OP_SET_TRIGGER  = 0x02U,  /* arg = trigger level 0..4095           */
    LINK_OP_SET_SPAN     = 0x03U,  /* arg = display span in counts 16..4095 */
    LINK_OP_RUN          = 0x04U,  /* arg ignored, resume the sweep         */
    LINK_OP_STOP         = 0x05U,  /* arg ignored, HOLD the current frame   */
    LINK_OP_GET_STATUS   = 0x06U,  /* arg ignored, reply carries state      */
    LINK_OP_STREAM       = 0x07U,  /* arg = 0 stop, 1 start sample frames   */
    LINK_OP_SET_TRIG_MODE= 0x08U,  /* arg = 0 AUTO, 1 NORM, 2 SINGLE        */
    LINK_OP_GET_VALUE    = 0x09U,  /* arg = subindex (see GETV_*), reply value */
    LINK_OP_SET_TRIG_SLOPE=0x0AU,  /* arg = 0 rising, 1 falling, 2 auto     */
    LINK_OP_CAL_SET_OFFSET_MV=0x0BU, /* arg = int16 mV (bit pattern in u16)  */
    LINK_OP_CAL_SET_GAIN_X1000=0x0CU,/* arg = gain*1000 (1000 = 1.0x)        */
    LINK_OP_CAL_SAVE     = 0x0DU,  /* persist active cal+table to flash     */
    LINK_OP_CAL_RESET    = 0x0EU,  /* reset cal to identity (+clear table)  */
    LINK_OP_WAVE_SAVE    = 0x0FU,  /* arg = slot 0/1, save current frame    */
    LINK_OP_WAVE_RECALL  = 0x10U,  /* arg = slot 0/1, recall to display     */
    LINK_OP_SET_AVG      = 0x11U,  /* arg = 1(off),2,4,8 frame average      */
    LINK_OP_COUNT        = 0x12U
};

/* GET_VALUE subindices (reply->value is u16). */
enum link_getv {
    GETV_TRIG_LEVEL  = 0U,
    GETV_TRIG_MODE   = 1U,
    GETV_TIMEBASE    = 2U,
    GETV_SPAN        = 3U,
    GETV_STATUS      = 4U,   /* same as GET_STATUS */
    GETV_VPP_COUNTS  = 5U,
    GETV_VRMS_COUNTS = 6U,
    GETV_MIN         = 7U,
    GETV_MAX         = 8U,
    GETV_FREQ_HZ     = 9U,   /* clamped to 65535 */
    GETV_DUTY_X10    = 10U,  /* 0.1% units */
    GETV_BUILD_SEQ   = 11U,  /* build seq & 0xFFFF */
    GETV_TRIG_SLOPE  = 12U,  /* 0 rising, 1 falling, 2 auto */
    GETV_CAL_OFFSET_MV= 13U, /* int16 mV (bit pattern) */
    GETV_CAL_GAIN_X1000=14U, /* gain*1000 */
    GETV_WAVE_SAVED_0= 15U,  /* 1 if slot 0 valid */
    GETV_WAVE_SAVED_1= 16U,  /* 1 if slot 1 valid */
    GETV_AVG_N       = 17U,  /* 1,2,4,8 */
    /* Diagnostic telemetry (read-only snapshot of last completed acquisition).
     * Added for trigger characterization; easy to remove. */
    GETV_DIAG_SEQ           = 18U,  /* increments per recorded acquisition */
    GETV_DIAG_GATE_TRIGGERED= 19U,  /* 1 if HW AWD triggered this cycle */
    GETV_DIAG_HW_INDEX      = 20U,  /* raw gate.trigger_index (ring idx) */
    GETV_DIAG_REFINED_INDEX = 21U,  /* final alignment index used */
    GETV_DIAG_REFINED_FOUND = 22U,  /* 1 if SW refinement found crossing */
    GETV_DIAG_FALLBACK      = 23U,  /* 0 refined, 1 used HW idx, 2 tail/untriggered */
    GETV_DIAG_TIMEBASE      = 24U,  /* timebase index */
    GETV_DIAG_TRIG_SLOPE    = 25U,  /* 0 rise, 1 fall, 2 auto */
    GETV_DIAG_TRIG_LEVEL    = 26U,  /* trigger level */
    GETV_DIAG_TRIG_MODE     = 27U,  /* 0 AUTO, 1 NORM, 2 SINGLE */
    GETV_COUNT       = 28U
};

struct link_cmd {
    uint8_t  seq;
    uint8_t  op;
    uint16_t arg;
};

/* Encode a command (host side).  Returns LINK_CMD_LEN, or 0 when dst is NULL,
 * cap is too small, op is unknown, or the sequence/op cannot be represented. */
uint32_t link_cmd_encode(uint8_t *dst, uint32_t cap, uint8_t seq,
                         uint8_t op, uint16_t arg);

/* Parse and validate a command (device side).  Returns 1 on success, 0 for a
 * short buffer, bad preamble, wrong type, unknown opcode or CRC mismatch.  On
 * failure `out` is left untouched. */
uint8_t link_cmd_parse(const uint8_t *src, uint32_t len, struct link_cmd *out);

/* Encode a reply.  Returns LINK_CMD_LEN, or 0 on bad arguments/capacity. */
uint32_t link_rep_encode(uint8_t *dst, uint32_t cap, uint8_t seq,
                         uint8_t op, uint8_t status, uint16_t value);

/* Parse a reply (host side).  Returns 1 on success, 0 otherwise; `out` is left
 * untouched on failure. */
uint8_t link_rep_parse(const uint8_t *src, uint32_t len, struct link_cmd *out,
                       uint8_t *status, uint16_t *value);

/* Range check helpers, so the firmware and the host agree on what is legal. */
uint8_t link_cmd_arg_valid(uint8_t op, uint16_t arg);

#endif /* LINK_CMD_H */
