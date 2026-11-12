/* Host unit tests for frame assembly (app/frame.c). */
#include "frame.h"

#include <stdio.h>

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static void fill_ring(uint16_t *ring, uint16_t n, uint16_t base)
{
    uint16_t i;

    for (i = 0U; i < n; i++) {
        ring[i] = (uint16_t)(base + i);
    }
}

static void test_straight(void)
{
    uint16_t ring[16];
    uint16_t dst[16];
    struct frame f;

    fill_ring(ring, 16U, 100U);
    /* trigger at index 8, window 8, 4 samples before -> start 4, 104..111 */
    CHECK(frame_build(ring, 16U, 8U, 8U, 4U, dst, 16U, &f) == 0U);
    CHECK(f.length == 8U);
    CHECK(f.win.start == 4U);
    CHECK(f.win.trigger_pos == 4U);
    CHECK(dst[0] == 104U && dst[7] == 111U);
    CHECK(f.stats.min == 104U);
    CHECK(f.stats.max == 111U);
    CHECK(f.stats.vpp == 7U);
    CHECK(f.stats.mean == 108U);        /* (104+111)/2 = 107.5, rounds up */
    CHECK(f.stats.n == 8U);
}

static void test_wrap(void)
{
    uint16_t ring[16];
    uint16_t dst[16];
    struct frame f;

    fill_ring(ring, 16U, 100U);
    /* trigger at 2, 4 before -> start 14: 114, 115, 100..105 */
    CHECK(frame_build(ring, 16U, 2U, 8U, 4U, dst, 16U, &f) == 0U);
    CHECK(f.win.start == 14U);
    CHECK(dst[0] == 114U);
    CHECK(dst[1] == 115U);
    CHECK(dst[2] == 100U);
    CHECK(dst[7] == 105U);
    CHECK(f.stats.min == 100U);
    CHECK(f.stats.max == 115U);
}

static void test_free_running_counter(void)
{
    uint16_t ring[16];
    uint16_t dst_a[16];
    uint16_t dst_b[16];
    struct frame a, b;

    fill_ring(ring, 16U, 100U);
    /* The DMA counter keeps counting past the buffer length; a trigger one
     * or four whole laps later must land on exactly the same window. */
    CHECK(frame_build(ring, 16U, 8U, 8U, 4U, dst_a, 16U, &a) == 0U);
    CHECK(frame_build(ring, 16U, 8U + 16U, 8U, 4U, dst_b, 16U, &b) == 0U);
    CHECK(a.win.start == b.win.start);
    CHECK(a.stats.mean == b.stats.mean);
    CHECK(frame_build(ring, 16U, 8U + 4U * 16U, 8U, 4U, dst_b, 16U, &b) == 0U);
    CHECK(a.win.start == b.win.start);
    CHECK(a.stats.rms == b.stats.rms);
}

static void test_tail_frame(void)
{
    uint16_t ring[16];
    uint16_t dst[16];
    struct frame f;

    fill_ring(ring, 16U, 100U);
    /* No trigger: newest window ends at write_index 5, so it is
     * 113,114,115,100,101,102,103,104. */
    CHECK(frame_build_tail(ring, 16U, 5U, 8U, dst, 16U, &f) == 0U);
    CHECK(f.length == 8U);
    CHECK(f.win.start == 13U);
    CHECK(f.win.trigger_pos == 0U);
    CHECK(dst[0] == 113U && dst[2] == 115U && dst[3] == 100U && dst[7] == 104U);
    CHECK(f.stats.min == 100U && f.stats.max == 115U);
    CHECK(f.stats.n == 8U);

    /* Truncation and parameter errors follow frame_build's contract. */
    CHECK(frame_build_tail(ring, 16U, 5U, 8U, dst, 2U, &f) == 2U);
    CHECK(f.length == 2U && dst[0] == 113U);
    CHECK(frame_build_tail(NULL, 16U, 5U, 8U, dst, 16U, &f) == 1U);
    CHECK(frame_build_tail(ring, 16U, 5U, 8U, dst, 16U, NULL) == 1U);
    CHECK(frame_build_tail(ring, 16U, 5U, 17U, dst, 16U, &f) == 1U);
}

static void test_truncation(void)
{    uint16_t ring[16];
    uint16_t dst[2];
    struct frame f;

    fill_ring(ring, 16U, 100U);
    /* Valid window, but only two samples fit: reported, not hidden. */
    CHECK(frame_build(ring, 16U, 8U, 8U, 4U, dst, 2U, &f) == 2U);
    CHECK(f.length == 2U);
    CHECK(dst[0] == 104U && dst[1] == 105U);
    CHECK(f.stats.n == 2U);
    CHECK(f.stats.min == 104U && f.stats.max == 105U);
}

static void test_rejections(void)
{
    uint16_t ring[16];
    uint16_t dst[16];
    struct frame f;

    fill_ring(ring, 16U, 100U);

    f.length = 99U;
    f.stats.n = 99U;
    CHECK(frame_build(NULL, 16U, 8U, 8U, 4U, dst, 16U, &f) == 1U);
    CHECK(f.length == 0U && f.stats.n == 0U);   /* cleared, not stale */

    CHECK(frame_build(ring, 16U, 8U, 8U, 4U, NULL, 16U, &f) == 1U);
    CHECK(frame_build(ring, 16U, 8U, 8U, 4U, dst, 0U, &f) == 1U);
    CHECK(frame_build(ring, 16U, 8U, 8U, 4U, dst, 16U, NULL) == 1U);

    /* Window larger than the ring, zero window, and pre >= window are all
     * acq_window's rejection paths and must propagate unchanged. */
    CHECK(frame_build(ring, 16U, 8U, 17U, 4U, dst, 16U, &f) == 1U);
    CHECK(frame_build(ring, 16U, 8U, 0U, 0U, dst, 16U, &f) == 1U);
    CHECK(frame_build(ring, 16U, 8U, 8U, 8U, dst, 16U, &f) == 1U);
    CHECK(frame_build(ring, 0U, 8U, 8U, 4U, dst, 16U, &f) == 1U);
}

int main(void)
{
    test_straight();
    test_wrap();
    test_free_running_counter();
    test_tail_frame();
    test_truncation();
    test_rejections();

    if (failures == 0) {
        printf("frame: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
