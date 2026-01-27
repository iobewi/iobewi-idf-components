/**
 * @file lib_a02_provider.c
 * @brief Implémentation du provider A02
 */

#include "lib_a02_provider/lib_a02_provider.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "lib_a02_provider";

/**
 * @brief Structure privée du provider A02
 */
struct lib_a02_provider_s {
    drv_a02yyuw_t *driver;
    uint8_t sensor_count;
    uint8_t median_filter_size;
    float range_min_m;
    float range_max_m;
    drv_a02yyuw_mode_t mode;
    uint32_t read_timeout_ms;
    bool discard_first_sample;

    // Buffer interne pour le filtre médian
    uint16_t *median_buffer_mm;
};

/**
 * @brief Fonction de comparaison pour qsort (uint16_t croissant)
 */
static int compare_uint16(const void *a, const void *b)
{
    uint16_t val_a = *(const uint16_t *)a;
    uint16_t val_b = *(const uint16_t *)b;
    return (val_a > val_b) - (val_a < val_b);
}

/**
 * @brief Calculer la médiane d'un tableau de uint16_t
 *
 * @param[in] values Tableau de valeurs
 * @param[in] count Nombre de valeurs (doit être > 0)
 * @return Valeur médiane
 *
 * @note Le tableau values est modifié (trié) par cette fonction
 */
static uint16_t compute_median_uint16(uint16_t *values, size_t count)
{
    if (count == 0) {
        return 0;
    }

    // Tri du tableau
    qsort(values, count, sizeof(uint16_t), compare_uint16);

    // Retourner la valeur médiane
    if (count % 2 == 1) {
        // Nombre impair : valeur centrale
        return values[count / 2];
    } else {
        // Nombre pair : moyenne des 2 valeurs centrales
        return (values[count / 2 - 1] + values[count / 2]) / 2;
    }
}

/**
 * @brief Lire N mesures d'un capteur et calculer la médiane
 *
 * @param[in] provider Handle du provider
 * @param[in] sensor_id Index du capteur
 * @param[out] median_mm Médiane calculée en millimètres
 * @return
 *     - ESP_OK: Succès
 *     - ESP_ERR_TIMEOUT: Au moins une lecture a échoué
 */
static esp_err_t read_sensor_median(lib_a02_provider_t *provider, int sensor_id, uint16_t *median_mm)
{
    esp_err_t ret;

    // Sélectionner le capteur
    ret = drv_a02yyuw_select(provider->driver, sensor_id, provider->mode);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to select sensor %d: %s", sensor_id, esp_err_to_name(ret));
        return ret;
    }

    // Jeter la première mesure si configuré
    if (provider->discard_first_sample) {
        uint16_t throwaway = 0;
        drv_a02yyuw_read(provider->driver, &throwaway, provider->read_timeout_ms);
        // On ignore le résultat de cette lecture
    }

    // Lire N mesures pour le filtre médian
    uint8_t valid_count = 0;
    for (uint8_t i = 0; i < provider->median_filter_size; i++) {
        uint16_t distance_mm = 0;
        ret = drv_a02yyuw_read(provider->driver, &distance_mm, provider->read_timeout_ms);

        if (ret == ESP_OK) {
            provider->median_buffer_mm[valid_count++] = distance_mm;
        } else {
            ESP_LOGW(TAG, "Sensor %d read %d/%d failed: %s",
                     sensor_id, i + 1, provider->median_filter_size, esp_err_to_name(ret));
        }
    }

    // Vérifier qu'on a au moins une mesure valide
    if (valid_count == 0) {
        ESP_LOGW(TAG, "Sensor %d: all reads failed", sensor_id);
        return ESP_ERR_TIMEOUT;
    }

    // Calculer la médiane
    *median_mm = compute_median_uint16(provider->median_buffer_mm, valid_count);

    ESP_LOGD(TAG, "Sensor %d: median = %u mm (from %d samples)",
             sensor_id, *median_mm, valid_count);

    return ESP_OK;
}

esp_err_t lib_a02_provider_config_init(lib_a02_provider_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(config, 0, sizeof(*config));

    // Valeurs par défaut
    config->driver = NULL;  // Doit être fourni par l'appelant
    config->sensor_count = 4;
    config->median_filter_size = 3;
    config->range_min_m = 0.3f;
    config->range_max_m = 4.5f;
    config->mode = DRV_A02YYUW_MODE_PROCESSED;
    config->read_timeout_ms = 500;
    config->discard_first_sample = true;

    return ESP_OK;
}

