/* Host tests for the PC-link device dispatcher (app/link_host.c). */
#include "link_host.h"

#include <stdio.h>

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static uint8_t  g_ok;
static uint16_t g_arg;
static uint8_t  g_run;
static uint8_t  g_stream;

static uint8_t fake_timebase(uint8_t i) { g_arg = i; return g_ok; }
static uint8_t fake_trigger(uint16_t v) { g_arg = v; return g_ok; }
static uint8_t fake_span(uint16_t v)    { g_arg = v; return g_ok; }
static uint8_t fake_run(uint8_t on)     { g_run = on; return g_ok; }
static uint8_t fake_stream(uint8_t on)  { g_stream = on; return g_ok; }
static uint16_t fake_status(void)       { return 0xBEEFU; }

int main(void)
{
    const struct link_ops ops = {
        fake_timebase, fake_trigger, fake_span, fake_run, fake_stream, fake_status
    };
    struct link_cmd cmd;
    uint8_t status = 0xFFU;
    uint16_t value = 0xFFFFU;

    g_ok = 1U;

    /* NOP answers OK with no payload. */
    cmd.seq = 1U; cmd.op = LINK_OP_NOP; cmd.arg = 0x1234U;
    CHECK(link_host_exec(&cmd, &ops, &status, &value) == 1U);
    CHECK(status == LINK_ST_OK && value == 0U);

    /* The three value commands pass the argument through and honour failure. */
    cmd.op = LINK_OP_SET_TIMEBASE; cmd.arg = 9U;
    CHECK(link_host_exec(&cmd, &ops, &status, &value) == 1U);
    CHECK(status == LINK_ST_OK && g_arg == 9U);

    g_ok = 0U;
    CHECK(link_host_exec(&cmd, &ops, &status, &value) == 1U);
    CHECK(status == LINK_ST_BAD_ARG);
    g_ok = 1U;

    cmd.op = LINK_OP_SET_TRIGGER; cmd.arg = 2048U;
    CHECK(link_host_exec(&cmd, &ops, &status, &value) == 1U);
    CHECK(status == LINK_ST_OK && g_arg == 2048U);

    cmd.op = LINK_OP_SET_SPAN; cmd.arg = 32U;
    CHECK(link_host_exec(&cmd, &ops, &status, &value) == 1U);
    CHECK(status == LINK_ST_OK && g_arg == 32U);

    /* RUN / STOP drive the same hook with 1 / 0. */
    g_run = 0xFFU;
    cmd.op = LINK_OP_RUN; cmd.arg = 0U;
    CHECK(link_host_exec(&cmd, &ops, &status, &value) == 1U);
    CHECK(g_run == 1U);

    cmd.op = LINK_OP_STOP; cmd.arg = 0U;
    CHECK(link_host_exec(&cmd, &ops, &status, &value) == 1U);
    CHECK(g_run == 0U);

    /* STREAM accepts exactly 0 or 1.  A wider argument is refused by the
     * range policy instead of being normalised behind the host's back. */
    cmd.op = LINK_OP_STREAM; cmd.arg = 1U;
    CHECK(link_host_exec(&cmd, &ops, &status, &value) == 1U);
    CHECK(g_stream == 1U && status == LINK_ST_OK && value == 1U);

    cmd.arg = 0U;
    CHECK(link_host_exec(&cmd, &ops, &status, &value) == 1U);
    CHECK(g_stream == 0U && status == LINK_ST_OK && value == 0U);

    g_stream = 0xAAU;
    cmd.arg = 0xFFFFU;
    CHECK(link_host_exec(&cmd, &ops, &status, &value) == 1U);
    CHECK(status == LINK_ST_BAD_ARG && value == 0U);
    CHECK(g_stream == 0xAAU);                 /* hook not reached */

    /* A 16-bit SET_TIMEBASE argument must not be truncated into a valid index:
     * 261 would otherwise become index 5 and be applied with OK. */
    g_arg = 0xBEEFU;
    cmd.op = LINK_OP_SET_TIMEBASE; cmd.arg = 261U;
    CHECK(link_host_exec(&cmd, &ops, &status, &value) == 1U);
    CHECK(status == LINK_ST_BAD_ARG && value == 0U);
    CHECK(g_arg == 0xBEEFU);                  /* hook not reached */

    /* Same for a trigger level wider than 12 bits and a span outside the
     * documented window. */
    g_arg = 0xBEEFU;
    cmd.op = LINK_OP_SET_TRIGGER; cmd.arg = 4096U;
    CHECK(link_host_exec(&cmd, &ops, &status, &value) == 1U);
    CHECK(status == LINK_ST_BAD_ARG);
    CHECK(g_arg == 0xBEEFU);

    g_arg = 0xBEEFU;
    cmd.op = LINK_OP_SET_SPAN; cmd.arg = 15U;
    CHECK(link_host_exec(&cmd, &ops, &status, &value) == 1U);
    CHECK(status == LINK_ST_BAD_ARG);
    CHECK(g_arg == 0xBEEFU);

    /* GET_STATUS reports the hook value. */
    cmd.op = LINK_OP_GET_STATUS; cmd.arg = 0U;
    CHECK(link_host_exec(&cmd, &ops, &status, &value) == 1U);
    CHECK(status == LINK_ST_OK && value == 0xBEEFU);

    /* Unknown opcode is refused, not silently OK. */
    cmd.op = 0x55U;
    CHECK(link_host_exec(&cmd, &ops, &status, &value) == 1U);
    CHECK(status == LINK_ST_UNKNOWN);

    /* Missing pieces never crash: NULL args return 0 (no reply), and a NULL
     * hook turns the command into BAD_ARG rather than a false OK. */
    CHECK(link_host_exec(NULL, &ops, &status, &value) == 0U);
    CHECK(link_host_exec(&cmd, NULL, &status, &value) == 0U);
    CHECK(link_host_exec(&cmd, &ops, NULL, &value) == 0U);
    CHECK(link_host_exec(&cmd, &ops, &status, NULL) == 0U);

    {
        const struct link_ops bare = { NULL, NULL, NULL, NULL, NULL, NULL };

        cmd.op = LINK_OP_SET_TRIGGER; cmd.arg = 100U;
        CHECK(link_host_exec(&cmd, &bare, &status, &value) == 1U);
        CHECK(status == LINK_ST_BAD_ARG);
    }

    if (failures == 0) {
        printf("link_host: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
