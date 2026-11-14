/* Host test of the complete PC-link command path (app/link_cmd.c +
 * app/link_host.c + app/link.c).
 *
 * The per-module tests prove the codec and the dispatcher separately.  This
 * one runs the same bytes through the whole chain the firmware uses:
 *
 *   host encode -> device parse -> dispatch -> device reply encode -> host parse
 *
 * so a change in one layer that breaks the composition (wrong length, wrong
 * CRC window, status/value swapped) fails here even when each layer still
 * passes its own unit tests.  Range policy is checked at the dispatch layer,
 * where the 16-bit wire argument meets the narrower engine types.
 */
#include "link_cmd.h"
#include "link_host.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static uint8_t  g_timebase = 0xFFU;
static uint16_t g_trigger  = 0xFFFFU;
static uint16_t g_span     = 0U;
static uint8_t  g_run      = 1U;
static uint8_t  g_stream   = 0U;

static uint8_t fake_timebase(uint8_t i) { g_timebase = i; return 1U; }
static uint8_t fake_trigger(uint16_t v) { g_trigger = v; return 1U; }
static uint8_t fake_span(uint16_t v)    { g_span = v; return 1U; }
static uint8_t fake_run(uint8_t on)     { g_run = on; return 1U; }
static uint8_t fake_stream(uint8_t on)  { g_stream = on; return 1U; }
static uint16_t fake_status(void)
{
    return (uint16_t)((g_run != 0U ? 0x01U : 0x00U)
                    | (g_stream != 0U ? 0x02U : 0x00U));
}

static const struct link_ops k_ops = {
    fake_timebase, fake_trigger, fake_span, fake_run, fake_stream, fake_status
};

/* Run one command through the full wire path and return the reply. */
static void roundtrip(uint8_t seq, uint8_t op, uint16_t arg,
                      uint8_t *out_status, uint16_t *out_value)
{
    uint8_t tx[LINK_CMD_LEN];
    uint8_t rx[LINK_CMD_LEN];
    struct link_cmd cmd;
    struct link_cmd echoed;
    uint8_t status = 0xFFU;
    uint16_t value = 0xFFFFU;

    memset(&cmd, 0, sizeof cmd);
    CHECK(link_cmd_encode(tx, (uint32_t)sizeof tx, seq, op, arg)
          == (uint32_t)LINK_CMD_LEN);
    CHECK(link_cmd_parse(tx, (uint32_t)sizeof tx, &cmd) == 1U);
    CHECK(cmd.seq == seq);
    CHECK(cmd.op == op);
    CHECK(cmd.arg == arg);

    CHECK(link_host_exec(&cmd, &k_ops, &status, &value) == 1U);
    CHECK(link_rep_encode(rx, (uint32_t)sizeof rx, cmd.seq, cmd.op,
                          status, value) == (uint32_t)LINK_CMD_LEN);

    memset(&echoed, 0, sizeof echoed);
    {
        uint8_t rstatus = 0xFFU;
        uint16_t rvalue = 0xFFFFU;

        CHECK(link_rep_parse(rx, (uint32_t)sizeof rx, &echoed,
                             &rstatus, &rvalue) == 1U);
        CHECK(echoed.seq == seq);
        CHECK(echoed.op == op);
        *out_status = rstatus;
        *out_value  = rvalue;
    }
}

