/**
 * @file app_scan_ultra_types.h
 * @brief Types publics pour app_scan_ultra
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "lib_a02_provider/lib_a02_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handle opaque de l'application scan ultra
 */
typedef struct app_scan_ultra_s app_scan_ultra_t;

/**
 * @brief Configuration de l'application scan ultra
 */
typedef struct {
    // Configuration du provider
    lib_a02_provider_config_t provider_config;

    // Configuration du LaserScan
    uint8_t bins;                       /**< Nombre de bins LaserScan (36 recommandé) */
    float angle_min;                    /**< Angle min en radians (0.0 = 0°) */
    float angle_max;                    /**< Angle max en radians (2*PI = 360°) */
    float range_min;                    /**< Distance min valide (m) */
    float range_max;                    /**< Distance max valide (m) */

    // Mapping capteurs → bins
    uint8_t sensor_bin_mapping[4];     /**< Mapping capteur[i] → bin[mapping[i]] */

    // Frame ROS
    const char *frame_id;               /**< Frame ID ROS (ex: "base_link") */
} app_scan_ultra_config_t;

#ifdef __cplusplus
}
#endif
