#pragma once

#include "drv_fan_tach/drv_fan_tach_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise une configuration par défaut
 */
esp_err_t drv_fan_tach_config_init(drv_fan_tach_config_t *config);

/**
 * @brief Crée une instance du driver tachymètre
 */
esp_err_t drv_fan_tach_new(
    const drv_fan_tach_config_t *config,
    drv_fan_tach_t **out
);

/**
 * @brief Démarre le comptage sur un canal
 */
esp_err_t drv_fan_tach_start(drv_fan_tach_t *handle, size_t index);

/**
 * @brief Arrête le comptage sur un canal
 */
esp_err_t drv_fan_tach_stop(drv_fan_tach_t *handle, size_t index);

/**
 * @brief Lit le nombre de pulses sur une période donnée
 *
 * @param period_ms Fenêtre de mesure
 */
esp_err_t drv_fan_tach_read(
    drv_fan_tach_t *handle,
    size_t index,
    uint32_t period_ms,
    drv_fan_tach_sample_t *out
);

/**
 * @brief Détruit le driver
 */
esp_err_t drv_fan_tach_del(drv_fan_tach_t *handle);

#ifdef __cplusplus
}
#endif
