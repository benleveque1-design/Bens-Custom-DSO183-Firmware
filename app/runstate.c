#include "runstate.h"

/* Explicitly zeroed, not .noinit: a reset must always come up running the
 * panel display and not streaming, whatever the previous build left behind. */
static uint8_t s_run = 1U;
static uint8_t s_stream = 0U;

void runstate_init(void)
{
    s_run = 1U;
    s_stream = 0U;
}

uint8_t runstate_run(void)
{
    return s_run;
}

void runstate_run_set(uint8_t on)
{
    s_run = (on != 0U) ? 1U : 0U;
}

uint8_t runstate_stream(void)
{
    return s_stream;
}

void runstate_stream_set(uint8_t on)
{
    s_stream = (on != 0U) ? 1U : 0U;
}