esp_err_t lib_a02_provider_new(const lib_a02_provider_config_t *config,
                                lib_a02_provider_t **out_handle)
{
    if (!config || !out_handle) {
        ESP_LOGE(TAG, "Invalid arguments (config or out_handle is NULL)");
        return ESP_ERR_INVALID_ARG;
    }

    if (!config->driver) {
        ESP_LOGE(TAG, "Driver handle is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (config->sensor_count == 0) {
        ESP_LOGE(TAG, "Sensor count is 0");
        return ESP_ERR_INVALID_ARG;
    }

    if (config->median_filter_size == 0 || config->median_filter_size % 2 == 0) {
        ESP_LOGE(TAG, "Median filter size must be odd and > 0 (got %d)", config->median_filter_size);
        return ESP_ERR_INVALID_ARG;
    }

    if (config->range_min_m >= config->range_max_m) {
        ESP_LOGE(TAG, "Invalid range: min=%f >= max=%f", config->range_min_m, config->range_max_m);
        return ESP_ERR_INVALID_ARG;
    }

    // Allocation du handle
    lib_a02_provider_t *handle = (lib_a02_provider_t *)calloc(1, sizeof(lib_a02_provider_t));
    if (!handle) {
        ESP_LOGE(TAG, "Failed to allocate provider handle");
        return ESP_ERR_NO_MEM;
    }

    // Allocation du buffer médian
    handle->median_buffer_mm = (uint16_t *)calloc(config->median_filter_size, sizeof(uint16_t));
    if (!handle->median_buffer_mm) {
        ESP_LOGE(TAG, "Failed to allocate median buffer");
        free(handle);
        return ESP_ERR_NO_MEM;
    }

    // Copie de la configuration
    handle->driver = config->driver;
    handle->sensor_count = config->sensor_count;
    handle->median_filter_size = config->median_filter_size;
    handle->range_min_m = config->range_min_m;
    handle->range_max_m = config->range_max_m;
    handle->mode = config->mode;
    handle->read_timeout_ms = config->read_timeout_ms;
    handle->discard_first_sample = config->discard_first_sample;

    ESP_LOGI(TAG, "Provider created (sensors=%d, median_size=%d, range=[%.1f, %.1f]m)",
             handle->sensor_count, handle->median_filter_size,
             handle->range_min_m, handle->range_max_m);

    *out_handle = handle;
    return ESP_OK;
}

esp_err_t lib_a02_provider_read_snapshot(lib_a02_provider_t *handle,
                                          ultrasonic_sample_t *samples,
                                          size_t count)
{
    if (!handle || !samples) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    if (count < handle->sensor_count) {
        ESP_LOGE(TAG, "Sample buffer too small (need %d, got %zu)", handle->sensor_count, count);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t valid_samples = 0;
    uint64_t start_time = esp_timer_get_time();

    // Lire tous les capteurs
    for (uint8_t i = 0; i < handle->sensor_count; i++) {
        // Initialiser le sample
        samples[i].distance_m = 0.0f;
        samples[i].valid = false;
        samples[i].timestamp = esp_timer_get_time();

        // Lire avec filtre médian
        uint16_t median_mm = 0;
        esp_err_t ret = read_sensor_median(handle, i, &median_mm);

        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Sensor %d: read failed", i);
            continue;
        }

        // Convertir mm → mètres
        float distance_m = median_mm / 1000.0f;

        // Valider le range
        if (distance_m < handle->range_min_m || distance_m > handle->range_max_m) {
            ESP_LOGW(TAG, "Sensor %d: out of range (%.2fm, valid=[%.1f, %.1f]m)",
                     i, distance_m, handle->range_min_m, handle->range_max_m);
            continue;
        }

        // Sample valide
        samples[i].distance_m = distance_m;
        samples[i].valid = true;
        valid_samples++;

        ESP_LOGD(TAG, "Sensor %d: %.2fm (valid)", i, distance_m);
    }

    uint64_t elapsed_us = esp_timer_get_time() - start_time;
    ESP_LOGI(TAG, "Snapshot complete: %d/%d valid samples (took %llu ms)",
             valid_samples, handle->sensor_count, elapsed_us / 1000);

    if (valid_samples == 0) {
        ESP_LOGE(TAG, "All sensors failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t lib_a02_provider_del(lib_a02_provider_t *handle)
{
    if (!handle) {
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_ERR_INVALID_ARG;
    }

    // Libérer le buffer médian
    if (handle->median_buffer_mm) {
        free(handle->median_buffer_mm);
    }

    // Libérer le handle
    free(handle);

    ESP_LOGI(TAG, "Provider destroyed");
    return ESP_OK;
}
