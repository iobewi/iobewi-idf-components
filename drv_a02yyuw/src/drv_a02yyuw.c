/**
 * @file drv_a02yyuw.c
 * @brief Implémentation du driver A02YYUW
 */

#include "drv_a02yyuw/drv_a02yyuw.h"

#include <string.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "drv_a02yyuw";

#define A02_HEADER    0xFF
#define A02_FRAME_LEN 4

/**
 * @brief Structure privée du driver A02YYUW
 */
struct drv_a02yyuw_s {
    // Configuration UART
    uart_port_t uart_num;
    int baudrate;
    int rx_buffer_size;

    // Configuration GPIO
    int gpio_mode;
    bool mode_active_high;
    int *gpio_en_list;
    int sensor_count;

    // Timings
    uint32_t t_mode_settle_ms;
    uint32_t t_power_up_ms;
    uint32_t t_power_down_ms;
};

/**
 * @brief Parser une trame A02YYUW et vérifier le checksum
 *
 * @param[in] frame Trame de 4 octets
 * @param[out] distance_mm Distance extraite en millimètres
 * @return true si la trame est valide, false sinon
 */
static bool parse_frame(const uint8_t frame[A02_FRAME_LEN], uint16_t *distance_mm)
{
    if (frame[0] != A02_HEADER) {
        return false;
    }

    uint8_t checksum = (uint8_t)(frame[0] + frame[1] + frame[2]);
    if (checksum != frame[3]) {
        return false;
    }

    *distance_mm = (uint16_t)(((uint16_t)frame[1] << 8) | frame[2]);
    return true;
}

/**
 * @brief Configurer un GPIO en sortie
 *
 * @param[in] gpio Numéro du GPIO
 * @return ESP_OK en cas de succès, code d'erreur sinon
 */
static esp_err_t configure_output_gpio(int gpio)
{
    if (gpio < 0) {
        return ESP_OK; // GPIO non utilisé
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&io_conf);
}

/**
 * @brief Définir le mode de fonctionnement du capteur
 *
 * @param[in] handle Handle du driver
 * @param[in] mode Mode souhaité
 */
static void set_mode(drv_a02yyuw_t *handle, drv_a02yyuw_mode_t mode)
{
    if (handle->gpio_mode < 0) {
        return; // GPIO de mode non configuré
    }

    int want_processed = (mode == DRV_A02YYUW_MODE_PROCESSED) ? 1 : 0;
    int level = handle->mode_active_high ? want_processed : !want_processed;
    gpio_set_level(handle->gpio_mode, level);
}

esp_err_t drv_a02yyuw_config_init(drv_a02yyuw_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(config, 0, sizeof(*config));

    // Valeurs par défaut
    config->uart_num = UART_NUM_1;
    config->uart_rx_gpio = -1; // Doit être configuré par l'utilisateur
    config->uart_tx_gpio = UART_PIN_NO_CHANGE;
    config->baudrate = 9600;
    config->rx_buffer_size = 256;

    config->gpio_mode = -1; // Non utilisé par défaut
    config->mode_active_high = true;

    config->gpio_en_list = NULL; // Doit être configuré par l'utilisateur
    config->sensor_count = 0;

    config->t_mode_settle_ms = 2;
    config->t_power_up_ms = 20;
    config->t_power_down_ms = 10;

    return ESP_OK;
}

