#include "continuity.h"

uint8_t continuity_update(uint8_t prev, uint16_t level,
                          uint16_t on_counts, uint16_t off_counts)
{
    if (on_counts >= off_counts) {
        /* Degenerate band: fall back to a single threshold. */
        return (level <= on_counts) ? 1U : 0U;
    }
    if (level <= on_counts) {
        return 1U;
    }
    if (level >= off_counts) {
        return 0U;
    }
    return (prev != 0U) ? 1U : 0U;
}
