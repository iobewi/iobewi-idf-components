#pragma once

#include <stdint.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief RGB color structure
 */
typedef struct {
    uint8_t r;  /**< Red component (0-255) */
    uint8_t g;  /**< Green component (0-255) */
    uint8_t b;  /**< Blue component (0-255) */
} drv_led_rgb_color_t;

/**
 * @brief LED strip type
 */
typedef enum {
    DRV_LED_RGB_TYPE_WS2812 = 0,
    DRV_LED_RGB_TYPE_SK6812,
    DRV_LED_RGB_TYPE_APA106,
} drv_led_rgb_type_t;

/**
 * @brief LED RGB driver configuration
 */
typedef struct {
    gpio_num_t gpio;                  /**< GPIO pin for LED data */
    uint8_t max_leds;                 /**< Maximum number of LEDs in strip (usually 1 for status) */
    drv_led_rgb_type_t led_type;     /**< LED strip type */
    uint8_t default_brightness;       /**< Default brightness (0-255), 0 = off, 255 = max */
} drv_led_rgb_config_t;

#ifdef __cplusplus
}
#endif
