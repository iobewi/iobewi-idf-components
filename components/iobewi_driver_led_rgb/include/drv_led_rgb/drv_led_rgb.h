#pragma once

#include "esp_err.h"
#include "drv_led_rgb/drv_led_rgb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle for LED RGB driver
 */
typedef struct drv_led_rgb drv_led_rgb_t;

/**
 * @brief Create a new LED RGB driver instance
 *
 * @param[in] cfg Configuration structure
 * @param[out] out Pointer to store the created handle
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if cfg or out is NULL
 *   - ESP_ERR_NO_MEM if allocation fails
 */
esp_err_t drv_led_rgb_new(const drv_led_rgb_config_t *cfg, drv_led_rgb_t **out);

/**
 * @brief Set color of a specific LED in the strip
 *
 * @param[in] handle Driver handle
 * @param[in] led_idx LED index (0 to max_leds-1)
 * @param[in] color RGB color to set
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if handle or color is NULL, or led_idx out of range
 */
esp_err_t drv_led_rgb_set_color(drv_led_rgb_t *handle, uint8_t led_idx,
                                  const drv_led_rgb_color_t *color);

/**
 * @brief Set brightness for all LEDs
 *
 * Note: This sets the brightness level that will be applied to future set_color() calls.
 * RGB values are scaled by brightness before being sent to hardware.
 *
 * @param[in] handle Driver handle
 * @param[in] brightness Brightness level (0-255, 0 = off, 255 = maximum)
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if handle is NULL
 */
esp_err_t drv_led_rgb_set_brightness(drv_led_rgb_t *handle, uint8_t brightness);

/**
 * @brief Clear all LEDs (turn off)
 *
 * @param[in] handle Driver handle
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if handle is NULL
 */
esp_err_t drv_led_rgb_clear(drv_led_rgb_t *handle);

/**
 * @brief Refresh LED strip (apply pending changes)
 *
 * Note: Changes made by drv_led_rgb_set_color() are buffered and only
 * applied to hardware when this function is called.
 *
 * @param[in] handle Driver handle
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if handle is NULL
 */
esp_err_t drv_led_rgb_refresh(drv_led_rgb_t *handle);

/**
 * @brief Delete LED RGB driver instance
 *
 * @param[in] handle Driver handle to delete
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if handle is NULL
 */
esp_err_t drv_led_rgb_del(drv_led_rgb_t *handle);

#ifdef __cplusplus
}
#endif
