#include "bench.h"

#include <stdio.h>

uint8_t bench_format_stats(const struct frame *f,
                           uint32_t cycles, uint32_t timeouts, uint32_t errors,
                           char *dst, uint32_t cap)
{
    int n;

    if ((dst == NULL) || (cap == 0U)) {
        return 1U;
    }
    dst[0] = '\0';
    if (f == NULL) {
        return 1U;
    }

    n = snprintf(dst, (size_t)cap,
                 "acq ok %lu to %lu err %lu | n %u min %u max %u vpp %u"
                 " mean %u rms %u\r\n",
                 (unsigned long)cycles, (unsigned long)timeouts,
                 (unsigned long)errors,
                 (unsigned)f->stats.n, (unsigned)f->stats.min,
                 (unsigned)f->stats.max, (unsigned)f->stats.vpp,
                 (unsigned)f->stats.mean, (unsigned)f->stats.rms);
    if ((n < 0) || ((uint32_t)n >= cap)) {
        dst[cap - 1U] = '\0';
        return 1U;
    }
    return 0U;
}

uint8_t bench_format_samples(const uint16_t *samples, uint16_t n,
                             char *dst, uint32_t cap)
{
    uint32_t used = 0U;
    uint16_t i;

    if ((dst == NULL) || (cap == 0U)) {
        return 1U;
    }
    dst[0] = '\0';
    if ((samples == NULL) || (n == 0U)) {
        return 1U;
    }

    for (i = 0U; i < n; i++) {
        uint32_t room = cap - used;
        int w = snprintf(dst + used, (size_t)room,
                         (i == 0U) ? "smp %u" : " %u", (unsigned)samples[i]);

        if ((w < 0) || ((uint32_t)w >= room)) {
            dst[used] = '\0';          /* keep the completed numbers only */
            return 1U;
        }
        used += (uint32_t)w;
    }

    if ((cap - used) < 3U) {
        dst[used] = '\0';
        return 1U;
    }
    dst[used++] = '\r';
    dst[used++] = '\n';
    dst[used] = '\0';
    return 0U;
}
