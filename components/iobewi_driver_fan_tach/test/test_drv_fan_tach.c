#include "unity.h"
#include "drv_fan_tach/drv_fan_tach.h"

TEST_CASE("drv_fan_tach_new rejects NULL config", "[drv_fan_tach]")
{
    drv_fan_tach_t *handle = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_fan_tach_new(NULL, &handle));
}

TEST_CASE("drv_fan_tach_new rejects NULL out", "[drv_fan_tach]")
{
    drv_fan_tach_config_t config = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_fan_tach_new(&config, NULL));
}

TEST_CASE("drv_fan_tach_del rejects NULL handle", "[drv_fan_tach]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_fan_tach_del(NULL));
}

TEST_CASE("drv_fan_tach lifecycle OK", "[drv_fan_tach]")
{
    drv_fan_tach_channel_cfg_t channels[1] = {
        {
            .gpio_num = 26,
            .counter_high_limit = 1000,
            .counter_low_limit = -1000,
            .pullup_enable = true,
        },
    };
    drv_fan_tach_config_t config;
    drv_fan_tach_t *handle = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, drv_fan_tach_config_init(&config));
    config.channels = channels;
    config.channel_count = 1;

    TEST_ASSERT_EQUAL(ESP_OK, drv_fan_tach_new(&config, &handle));
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(ESP_OK, drv_fan_tach_del(handle));
}

TEST_CASE("drv_fan_tach repeated lifecycle OK", "[drv_fan_tach]")
{
    drv_fan_tach_channel_cfg_t channels[1] = {
        {
            .gpio_num = 26,
            .counter_high_limit = 1000,
            .counter_low_limit = -1000,
            .pullup_enable = true,
        },
    };
    drv_fan_tach_config_t config;
    drv_fan_tach_t *h1 = NULL;
    drv_fan_tach_t *h2 = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, drv_fan_tach_config_init(&config));
    config.channels = channels;
    config.channel_count = 1;

    TEST_ASSERT_EQUAL(ESP_OK, drv_fan_tach_new(&config, &h1));
    TEST_ASSERT_NOT_NULL(h1);
    TEST_ASSERT_EQUAL(ESP_OK, drv_fan_tach_del(h1));

    TEST_ASSERT_EQUAL(ESP_OK, drv_fan_tach_new(&config, &h2));
    TEST_ASSERT_NOT_NULL(h2);
    TEST_ASSERT_EQUAL(ESP_OK, drv_fan_tach_del(h2));
}
