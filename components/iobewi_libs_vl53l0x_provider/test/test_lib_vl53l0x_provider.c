#include "unity.h"
#include "lib_vl53l0x_provider/lib_vl53l0x_provider.h"

TEST_CASE("lib_vl53l0x_provider_new rejects NULL config", "[lib_vl53l0x_provider]")
{
    lib_vl53l0x_provider_t *handle = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, lib_vl53l0x_provider_new(NULL, &handle));
}

TEST_CASE("lib_vl53l0x_provider_new rejects NULL out", "[lib_vl53l0x_provider]")
{
    lib_vl53l0x_provider_config_t config = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, lib_vl53l0x_provider_new(&config, NULL));
}

TEST_CASE("lib_vl53l0x_provider_del rejects NULL handle", "[lib_vl53l0x_provider]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, lib_vl53l0x_provider_del(NULL));
}

TEST_CASE("lib_vl53l0x_provider lifecycle OK", "[lib_vl53l0x_provider]")
{
    lib_vl53l0x_bus_config_t bus_config = {
        .sda_gpio = GPIO_NUM_21,
        .scl_gpio = GPIO_NUM_22,
        .i2c_freq_hz = 400000,
        .timing_budget_us = 30000,
        .gpio_ready_timeout_ms = 1000,
    };
    lib_vl53l0x_hw_config_t hw_configs[1] = {
        {
            .xshut_gpio = GPIO_NUM_25,
            .int_gpio = GPIO_NUM_26,
            .addr_7b = 0x30,
            .bin_idx = 0,
        },
    };
    lib_vl53l0x_provider_config_t config;
    lib_vl53l0x_provider_t *handle = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, lib_vl53l0x_provider_config_init(&config));
    config.bus_config = &bus_config;
    config.hw_configs = hw_configs;
    config.sensor_count = 1;

    TEST_ASSERT_EQUAL(ESP_OK, lib_vl53l0x_provider_new(&config, &handle));
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(ESP_OK, lib_vl53l0x_provider_del(handle));
}

TEST_CASE("lib_vl53l0x_provider repeated lifecycle OK", "[lib_vl53l0x_provider]")
{
    lib_vl53l0x_bus_config_t bus_config = {
        .sda_gpio = GPIO_NUM_21,
        .scl_gpio = GPIO_NUM_22,
        .i2c_freq_hz = 400000,
        .timing_budget_us = 30000,
        .gpio_ready_timeout_ms = 1000,
    };
    lib_vl53l0x_hw_config_t hw_configs[1] = {
        {
            .xshut_gpio = GPIO_NUM_25,
            .int_gpio = GPIO_NUM_26,
            .addr_7b = 0x30,
            .bin_idx = 0,
        },
    };
    lib_vl53l0x_provider_config_t config;
    lib_vl53l0x_provider_t *h1 = NULL;
    lib_vl53l0x_provider_t *h2 = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, lib_vl53l0x_provider_config_init(&config));
    config.bus_config = &bus_config;
    config.hw_configs = hw_configs;
    config.sensor_count = 1;

    TEST_ASSERT_EQUAL(ESP_OK, lib_vl53l0x_provider_new(&config, &h1));
    TEST_ASSERT_NOT_NULL(h1);
    TEST_ASSERT_EQUAL(ESP_OK, lib_vl53l0x_provider_del(h1));

    TEST_ASSERT_EQUAL(ESP_OK, lib_vl53l0x_provider_new(&config, &h2));
    TEST_ASSERT_NOT_NULL(h2);
    TEST_ASSERT_EQUAL(ESP_OK, lib_vl53l0x_provider_del(h2));
}
