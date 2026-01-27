#pragma once

#include "drv_ntc_adc/drv_ntc_adc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise une configuration par défaut
 */
esp_err_t drv_ntc_adc_config_init(drv_ntc_adc_config_t *config);

/**
 * @brief Crée une instance du driver ADC
 */
esp_err_t drv_ntc_adc_new(
    const drv_ntc_adc_config_t *config,
    drv_ntc_adc_t **out
);

/**
 * @brief Lit un échantillon ADC brut
 *
 * @param handle  Handle du driver
 * @param index   Index du canal (0..channel_count-1)
 * @param out     Échantillon brut
 */
esp_err_t drv_ntc_adc_read(
    drv_ntc_adc_t *handle,
    size_t index,
    drv_ntc_adc_sample_t *out
);

/**
 * @brief Détruit le driver
 */
esp_err_t drv_ntc_adc_del(drv_ntc_adc_t *handle);

#ifdef __cplusplus
}
#endif