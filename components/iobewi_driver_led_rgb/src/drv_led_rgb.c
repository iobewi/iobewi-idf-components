#include "drv_led_rgb/drv_led_rgb.h"

#include <string.h>
#include "esp_log.h"
#include "led_strip.h"

static const char *TAG = "drv_led_rgb";

/**
 * @brief Internal driver context
 */
struct drv_led_rgb {
    led_strip_handle_t strip;
    drv_led_rgb_config_t config;
    uint8_t brightness;  // Current brightness (0-255, 0=off, 255=max)
};

/**
 * @brief Scale RGB component by brightness
 * @param value RGB component value (0-255)
 * @param brightness Brightness level (0-255)
 * @return Scaled value
 */
static uint8_t scale_brightness(uint8_t value, uint8_t brightness)
{
    uint32_t scaled = (uint32_t)value * (uint32_t)brightness;
    return (uint8_t)(scaled / 255U);
}

esp_err_t drv_led_rgb_new(const drv_led_rgb_config_t *cfg, drv_led_rgb_t **out)
{
    if (cfg == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (cfg->max_leds == 0) {
        ESP_LOGE(TAG, "max_leds must be > 0");
        return ESP_ERR_INVALID_ARG;
    }

    // Allocate driver context
    drv_led_rgb_t *ctx = (drv_led_rgb_t *)calloc(1, sizeof(drv_led_rgb_t));
    if (ctx == NULL) {
        ESP_LOGE(TAG, "Failed to allocate driver context");
        return ESP_ERR_NO_MEM;
    }

    // Store config
    memcpy(&ctx->config, cfg, sizeof(drv_led_rgb_config_t));

    // Initialize brightness (0 = off, 255 = max)
    ctx->brightness = cfg->default_brightness;

    // Configure LED strip
    led_strip_config_t strip_config = {
        .strip_gpio_num = cfg->gpio,
        .max_leds = cfg->max_leds,
        .led_model = LED_MODEL_WS2812,  // Default, can be extended
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        .clk_src = RMT_CLK_SRC_DEFAULT,
#endif
        .resolution_hz = 10 * 1000 * 1000, // 10 MHz
        .flags.with_dma = false,
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &ctx->strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(ret));
        free(ctx);
        return ret;
    }

    // Clear strip on init
    led_strip_clear(ctx->strip);

    *out = ctx;
    ESP_LOGI(TAG, "LED RGB driver created (GPIO=%d, max_leds=%d)",
             cfg->gpio, cfg->max_leds);
    return ESP_OK;
}

esp_err_t drv_led_rgb_set_color(drv_led_rgb_t *handle, uint8_t led_idx,
                                  const drv_led_rgb_color_t *color)
{
    if (handle == NULL || color == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (led_idx >= handle->config.max_leds) {
        ESP_LOGE(TAG, "LED index %d out of range (max=%d)", led_idx, handle->config.max_leds);
        return ESP_ERR_INVALID_ARG;
    }

    // Apply brightness scaling to RGB values
    uint8_t r = scale_brightness(color->r, handle->brightness);
    uint8_t g = scale_brightness(color->g, handle->brightness);
    uint8_t b = scale_brightness(color->b, handle->brightness);

    esp_err_t ret = led_strip_set_pixel(handle->strip, led_idx, r, g, b);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set LED color: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t drv_led_rgb_set_brightness(drv_led_rgb_t *handle, uint8_t brightness)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Store brightness (will be applied on next set_color call)
    handle->brightness = brightness;

    ESP_LOGI(TAG, "Brightness set to %d/255", brightness);
    return ESP_OK;
}

esp_err_t drv_led_rgb_clear(drv_led_rgb_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = led_strip_clear(handle->strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear LED strip: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t drv_led_rgb_refresh(drv_led_rgb_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = led_strip_refresh(handle->strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to refresh LED strip: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t drv_led_rgb_del(drv_led_rgb_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Clear before delete
    led_strip_clear(handle->strip);
    led_strip_del(handle->strip);

    free(handle);
    ESP_LOGI(TAG, "LED RGB driver deleted");
    return ESP_OK;
}
