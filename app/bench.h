/* Bench reporting - pure text formatting, host-testable.
 *
 * The first live acquisition run has no screen renderer yet, so the only way
 * to see whether the hardware path works is to print what came back.  Keeping
 * the formatting here (instead of inside main.c) means the exact text, the
 * truncation behaviour and the rejection paths are all covered by host tests;
 * main.c only has to call two functions and write the result to the console.
 *
 * Both functions NUL-terminate `dst` whenever dst != NULL and cap > 0, and
 * return 1 on a parameter error (NULL pointer, cap 0, empty window) or when
 * the text did not fit.
 */
#ifndef BENCH_H
#define BENCH_H

#include <stdint.h>

#include "frame.h"

/* One line: cycle/timeout/error counters plus the measurements of a frame.
 * `cap` is the destination size in bytes (64 covers the widest line). */
uint8_t bench_format_stats(const struct frame *f,
                           uint32_t cycles, uint32_t timeouts, uint32_t errors,
                           char *dst, uint32_t cap);

/* One line: up to `n` raw counts, space separated.  Stops early when the
 * line would not fit rather than truncating a number in half. */
uint8_t bench_format_samples(const uint16_t *samples, uint16_t n,
                             char *dst, uint32_t cap);

#endif /* BENCH_H */
