#include "lib_status_led/lib_status_led.h"

#include <string.h>
#include "esp_log.h"
#include "drv_led_rgb/drv_led_rgb.h"

static const char *TAG = "lib_status_led";

/**
 * @brief Context interne du middleware
 */
struct lib_status_led {
    drv_led_rgb_t *driver;
    lib_status_led_config_t config;
    lib_status_led_state_t current_state;
};

/**
 * @brief Mapping état applicatif → couleur RGB
 */
static const drv_led_rgb_color_t state_colors[] = {
    [LIB_STATUS_LED_OFF]       = {.r = 0,   .g = 0,   .b = 0},    // Éteint
    [LIB_STATUS_LED_WAITING]   = {.r = 0,   .g = 0,   .b = 255},  // Bleu
    [LIB_STATUS_LED_CONNECTED] = {.r = 0,   .g = 255, .b = 0},    // Vert
    [LIB_STATUS_LED_ERROR]     = {.r = 255, .g = 0,   .b = 0},    // Rouge
};

static const char *state_names[] = {
    [LIB_STATUS_LED_OFF]       = "OFF",
    [LIB_STATUS_LED_WAITING]   = "WAITING",
    [LIB_STATUS_LED_CONNECTED] = "CONNECTED",
    [LIB_STATUS_LED_ERROR]     = "ERROR",
};

esp_err_t lib_status_led_new(const lib_status_led_config_t *cfg, lib_status_led_t **out)
{
    if (cfg == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Allocation du contexte middleware
    lib_status_led_t *ctx = (lib_status_led_t *)calloc(1, sizeof(lib_status_led_t));
    if (ctx == NULL) {
        ESP_LOGE(TAG, "Failed to allocate middleware context");
        return ESP_ERR_NO_MEM;
    }

    // Sauvegarde config
    memcpy(&ctx->config, cfg, sizeof(lib_status_led_config_t));
    ctx->current_state = LIB_STATUS_LED_OFF;

    // Configuration du driver LED RGB sous-jacent
    drv_led_rgb_config_t drv_cfg = {
        .gpio = cfg->gpio,
        .max_leds = 1,  // Une seule LED pour status
        .led_type = DRV_LED_RGB_TYPE_WS2812,
        .default_brightness = cfg->brightness,
    };

    esp_err_t ret = drv_led_rgb_new(&drv_cfg, &ctx->driver);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED driver: %s", esp_err_to_name(ret));
        free(ctx);
        return ret;
    }

    // Init état OFF
    drv_led_rgb_clear(ctx->driver);
    drv_led_rgb_refresh(ctx->driver);

    *out = ctx;
    ESP_LOGI(TAG, "Status LED middleware created (GPIO=%d, brightness=%d)",
             cfg->gpio, cfg->brightness);
    return ESP_OK;
}

esp_err_t lib_status_led_set_state(lib_status_led_t *handle, lib_status_led_state_t state)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (state < LIB_STATUS_LED_OFF || state > LIB_STATUS_LED_ERROR) {
        ESP_LOGE(TAG, "Invalid state: %d", state);
        return ESP_ERR_INVALID_ARG;
    }

    // Pas de changement si déjà dans cet état
    if (state == handle->current_state) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "State change: %s -> %s",
             state_names[handle->current_state],
             state_names[state]);

    // Mapping état → couleur
    const drv_led_rgb_color_t *color = &state_colors[state];

    esp_err_t ret;
    if (state == LIB_STATUS_LED_OFF) {
        // État OFF: éteindre la LED
        ret = drv_led_rgb_clear(handle->driver);
    } else {
        // Autres états: appliquer la couleur correspondante
        ret = drv_led_rgb_set_color(handle->driver, 0, color);
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set color: %s", esp_err_to_name(ret));
        return ret;
    }

    // Appliquer au matériel
    ret = drv_led_rgb_refresh(handle->driver);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to refresh LED: %s", esp_err_to_name(ret));
        return ret;
    }

    handle->current_state = state;
    return ESP_OK;
}

esp_err_t lib_status_led_del(lib_status_led_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Éteindre avant destruction
    drv_led_rgb_clear(handle->driver);
    drv_led_rgb_refresh(handle->driver);

    // Supprimer le driver
    drv_led_rgb_del(handle->driver);

    free(handle);
    ESP_LOGI(TAG, "Status LED middleware deleted");
    return ESP_OK;
}
