#include "link.h"

#include <stdio.h>

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

int main(void)
{
    static const char *check = "123456789";
    uint8_t buf[64];
    uint16_t s[3] = { 0x1234U, 0x00FFU, 0xABCDU };
    uint32_t n;
    uint16_t crc;

    CHECK(link_crc16((const uint8_t *)check, 9U) == 0x29B1U);
    CHECK(link_crc16(NULL, 5U) == 0U);

    n = link_encode_frame(buf, sizeof buf, 7U, s, 3U);
    CHECK(n == 14U);                       /* 8 + 2*3 */
    CHECK(buf[0] == 0xA5U && buf[1] == 0x5AU);
    CHECK(buf[2] == 0x01U && buf[3] == 0x07U);
    CHECK(buf[4] == 0x03U && buf[5] == 0x00U);
    CHECK(buf[6] == 0x34U && buf[7] == 0x12U);
    CHECK(buf[8] == 0xFFU && buf[9] == 0x00U);
    CHECK(buf[10] == 0xCDU && buf[11] == 0xABU);
    crc = link_crc16(&buf[2], 10U);
    CHECK(buf[12] == (uint8_t)(crc & 0xFFU));
    CHECK(buf[13] == (uint8_t)(crc >> 8));

    /* exact fit works, one byte short is refused */
    CHECK(link_encode_frame(buf, 14U, 0U, s, 3U) == 14U);
    CHECK(link_encode_frame(buf, 13U, 0U, s, 3U) == 0U);
    /* rejections */
    CHECK(link_encode_frame(NULL, 64U, 0U, s, 3U) == 0U);
    CHECK(link_encode_frame(buf, 64U, 0U, NULL, 3U) == 0U);
    CHECK(link_encode_frame(buf, 64U, 0U, s, 0U) == 0U);

    if (failures == 0) {
        printf("link: all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
