/**
 * @file app_scan_ultra.c
 * @brief Implémentation de l'application scan ultra
 */

#include "app_scan_ultra/app_scan_ultra.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "sensor_msgs/msg/laser_scan.h"

static const char *TAG = "app_scan_ultra";

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * @brief Structure privée de l'application
 */
struct app_scan_ultra_s {
    // Provider
    lib_a02_provider_t *provider;

    // Configuration LaserScan
    uint8_t bins;
    float angle_min;
    float angle_max;
    float angle_increment;
    float range_min;
    float range_max;

    // Mapping capteurs → bins
    uint8_t sensor_bin_mapping[4];
    uint8_t sensor_count;

    // Frame ID
    char frame_id[64];

    // Buffers pour les samples
    ultrasonic_sample_t *samples;
};

esp_err_t app_scan_ultra_config_init(app_scan_ultra_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(config, 0, sizeof(*config));

    // Provider config (doit être configuré par l'appelant)
    lib_a02_provider_config_init(&config->provider_config);

    // LaserScan config : 36 bins, 10° résolution
    config->bins = 36;
    config->angle_min = 0.0f;
    config->angle_max = 2.0f * M_PI;  // 360°
    config->range_min = 0.3f;
    config->range_max = 4.5f;

    // Mapping par défaut : 0°, 90°, 180°, 270° pour 36 bins
    config->sensor_bin_mapping[0] = 0;   // 0° = bin 0
    config->sensor_bin_mapping[1] = 9;   // 90° = bin 9
    config->sensor_bin_mapping[2] = 18;  // 180° = bin 18
    config->sensor_bin_mapping[3] = 27;  // 270° = bin 27

    // Frame ID
    config->frame_id = "base_link";

    return ESP_OK;
}

esp_err_t app_scan_ultra_new(const app_scan_ultra_config_t *config,
                              app_scan_ultra_t **out_handle)
{
    if (!config || !out_handle) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    if (config->bins == 0 || config->bins > 360) {
        ESP_LOGE(TAG, "Invalid bins count: %d", config->bins);
        return ESP_ERR_INVALID_ARG;
    }

    if (config->angle_min >= config->angle_max) {
        ESP_LOGE(TAG, "Invalid angle range: [%.2f, %.2f]", config->angle_min, config->angle_max);
        return ESP_ERR_INVALID_ARG;
    }

    // Allocation du handle
    app_scan_ultra_t *handle = (app_scan_ultra_t *)calloc(1, sizeof(app_scan_ultra_t));
    if (!handle) {
        ESP_LOGE(TAG, "Failed to allocate handle");
        return ESP_ERR_NO_MEM;
    }

    // Créer le provider
    esp_err_t ret = lib_a02_provider_new(&config->provider_config, &handle->provider);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create provider: %s", esp_err_to_name(ret));
        free(handle);
        return ret;
    }

    // Allocation du buffer de samples
    handle->sensor_count = config->provider_config.sensor_count;
    handle->samples = (ultrasonic_sample_t *)calloc(handle->sensor_count, sizeof(ultrasonic_sample_t));
    if (!handle->samples) {
        ESP_LOGE(TAG, "Failed to allocate samples buffer");
        lib_a02_provider_del(handle->provider);
        free(handle);
        return ESP_ERR_NO_MEM;
    }

    // Copie de la configuration
    handle->bins = config->bins;
    handle->angle_min = config->angle_min;
    handle->angle_max = config->angle_max;
    handle->angle_increment = (config->angle_max - config->angle_min) / config->bins;
    handle->range_min = config->range_min;
    handle->range_max = config->range_max;

    memcpy(handle->sensor_bin_mapping, config->sensor_bin_mapping,
           sizeof(handle->sensor_bin_mapping));

    strncpy(handle->frame_id, config->frame_id, sizeof(handle->frame_id) - 1);
    handle->frame_id[sizeof(handle->frame_id) - 1] = '\0';

    ESP_LOGI(TAG, "App created (bins=%d, angle_inc=%.3f rad, sensors=%d)",
             handle->bins, handle->angle_increment, handle->sensor_count);
    ESP_LOGI(TAG, "  Mapping: [%d, %d, %d, %d]",
             handle->sensor_bin_mapping[0], handle->sensor_bin_mapping[1],
             handle->sensor_bin_mapping[2], handle->sensor_bin_mapping[3]);

    *out_handle = handle;
    return ESP_OK;
}

