#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "esp_err.h"
#include "driver/ledc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct drv_fan_pwm_s drv_fan_pwm_t;

/**
 * @brief Configuration d'un canal PWM ventilateur
 */
typedef struct {
    int gpio_num;                 /*!< GPIO output PWM */
    ledc_channel_t channel;       /*!< LEDC channel */
    uint32_t hpoint;              /*!< généralement 0 */
} drv_fan_pwm_channel_cfg_t;

/**
 * @brief Configuration globale du driver PWM
 */
typedef struct {
    ledc_mode_t speed_mode;               /*!< LEDC_LOW_SPEED_MODE (souvent) */
    ledc_timer_t timer;                   /*!< LEDC_TIMER_0.. */
    ledc_timer_bit_t duty_resolution;     /*!< ex: LEDC_TIMER_10_BIT */
    uint32_t freq_hz;                     /*!< ex: 25000 */
    ledc_clk_cfg_t clk_cfg;               /*!< LEDC_AUTO_CLK */

    const drv_fan_pwm_channel_cfg_t *channels;
    size_t channel_count;
} drv_fan_pwm_config_t;

/**
 * @brief Etat simple par canal (debug/diag)
 */
typedef struct {
    uint32_t duty_raw;    /*!< duty en ticks (0..max) */
    bool enabled;
} drv_fan_pwm_channel_state_t;

#ifdef __cplusplus
}
#endif
