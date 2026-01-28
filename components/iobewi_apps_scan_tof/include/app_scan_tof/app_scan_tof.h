/**
 * @file app_scan_tof.h
 * @brief API publique de l'application de scan ToF 360°
 *
 * Application complète pour scanner l'environnement à 360° avec
 * plusieurs capteurs VL53L0X et publier les résultats en LaserScan.
 *
 * @note Conforme au CDC iobewi-idf-components
 */

#pragma once

#include "app_scan_tof/app_scan_tof_types.h"
#include <sensor_msgs/msg/laser_scan.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise la configuration avec valeurs par défaut
 *
 * @param[out] config  Configuration à initialiser (non NULL)
 * @return ESP_OK en cas de succès
 * @return ESP_ERR_INVALID_ARG si config est NULL
 */
esp_err_t app_scan_tof_config_init(app_scan_tof_config_t *config);

/**
 * @brief Crée une nouvelle instance de l'application scan ToF
 *
 * Cette fonction :
 * 1. Crée le provider VL53L0X multi-capteurs
 * 2. Valide la configuration scan/mapping
 * 3. Prépare les structures de données
 *
 * @param[in]  config  Configuration (non NULL)
 * @param[out] out     Pointeur vers le handle (non NULL)
 * @return ESP_OK en cas de succès
 * @return ESP_ERR_INVALID_ARG si config ou out est NULL
 * @return ESP_ERR_NO_MEM si allocation échoue
 * @return ESP_FAIL en cas d'erreur d'initialisation
 *
 * @warning La config doit rester valide pendant toute la durée de vie de l'instance
 */
esp_err_t app_scan_tof_new(const app_scan_tof_config_t *config,
                            app_scan_tof_t **out);

/**
 * @brief Effectue une itération de scan et remplit le message LaserScan
 *
 * Cette fonction :
 * 1. Lit un snapshot atomique des capteurs VL53L0X
 * 2. Remplit le message LaserScan avec les données
 * 3. Ajoute le timestamp (time_provider ou esp_timer)
 *
 * @param[in]  handle   Handle de l'application (non NULL)
 * @param[out] out_msg  Message LaserScan à remplir (non NULL)
 * @return ESP_OK en cas de succès
 * @return ESP_ERR_INVALID_ARG si handle ou out_msg est NULL
 * @return ESP_FAIL en cas d'erreur de lecture capteurs
 *
 * @note Cette fonction est thread-safe
 * @note Appeler à la fréquence désirée (typiquement 5-10 Hz)
 */
esp_err_t app_scan_tof_step(app_scan_tof_t *handle,
                             sensor_msgs__msg__LaserScan *out_msg);

/**
 * @brief Configure le provider de temps (optionnel)
 *
 * Par défaut, l'application utilise esp_timer_get_time().
 * Cette fonction permet d'utiliser un autre provider (ex: synchronisation ROS).
 *
 * @param[in] handle      Handle de l'application (non NULL)
 * @param[in] time_provider_ns  Fonction retournant le temps en nanosecondes (ou NULL pour défaut)
 * @return ESP_OK en cas de succès
 * @return ESP_ERR_INVALID_ARG si handle est NULL
 */
esp_err_t app_scan_tof_set_time_provider(app_scan_tof_t *handle,
                                          int64_t (*time_provider_ns)(void));

/**
 * @brief Détruit l'instance et libère toutes les ressources
 *
 * Cette fonction :
 * 1. Détruit le provider VL53L0X
 * 2. Libère la mémoire
 *
 * @param[in] handle  Handle à détruire (non NULL)
 * @return ESP_OK en cas de succès
 * @return ESP_ERR_INVALID_ARG si handle est NULL
 */
esp_err_t app_scan_tof_del(app_scan_tof_t *handle);

#ifdef __cplusplus
}
#endif
