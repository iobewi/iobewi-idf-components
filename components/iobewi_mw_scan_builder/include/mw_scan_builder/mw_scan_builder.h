#pragma once

#include <esp_err.h>
#include <sensor_msgs/msg/laser_scan.h>
#include "mw_scan_builder/mw_scan_builder_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create and initialize a scan builder handle.
 *
 * @param config Scan configuration (bin count, angular layout, range limits,
 *               scan timing, frame identifier).
 * @param out    Output pointer receiving the allocated handle.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on invalid parameters, ESP_ERR_NO_MEM on allocation failure.
 */
esp_err_t mw_scan_builder_new(const mw_scan_builder_config_t *config, mw_scan_builder_t **out);

/**
 * @brief Destroy a scan builder handle created with mw_scan_builder_new().
 *
 * @param handle Scan builder handle.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on invalid parameters.
 */
esp_err_t mw_scan_builder_del(mw_scan_builder_t *handle);

/**
 * @brief Initialize a LaserScan message according to the provided scan configuration.
 *
 * This function initializes the LaserScan structure, sets all static fields
 * (frame_id, angle_min, angle_increment, angle_max, range limits, timing),
 * assigns the ranges/frame_id buffers, preallocates the (unused) intensities buffer,
 * and initializes all range values to NAN.
 *
 * Ownership model:
 * - If storage provides non-NULL buffers, the caller retains ownership.
 * - If buffers are NULL and CONFIG_MICRO_ROS_SCAN_BUILDER_ALLOC_MALLOC is enabled,
 *   scan_builder_init allocates them with malloc and sets ownership flags in
 *   storage; scan_builder_deinit will free them.
 * - If buffers are NULL and malloc support is disabled, initialization fails.
 *
 * Intensities are not used and are left empty.
 *
 * This function must be called once before repeated calls to scan_builder_fill().
 *
 * @param msg   Pointer to the LaserScan message to initialize.
 * @param cfg   Scan configuration (bin count, angular layout, range limits,
 *              scan timing, frame identifier).
 * @param storage Storage buffers and ownership tracking for ranges and frame_id.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on invalid parameters, ESP_ERR_NO_MEM on allocation failure.
 */
esp_err_t mw_scan_builder_init(sensor_msgs__msg__LaserScan *msg,
                               const mw_scan_builder_config_t *cfg,
                               mw_scan_builder_storage_t *storage);

/**
 * @brief Deinitialize a LaserScan message previously initialized by scan_builder_init().
 *
 * This function releases the ranges buffer and resets ranges/intensities fields
 * to a safe empty state. Call it when the scan builder is no longer needed or
 * before reinitializing.
 *
 * @param msg   Pointer to the LaserScan message to deinitialize.
 * @param storage Storage buffers and ownership tracking used at init time.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on invalid parameters.
 */
esp_err_t mw_scan_builder_deinit(sensor_msgs__msg__LaserScan *msg, mw_scan_builder_storage_t *storage);

/**
 * @brief Populate a LaserScan message from a snapshot of ToF samples,
 *        using a fixed bin index mapping.
 *
 * The scan is first cleared (all ranges set to NAN). Each ToF sample is then
 * written to the LaserScan bin specified by hw_cfg[sensor_index].bin_idx.
 *
 * - The mapping is index-based (no angle computation or rounding).
 * - Each ToF sensor is expected to map to a unique bin (no overlap).
 * - Bins not associated with any sensor remain NAN (unobserved space).
 * - Invalid or out-of-range samples are ignored, leaving the bin as NAN.
 *
 * @param msg       Pointer to the LaserScan message to fill.
 * @param cfg       Scan configuration (bin count, angle_min, angle_increment,
 *                  range_min, range_max, scan timing).
 * @param samples   Snapshot array of TOF_COUNT ToF measurements.
 * @param hw_cfg    Hardware configuration table (includes bin indices).
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on invalid parameters.
 */
esp_err_t mw_scan_builder_fill(sensor_msgs__msg__LaserScan *msg,
                               const mw_scan_builder_config_t *cfg,
                               const tof_sample_t *samples,
                               const tof_hw_config_t *hw_cfg,
                               uint8_t sensor_count);


#ifdef __cplusplus
}
#endif
