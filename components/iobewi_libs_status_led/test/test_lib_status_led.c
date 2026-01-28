#include "unity.h"
#include "lib_status_led/lib_status_led.h"

TEST_CASE("lib_status_led_new rejects NULL config", "[lib_status_led]")
{
    lib_status_led_t *handle = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, lib_status_led_new(NULL, &handle));
}

TEST_CASE("lib_status_led_new rejects NULL out", "[lib_status_led]")
{
    lib_status_led_config_t config = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, lib_status_led_new(&config, NULL));
}

TEST_CASE("lib_status_led_del rejects NULL handle", "[lib_status_led]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, lib_status_led_del(NULL));
}

TEST_CASE("lib_status_led lifecycle OK", "[lib_status_led]")
{
    lib_status_led_config_t config = {
        .gpio = 25,
        .brightness = 255
    };
    lib_status_led_t *handle = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, lib_status_led_new(&config, &handle));
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(ESP_OK, lib_status_led_del(handle));
}
