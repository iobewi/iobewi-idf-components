#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <esp_err.h>
#include <rcl/rcl.h>
#include <rmw/qos_profiles.h>
#include <rosidl_runtime_c/message_type_support_struct.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration
typedef struct uros_core_context uros_core_context_t;

/**
 * @brief Application interface for pluggable micro-ROS applications
 *
 * The application provides callbacks for initialization, step execution, and cleanup.
 * The core allocates the ROS message and calls app_step() to fill it.
 */
typedef struct {
    /** ROS message type support (e.g., ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, LaserScan)) */
    const rosidl_message_type_support_t *type_support;

    /** Size of the ROS message structure (e.g., sizeof(sensor_msgs__msg__LaserScan)) */
    size_t message_size;

    /**
     * @brief Initialize application-specific resources
     * @param app_context Application context pointer
     * @return true on success, false on failure
     */
    bool (*app_init)(void *app_context);

    /**
     * @brief Execute one application step and fill the ROS message
     * @param app_context Application context pointer
     * @param ros_message Pointer to the allocated ROS message (to be filled by app)
     * @return true on success, false on failure
     */
    bool (*app_step)(void *app_context, void *ros_message);

    /**
     * @brief Cleanup application-specific resources
     * @param app_context Application context pointer
     */
    void (*app_fini)(void *app_context);

    /** Application-specific context (passed to callbacks) */
    void *app_context;

    /** Size of application context (for validation) */
    size_t app_context_size;
} uros_app_interface_t;

/**
 * @brief Configuration for micro-ROS core
 */
typedef struct {
    // ROS configuration
    const char *node_name;           /**< ROS node name */
    const char *topic_name;          /**< Publisher topic name */
    uint32_t domain_id;              /**< ROS domain ID */
    uint32_t timer_period_ms;        /**< Timer period in milliseconds */

    // QoS configuration
    rmw_qos_history_policy_t qos_history;         /**< QoS history policy */
    size_t qos_depth;                             /**< QoS queue depth */
    rmw_qos_reliability_policy_t qos_reliability; /**< QoS reliability policy */
    rmw_qos_durability_policy_t qos_durability;   /**< QoS durability policy */

    // Task configuration
    uint32_t stack_size;             /**< Stack size for tasks (bytes) */
    uint32_t task_priority;          /**< Task priority (UBaseType_t) */
    int32_t core_affinity;           /**< Core affinity (BaseType_t, -1 for any) */

    // Status LED configuration (optional)
    int32_t status_led_gpio;         /**< GPIO for status LED (-1 to disable) */
    uint8_t status_led_brightness;   /**< LED brightness (0-255, 0=off, 255=max) */

    // Observability hooks (optional, can be NULL)
    /**
     * @brief Called periodically with runtime metrics
     * @param publish_failures Total publish failures
     * @param app_step_failures Total app step failures
     */
    void (*on_metrics)(uint32_t publish_failures, uint32_t app_step_failures);
} uros_core_config_t;

#ifdef __cplusplus
}
#endif
