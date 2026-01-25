#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "mw_uros_core/mw_uros_core.h"
#include <std_msgs/msg/string.h>

static const char *TAG = "basic_app";

/**
 * @brief Application context
 */
typedef struct {
    uint32_t counter;
} basic_app_context_t;

/**
 * @brief Initialize application resources
 */
bool basic_app_init(void *app_context)
{
    basic_app_context_t *ctx = (basic_app_context_t *)app_context;
    ctx->counter = 0;
    ESP_LOGI(TAG, "Application initialized");
    return true;
}

/**
 * @brief Application step: fill String message
 */
bool basic_app_step(void *app_context, void *ros_message)
{
    basic_app_context_t *ctx = (basic_app_context_t *)app_context;
    std_msgs__msg__String *msg = (std_msgs__msg__String *)ros_message;

    // Create message content
    static char buffer[128];
    snprintf(buffer, sizeof(buffer), "Hello from ESP32-S3! Counter: %u", ctx->counter++);

    // Fill message (micro-ROS uses rosidl_runtime_c__String)
    msg->data.data = buffer;
    msg->data.size = strlen(buffer);
    msg->data.capacity = msg->data.size + 1;

    if (ctx->counter % 10 == 0) {
        ESP_LOGI(TAG, "Published: %s", buffer);
    }

    return true;
}

/**
 * @brief Cleanup application resources
 */
void basic_app_fini(void *app_context)
{
    ESP_LOGI(TAG, "Application cleanup");
}

void app_main(void)
{
    ESP_LOGI(TAG, "mw_uros_core basic example");

    // Initialize NVS (required for WiFi/networking)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Configure micro-ROS core
    uros_core_config_t config = {
        // ROS configuration
        .node_name = "basic_example_node",
        .topic_name = "/hello",
        .domain_id = 0,
        .timer_period_ms = 100,  // Publish at 10 Hz

        // QoS configuration
        .qos_history = RMW_QOS_POLICY_HISTORY_KEEP_LAST,
        .qos_depth = 10,
        .qos_reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT,
        .qos_durability = RMW_QOS_POLICY_DURABILITY_VOLATILE,

        // Task configuration
        .stack_size = 16000,
        .task_priority = 5,
        .core_affinity = 1,  // Pin to core 1

        // Status LED configuration
        .status_led_gpio = 38,       // GPIO 38 for status LED (set to -1 to disable)
        .status_led_brightness = 100, // Brightness 0-255

        // Observability hooks
        .on_metrics = NULL,  // No metrics callback for this example
    };

    // Application context
    static basic_app_context_t app_ctx;

    // Configure application interface
    uros_app_interface_t app = {
        .type_support = ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, String),
        .message_size = sizeof(std_msgs__msg__String),
        .app_init = basic_app_init,
        .app_step = basic_app_step,
        .app_fini = basic_app_fini,
        .app_context = &app_ctx,
        .app_context_size = sizeof(basic_app_context_t),
    };

    // Create core context
    uros_core_context_t *core = uros_core_create(&config, &app);
    if (core == NULL) {
        ESP_LOGE(TAG, "Failed to create core context");
        return;
    }

    // Start micro-ROS core (spawns tasks)
    if (!uros_core_start(core)) {
        ESP_LOGE(TAG, "Failed to start core");
        uros_core_destroy(core);
        return;
    }

    ESP_LOGI(TAG, "micro-ROS core started successfully");
    ESP_LOGI(TAG, "Connect micro-ROS agent to see messages on /hello topic");
    ESP_LOGI(TAG, "Example: ros2 topic echo /hello");

    // Main task done - micro-ROS tasks are now running
}
