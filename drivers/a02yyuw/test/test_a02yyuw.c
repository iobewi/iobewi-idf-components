#include "unity.h"
#include "a02yyuw_driver.h"

void test_a02yyuw_parse_frame_ok(void)
{
    uint8_t frame[4] = {0xFF, 0x01, 0x02, 0x00};
    frame[3] = (uint8_t)(frame[0] + frame[1] + frame[2]);

    uint16_t mm = 0;
    TEST_ASSERT_TRUE(a02yyuw_parse_frame(frame, &mm));
    TEST_ASSERT_EQUAL_UINT16(0x0102, mm);
}

void test_a02yyuw_parse_frame_bad_header(void)
{
    uint8_t frame[4] = {0x00, 0x01, 0x02, 0x00};
    frame[3] = (uint8_t)(frame[0] + frame[1] + frame[2]);

    uint16_t mm = 0;
    TEST_ASSERT_FALSE(a02yyuw_parse_frame(frame, &mm));
}

void test_a02yyuw_parse_frame_bad_checksum(void)
{
    uint8_t frame[4] = {0xFF, 0x01, 0x02, 0x00};
    frame[3] = (uint8_t)(frame[0] + frame[1] + frame[2] + 1);

    uint16_t mm = 0;
    TEST_ASSERT_FALSE(a02yyuw_parse_frame(frame, &mm));
}

void app_main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a02yyuw_parse_frame_ok);
    RUN_TEST(test_a02yyuw_parse_frame_bad_header);
    RUN_TEST(test_a02yyuw_parse_frame_bad_checksum);
    UNITY_END();
}
