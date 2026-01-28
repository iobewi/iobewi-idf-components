/**
 * @file main.c
 * @brief Exemple micro-ROS pour app_scan_ultra
 *
 * Cet exemple montre comment publier des messages sensor_msgs/LaserScan
 * à partir de 4 capteurs A02YYUW via micro-ROS.
 */

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "drv_a02yyuw/drv_a02yyuw.h"
#include "app_scan_ultra/app_scan_ultra.h"
#include "mw_uros_core/mw_uros_core.h"

#include <sensor_msgs/msg/laser_scan.h>

static const char *TAG = "main";

// GPIO EN (STMPS2141STR) - un par capteur
static const int EN_PINS[] = {
    CONFIG_APP_SCAN_EN_GPIO_0,  // Avant (0°)
    CONFIG_APP_SCAN_EN_GPIO_1,  // Droite (90°)
    CONFIG_APP_SCAN_EN_GPIO_2,  // Arrière (180°)
    CONFIG_APP_SCAN_EN_GPIO_3,  // Gauche (270°)
};

#define SENSOR_COUNT (sizeof(EN_PINS) / sizeof(EN_PINS[0]))

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void app_main(void)
{
    ESP_LOGI(TAG, "=== Ultrasonic LaserScan Application ===");
    ESP_LOGI(TAG, "Configuration:");
    ESP_LOGI(TAG, "  UART RX GPIO: %d", CONFIG_APP_SCAN_UART_RX_GPIO);
    ESP_LOGI(TAG, "  Mode GPIO: %d", CONFIG_APP_SCAN_MODE_GPIO);
    ESP_LOGI(TAG, "  EN GPIOs: [%d, %d, %d, %d]",
             EN_PINS[0], EN_PINS[1], EN_PINS[2], EN_PINS[3]);
    ESP_LOGI(TAG, "  Status LED: %d", CONFIG_APP_SCAN_STATUS_LED_GPIO);

    // ========================================================================
    // 1. Créer et initialiser le driver A02YYUW
    // ========================================================================

    ESP_LOGI(TAG, "Creating driver A02YYUW...");

    drv_a02yyuw_config_t drv_config;
    ESP_ERROR_CHECK(drv_a02yyuw_config_init(&drv_config));

    drv_config.uart_num = UART_NUM_1;
    drv_config.uart_rx_gpio = CONFIG_APP_SCAN_UART_RX_GPIO;
    drv_config.uart_tx_gpio = UART_PIN_NO_CHANGE;
    drv_config.baudrate = 9600;
    drv_config.rx_buffer_size = 256;

    drv_config.gpio_mode = CONFIG_APP_SCAN_MODE_GPIO;
    drv_config.mode_active_high = true;

    drv_config.gpio_en_list = EN_PINS;
    drv_config.sensor_count = SENSOR_COUNT;

    drv_config.t_mode_settle_ms = 2;
    drv_config.t_power_up_ms = 20;
    drv_config.t_power_down_ms = 10;

    drv_a02yyuw_t *driver = NULL;
    esp_err_t ret = drv_a02yyuw_new(&drv_config, &driver);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create driver: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Driver A02YYUW initialized");

    // ========================================================================
    // 2. Configurer l'application scan ultra
    // ========================================================================

    ESP_LOGI(TAG, "Configuring app_scan_ultra...");

    app_scan_ultra_config_t app_config;
    ESP_ERROR_CHECK(app_scan_ultra_config_init(&app_config));

    // Configurer le provider
    app_config.provider_config.driver = driver;
    app_config.provider_config.sensor_count = SENSOR_COUNT;
    app_config.provider_config.median_filter_size = 3;
    app_config.provider_config.range_min_m = 0.3f;
    app_config.provider_config.range_max_m = 4.5f;
    app_config.provider_config.mode = DRV_A02YYUW_MODE_PROCESSED;
    app_config.provider_config.read_timeout_ms = 500;
    app_config.provider_config.discard_first_sample = true;

    // LaserScan : 36 bins, résolution 10°
    app_config.bins = 36;
    app_config.angle_min = 0.0f;
    app_config.angle_max = 2.0f * M_PI;
    app_config.range_min = 0.3f;
    app_config.range_max = 4.5f;

    // Mapping capteurs → bins (0°, 90°, 180°, 270°)
    app_config.sensor_bin_mapping[0] = 0;   // Avant (0°) → bin 0
    app_config.sensor_bin_mapping[1] = 9;   // Droite (90°) → bin 9
    app_config.sensor_bin_mapping[2] = 18;  // Arrière (180°) → bin 18
    app_config.sensor_bin_mapping[3] = 27;  // Gauche (270°) → bin 27

    app_config.frame_id = "base_link";

    // Créer l'application
    app_scan_ultra_t *app = NULL;
    ret = app_scan_ultra_new(&app_config, &app);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create app: %s", esp_err_to_name(ret));
        drv_a02yyuw_del(driver);
        return;
    }

    ESP_LOGI(TAG, "app_scan_ultra initialized");
    ESP_LOGI(TAG, "  LaserScan: %d bins (%.1f° resolution)",
             app_config.bins, 360.0f / app_config.bins);
    ESP_LOGI(TAG, "  Range: [%.1f, %.1f] m", app_config.range_min, app_config.range_max);
    ESP_LOGI(TAG, "  Mapping: [%d, %d, %d, %d]",
             app_config.sensor_bin_mapping[0], app_config.sensor_bin_mapping[1],
             app_config.sensor_bin_mapping[2], app_config.sensor_bin_mapping[3]);

    // ========================================================================
    // 3. Configurer micro-ROS
    // ========================================================================

    ESP_LOGI(TAG, "Configuring micro-ROS...");

    uros_core_config_t uros_config = {
        .node_name = "esp32_ultrasonic",
        .topic_name = "/scan",
        .domain_id = 0,
        .timer_period_ms = 200,  // 5 Hz (max ~5.7 Hz)

        // QoS
        .qos_history = RMW_QOS_POLICY_HISTORY_KEEP_LAST,
        .qos_depth = 10,
        .qos_reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT,
        .qos_durability = RMW_QOS_POLICY_DURABILITY_VOLATILE,

        // Task
        .stack_size = 16000,
        .task_priority = 5,
        .core_affinity = 1,

        // Status LED
        .status_led_gpio = CONFIG_APP_SCAN_STATUS_LED_GPIO,
        .status_led_brightness = 100,

        // Métriques
        .on_metrics = NULL,
    };

    // Interface applicative
    uros_app_interface_t app_interface = {
        .type_support = ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, LaserScan),
        .message_size = sizeof(sensor_msgs__msg__LaserScan),
        .app_init = app_scan_ultra_init,
        .app_step = app_scan_ultra_step,
        .app_fini = app_scan_ultra_fini,
        .app_context = app,
        .app_context_size = sizeof(app_scan_ultra_t),
    };

    // ========================================================================
    // 4. Créer et démarrer le core micro-ROS
    // ========================================================================

    ESP_LOGI(TAG, "Creating micro-ROS core...");

    mw_uros_core_t *core = NULL;
    ret = mw_uros_core_create(&uros_config, &app_interface, &core);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create uros_core: %s", esp_err_to_name(ret));
        app_scan_ultra_del(app);
        drv_a02yyuw_del(driver);
        return;
    }

    ESP_LOGI(TAG, "micro-ROS core created");
    ESP_LOGI(TAG, "  Node: %s", uros_config.node_name);
    ESP_LOGI(TAG, "  Topic: %s", uros_config.topic_name);
    ESP_LOGI(TAG, "  Frequency: %.1f Hz", 1000.0f / uros_config.timer_period_ms);

    // Démarrer le core
    ESP_LOGI(TAG, "Starting micro-ROS core...");
    ESP_LOGI(TAG, "Waiting for agent on /dev/ttyUSB0...");
    ESP_LOGI(TAG, "Run: ros2 run micro_ros_agent micro_ros_agent serial --dev /dev/ttyUSB0");

    ret = mw_uros_core_start(core);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start uros_core: %s", esp_err_to_name(ret));
        app_scan_ultra_del(app);
        drv_a02yyuw_del(driver);
        return;
    }

    ESP_LOGI(TAG, "micro-ROS core started successfully!");
    ESP_LOGI(TAG, "Publishing LaserScan on topic /scan");
    ESP_LOGI(TAG, "Visualize: ros2 run rviz2 rviz2");

    // Le core s'exécute maintenant en tâche FreeRTOS
    // La tâche principale peut continuer d'autres opérations
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }

    // ========================================================================
    // 5. Nettoyage (jamais atteint dans cet exemple)
    // ========================================================================

    app_scan_ultra_del(app);
    drv_a02yyuw_del(driver);

    ESP_LOGI(TAG, "Application finished");
}
