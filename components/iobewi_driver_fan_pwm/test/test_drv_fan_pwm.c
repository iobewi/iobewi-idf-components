#include "unity.h"
#include "drv_fan_pwm/drv_fan_pwm.h"

TEST_CASE("drv_fan_pwm_new rejects NULL config", "[drv_fan_pwm]")
{
    drv_fan_pwm_t *handle = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_fan_pwm_new(NULL, &handle));
}

TEST_CASE("drv_fan_pwm_new rejects NULL out", "[drv_fan_pwm]")
{
    drv_fan_pwm_config_t config = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_fan_pwm_new(&config, NULL));
}

TEST_CASE("drv_fan_pwm_del rejects NULL handle", "[drv_fan_pwm]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_fan_pwm_del(NULL));
}

TEST_CASE("drv_fan_pwm lifecycle OK", "[drv_fan_pwm]")
{
    drv_fan_pwm_channel_cfg_t channels[1] = {
        {
            .gpio_num = 25,
            .channel = LEDC_CHANNEL_0,
            .hpoint = 0,
        },
    };
    drv_fan_pwm_config_t config;
    drv_fan_pwm_t *handle = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, drv_fan_pwm_config_init(&config));
    config.channels = channels;
    config.channel_count = 1;
    config.freq_hz = 25000;

    TEST_ASSERT_EQUAL(ESP_OK, drv_fan_pwm_new(&config, &handle));
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(ESP_OK, drv_fan_pwm_del(handle));
}

TEST_CASE("drv_fan_pwm repeated lifecycle OK", "[drv_fan_pwm]")
{
    drv_fan_pwm_channel_cfg_t channels[1] = {
        {
            .gpio_num = 25,
            .channel = LEDC_CHANNEL_0,
            .hpoint = 0,
        },
    };
    drv_fan_pwm_config_t config;
    drv_fan_pwm_t *h1 = NULL;
    drv_fan_pwm_t *h2 = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, drv_fan_pwm_config_init(&config));
    config.channels = channels;
    config.channel_count = 1;
    config.freq_hz = 25000;

    TEST_ASSERT_EQUAL(ESP_OK, drv_fan_pwm_new(&config, &h1));
    TEST_ASSERT_NOT_NULL(h1);
    TEST_ASSERT_EQUAL(ESP_OK, drv_fan_pwm_del(h1));

    TEST_ASSERT_EQUAL(ESP_OK, drv_fan_pwm_new(&config, &h2));
    TEST_ASSERT_NOT_NULL(h2);
    TEST_ASSERT_EQUAL(ESP_OK, drv_fan_pwm_del(h2));
}
