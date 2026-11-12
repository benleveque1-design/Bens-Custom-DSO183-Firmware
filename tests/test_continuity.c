#include "continuity.h"

#include <stdio.h>

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

int main(void)
{
    /* Below the on threshold: connected, whatever the previous state. */
    CHECK(continuity_update(0U, 0U, 60U, 120U) == 1U);
    CHECK(continuity_update(0U, 60U, 60U, 120U) == 1U);

    /* Above the off threshold: open. */
    CHECK(continuity_update(1U, 120U, 60U, 120U) == 0U);
    CHECK(continuity_update(1U, 4000U, 60U, 120U) == 0U);

    /* Inside the band: hold the previous state (hysteresis). */
    CHECK(continuity_update(1U, 90U, 60U, 120U) == 1U);
    CHECK(continuity_update(0U, 90U, 60U, 120U) == 0U);

    /* Degenerate thresholds must not invert. */
    CHECK(continuity_update(1U, 500U, 120U, 60U) == 0U);

    if (failures == 0) {
        printf("continuity: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
