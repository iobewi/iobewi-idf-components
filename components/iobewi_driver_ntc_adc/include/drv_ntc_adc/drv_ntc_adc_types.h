#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "hal/adc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handle opaque du driver NTC ADC
 */
typedef struct drv_ntc_adc_s drv_ntc_adc_t;

/**
 * @brief Configuration d’un canal ADC
 */
typedef struct {
    adc_channel_t channel;   /*!< Canal ADC ESP-IDF */
    adc_atten_t   atten;     /*!< Atténuation ADC */
} drv_ntc_adc_channel_cfg_t;

/**
 * @brief Configuration globale du driver
 */
typedef struct {
    adc_unit_t unit;                               /*!< ADC_UNIT_1 ou ADC_UNIT_2 */
    adc_bitwidth_t bitwidth;                       /*!< Résolution ADC */
    const drv_ntc_adc_channel_cfg_t *channels;     /*!< Tableau de canaux */
    size_t channel_count;                          /*!< Nombre de canaux */
} drv_ntc_adc_config_t;

/**
 * @brief Échantillon ADC brut
 */
typedef struct {
    uint32_t raw;      /*!< Valeur ADC brute */
    bool valid;        /*!< Échantillon valide */
} drv_ntc_adc_sample_t;

#ifdef __cplusplus
}
#endif
