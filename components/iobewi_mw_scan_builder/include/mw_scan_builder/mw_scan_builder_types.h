#pragma once
#include <stdbool.h>
#include <stddef.h>

#include "lib_vl53l0x_provider/lib_vl53l0x_provider_types.h"

// Compatibilité avec anciens noms
typedef lib_vl53l0x_sample_t tof_sample_t;
typedef lib_vl53l0x_hw_config_t tof_hw_config_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration for LaserScan message generation.
 */
typedef struct {
    float angle_min;        ///< Minimum scan angle (radians)
    float angle_inc;        ///< Angular increment between bins (radians)
    int bins;               ///< Number of bins in the scan

    float range_min;        ///< Minimum valid range (meters)
    float range_max;        ///< Maximum valid range (meters)

    float scan_time;        ///< Total time for one complete scan (seconds)
    float time_increment;   ///< Time between individual measurements (seconds)

    const char *frame_id;   ///< Frame identifier for the scan
} mw_scan_builder_config_t;

/**
 * @brief Handle opaque du scan builder.
 */
typedef struct mw_scan_builder_s mw_scan_builder_t;

/**
 * @brief Storage buffers and ownership tracking for LaserScan message.
 */
typedef struct {
    float *ranges_buffer;           ///< Buffer for range data
    size_t ranges_capacity;         ///< Capacity of ranges buffer
    char *frame_id_buffer;          ///< Buffer for frame ID string
    size_t frame_id_capacity;       ///< Capacity of frame ID buffer
    bool owns_ranges_buffer;        ///< True if this structure owns the ranges buffer
    bool owns_frame_id_buffer;      ///< True if this structure owns the frame ID buffer
} mw_scan_builder_storage_t;

// Backward compatibility aliases
typedef mw_scan_builder_config_t scan_config_t;
typedef mw_scan_builder_storage_t scan_builder_storage_t;

#ifdef __cplusplus
}
#endif
