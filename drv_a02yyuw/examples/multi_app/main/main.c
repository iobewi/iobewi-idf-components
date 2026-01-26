/**
 * @file main.c
 * @brief Exemple multi-capteurs pour le driver drv_a02yyuw
 *
 * Cet exemple montre comment gérer plusieurs capteurs A02YYUW
 * sur un UART partagé avec sélection par alimentation.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "drv_a02yyuw/drv_a02yyuw.h"

static const char *TAG = "app";

// GPIO EN (STMPS2141STR) - un par capteur, adapte selon ton PCB
static const int EN_PINS[] = {14, 10, 7, 4};

// UART partagé: ESP_RX = GPIO18 ; RX capteur (mode) = GPIO5
#define A02_UART_NUM   UART_NUM_1
#define A02_UART_RX    18
#define A02_MODE_GPIO  5

/**
 * @brief Calculer la médiane de 3 valeurs uint16_t
 */
static inline uint16_t median3_u16(uint16_t a, uint16_t b, uint16_t c)
{
    if (a > b) { uint16_t t = a; a = b; b = t; }
    if (b > c) { uint16_t t = b; b = c; c = t; }
    if (a > b) { uint16_t t = a; a = b; b = t; }
    return b;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting A02YYUW multi-sensor example");

    // Configuration du driver
    drv_a02yyuw_config_t config;
    ESP_ERROR_CHECK(drv_a02yyuw_config_init(&config));

    config.uart_num = A02_UART_NUM;
    config.uart_rx_gpio = A02_UART_RX;
    config.uart_tx_gpio = UART_PIN_NO_CHANGE;
    config.baudrate = 9600;
    config.rx_buffer_size = 256;

    config.gpio_mode = A02_MODE_GPIO;
    config.mode_active_high = true; // Selon datasheet: RX float/high = mode filtré

    config.gpio_en_list = EN_PINS;
    config.sensor_count = sizeof(EN_PINS) / sizeof(EN_PINS[0]);

    config.t_mode_settle_ms = 2;
    config.t_power_up_ms = 20;
    config.t_power_down_ms = 10;

    // Création de l'instance du driver
    drv_a02yyuw_t *sensor = NULL;
    esp_err_t ret = drv_a02yyuw_new(&config, &sensor);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create driver: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Driver initialized with %d sensors", config.sensor_count);

    // Boucle de lecture : scanner tous les capteurs
    while (1) {
        for (int i = 0; i < config.sensor_count; i++) {
            // Sélectionner le capteur i en mode filtré
            ret = drv_a02yyuw_select(sensor, i, DRV_A02YYUW_MODE_PROCESSED);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to select sensor %d: %s", i, esp_err_to_name(ret));
                continue;
            }

            // Jeter la première trame après power-up
            uint16_t throwaway = 0;
            (void)drv_a02yyuw_read(sensor, &throwaway, 200);

            // Lire 3 mesures et calculer la médiane
            uint16_t a = 0, b = 0, c = 0;
            esp_err_t e1 = drv_a02yyuw_read(sensor, &a, 500);
            esp_err_t e2 = drv_a02yyuw_read(sensor, &b, 500);
            esp_err_t e3 = drv_a02yyuw_read(sensor, &c, 500);

            if (e1 == ESP_OK && e2 == ESP_OK && e3 == ESP_OK) {
                uint16_t distance = median3_u16(a, b, c);
                ESP_LOGI(TAG, "Sensor[%d] = %u mm (raw: %u/%u/%u)",
                         i, (unsigned)distance,
                         (unsigned)a, (unsigned)b, (unsigned)c);
            } else {
                ESP_LOGW(TAG, "Sensor[%d] read failed: %s %s %s",
                         i,
                         esp_err_to_name(e1),
                         esp_err_to_name(e2),
                         esp_err_to_name(e3));
            }

            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    // Nettoyage (jamais atteint dans cet exemple)
    drv_a02yyuw_del(sensor);
}
