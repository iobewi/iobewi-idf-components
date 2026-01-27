/**
 * @file app_scan_tof.c
 * @brief Implémentation de l'application de scan ToF 360°
 */

#include "app_scan_tof/app_scan_tof.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"

#include "lib_vl53l0x_provider/lib_vl53l0x_provider.h"

static const char *TAG = "app_scan_tof";

/**
 * @brief Structure privée de l'application (JAMAIS exposée dans .h)
 */
struct app_scan_tof_s {
    // Configuration
    const app_scan_tof_config_t *config;

    // Provider VL53L0X
    lib_vl53l0x_provider_t *provider;

    // Buffer pour snapshot
    lib_vl53l0x_sample_t *samples;
    uint8_t sensor_count;

    // Provider de temps
    int64_t (*time_provider)(void);

    // Flags de logging (éviter spam)
    bool time_fallback_logged;
    bool tof_map_logged;
};

/**
 * @brief Provider de temps par défaut (esp_timer)
 */
static int64_t default_time_provider(void)
{
    return esp_timer_get_time() * 1000LL;  // µs → ns
}

/**
 * @brief Valide et log le mapping capteurs → bins
 */
static bool validate_tof_bin_map(const mw_scan_builder_config_t *scan_cfg,
                                 const lib_vl53l0x_hw_config_t *hw_configs,
                                 uint8_t sensor_count,
                                 bool *tof_map_logged)
{
    if (scan_cfg == NULL || hw_configs == NULL) {
        return false;
    }

    if (!*tof_map_logged) {
        ESP_LOGI(TAG, "ToF bin mapping (sensor -> bin):");
        for (int i = 0; i < sensor_count; i++) {
            ESP_LOGI(TAG, "  sensor[%d] -> bin[%u]", i, (unsigned)hw_configs[i].bin_idx);
        }
        *tof_map_logged = true;
    }

    bool valid = true;
    for (int i = 0; i < sensor_count; i++) {
        uint8_t idx = hw_configs[i].bin_idx;
        if ((int)idx >= scan_cfg->bins) {
            ESP_LOGE(TAG,
                     "ToF bin index out of range (sensor=%d idx=%u bins=%d)",
                     i, (unsigned)idx, scan_cfg->bins);
            valid = false;
        }
    }

    return valid;
}

/**
 * @brief Définit le timestamp du message LaserScan
 */
static void set_timestamp(app_scan_tof_t *handle, sensor_msgs__msg__LaserScan *msg)
{
    int64_t now_ns = 0;

    if (handle->time_provider != NULL) {
        now_ns = handle->time_provider();
    }

    if (now_ns <= 0) {
        now_ns = default_time_provider();
        if (!handle->time_fallback_logged) {
            ESP_LOGW(TAG, "Time sync unavailable, using esp_timer fallback");
            handle->time_fallback_logged = true;
        }
    }

    if (now_ns > 0) {
        msg->header.stamp.sec = (int32_t)(now_ns / 1000000000LL);
        msg->header.stamp.nanosec = (uint32_t)(now_ns % 1000000000LL);
    } else {
        msg->header.stamp.sec = 0;
        msg->header.stamp.nanosec = 0;
    }
}

// ============================================================================
// API Publique
// ============================================================================

esp_err_t app_scan_tof_config_init(app_scan_tof_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(config, 0, sizeof(app_scan_tof_config_t));

    // Initialiser les sous-configs
    esp_err_t ret = lib_vl53l0x_provider_config_init(&config->provider_config);
    if (ret != ESP_OK) {
        return ret;
    }

    // scan_config à remplir par l'utilisateur
    config->time_provider = NULL;  // Utiliser défaut

    return ESP_OK;
}

esp_err_t app_scan_tof_new(const app_scan_tof_config_t *config,
                            app_scan_tof_t **out)
{
    if (config == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Valider la config
    if (config->provider_config.hw_configs == NULL ||
        config->provider_config.sensor_count == 0) {
        ESP_LOGE(TAG, "Invalid provider config");
        return ESP_ERR_INVALID_ARG;
    }

    // Allouer handle
    app_scan_tof_t *handle = calloc(1, sizeof(app_scan_tof_t));
    if (handle == NULL) {
        return ESP_ERR_NO_MEM;
    }

    handle->config = config;
    handle->sensor_count = config->provider_config.sensor_count;
    handle->time_provider = config->time_provider != NULL ? config->time_provider : default_time_provider;
    handle->time_fallback_logged = false;
    handle->tof_map_logged = false;

    // Allouer buffer samples
    handle->samples = calloc(handle->sensor_count, sizeof(lib_vl53l0x_sample_t));
    if (handle->samples == NULL) {
        free(handle);
        return ESP_ERR_NO_MEM;
    }

    // Valider mapping
    if (!validate_tof_bin_map(&config->scan_config,
                              config->provider_config.hw_configs,
                              handle->sensor_count,
                              &handle->tof_map_logged)) {
        ESP_LOGE(TAG, "Invalid ToF bin mapping");
        free(handle->samples);
        free(handle);
        return ESP_ERR_INVALID_ARG;
    }

    // Créer provider VL53L0X
    ESP_LOGI(TAG, "Creating VL53L0X provider (%d sensors)...", handle->sensor_count);
    esp_err_t ret = lib_vl53l0x_provider_new(&config->provider_config, &handle->provider);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create provider: %s", esp_err_to_name(ret));
        free(handle->samples);
        free(handle);
        return ret;
    }

    ESP_LOGI(TAG, "app_scan_tof initialized successfully");

    *out = handle;
    return ESP_OK;
}

esp_err_t app_scan_tof_step(app_scan_tof_t *handle,
                             sensor_msgs__msg__LaserScan *out_msg)
{
    if (handle == NULL || out_msg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (handle->provider == NULL || handle->config == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // 1. Lire snapshot atomique des capteurs
    esp_err_t ret = lib_vl53l0x_provider_read_snapshot(handle->provider, handle->samples);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read snapshot: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. Remplir le message LaserScan avec scan_builder
    ret = mw_scan_builder_fill(out_msg,
                                &handle->config->scan_config,
                                handle->samples,
                                handle->config->provider_config.hw_configs,
                                handle->sensor_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to fill scan: %s", esp_err_to_name(ret));
        return ret;
    }

    // 3. Ajouter timestamp
    set_timestamp(handle, out_msg);

    return ESP_OK;
}

esp_err_t app_scan_tof_set_time_provider(app_scan_tof_t *handle,
                                          int64_t (*time_provider_ns)(void))
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (time_provider_ns == NULL) {
        handle->time_provider = default_time_provider;
    } else {
        handle->time_provider = time_provider_ns;
    }

    return ESP_OK;
}

esp_err_t app_scan_tof_del(app_scan_tof_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Détruire provider
    if (handle->provider != NULL) {
        lib_vl53l0x_provider_del(handle->provider);
        handle->provider = NULL;
    }

    // Libérer samples
    free(handle->samples);

    // Libérer handle
    free(handle);

    return ESP_OK;
}
