#include "unity.h"
#include "drv_led_rgb/drv_led_rgb.h"

TEST_CASE("drv_led_rgb_new rejects NULL config", "[drv_led_rgb]")
{
    drv_led_rgb_t *handle = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_led_rgb_new(NULL, &handle));
}

TEST_CASE("drv_led_rgb_new rejects NULL out", "[drv_led_rgb]")
{
    drv_led_rgb_config_t config = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_led_rgb_new(&config, NULL));
}

TEST_CASE("drv_led_rgb_del rejects NULL handle", "[drv_led_rgb]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_led_rgb_del(NULL));
}

TEST_CASE("drv_led_rgb lifecycle OK", "[drv_led_rgb]")
{
    drv_led_rgb_config_t config = {
        .gpio = 25,
        .max_leds = 1,
        .led_type = DRV_LED_RGB_TYPE_WS2812,
        .default_brightness = 255
    };
    drv_led_rgb_t *handle = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, drv_led_rgb_new(&config, &handle));
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(ESP_OK, drv_led_rgb_del(handle));
}
