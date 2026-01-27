/**
 * @file app_scan_tof_types.h
 * @brief Types publics pour l'application de scan ToF 360°
 *
 * Application de scan ToF multi-capteurs VL53L0X avec publication
 * de messages sensor_msgs/LaserScan via micro-ROS.
 *
 * @note Conforme au CDC iobewi-idf-components
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#include "lib_vl53l0x_provider/lib_vl53l0x_provider_types.h"
#include "mw_scan_builder/mw_scan_builder.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handle opaque de l'application scan ToF
 */
typedef struct app_scan_tof_s app_scan_tof_t;

/**
 * @brief Configuration de l'application scan ToF
 */
typedef struct {
    // Configuration du provider VL53L0X
    lib_vl53l0x_provider_config_t provider_config;

    // Configuration du scan LaserScan
    mw_scan_builder_config_t scan_config;

    // Provider de temps (optionnel, NULL = esp_timer)
    int64_t (*time_provider)(void);
} app_scan_tof_config_t;

#ifdef __cplusplus
}
#endif
