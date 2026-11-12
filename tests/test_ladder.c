/* Host unit tests for the front-panel ladder decoding.
 * Build and run with:  make -C custom-firmware test
 */
#include <stdio.h>
#include <string.h>

#include "../app/ladder.h"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static void test_map(void)
{
    /* vendor thresholds: <=0x600 -> 0, <=0xA00 -> 1, >0xA00 -> 2 */
    CHECK(ladder_map(0u) == 0u);
    CHECK(ladder_map(0x600u) == 0u);
    CHECK(ladder_map(0x601u) == 1u);
    CHECK(ladder_map(0xA00u) == 1u);
    CHECK(ladder_map(0xA01u) == 2u);
    CHECK(ladder_map(0xFFFFu) == 2u);
}

static void test_combo(void)
{
    /* combo = 4 + 3*SEN1 + SEN2, nine combinations 4..12 */
    CHECK(ladder_combo(0u, 0u) == 4u);
    CHECK(ladder_combo(0u, 1u) == 5u);
    CHECK(ladder_combo(0u, 2u) == 6u);
    CHECK(ladder_combo(1u, 0u) == 7u);
    CHECK(ladder_combo(1u, 1u) == 8u);
    CHECK(ladder_combo(1u, 2u) == 9u);
    CHECK(ladder_combo(2u, 0u) == 10u);
    CHECK(ladder_combo(2u, 1u) == 11u);
    CHECK(ladder_combo(2u, 2u) == 12u);
    /* defensive: an out-of-range position must not produce a valid combo */
    CHECK(ladder_combo(3u, 0u) == 0u);
    CHECK(ladder_combo(0u, 9u) == 0u);
}

static void test_coupling(void)
{
    /* the vendor inverts the CPL position: coupling = 2 - pos */
    CHECK(ladder_coupling(0u) == 2u);
    CHECK(ladder_coupling(1u) == 1u);
    CHECK(ladder_coupling(2u) == 0u);
}

static void test_cal_index(void)
{
    CHECK(ladder_cal_index(4u) == 0);
    CHECK(ladder_cal_index(12u) == 8);
    CHECK(ladder_cal_index(3u) == -1);
    CHECK(ladder_cal_index(13u) == -1);
    /* the calibration table is indexed by combo - 4 (vendor behaviour) */
    for (int c = LADDER_COMBO_MIN; c <= LADDER_COMBO_MAX; c++) {
        CHECK(ladder_cal_index((uint8_t)c) == c - LADDER_COMBO_MIN);
    }
}

static void test_positions_and_decode(void)
{
    uint16_t raw[3];
    uint8_t  pos[3];
    uint8_t  combo = 0xAAu;
    uint8_t  coupling = 0xAAu;

    /* vendor mapping, confirmed against the live ADC1_JSQR over SWD:
     * JL=2 (three ranks), JSQ2=6, JSQ3=5, JSQ4=7.  With three conversions
     * rank n lands on JSQ(n+1), so JDR1..3 = ch6/ch5/ch7 = SEN1/SEN2/CPL. */
    CHECK(LADDER_CH_SEN1 == 6u);
    CHECK(LADDER_CH_SEN2 == 5u);
    CHECK(LADDER_CH_CPL  == 7u);

    /* below LO -> 0, between -> 1, above HI -> 2 */
    raw[0] = 0x100u; raw[1] = 0x700u; raw[2] = 0xB00u;
    ladder_positions(raw, pos);
    CHECK(pos[0] == 0u);
    CHECK(pos[1] == 1u);
    CHECK(pos[2] == 2u);

    /* combo = 4 + 3*SEN1 + SEN2, coupling = 2 - CPL */
    CHECK(ladder_decode(0x100u, 0x700u, 0xB00u, &combo, &coupling) == 1u);
    CHECK(combo == 4u + 3u * 0u + 1u);
    CHECK(coupling == 0u);

    CHECK(ladder_decode(0xB00u, 0xB00u, 0x100u, &combo, &coupling) == 1u);
    CHECK(combo == 4u + 3u * 2u + 2u);
    CHECK(coupling == 2u);

    /* a missing reading invalidates the whole decode */
    CHECK(ladder_decode(0x100u, LADDER_RAW_INVALID, 0x100u,
                        &combo, &coupling) == 0u);
    CHECK(combo == 0u);
    CHECK(coupling == 0xFFu);

    raw[1] = LADDER_RAW_INVALID;
    ladder_positions(raw, pos);
    CHECK(pos[1] == 3u);
}

int main(void)
{
    test_map();
    test_combo();
    test_coupling();
    test_cal_index();
    test_positions_and_decode();
    if (failures == 0) {
        printf("ladder: all tests passed\n");
        return 0;
    }
    printf("ladder: %d failure(s)\n", failures);
    return 1;
}
