/**
 * @file lib_a02_provider.h
 * @brief Provider pour capteurs ultrasoniques A02YYUW
 *
 * Ce composant transforme les lectures brutes du driver drv_a02yyuw en échantillons
 * ultrasoniques avec filtrage médian, validation de range et conversion d'unités.
 *
 * Responsabilités :
 * - Acquisition séquentielle des capteurs avec power gating
 * - Rejet de la première mesure après power-up (optionnel)
 * - Filtre médian sur N mesures (3 par défaut)
 * - Conversion mm → mètres
 * - Validation range min/max
 * - Gestion des timeouts et erreurs
 *
 * @note Ce composant appartient à la catégorie lib_* (bibliothèque utilitaire)
 * @note Aucune dépendance micro-ROS
 * @note Aucune logique métier applicative
 */

#pragma once

#include "lib_a02_provider_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialiser la configuration avec valeurs par défaut
 *
 * Valeurs par défaut :
 * - median_filter_size = 3
 * - range_min_m = 0.3
 * - range_max_m = 4.5
 * - mode = DRV_A02YYUW_MODE_PROCESSED
 * - read_timeout_ms = 500
 * - discard_first_sample = true
 *
 * @param[out] config Structure de configuration à initialiser
 * @return
 *     - ESP_OK: Succès
 *     - ESP_ERR_INVALID_ARG: config est NULL
 */
esp_err_t lib_a02_provider_config_init(lib_a02_provider_config_t *config);

/**
 * @brief Créer une instance du provider A02
 *
 * Cette fonction alloue et initialise une instance du provider.
 * Le driver A02YYUW doit être créé et initialisé avant d'appeler cette fonction.
 *
 * @param[in] config Configuration du provider (ne peut pas être NULL)
 * @param[out] out_handle Pointeur pour recevoir le handle du provider
 * @return
 *     - ESP_OK: Succès
 *     - ESP_ERR_INVALID_ARG: Paramètres invalides (config NULL, driver NULL, etc.)
 *     - ESP_ERR_NO_MEM: Allocation mémoire échouée
 *
 * @note La configuration est copiée, elle peut être libérée après l'appel
 * @note Le driver A02YYUW doit rester valide pendant toute la durée de vie du provider
 */
esp_err_t lib_a02_provider_new(const lib_a02_provider_config_t *config,
                                lib_a02_provider_t **out_handle);

/**
 * @brief Lire un snapshot de tous les capteurs
 *
 * Cette fonction effectue une acquisition complète de tous les capteurs configurés.
 * Pour chaque capteur :
 * 1. Sélectionne le capteur (power-up + mode)
 * 2. Jette la première mesure (si discard_first_sample = true)
 * 3. Lit N mesures (selon median_filter_size)
 * 4. Applique un filtre médian
 * 5. Convertit en mètres et valide le range
 * 6. Remplit le sample avec distance_m et valid
 *
 * En cas d'erreur (timeout, hors range), le sample est marqué comme invalide (valid = false).
 *
 * @param[in] handle Handle du provider
 * @param[out] samples Tableau de samples à remplir (doit être alloué par l'appelant)
 * @param[in] count Nombre d'éléments dans samples (doit être >= sensor_count)
 * @return
 *     - ESP_OK: Succès (au moins un sample valide)
 *     - ESP_ERR_INVALID_ARG: handle NULL, samples NULL, ou count insuffisant
 *     - ESP_FAIL: Tous les capteurs ont échoué
 *
 * @note Les samples invalides ont valid=false et distance_m=0.0
 * @note Le timestamp est rempli avec esp_timer_get_time() (µs depuis boot)
 * @note Cette fonction est bloquante (~44ms par capteur)
 */
esp_err_t lib_a02_provider_read_snapshot(lib_a02_provider_t *handle,
                                          ultrasonic_sample_t *samples,
                                          size_t count);

/**
 * @brief Détruire une instance du provider
 *
 * Cette fonction libère la mémoire allouée par le provider.
 * Le driver A02YYUW n'est PAS détruit (doit être géré par l'appelant).
 *
 * @param[in] handle Handle du provider à détruire
 * @return
 *     - ESP_OK: Succès
 *     - ESP_ERR_INVALID_ARG: handle NULL
 *
 * @note Après cet appel, le handle ne doit plus être utilisé
 * @note Le driver A02YYUW reste valide et doit être détruit séparément
 */
esp_err_t lib_a02_provider_del(lib_a02_provider_t *handle);

#ifdef __cplusplus
}
#endif
