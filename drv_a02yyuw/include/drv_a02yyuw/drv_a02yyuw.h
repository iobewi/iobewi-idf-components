/**
 * @file drv_a02yyuw.h
 * @brief Driver pour capteur de distance ultrasonique A02YYUW (SEN0311)
 *
 * Ce driver permet de piloter plusieurs capteurs A02YYUW sur un UART partagé.
 * Chaque capteur est alimenté individuellement via un contrôleur d'alimentation
 * STMPS2141STR (GPIO EN dédié par capteur).
 *
 * Protocole UART:
 * - 9600 bps, 8N1
 * - Trame: [0xFF, DATA_H, DATA_L, SUM]
 * - SUM = (0xFF + DATA_H + DATA_L) & 0xFF
 * - distance_mm = (DATA_H << 8) | DATA_L
 *
 * @note Ce driver appartient à la catégorie drv_* (accès matériel direct)
 * @note Aucune dépendance micro-ROS
 */

#pragma once

#include "drv_a02yyuw_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialiser les valeurs par défaut de la configuration
 *
 * @param[out] config Structure de configuration à initialiser
 * @return
 *     - ESP_OK: Succès
 *     - ESP_ERR_INVALID_ARG: config est NULL
 */
esp_err_t drv_a02yyuw_config_init(drv_a02yyuw_config_t *config);

/**
 * @brief Créer une instance du driver A02YYUW
 *
 * Cette fonction alloue et initialise une instance du driver, configure l'UART
 * et initialise les GPIO de contrôle (mode et EN).
 *
 * @param[in] config Configuration du driver (ne peut pas être NULL)
 * @param[out] out_handle Pointeur pour recevoir le handle du driver
 * @return
 *     - ESP_OK: Succès
 *     - ESP_ERR_INVALID_ARG: Paramètres invalides
 *     - ESP_ERR_NO_MEM: Allocation mémoire échouée
 *     - Autres codes d'erreur ESP-IDF (UART, GPIO)
 *
 * @note La configuration est copiée, elle peut être libérée après l'appel
 * @note Tous les capteurs sont initialement éteints
 */
esp_err_t drv_a02yyuw_new(const drv_a02yyuw_config_t *config, drv_a02yyuw_t **out_handle);

/**
 * @brief Sélectionner un capteur et configurer son mode
 *
 * Cette fonction:
 * 1. Éteint tous les capteurs
 * 2. Configure le mode (temps réel ou filtré)
 * 3. Allume le capteur sélectionné
 * 4. Vide le buffer UART
 *
 * @param[in] handle Handle du driver
 * @param[in] sensor_id Index du capteur (0 à sensor_count-1)
 * @param[in] mode Mode de fonctionnement souhaité
 * @return
 *     - ESP_OK: Succès
 *     - ESP_ERR_INVALID_ARG: handle NULL ou sensor_id invalide
 *
 * @note Cette fonction introduit des délais (power_down + mode_settle + power_up)
 * @note Après cet appel, il est recommandé de jeter la première lecture
 */
esp_err_t drv_a02yyuw_select(drv_a02yyuw_t *handle, int sensor_id, drv_a02yyuw_mode_t mode);

/**
 * @brief Lire une mesure de distance
 *
 * Cette fonction lit une trame complète depuis l'UART, vérifie le checksum
 * et retourne la distance en millimètres.
 *
 * @param[in] handle Handle du driver
 * @param[out] distance_mm Pointeur pour recevoir la distance en millimètres
 * @param[in] timeout_ms Timeout en millisecondes
 * @return
 *     - ESP_OK: Succès, distance valide
 *     - ESP_ERR_INVALID_ARG: Paramètres invalides
 *     - ESP_ERR_TIMEOUT: Timeout avant réception d'une trame valide
 *
 * @note Un capteur doit être sélectionné avant d'appeler cette fonction
 * @note La première mesure après drv_a02yyuw_select() peut être invalide
 */
esp_err_t drv_a02yyuw_read(drv_a02yyuw_t *handle, uint16_t *distance_mm, uint32_t timeout_ms);

/**
 * @brief Détruire une instance du driver
 *
 * Cette fonction:
 * 1. Éteint tous les capteurs
 * 2. Désinstalle le driver UART
 * 3. Libère la mémoire
 *
 * @param[in] handle Handle du driver à détruire
 * @return
 *     - ESP_OK: Succès
 *     - ESP_ERR_INVALID_ARG: handle NULL
 *
 * @note Après cet appel, le handle ne doit plus être utilisé
 */
esp_err_t drv_a02yyuw_del(drv_a02yyuw_t *handle);

#ifdef __cplusplus
}
#endif
