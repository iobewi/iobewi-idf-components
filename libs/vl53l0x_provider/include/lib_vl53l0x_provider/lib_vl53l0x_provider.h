/**
 * @file lib_vl53l0x_provider.h
 * @brief API publique du provider VL53L0X multi-capteurs
 *
 * Fournit une abstraction pour gérer plusieurs capteurs VL53L0X avec :
 * - Initialisation automatique (I2C, assignation d'adresses, démarrage)
 * - Lecture parallèle en tâches FreeRTOS
 * - Snapshot atomique lock-free des dernières mesures
 *
 * @note Conforme au CDC iobewi-idf-components
 */

#pragma once

#include "lib_vl53l0x_provider/lib_vl53l0x_provider_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise la configuration du provider avec valeurs par défaut
 *
 * Valeurs par défaut :
 * - bus_config = NULL (à remplir par l'utilisateur)
 * - hw_configs = NULL (à remplir par l'utilisateur)
 * - sensor_count = 0 (à remplir par l'utilisateur)
 *
 * @param[out] config  Configuration à initialiser (non NULL)
 * @return ESP_OK en cas de succès
 * @return ESP_ERR_INVALID_ARG si config est NULL
 */
esp_err_t lib_vl53l0x_provider_config_init(lib_vl53l0x_provider_config_t *config);

/**
 * @brief Crée et initialise un nouveau provider VL53L0X
 *
 * Cette fonction :
 * 1. Initialise le bus I2C
 * 2. Assigne les adresses I2C via XSHUT
 * 3. Initialise chaque capteur VL53L0X
 * 4. Active les GPIO data-ready
 * 5. Démarre les mesures continues
 * 6. Crée une tâche FreeRTOS par capteur
 *
 * @param[in]  config  Configuration (non NULL)
 * @param[out] out     Pointeur vers le handle (non NULL)
 * @return ESP_OK en cas de succès
 * @return ESP_ERR_INVALID_ARG si config ou out est NULL
 * @return ESP_ERR_NO_MEM si allocation échoue
 * @return ESP_FAIL en cas d'erreur I2C ou hardware
 *
 * @warning En cas d'erreur, les ressources partiellement initialisées sont nettoyées
 */
esp_err_t lib_vl53l0x_provider_new(const lib_vl53l0x_provider_config_t *config,
                                     lib_vl53l0x_provider_t **out);

/**
 * @brief Prend un snapshot atomique des dernières mesures
 *
 * Cette fonction effectue une lecture lock-free des derniers échantillons
 * de tous les capteurs. La cohérence est garantie par un mécanisme de
 * séquence (double-check).
 *
 * @param[in]  handle  Handle du provider (non NULL)
 * @param[out] samples Tableau d'échantillons (taille = sensor_count)
 * @return ESP_OK en cas de succès
 * @return ESP_ERR_INVALID_ARG si handle ou samples est NULL
 *
 * @note Cette fonction est thread-safe et peut être appelée depuis n'importe quel contexte
 * @note Les échantillons invalides ont valid=false et range_m=NAN
 */
esp_err_t lib_vl53l0x_provider_read_snapshot(lib_vl53l0x_provider_t *handle,
                                               lib_vl53l0x_sample_t *samples);

/**
 * @brief Détruit le provider et libère toutes les ressources
 *
 * Cette fonction :
 * 1. Arrête toutes les tâches de lecture
 * 2. Arrête les mesures continues
 * 3. Désinitialise le bus I2C
 * 4. Libère la mémoire
 *
 * @param[in] handle  Handle à détruire (non NULL)
 * @return ESP_OK en cas de succès
 * @return ESP_ERR_INVALID_ARG si handle est NULL
 *
 * @warning Ne pas appeler cette fonction si des threads utilisent encore read_snapshot
 */
esp_err_t lib_vl53l0x_provider_del(lib_vl53l0x_provider_t *handle);

/**
 * @brief Lecture snapshot avec configuration avancée (utilitaire interne)
 *
 * Fonction utilitaire pour lecture snapshot avec gestion de timeout et logging.
 * Utilisée en interne par read_snapshot.
 *
 * @param[in]  tag          Tag pour logging (non NULL)
 * @param[in]  config       Configuration snapshot (non NULL)
 * @param[in]  samples      Échantillons sources (non NULL)
 * @param[in]  seq          Séquences sources (non NULL)
 * @param[in,out] log_state État de logging (non NULL)
 * @param[out] out          Échantillons de sortie (non NULL)
 *
 * @note Cette fonction est exposée pour compatibilité mais son usage est déconseillé
 */
void lib_vl53l0x_provider_snapshot_read(const char *tag,
                                         const lib_vl53l0x_snapshot_config_t *config,
                                         const lib_vl53l0x_sample_t *samples,
                                         const uint32_t *seq,
                                         lib_vl53l0x_snapshot_log_t *log_state,
                                         lib_vl53l0x_sample_t *out,
                                         uint8_t count);

#ifdef __cplusplus
}
#endif
