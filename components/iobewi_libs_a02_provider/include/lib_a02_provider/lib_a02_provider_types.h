/**
 * @file lib_a02_provider_types.h
 * @brief Types publics pour lib_a02_provider
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "drv_a02yyuw/drv_a02yyuw.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handle opaque du provider A02
 */
typedef struct lib_a02_provider_s lib_a02_provider_t;

/**
 * @brief Échantillon de mesure ultrasonique
 */
typedef struct {
    float distance_m;     /**< Distance en mètres */
    bool valid;           /**< true si la mesure est valide */
    uint32_t timestamp;   /**< Timestamp en microsecondes depuis boot (optionnel) */
} ultrasonic_sample_t;

/**
 * @brief Configuration du provider A02
 */
typedef struct {
    drv_a02yyuw_t *driver;              /**< Handle du driver A02YYUW (doit être initialisé) */
    uint8_t sensor_count;                /**< Nombre de capteurs (typiquement 4) */
    uint8_t median_filter_size;          /**< Taille du filtre médian (3 par défaut, doit être impair) */
    float range_min_m;                   /**< Distance minimale valide en mètres (0.3m typique) */
    float range_max_m;                   /**< Distance maximale valide en mètres (4.5m typique) */
    drv_a02yyuw_mode_t mode;             /**< Mode de lecture (REALTIME ou PROCESSED) */
    uint32_t read_timeout_ms;            /**< Timeout de lecture par mesure en ms (500ms typique) */
    bool discard_first_sample;           /**< true pour jeter la première mesure après power-up */
} lib_a02_provider_config_t;

#ifdef __cplusplus
}
#endif