int main(void)
{
    uint8_t  status;
    uint16_t value;

    /* NOP: acknowledged, no payload. */
    roundtrip(1U, LINK_OP_NOP, 0U, &status, &value);
    CHECK(status == LINK_ST_OK && value == 0U);

    /* Timebase: the whole legal window reaches the hook unchanged. */
    roundtrip(2U, LINK_OP_SET_TIMEBASE, 5U, &status, &value);
    CHECK(status == LINK_ST_OK && g_timebase == 5U);
    roundtrip(3U, LINK_OP_SET_TIMEBASE, 23U, &status, &value);
    CHECK(status == LINK_ST_OK && g_timebase == 23U);

    /* ETS (equivalent-time) entries 0..4 are now accepted. */
    g_timebase = 0xEEU;
    roundtrip(4U, LINK_OP_SET_TIMEBASE, 4U, &status, &value);
    CHECK(status == LINK_ST_OK && value == 0U);
    CHECK(g_timebase == 4U);

    /* A value that does not fit a uint8 must not wrap into a legal index:
     * 261 is 0x0105, so a blind cast would select timebase 5. */
    g_timebase = 0xEEU;
    roundtrip(5U, LINK_OP_SET_TIMEBASE, 261U, &status, &value);
    CHECK(status == LINK_ST_BAD_ARG && value == 0U);
    CHECK(g_timebase == 0xEEU);

    /* Trigger level: 12-bit only. */
    roundtrip(6U, LINK_OP_SET_TRIGGER, 4095U, &status, &value);
    CHECK(status == LINK_ST_OK && g_trigger == 4095U);
    g_trigger = 0xEEEEU;
    roundtrip(7U, LINK_OP_SET_TRIGGER, 4096U, &status, &value);
    CHECK(status == LINK_ST_BAD_ARG && value == 0U);
    CHECK(g_trigger == 0xEEEEU);

    /* Display span: 16..4095. */
    roundtrip(8U, LINK_OP_SET_SPAN, 16U, &status, &value);
    CHECK(status == LINK_ST_OK && g_span == 16U);
    roundtrip(9U, LINK_OP_SET_SPAN, 4095U, &status, &value);
    CHECK(status == LINK_ST_OK && g_span == 4095U);
    g_span = 0xEEEEU;
    roundtrip(10U, LINK_OP_SET_SPAN, 15U, &status, &value);
    CHECK(status == LINK_ST_BAD_ARG && value == 0U);
    CHECK(g_span == 0xEEEEU);
    roundtrip(11U, LINK_OP_SET_SPAN, 4096U, &status, &value);
    CHECK(status == LINK_ST_BAD_ARG && value == 0U);

    /* RUN / STOP / STREAM change the device state and the reply echoes it. */
    roundtrip(12U, LINK_OP_STOP, 0U, &status, &value);
    CHECK(status == LINK_ST_OK && g_run == 0U);
    roundtrip(13U, LINK_OP_GET_STATUS, 0U, &status, &value);
    CHECK(status == LINK_ST_OK && value == 0x00U);   /* stopped, not streaming */

    roundtrip(14U, LINK_OP_RUN, 0U, &status, &value);
    CHECK(status == LINK_ST_OK && g_run == 1U && value == 1U);

    roundtrip(15U, LINK_OP_STREAM, 1U, &status, &value);
    CHECK(status == LINK_ST_OK && g_stream == 1U && value == 1U);
    roundtrip(16U, LINK_OP_GET_STATUS, 0U, &status, &value);
    CHECK(status == LINK_ST_OK && value == 0x03U);   /* run + stream */

    roundtrip(17U, LINK_OP_STREAM, 0U, &status, &value);
    CHECK(status == LINK_ST_OK && g_stream == 0U && value == 0U);
    roundtrip(18U, LINK_OP_GET_STATUS, 0U, &status, &value);
    CHECK(status == LINK_ST_OK && value == 0x01U);

    g_stream = 0xEEU;
    roundtrip(19U, LINK_OP_STREAM, 2U, &status, &value);
    CHECK(status == LINK_ST_BAD_ARG && value == 0U);
    CHECK(g_stream == 0xEEU);

    /* A corrupt byte must not produce a reply at all. */
    {
        uint8_t tx[LINK_CMD_LEN];

        CHECK(link_cmd_encode(tx, (uint32_t)sizeof tx, 20U,
                              LINK_OP_SET_SPAN, 100U)
              == (uint32_t)LINK_CMD_LEN);
        tx[6] ^= 0x01U;
        {
            struct link_cmd cmd;

            memset(&cmd, 0, sizeof cmd);
            CHECK(link_cmd_parse(tx, (uint32_t)sizeof tx, &cmd) == 0U);
        }
    }

    if (failures == 0) {
        printf("link_e2e: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
