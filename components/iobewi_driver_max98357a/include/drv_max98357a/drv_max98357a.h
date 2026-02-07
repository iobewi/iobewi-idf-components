#pragma once

#include "esp_err.h"
#include "drv_max98357a/drv_max98357a_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Crée une nouvelle instance du driver MAX98357A
 *
 * Initialise le périphérique I2S et configure les GPIO nécessaires.
 * Le mode SD_MODE est configuré en mode enabled par défaut.
 *
 * @param[in] cfg Structure de configuration
 * @param[out] out Pointeur pour stocker le handle créé
 * @return
 *   - ESP_OK en cas de succès
 *   - ESP_ERR_INVALID_ARG si cfg ou out est NULL ou configuration invalide
 *   - ESP_ERR_NO_MEM si l'allocation échoue
 *   - ESP_FAIL si l'initialisation I2S échoue
 */
esp_err_t drv_max98357a_new(const drv_max98357a_config_t *cfg, drv_max98357a_t **out);

/**
 * @brief Écrit des données audio PCM vers le MAX98357A
 *
 * Les données doivent être au format configuré (sample_rate, bits_per_sample).
 * La fonction bloque jusqu'à ce que toutes les données soient écrites ou timeout.
 *
 * @param[in] handle Handle du driver
 * @param[in] data Buffer de données audio PCM
 * @param[in] size Taille du buffer en octets
 * @param[out] bytes_written Nombre d'octets effectivement écrits (peut être NULL)
 * @param[in] timeout_ms Timeout en millisecondes (portMAX_DELAY pour infini)
 * @return
 *   - ESP_OK en cas de succès
 *   - ESP_ERR_INVALID_ARG si handle ou data est NULL
 *   - ESP_ERR_TIMEOUT si timeout
 *   - ESP_FAIL en cas d'erreur d'écriture
 */
esp_err_t drv_max98357a_write(drv_max98357a_t *handle, const void *data,
                               size_t size, size_t *bytes_written,
                               uint32_t timeout_ms);

/**
 * @brief Active le MAX98357A (sortie de shutdown)
 *
 * Met SD_MODE à HIGH pour activer l'amplificateur.
 * Nécessite que sd_mode_gpio soit configuré (pas GPIO_NUM_NC).
 *
 * @param[in] handle Handle du driver
 * @return
 *   - ESP_OK en cas de succès
 *   - ESP_ERR_INVALID_ARG si handle est NULL
 *   - ESP_ERR_NOT_SUPPORTED si sd_mode_gpio n'est pas configuré
 */
esp_err_t drv_max98357a_enable(drv_max98357a_t *handle);

/**
 * @brief Désactive le MAX98357A (mode shutdown)
 *
 * Met SD_MODE à LOW pour désactiver l'amplificateur (économie d'énergie).
 * Nécessite que sd_mode_gpio soit configuré (pas GPIO_NUM_NC).
 *
 * @param[in] handle Handle du driver
 * @return
 *   - ESP_OK en cas de succès
 *   - ESP_ERR_INVALID_ARG si handle est NULL
 *   - ESP_ERR_NOT_SUPPORTED si sd_mode_gpio n'est pas configuré
 */
esp_err_t drv_max98357a_disable(drv_max98357a_t *handle);

/**
 * @brief Obtient le handle I2S sous-jacent
 *
 * Permet un accès direct au canal I2S pour des opérations avancées.
 * À utiliser avec précaution.
 *
 * @param[in] handle Handle du driver
 * @param[out] i2s_handle Handle I2S
 * @return
 *   - ESP_OK en cas de succès
 *   - ESP_ERR_INVALID_ARG si handle ou i2s_handle est NULL
 */
esp_err_t drv_max98357a_get_i2s_handle(drv_max98357a_t *handle,
                                        i2s_chan_handle_t *i2s_handle);

/**
 * @brief Supprime l'instance du driver MAX98357A
 *
 * Désactive l'amplificateur, libère les ressources I2S et GPIO.
 *
 * @param[in] handle Handle du driver à supprimer
 * @return
 *   - ESP_OK en cas de succès
 *   - ESP_ERR_INVALID_ARG si handle est NULL
 */
esp_err_t drv_max98357a_del(drv_max98357a_t *handle);

#ifdef __cplusplus
}
#endif