esp_err_t drv_a02yyuw_new(const drv_a02yyuw_config_t *config, drv_a02yyuw_t **out_handle)
{
    if (!config || !out_handle) {
        ESP_LOGE(TAG, "Invalid arguments (config or out_handle is NULL)");
        return ESP_ERR_INVALID_ARG;
    }

    if (config->uart_rx_gpio < 0) {
        ESP_LOGE(TAG, "UART RX GPIO must be configured");
        return ESP_ERR_INVALID_ARG;
    }

    if (config->sensor_count <= 0 || !config->gpio_en_list) {
        ESP_LOGE(TAG, "Invalid sensor configuration (count=%d, en_list=%p)",
                 config->sensor_count, config->gpio_en_list);
        return ESP_ERR_INVALID_ARG;
    }

    // Allocation du handle
    drv_a02yyuw_t *handle = (drv_a02yyuw_t *)calloc(1, sizeof(drv_a02yyuw_t));
    if (!handle) {
        ESP_LOGE(TAG, "Failed to allocate driver handle");
        return ESP_ERR_NO_MEM;
    }

    // Copie de la liste des GPIO EN
    handle->gpio_en_list = (int *)calloc(config->sensor_count, sizeof(int));
    if (!handle->gpio_en_list) {
        ESP_LOGE(TAG, "Failed to allocate GPIO EN list");
        free(handle);
        return ESP_ERR_NO_MEM;
    }
    memcpy(handle->gpio_en_list, config->gpio_en_list, config->sensor_count * sizeof(int));

    // Copie de la configuration
    handle->uart_num = config->uart_num;
    handle->baudrate = config->baudrate;
    handle->rx_buffer_size = config->rx_buffer_size;
    handle->gpio_mode = config->gpio_mode;
    handle->mode_active_high = config->mode_active_high;
    handle->sensor_count = config->sensor_count;
    handle->t_mode_settle_ms = config->t_mode_settle_ms;
    handle->t_power_up_ms = config->t_power_up_ms;
    handle->t_power_down_ms = config->t_power_down_ms;

    // Configuration de l'UART
    uart_config_t uart_config = {
        .baud_rate = handle->baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(handle->uart_num, handle->rx_buffer_size, 0, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(err));
        free(handle->gpio_en_list);
        free(handle);
        return err;
    }

    err = uart_param_config(handle->uart_num, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(err));
        uart_driver_delete(handle->uart_num);
        free(handle->gpio_en_list);
        free(handle);
        return err;
    }

    err = uart_set_pin(handle->uart_num,
                       config->uart_tx_gpio,
                       config->uart_rx_gpio,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(err));
        uart_driver_delete(handle->uart_num);
        free(handle->gpio_en_list);
        free(handle);
        return err;
    }

    uart_flush_input(handle->uart_num);

    // Configuration des GPIO EN (alimentation des capteurs)
    for (int i = 0; i < handle->sensor_count; i++) {
        err = configure_output_gpio(handle->gpio_en_list[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure EN GPIO %d: %s",
                     handle->gpio_en_list[i], esp_err_to_name(err));
            uart_driver_delete(handle->uart_num);
            free(handle->gpio_en_list);
            free(handle);
            return err;
        }
        gpio_set_level(handle->gpio_en_list[i], 0); // Éteindre tous les capteurs
    }

    // Configuration du GPIO de mode
    err = configure_output_gpio(handle->gpio_mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure mode GPIO: %s", esp_err_to_name(err));
        uart_driver_delete(handle->uart_num);
        free(handle->gpio_en_list);
        free(handle);
        return err;
    }

    if (handle->gpio_mode >= 0) {
        set_mode(handle, DRV_A02YYUW_MODE_PROCESSED);
    }

    ESP_LOGI(TAG, "Driver initialized (UART=%d, RX_GPIO=%d, sensors=%d, mode_GPIO=%d)",
             handle->uart_num, config->uart_rx_gpio, handle->sensor_count, handle->gpio_mode);

    *out_handle = handle;
    return ESP_OK;
}

esp_err_t drv_a02yyuw_select(drv_a02yyuw_t *handle, int sensor_id, drv_a02yyuw_mode_t mode)
{
    if (!handle) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_ERR_INVALID_ARG;
    }

    if (sensor_id < 0 || sensor_id >= handle->sensor_count) {
        ESP_LOGE(TAG, "Invalid sensor ID %d (count=%d)", sensor_id, handle->sensor_count);
        return ESP_ERR_INVALID_ARG;
    }

    // 1. Éteindre tous les capteurs
    for (int i = 0; i < handle->sensor_count; i++) {
        gpio_set_level(handle->gpio_en_list[i], 0);
    }
    vTaskDelay(pdMS_TO_TICKS(handle->t_power_down_ms));

    // 2. Configurer le mode
    set_mode(handle, mode);
    vTaskDelay(pdMS_TO_TICKS(handle->t_mode_settle_ms));

    // 3. Allumer le capteur sélectionné
    gpio_set_level(handle->gpio_en_list[sensor_id], 1);
    vTaskDelay(pdMS_TO_TICKS(handle->t_power_up_ms));

    // 4. Vider le buffer UART
    uart_flush_input(handle->uart_num);

    ESP_LOGI(TAG, "Selected sensor %d (mode=%s)",
             sensor_id,
             (mode == DRV_A02YYUW_MODE_PROCESSED) ? "PROCESSED" : "REALTIME");

    return ESP_OK;
}

esp_err_t drv_a02yyuw_read(drv_a02yyuw_t *handle, uint16_t *distance_mm, uint32_t timeout_ms)
{
    if (!handle || !distance_mm) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    uint8_t byte = 0;
    uint8_t frame[A02_FRAME_LEN];
    int frame_idx = 0;

    while ((int32_t)(deadline - xTaskGetTickCount()) > 0) {
        int bytes_read = uart_read_bytes(handle->uart_num, &byte, 1, pdMS_TO_TICKS(20));
        if (bytes_read <= 0) {
            continue;
        }

        // Recherche du header
        if (frame_idx == 0) {
            if (byte != A02_HEADER) {
                continue;
            }
            frame[frame_idx++] = byte;
            continue;
        }

        // Remplissage de la trame
        frame[frame_idx++] = byte;

        // Trame complète
        if (frame_idx == A02_FRAME_LEN) {
            frame_idx = 0;
            uint16_t dist = 0;
            if (parse_frame(frame, &dist)) {
                *distance_mm = dist;
                return ESP_OK;
            }
            // Trame invalide, continuer à chercher
        }
    }

    ESP_LOGW(TAG, "Timeout waiting for valid frame");
    return ESP_ERR_TIMEOUT;
}

esp_err_t drv_a02yyuw_del(drv_a02yyuw_t *handle)
{
    if (!handle) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_ERR_INVALID_ARG;
    }

    // Éteindre tous les capteurs
    for (int i = 0; i < handle->sensor_count; i++) {
        gpio_set_level(handle->gpio_en_list[i], 0);
    }

    // Désinstaller le driver UART
    uart_driver_delete(handle->uart_num);

    // Libérer la mémoire
    free(handle->gpio_en_list);
    free(handle);

    ESP_LOGI(TAG, "Driver destroyed");
    return ESP_OK;
}
