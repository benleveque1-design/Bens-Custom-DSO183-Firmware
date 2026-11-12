/* Host unit tests for circular-window placement (app/acq_window.c). */
#include "acq_window.h"

#include <stdio.h>

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

#define BUF 16U

static uint16_t buf[BUF];

static void init_buf(void)
{
    uint16_t i;

    for (i = 0U; i < BUF; i++) {
        buf[i] = (uint16_t)(100U + i);       /* 100..115: identifiable      */
    }
}

static void test_centred(void)
{
    struct acq_win w = {0xFFFFu, 0xFFFFu, 0xFFFFu};
    uint16_t out[8];

    CHECK(acq_win_place(BUF, 8U, 8U, 4U, &w) == 0U);
    CHECK(w.start == 4U && w.length == 8U && w.trigger_pos == 4U);
    CHECK(acq_win_copy(buf, BUF, &w, out, 8U) == 8U);
    CHECK(out[0] == 104U && out[3] == 107U && out[4] == 108U && out[7] == 111U);
}

static void test_wrap(void)
{
    struct acq_win w;
    uint16_t out[8];

    /* Trigger at index 2 with 4 samples before it: start = (2+16-4)%16 = 14,
     * so the window runs 114,115 then 100..105 across the wrap. */
    CHECK(acq_win_place(BUF, 2U, 8U, 4U, &w) == 0U);
    CHECK(w.start == (BUF - 2U));
    CHECK(acq_win_copy(buf, BUF, &w, out, 8U) == 8U);
    CHECK(out[0] == 114U);
    CHECK(out[1] == 115U);
    CHECK(out[2] == 100U);            /* wrapped                          */
    CHECK(out[7] == 105U);
}

static void test_trigger_first_column(void)
{
    struct acq_win w;
    uint16_t out[4];

    CHECK(acq_win_place(BUF, 0U, 4U, 0U, &w) == 0U);
    CHECK(w.start == 0U && w.trigger_pos == 0U);
    CHECK(acq_win_copy(buf, BUF, &w, out, 4U) == 4U);
    CHECK(out[0] == 100U && out[3] == 103U);
}

static void test_free_running_counter(void)
{
    struct acq_win a, b;

    /* The DMA position is a u32 counter; reducing mod the buffer is what
     * makes a wrapped counter behave like a fresh one. */
    CHECK(acq_win_place(BUF, 8U, 8U, 4U, &a) == 0U);
    CHECK(acq_win_place(BUF, 8U + 4U * BUF, 8U, 4U, &b) == 0U);
    CHECK(a.start == b.start && a.length == b.length &&
          a.trigger_pos == b.trigger_pos);
}

static void test_full_window(void)
{
    struct acq_win w;
    uint16_t out[BUF];
    uint16_t i;

    CHECK(acq_win_place(BUF, 5U, BUF, 0U, &w) == 0U);
    CHECK(acq_win_copy(buf, BUF, &w, out, BUF) == BUF);
    for (i = 0U; i < BUF; i++) {
        CHECK(out[i] == buf[(5U + i) % BUF]);
    }
}

static void test_cap_truncation(void)
{
    struct acq_win w;
    uint16_t out[3] = {0U, 0U, 0U};

    CHECK(acq_win_place(BUF, 8U, 8U, 4U, &w) == 0U);
    CHECK(acq_win_copy(buf, BUF, &w, out, 3U) == 3U);
    CHECK(out[0] == 104U && out[2] == 106U);
}

static void test_tail_window(void)
{
    struct acq_win w;
    uint16_t out[8];

    /* Newest sample is write_pos-1 = 4, so the 8-sample tail is 13..15,0..4
     * (113,114,115,100..104). */
    CHECK(acq_win_place_tail(BUF, 5U, 8U, &w) == 0U);
    CHECK(w.start == 13U && w.length == 8U && w.trigger_pos == 0U);
    CHECK(acq_win_copy(buf, BUF, &w, out, 8U) == 8U);
    CHECK(out[0] == 113U && out[2] == 115U);
    CHECK(out[3] == 100U && out[7] == 104U);

    /* write_pos exactly at 0 wraps the start to buf_size - window. */
    CHECK(acq_win_place_tail(BUF, 0U, 8U, &w) == 0U);
    CHECK(w.start == 8U);

    /* Free-running counter reduces like the trigger index. */
    CHECK(acq_win_place_tail(BUF, 5U + 3U * BUF, 8U, &w) == 0U);
    CHECK(w.start == 13U);

    /* Full-buffer tail is the whole ring, starting at write_pos. */
    CHECK(acq_win_place_tail(BUF, 7U, BUF, &w) == 0U);
    CHECK(w.start == 7U && w.length == BUF);

    CHECK(acq_win_place_tail(BUF, 5U, 1U, &w) == 1U);
    CHECK(acq_win_place_tail(BUF, 5U, BUF + 1U, &w) == 1U);
    CHECK(acq_win_place_tail(0U, 5U, 8U, &w) == 1U);
    CHECK(acq_win_place_tail(BUF, 5U, 8U, NULL) == 1U);
}

static void test_ring_advance(void)
{
    CHECK(acq_ring_advance(3U, 7U, BUF) == 4U);
    CHECK(acq_ring_advance(14U, 2U, BUF) == 4U);   /* wraps forward */
    CHECK(acq_ring_advance(5U, 5U, BUF) == 0U);
    CHECK(acq_ring_advance(15U, 0U, BUF) == 1U);

    /* Bad arguments are a no-op, not a huge distance. */
    CHECK(acq_ring_advance(0U, 0U, 0U) == 0U);
    CHECK(acq_ring_advance(BUF, 1U, BUF) == 0U);
    CHECK(acq_ring_advance(1U, BUF, BUF) == 0U);
}

static void test_rejects_bad_input(void)
{
    struct acq_win w = {0x1234u, 0x5678u, 0x9ABCu};
    struct acq_win bad = {BUF, 4U, 0U};
    uint16_t out[4];

    CHECK(acq_win_place(BUF, 8U, 8U, 4U, NULL) == 1U);
    CHECK(acq_win_place(0U, 8U, 8U, 4U, &w) == 1U);
    CHECK(acq_win_place(BUF, 8U, 1U, 0U, &w) == 1U);        /* < 2 samples  */
    CHECK(acq_win_place(BUF, 8U, BUF + 1U, 0U, &w) == 1U);  /* > buffer     */
    CHECK(acq_win_place(BUF, 8U, 8U, 8U, &w) == 1U);        /* pre = window */
    CHECK(w.start == 0x1234u && w.length == 0x5678u);       /* untouched    */

    CHECK(acq_win_copy(NULL, BUF, &bad, out, 4U) == 0U);
    CHECK(acq_win_copy(buf, BUF, NULL, out, 4U) == 0U);
    CHECK(acq_win_copy(buf, BUF, &bad, NULL, 4U) == 0U);
    CHECK(acq_win_copy(buf, 0U, &bad, out, 4U) == 0U);
    CHECK(acq_win_copy(buf, BUF, &w, out, 4U) == 0U);       /* start >= size */
}

int main(void)
{
    init_buf();

    test_centred();
    test_wrap();
    test_trigger_first_column();
    test_free_running_counter();
    test_full_window();
    test_cap_truncation();
    test_tail_window();
    test_ring_advance();
    test_rejects_bad_input();

    if (failures == 0) {
        printf("acq_window: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
