#include <stdio.h>
#include "esp_log.h"
#include "app_scan_tof/app_scan_tof.h"

static const char *TAG = "basic_app";

#define SENSOR_COUNT 2

void app_main(void)
{
    ESP_LOGI(TAG, "Démarrage basic_app app_scan_tof");

    app_scan_tof_config_t config;
    esp_err_t err = app_scan_tof_config_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Échec init config: %d", err);
        return;
    }

    lib_vl53l0x_bus_config_t bus_config = {
        .sda_gpio = GPIO_NUM_21,
        .scl_gpio = GPIO_NUM_22,
        .i2c_freq_hz = 400000,
        .timing_budget_us = 30000,
        .gpio_ready_timeout_ms = 1000,
    };

    lib_vl53l0x_hw_config_t hw_configs[SENSOR_COUNT] = {
        {
            .xshut_gpio = GPIO_NUM_25,
            .int_gpio = GPIO_NUM_26,
            .addr_7b = 0x30,
            .bin_idx = 0,
        },
        {
            .xshut_gpio = GPIO_NUM_27,
            .int_gpio = GPIO_NUM_14,
            .addr_7b = 0x31,
            .bin_idx = 4,
        },
    };

    config.provider_config.bus_config = &bus_config;
    config.provider_config.hw_configs = hw_configs;
    config.provider_config.sensor_count = SENSOR_COUNT;

    config.scan_config = (mw_scan_builder_config_t) {
        .angle_min = 0.0f,
        .angle_inc = 0.5f,
        .bins = 8,
        .range_min = 0.02f,
        .range_max = 2.0f,
        .scan_time = 0.1f,
        .time_increment = 0.0125f,
        .frame_id = "base_link",
    };

    app_scan_tof_t *scanner = NULL;
    err = app_scan_tof_new(&config, &scanner);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Échec création scanner: %d", err);
        return;
    }

    ESP_LOGI(TAG, "Scanner TOF créé avec succès");

    app_scan_tof_del(scanner);
    ESP_LOGI(TAG, "Scanner TOF détruit");
}