bool app_scan_ultra_init(void *ctx)
{
    if (!ctx) {
        ESP_LOGE(TAG, "Invalid context");
        return false;
    }

    app_scan_ultra_t *app = (app_scan_ultra_t *)ctx;
    ESP_LOGI(TAG, "App initialized (frame_id=%s)", app->frame_id);
    return true;
}

bool app_scan_ultra_step(void *ctx, void *ros_msg)
{
    if (!ctx || !ros_msg) {
        ESP_LOGE(TAG, "Invalid arguments");
        return false;
    }

    app_scan_ultra_t *app = (app_scan_ultra_t *)ctx;
    sensor_msgs__msg__LaserScan *scan_msg = (sensor_msgs__msg__LaserScan *)ros_msg;

    // 1. Acquérir un snapshot des capteurs
    esp_err_t ret = lib_a02_provider_read_snapshot(app->provider, app->samples, app->sensor_count);
    if (ret != ESP_OK && ret != ESP_FAIL) {
        ESP_LOGW(TAG, "Snapshot read failed: %s", esp_err_to_name(ret));
        return false;
    }

    // 2. Remplir le header
    scan_msg->header.stamp.sec = (int32_t)(esp_timer_get_time() / 1000000ULL);
    scan_msg->header.stamp.nanosec = (uint32_t)((esp_timer_get_time() % 1000000ULL) * 1000ULL);

    // Copy frame_id (assume frame_id buffer is already allocated)
    if (scan_msg->header.frame_id.data) {
        strncpy(scan_msg->header.frame_id.data, app->frame_id, scan_msg->header.frame_id.capacity - 1);
        scan_msg->header.frame_id.data[scan_msg->header.frame_id.capacity - 1] = '\0';
        scan_msg->header.frame_id.size = strlen(scan_msg->header.frame_id.data);
    }

    // 3. Remplir les paramètres du scan
    scan_msg->angle_min = app->angle_min;
    scan_msg->angle_max = app->angle_max;
    scan_msg->angle_increment = app->angle_increment;
    scan_msg->range_min = app->range_min;
    scan_msg->range_max = app->range_max;
    scan_msg->scan_time = 0.176f;  // ~176ms pour 4 capteurs
    scan_msg->time_increment = 0.044f;  // ~44ms par capteur

    // 4. Initialiser tous les bins à NAN
    for (size_t i = 0; i < scan_msg->ranges.size; i++) {
        scan_msg->ranges.data[i] = NAN;
    }

    // 5. Mapper les samples valides sur les bins
    uint8_t valid_count = 0;
    for (uint8_t i = 0; i < app->sensor_count; i++) {
        if (app->samples[i].valid) {
            uint8_t bin_idx = app->sensor_bin_mapping[i];

            if (bin_idx < scan_msg->ranges.size) {
                scan_msg->ranges.data[bin_idx] = app->samples[i].distance_m;
                valid_count++;

                ESP_LOGD(TAG, "Sensor[%d] → bin[%d] = %.2f m",
                         i, bin_idx, app->samples[i].distance_m);
            } else {
                ESP_LOGW(TAG, "Sensor[%d]: bin index %d out of range (max %zu)",
                         i, bin_idx, scan_msg->ranges.size);
            }
        } else {
            ESP_LOGD(TAG, "Sensor[%d]: invalid sample (bin[%d] = NAN)",
                     i, app->sensor_bin_mapping[i]);
        }
    }

    ESP_LOGI(TAG, "LaserScan published: %d/%d valid samples",
             valid_count, app->sensor_count);

    return true;
}

void app_scan_ultra_fini(void *ctx)
{
    if (!ctx) {
        return;
    }

    ESP_LOGI(TAG, "App finalized");
}

esp_err_t app_scan_ultra_del(app_scan_ultra_t *handle)
{
    if (!handle) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_ERR_INVALID_ARG;
    }

    // Détruire le provider
    if (handle->provider) {
        lib_a02_provider_del(handle->provider);
    }

    // Libérer le buffer de samples
    if (handle->samples) {
        free(handle->samples);
    }

    // Libérer le handle
    free(handle);

    ESP_LOGI(TAG, "App destroyed");
    return ESP_OK;
}
