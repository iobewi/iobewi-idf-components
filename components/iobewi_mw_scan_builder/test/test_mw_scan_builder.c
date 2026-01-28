#include "unity.h"
#include "mw_scan_builder/mw_scan_builder.h"

static mw_scan_builder_config_t test_config(void)
{
    mw_scan_builder_config_t config = {
        .angle_min = 0.0f,
        .angle_inc = 0.1f,
        .bins = 8,
        .range_min = 0.02f,
        .range_max = 2.0f,
        .scan_time = 0.1f,
        .time_increment = 0.0125f,
        .frame_id = "base_link",
    };
    return config;
}

TEST_CASE("mw_scan_builder_new rejects NULL config", "[mw_scan_builder]")
{
    mw_scan_builder_t *handle = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mw_scan_builder_new(NULL, &handle));
}

TEST_CASE("mw_scan_builder_new rejects NULL out", "[mw_scan_builder]")
{
    mw_scan_builder_config_t config = test_config();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mw_scan_builder_new(&config, NULL));
}

TEST_CASE("mw_scan_builder_del rejects NULL handle", "[mw_scan_builder]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mw_scan_builder_del(NULL));
}

TEST_CASE("mw_scan_builder lifecycle OK", "[mw_scan_builder]")
{
    mw_scan_builder_config_t config = test_config();
    mw_scan_builder_t *handle = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, mw_scan_builder_new(&config, &handle));
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(ESP_OK, mw_scan_builder_del(handle));
}
