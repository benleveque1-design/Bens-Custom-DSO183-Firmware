/* Host tests for the PC link command layer (app/link_cmd.c). */
#include "link_cmd.h"
#include "link.h"

#include <stdio.h>

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

int main(void)
{
    uint8_t buf[LINK_CMD_LEN];
    struct link_cmd c;
    uint8_t status = 0U;
    uint16_t value = 0U;

    /* --- encode a command: SET_TIMEBASE index 9, seq 7 ------------------- */
    CHECK(link_cmd_encode(buf, sizeof buf, 7U, LINK_OP_SET_TIMEBASE, 9U)
          == LINK_CMD_LEN);
    CHECK(buf[0] == 0xA5U && buf[1] == 0x5AU);   /* preamble   */
    CHECK(buf[2] == LINK_CMD_TYPE_CMD);          /* type       */
    CHECK(buf[3] == 7U && buf[4] == LINK_OP_SET_TIMEBASE);
    CHECK(buf[5] == 9U && buf[6] == 0U);         /* arg LE     */
    CHECK(buf[8] == (uint8_t)(link_crc16(&buf[2], 6U) & 0xFFU));
    CHECK(buf[9] == (uint8_t)(link_crc16(&buf[2], 6U) >> 8));

    /* --- parse it back --------------------------------------------------- */
    c.seq = 0U; c.op = 0xFFU; c.arg = 0xFFFFU;
    CHECK(link_cmd_parse(buf, sizeof buf, &c) == 1U);
    CHECK(c.seq == 7U && c.op == LINK_OP_SET_TIMEBASE && c.arg == 9U);

    /* --- rejections leave the output untouched --------------------------- */
    c.seq = 0x42U;
    CHECK(link_cmd_parse(buf, 5U, &c) == 0U);            /* short        */
    CHECK(c.seq == 0x42U);
    buf[0] ^= 0xFFU;
    CHECK(link_cmd_parse(buf, sizeof buf, &c) == 0U);    /* bad preamble */
    buf[0] ^= 0xFFU;
    buf[8] ^= 0x01U;
    CHECK(link_cmd_parse(buf, sizeof buf, &c) == 0U);    /* bad CRC      */
    buf[8] ^= 0x01U;
    CHECK(link_cmd_parse(NULL, sizeof buf, &c) == 0U);
    CHECK(link_cmd_parse(buf, sizeof buf, NULL) == 0U);

    /* --- unknown opcode with a VALID crc is still refused ---------------- */
    buf[4] = 0x7FU;
    {
        uint16_t crc = link_crc16(&buf[2], 6U);
        buf[8] = (uint8_t)(crc & 0xFFU);
        buf[9] = (uint8_t)(crc >> 8);
    }
    CHECK(link_cmd_parse(buf, sizeof buf, &c) == 0U);
    CHECK(link_cmd_encode(buf, sizeof buf, 1U, 0x7FU, 0U) == 0U);

    /* --- capacity -------------------------------------------------------- */
    CHECK(link_cmd_encode(buf, LINK_CMD_LEN - 1U, 1U, LINK_OP_RUN, 0U) == 0U);
    CHECK(link_cmd_encode(buf, LINK_CMD_LEN, 1U, LINK_OP_RUN, 0U) == LINK_CMD_LEN);

    /* --- reply round trip ------------------------------------------------ */
    CHECK(link_rep_encode(buf, sizeof buf, 3U, LINK_OP_GET_STATUS,
                          LINK_ST_OK, 0x1234U) == LINK_CMD_LEN);
    CHECK(buf[2] == LINK_CMD_TYPE_REP && buf[4] == LINK_OP_GET_STATUS);
    CHECK(buf[5] == LINK_ST_OK && buf[6] == 0x34U && buf[7] == 0x12U);
    CHECK(link_rep_parse(buf, sizeof buf, &c, &status, &value) == 1U);
    CHECK(c.seq == 3U && c.op == LINK_OP_GET_STATUS);
    CHECK(status == LINK_ST_OK && value == 0x1234U);
    CHECK(link_rep_parse(buf, 9U, &c, &status, &value) == 0U);
    CHECK(link_rep_parse(buf, sizeof buf, NULL, &status, &value) == 0U);

    /* --- argument policy ------------------------------------------------- */
    CHECK(link_cmd_arg_valid(LINK_OP_SET_TIMEBASE, 0U) == 0U);
    CHECK(link_cmd_arg_valid(LINK_OP_SET_TIMEBASE, 4U) == 0U);
    CHECK(link_cmd_arg_valid(LINK_OP_SET_TIMEBASE, 5U) == 1U);
    CHECK(link_cmd_arg_valid(LINK_OP_SET_TIMEBASE, 23U) == 1U);
    CHECK(link_cmd_arg_valid(LINK_OP_SET_TIMEBASE, 24U) == 0U);
    CHECK(link_cmd_arg_valid(LINK_OP_SET_TIMEBASE, 26U) == 0U);
    CHECK(link_cmd_arg_valid(LINK_OP_SET_TRIGGER, 4095U) == 1U);
    CHECK(link_cmd_arg_valid(LINK_OP_SET_TRIGGER, 4096U) == 0U);
    CHECK(link_cmd_arg_valid(LINK_OP_SET_SPAN, 15U) == 0U);
    CHECK(link_cmd_arg_valid(LINK_OP_SET_SPAN, 16U) == 1U);
    CHECK(link_cmd_arg_valid(LINK_OP_SET_SPAN, 4095U) == 1U);
    CHECK(link_cmd_arg_valid(LINK_OP_STREAM, 1U) == 1U);
    CHECK(link_cmd_arg_valid(LINK_OP_STREAM, 2U) == 0U);
    CHECK(link_cmd_arg_valid(LINK_OP_RUN, 0xFFFFU) == 1U);

    if (failures == 0) {
        printf("link_cmd: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
