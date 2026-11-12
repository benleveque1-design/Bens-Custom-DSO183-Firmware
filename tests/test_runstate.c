/* Host unit tests for the shared run/stream state (app/runstate.c).
 *
 * These two flags used to live in main.c as loose statics, which let the
 * menu save a stale copy over a PC-link RUN/STOP.  The contract tested here
 * is small but load-bearing: reset comes up running and not streaming, and
 * every setter normalises any non-zero value to 1.
 */
#include "runstate.h"

#include <stdio.h>

#define FAIL(msg) do { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); return 1; } while (0)
#define CHECK(cond) do { if (!(cond)) { FAIL(#cond); } } while (0)

static int test_init(void)
{
    runstate_run_set(0U);
    runstate_stream_set(1U);
    runstate_init();
    CHECK(runstate_run() == 1U);
    CHECK(runstate_stream() == 0U);
    return 0;
}

static int test_normalise(void)
{
    runstate_run_set(7U);
    CHECK(runstate_run() == 1U);
    runstate_run_set(0U);
    CHECK(runstate_run() == 0U);

    runstate_stream_set(0xFFU);
    CHECK(runstate_stream() == 1U);
    runstate_stream_set(0U);
    CHECK(runstate_stream() == 0U);
    return 0;
}

static int test_independent(void)
{
    runstate_run_set(0U);
    runstate_stream_set(0U);
    runstate_stream_set(1U);
    CHECK(runstate_run() == 0U);      /* HOLD while streaming */
    runstate_run_set(1U);
    CHECK(runstate_stream() == 1U);
    return 0;
}

int main(void)
{
    if (test_init()) return 1;
    if (test_normalise()) return 1;
    if (test_independent()) return 1;
    printf("All runstate tests passed\n");
    return 0;
}