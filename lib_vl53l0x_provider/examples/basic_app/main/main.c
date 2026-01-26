/**
 * @file main.c
 * @brief Exemple d'utilisation du provider VL53L0X multi-capteurs
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "lib_vl53l0x_provider/lib_vl53l0x_provider.h"

static const char *TAG = "APP";

// Configuration hardware (adapter selon votre câblage)
#define SENSOR_COUNT 4

void app_main(void)
{
    ESP_LOGI(TAG, "=== lib_vl53l0x_provider basic_app ===");

    // Configuration bus I2C
    lib_vl53l0x_bus_config_t bus_config = {
        .sda_gpio = GPIO_NUM_21,
        .scl_gpio = GPIO_NUM_22,
        .i2c_freq_hz = 400000,              // 400 kHz
        .timing_budget_us = 30000,          // 30 ms
        .gpio_ready_timeout_ms = 1000,      // 1 second
    };

    // Configuration capteurs
    // NOTE : Adapter les GPIO selon votre montage hardware
    lib_vl53l0x_hw_config_t hw_configs[SENSOR_COUNT] = {
        {
            .xshut_gpio = GPIO_NUM_25,
            .int_gpio = GPIO_NUM_26,
            .addr_7b = 0x30,
            .bin_idx = 0,  // 0° (avant)
        },
        {
            .xshut_gpio = GPIO_NUM_27,
            .int_gpio = GPIO_NUM_14,
            .addr_7b = 0x31,
            .bin_idx = 9,  // 90° (droite)
        },
        {
            .xshut_gpio = GPIO_NUM_12,
            .int_gpio = GPIO_NUM_13,
            .addr_7b = 0x32,
            .bin_idx = 18,  // 180° (arrière)
        },
        {
            .xshut_gpio = GPIO_NUM_15,
            .int_gpio = GPIO_NUM_2,
            .addr_7b = 0x33,
            .bin_idx = 27,  // 270° (gauche)
        },
    };

    ESP_LOGI(TAG, "Configuration:");
    ESP_LOGI(TAG, "  I2C: SDA=%d, SCL=%d, %lu Hz",
             bus_config.sda_gpio, bus_config.scl_gpio, bus_config.i2c_freq_hz);
    ESP_LOGI(TAG, "  Sensor count: %d", SENSOR_COUNT);

    // Configuration provider
    lib_vl53l0x_provider_config_t config;
    ESP_ERROR_CHECK(lib_vl53l0x_provider_config_init(&config));
    config.bus_config = &bus_config;
    config.hw_configs = hw_configs;
    config.sensor_count = SENSOR_COUNT;

    // Créer provider
    ESP_LOGI(TAG, "Creating provider...");
    lib_vl53l0x_provider_t *provider = NULL;
    esp_err_t ret = lib_vl53l0x_provider_new(&config, &provider);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create provider: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Provider created successfully");

    // Boucle de lecture
    lib_vl53l0x_sample_t samples[SENSOR_COUNT];

    while (1) {
        // Lire snapshot atomique
        ret = lib_vl53l0x_provider_read_snapshot(provider, samples);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read snapshot: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // Afficher résultats
        ESP_LOGI(TAG, "--- Snapshot ---");
        for (int i = 0; i < SENSOR_COUNT; i++) {
            if (samples[i].valid) {
                ESP_LOGI(TAG, "  Sensor[%d]: %.2f m (status=%d)",
                         i, samples[i].range_m, samples[i].status);
            } else {
                ESP_LOGI(TAG, "  Sensor[%d]: INVALID (status=%d)",
                         i, samples[i].status);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(200));  // 5 Hz
    }

    // Nettoyage (jamais atteint ici)
    lib_vl53l0x_provider_del(provider);

    ESP_LOGI(TAG, "Application finished");
}
