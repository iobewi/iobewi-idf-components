/**
 * @file app_scan_ultra.h
 * @brief Application de scan ultrasonique pour micro-ROS
 *
 * Cette application orchestre lib_a02_provider et mw_scan_builder pour publier
 * des messages sensor_msgs/LaserScan via micro-ROS.
 *
 * Architecture :
 * - Utilise lib_a02_provider pour acquérir les mesures ultrasoniques
 * - Mappe les capteurs sur des bins angulaires
 * - Construit un message LaserScan via mw_scan_builder
 * - Publie via mw_uros_core
 *
 * Responsabilités :
 * - Orchestration de lib_a02_provider + mw_scan_builder
 * - Mapping capteurs → bins angulaires
 * - Callbacks micro-ROS (app_init, app_step, app_fini)
 * - Gestion des samples invalides (bins → NAN)
 *
 * @note Ce composant appartient à la catégorie app_* (logique métier)
 * @note Dépend de lib_*, mw_* et potentiellement drv_*
 */

#pragma once

#include "app_scan_ultra_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialiser la configuration avec valeurs par défaut
 *
 * Valeurs par défaut :
 * - bins = 36 (résolution 10°)
 * - angle_min = 0.0 (0°)
 * - angle_max = 6.283185 (360° = 2*PI)
 * - range_min = 0.3m
 * - range_max = 4.5m
 * - sensor_bin_mapping = {0, 9, 18, 27} (0°, 90°, 180°, 270°)
 * - frame_id = "base_link"
 *
 * @param[out] config Structure de configuration à initialiser
 * @return
 *     - ESP_OK: Succès
 *     - ESP_ERR_INVALID_ARG: config est NULL
 */
esp_err_t app_scan_ultra_config_init(app_scan_ultra_config_t *config);

/**
 * @brief Créer une instance de l'application
 *
 * Cette fonction alloue et initialise l'application.
 * Elle crée également le provider A02 en interne.
 *
 * @param[in] config Configuration de l'application (ne peut pas être NULL)
 * @param[out] out_handle Pointeur pour recevoir le handle de l'application
 * @return
 *     - ESP_OK: Succès
 *     - ESP_ERR_INVALID_ARG: Paramètres invalides
 *     - ESP_ERR_NO_MEM: Allocation mémoire échouée
 *
 * @note La configuration est copiée, elle peut être libérée après l'appel
 * @note Le driver A02YYUW doit être créé avant d'appeler cette fonction
 */
esp_err_t app_scan_ultra_new(const app_scan_ultra_config_t *config,
                              app_scan_ultra_t **out_handle);

/**
 * @brief Callback d'initialisation pour micro-ROS
 *
 * Cette fonction est appelée par mw_uros_core lors de l'initialisation
 * de la session micro-ROS.
 *
 * @param[in] ctx Contexte applicatif (app_scan_ultra_t*)
 * @return true si l'initialisation a réussi, false sinon
 */
bool app_scan_ultra_init(void *ctx);

/**
 * @brief Callback de step pour micro-ROS
 *
 * Cette fonction est appelée périodiquement par mw_uros_core.
 * Elle :
 * 1. Acquiert un snapshot des capteurs via lib_a02_provider
 * 2. Mappe les capteurs sur les bins angulaires
 * 3. Remplit le message LaserScan (bins non observés = NAN)
 *
 * @param[in] ctx Contexte applicatif (app_scan_ultra_t*)
 * @param[in,out] ros_msg Message ROS à remplir (sensor_msgs__msg__LaserScan*)
 * @return true si le step a réussi, false sinon
 */
bool app_scan_ultra_step(void *ctx, void *ros_msg);

/**
 * @brief Callback de nettoyage pour micro-ROS
 *
 * Cette fonction est appelée par mw_uros_core lors de la fermeture
 * de la session micro-ROS.
 *
 * @param[in] ctx Contexte applicatif (app_scan_ultra_t*)
 */
void app_scan_ultra_fini(void *ctx);

/**
 * @brief Détruire une instance de l'application
 *
 * Cette fonction libère toute la mémoire allouée, y compris le provider.
 * Le driver A02YYUW n'est PAS détruit (doit être géré par l'appelant).
 *
 * @param[in] handle Handle de l'application à détruire
 * @return
 *     - ESP_OK: Succès
 *     - ESP_ERR_INVALID_ARG: handle NULL
 *
 * @note Après cet appel, le handle ne doit plus être utilisé
 * @note Le driver A02YYUW reste valide et doit être détruit séparément
 */
esp_err_t app_scan_ultra_del(app_scan_ultra_t *handle);

#ifdef __cplusplus
}
#endif
