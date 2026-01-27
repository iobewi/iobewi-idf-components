#pragma once

#include "drv_fan_pwm/drv_fan_pwm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise une config avec valeurs par défaut sûres
 */
esp_err_t drv_fan_pwm_config_init(drv_fan_pwm_config_t *config);

/**
 * @brief Crée une nouvelle instance du driver PWM
 */
esp_err_t drv_fan_pwm_new(const drv_fan_pwm_config_t *config, drv_fan_pwm_t **out);

/**
 * @brief Active/désactive un canal (duty conservé)
 */
esp_err_t drv_fan_pwm_enable(drv_fan_pwm_t *handle, size_t index, bool enable);

/**
 * @brief Définit le duty en pourcentage (0.0 à 100.0)
 */
esp_err_t drv_fan_pwm_set_duty_pct(drv_fan_pwm_t *handle, size_t index, float duty_pct);

/**
 * @brief Lit l'état d'un canal (debug/diag)
 */
esp_err_t drv_fan_pwm_get_state(drv_fan_pwm_t *handle, size_t index, drv_fan_pwm_channel_state_t *out);

/**
 * @brief Détruit l'instance et libère ressources
 */
esp_err_t drv_fan_pwm_del(drv_fan_pwm_t *handle);

#ifdef __cplusplus
}
#endif
