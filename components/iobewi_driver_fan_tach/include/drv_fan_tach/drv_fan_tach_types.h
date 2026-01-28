#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/pulse_cnt.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handle opaque du driver tachymètre
 */
typedef struct drv_fan_tach_s drv_fan_tach_t;

/**
 * @brief Configuration d'un canal tachymètre
 */
typedef struct {
    int gpio_num;                     /*!< GPIO d'entrée tachy */
    int16_t counter_high_limit;       /*!< Limite haute compteur */
    int16_t counter_low_limit;        /*!< Limite basse compteur */
    bool pullup_enable;               /*!< Active pull-up interne */
} drv_fan_tach_channel_cfg_t;

/**
 * @brief Configuration globale du driver
 */
typedef struct {
    const drv_fan_tach_channel_cfg_t *channels;
    size_t channel_count;
} drv_fan_tach_config_t;

/**
 * @brief Mesure brute tachymètre
 */
typedef struct {
    int32_t pulse_count;   /*!< Nombre de pulses comptées */
    uint32_t period_ms;    /*!< Fenêtre de mesure */
    bool valid;
} drv_fan_tach_sample_t;

#ifdef __cplusplus
}
#endif
