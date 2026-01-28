#include "unity.h"
#include "app_scan_tof/app_scan_tof.h"

TEST_CASE("app_scan_tof_new rejects NULL config", "[app_scan_tof]")
{
    app_scan_tof_t *handle = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, app_scan_tof_new(NULL, &handle));
}

TEST_CASE("app_scan_tof_new rejects NULL out", "[app_scan_tof]")
{
    app_scan_tof_config_t config = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, app_scan_tof_new(&config, NULL));
}

TEST_CASE("app_scan_tof_del rejects NULL handle", "[app_scan_tof]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, app_scan_tof_del(NULL));
}

TEST_CASE("app_scan_tof lifecycle OK", "[app_scan_tof]")
{
    app_scan_tof_config_t config;
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

    TEST_ASSERT_EQUAL(ESP_OK, app_scan_tof_config_init(&config));
    config.provider_config.bus_config = &bus_config;
    config.provider_config.hw_configs = hw_configs;
    config.provider_config.sensor_count = 1;
    config.scan_config = (mw_scan_builder_config_t) {
        .angle_min = 0.0f,
        .angle_inc = 0.1f,
        .bins = 1,
        .range_min = 0.02f,
        .range_max = 2.0f,
        .scan_time = 0.1f,
        .time_increment = 0.1f,
        .frame_id = "base_link",
    };

    app_scan_tof_t *handle = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, app_scan_tof_new(&config, &handle));
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(ESP_OK, app_scan_tof_del(handle));
}
