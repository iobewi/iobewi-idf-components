#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "lib_status_led/lib_status_led_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration du middleware status LED
 */
typedef struct {
    gpio_num_t gpio;               /**< GPIO de la LED RGB */
    uint8_t brightness;            /**< Luminosité (0-255, 0=max) */
} lib_status_led_config_t;

/**
 * @brief Créer une instance du middleware status LED
 *
 * @param[in] cfg Configuration
 * @param[out] out Pointeur pour stocker le handle créé
 * @return
 *   - ESP_OK si succès
 *   - ESP_ERR_INVALID_ARG si cfg ou out est NULL
 *   - ESP_ERR_NO_MEM si allocation échoue
 */
esp_err_t lib_status_led_new(const lib_status_led_config_t *cfg, lib_status_led_t **out);

/**
 * @brief Changer l'état applicatif de la LED
 *
 * Mapping état → couleur:
 * - MW_STATUS_LED_OFF       → LED éteinte
 * - MW_STATUS_LED_WAITING   → Bleu (0, 0, 255)
 * - MW_STATUS_LED_CONNECTED → Vert (0, 255, 0)
 * - MW_STATUS_LED_ERROR     → Rouge (255, 0, 0)
 *
 * @param[in] handle Handle du middleware
 * @param[in] state Nouvel état applicatif
 * @return
 *   - ESP_OK si succès
 *   - ESP_ERR_INVALID_ARG si handle est NULL ou state invalide
 */
esp_err_t lib_status_led_set_state(lib_status_led_t *handle, lib_status_led_state_t state);

/**
 * @brief Supprimer l'instance du middleware status LED
 *
 * @param[in] handle Handle à supprimer
 * @return
 *   - ESP_OK si succès
 *   - ESP_ERR_INVALID_ARG si handle est NULL
 */
esp_err_t lib_status_led_del(lib_status_led_t *handle);

#ifdef __cplusplus
}
#endif
