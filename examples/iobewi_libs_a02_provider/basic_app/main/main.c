/**
 * @file main.c
 * @brief Exemple basic pour lib_a02_provider
 *
 * Cet exemple montre comment utiliser lib_a02_provider pour lire 4 capteurs
 * A02YYUW avec filtrage médian et validation de range.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "drv_a02yyuw/drv_a02yyuw.h"
#include "lib_a02_provider/lib_a02_provider.h"

static const char *TAG = "app";

// GPIO EN (STMPS2141STR) - un par capteur
static const int EN_PINS[] = {
    CONFIG_A02_PROVIDER_EN_GPIO_0,
    CONFIG_A02_PROVIDER_EN_GPIO_1,
    CONFIG_A02_PROVIDER_EN_GPIO_2,
    CONFIG_A02_PROVIDER_EN_GPIO_3,
};

#define SENSOR_COUNT (sizeof(EN_PINS) / sizeof(EN_PINS[0]))

void app_main(void)
{
    ESP_LOGI(TAG, "Starting lib_a02_provider basic example");
    ESP_LOGI(TAG, "Configuration:");
    ESP_LOGI(TAG, "  UART RX GPIO: %d", CONFIG_A02_PROVIDER_UART_RX_GPIO);
    ESP_LOGI(TAG, "  Mode GPIO: %d", CONFIG_A02_PROVIDER_MODE_GPIO);
    ESP_LOGI(TAG, "  EN GPIOs: [%d, %d, %d, %d]",
             EN_PINS[0], EN_PINS[1], EN_PINS[2], EN_PINS[3]);

    // ========================================================================
    // 1. Créer et initialiser le driver A02YYUW
    // ========================================================================

    drv_a02yyuw_config_t drv_config;
    ESP_ERROR_CHECK(drv_a02yyuw_config_init(&drv_config));

    drv_config.uart_num = UART_NUM_1;
    drv_config.uart_rx_gpio = CONFIG_A02_PROVIDER_UART_RX_GPIO;
    drv_config.uart_tx_gpio = UART_PIN_NO_CHANGE;
    drv_config.baudrate = 9600;
    drv_config.rx_buffer_size = 256;

    drv_config.gpio_mode = CONFIG_A02_PROVIDER_MODE_GPIO;
    drv_config.mode_active_high = true;

    drv_config.gpio_en_list = EN_PINS;
    drv_config.sensor_count = SENSOR_COUNT;

    drv_config.t_mode_settle_ms = 2;
    drv_config.t_power_up_ms = 20;
    drv_config.t_power_down_ms = 10;

    drv_a02yyuw_t *driver = NULL;
    esp_err_t ret = drv_a02yyuw_new(&drv_config, &driver);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create driver: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Driver A02YYUW initialized successfully");

    // ========================================================================
    // 2. Créer et initialiser le provider
    // ========================================================================

    lib_a02_provider_config_t prov_config;
    ESP_ERROR_CHECK(lib_a02_provider_config_init(&prov_config));

    prov_config.driver = driver;
    prov_config.sensor_count = SENSOR_COUNT;
    prov_config.median_filter_size = 3;
    prov_config.range_min_m = 0.3f;
    prov_config.range_max_m = 4.5f;
    prov_config.mode = DRV_A02YYUW_MODE_PROCESSED;
    prov_config.read_timeout_ms = 500;
    prov_config.discard_first_sample = true;

    lib_a02_provider_t *provider = NULL;
    ret = lib_a02_provider_new(&prov_config, &provider);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create provider: %s", esp_err_to_name(ret));
        drv_a02yyuw_del(driver);
        return;
    }

    ESP_LOGI(TAG, "Provider initialized successfully");
    ESP_LOGI(TAG, "  Median filter size: %d", prov_config.median_filter_size);
    ESP_LOGI(TAG, "  Range: [%.1f, %.1f] m", prov_config.range_min_m, prov_config.range_max_m);

    // ========================================================================
    // 3. Boucle de lecture
    // ========================================================================

    ultrasonic_sample_t samples[SENSOR_COUNT];
    uint32_t scan_count = 0;

    ESP_LOGI(TAG, "Starting scan loop...");

    while (1) {
        scan_count++;
        ESP_LOGI(TAG, "--- Scan #%lu ---", scan_count);

        // Lire tous les capteurs
        ret = lib_a02_provider_read_snapshot(provider, samples, SENSOR_COUNT);

        if (ret == ESP_OK) {
            // Afficher les résultats
            printf("\n");
            printf("╔════════════════════════════════════════╗\n");
            printf("║         Scan #%-5lu                   ║\n", scan_count);
            printf("╠════════════════════════════════════════╣\n");

            for (int i = 0; i < SENSOR_COUNT; i++) {
                if (samples[i].valid) {
                    printf("║ Sensor[%d]:  %.2f m  ✓ VALID      ║\n", i, samples[i].distance_m);
                } else {
                    printf("║ Sensor[%d]:  ------- ✗ INVALID    ║\n", i);
                }
            }

            printf("╚════════════════════════════════════════╝\n");
            printf("\n");

        } else if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "All sensors failed!");
        } else {
            ESP_LOGW(TAG, "Snapshot read failed: %s", esp_err_to_name(ret));
        }

        // Attendre avant le prochain scan
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    // ========================================================================
    // 4. Nettoyage (jamais atteint dans cet exemple)
    // ========================================================================

    lib_a02_provider_del(provider);
    drv_a02yyuw_del(driver);

    ESP_LOGI(TAG, "Example finished");
}
