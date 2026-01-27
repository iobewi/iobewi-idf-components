#pragma once

#include "mw_uros_core/mw_uros_core_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a micro-ROS core context
 *
 * @param config Core configuration
 * @param app Application interface
 * @param out Output pointer to store created context
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on invalid parameters, ESP_ERR_NO_MEM on allocation failure
 */
esp_err_t uros_core_create(const uros_core_config_t *config,
                           const uros_app_interface_t *app,
                           uros_core_context_t **out);

/**
 * @brief Destroy a micro-ROS core context
 *
 * @param ctx Core context to destroy
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on invalid parameters
 */
esp_err_t uros_core_destroy(uros_core_context_t *ctx);

/**
 * @brief Start the micro-ROS core (spawns tasks)
 *
 * @param ctx Core context
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on invalid parameters, ESP_FAIL on start failure
 */
esp_err_t uros_core_start(uros_core_context_t *ctx);

/**
 * @brief Synchronize time with the micro-ROS agent
 *
 * Utility function for time synchronization. Can be called by applications
 * or internally by the core.
 * @return ESP_OK on success, ESP_FAIL on sync failure
 */
esp_err_t uros_core_sync_time(void);

/**
 * @brief Log an RCL failure with error string
 *
 * @param tag Log tag
 * @param label Operation label
 * @param rc RCL return code
 * @return ESP_OK on success
 */
esp_err_t uros_core_log_rcl_failure(const char *tag, const char *label, rcl_ret_t rc);

/**
 * @brief Configure entity destroy timeout for clean shutdown
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t uros_core_configure_entity_timeout(void);

#ifdef __cplusplus
}
#endif
