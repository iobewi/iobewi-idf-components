/**
 * @file lib_vl53l0x_provider.c
 * @brief Implémentation du provider VL53L0X multi-capteurs
 *
 * Gestion multi-capteurs avec :
 * - Tasks FreeRTOS par capteur
 * - Lecture parallèle continue
 * - Snapshot atomique lock-free
 * - Retry automatique avec backoff exponentiel
 */

#include "lib_vl53l0x_provider/lib_vl53l0x_provider.h"

#include <math.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "drv_vl53l0x/drv_vl53l0x.h"

static const char *TAG = "lib_vl53l0x_provider";

// Constantes snapshot
static const uint32_t kSnapshotSpinBackoffInterval = 64;
static const TickType_t kSnapshotTimeoutTicks = pdMS_TO_TICKS(2);
static const uint32_t kSnapshotMaxSpins = 2000;
static const uint32_t kSnapshotOddYieldThreshold = 50;
static const uint8_t kSnapshotTimeoutStatus = 252;
static const TickType_t kSnapshotLogIntervalTicks = pdMS_TO_TICKS(1000);

// Weak symbols pour compatibilité ST API
#if defined(__GNUC__)
__attribute__((weak)) VL53L0X_Error VL53L0X_StopMeasurement(VL53L0X_DEV Dev);
__attribute__((weak)) esp_err_t vl53l0x_i2c_master_deinit(void);
#endif

/**
 * @brief Contexte d'une tâche capteur
 */
typedef struct {
    int idx;                         ///< Index capteur
    vl53l0x_dev_t *dev;             ///< Device VL53L0X
    SemaphoreHandle_t i2c_mutex;    ///< Mutex I2C partagé
    TickType_t timeout;             ///< Timeout GPIO ready
    volatile bool stop_requested;   ///< Flag d'arrêt
    lib_vl53l0x_sample_t *samples;  ///< Tableau d'échantillons partagé
    uint32_t *seq;                  ///< Tableau de séquences partagé
    portMUX_TYPE *mux;              ///< Spinlock pour update
} sensor_task_ctx_t;

/**
 * @brief Structure privée du provider (JAMAIS exposée dans .h)
 */
struct lib_vl53l0x_provider_s {
    uint8_t sensor_count;

    // Configuration
    lib_vl53l0x_bus_config_t bus_config;
    lib_vl53l0x_hw_config_t *hw_configs;

    // Devices VL53L0X
    vl53l0x_dev_t *devs;

    // Tasks et synchronisation
    TaskHandle_t *task_handles;
    sensor_task_ctx_t *task_contexts;
    SemaphoreHandle_t i2c_mutex;

    // État partagé (lock-free)
    lib_vl53l0x_sample_t *samples;
    uint32_t *seq;
    portMUX_TYPE mux;

    // Logging snapshot
    lib_vl53l0x_snapshot_log_t snapshot_log;
};

// ============================================================================
// Utilitaires snapshot
// ============================================================================

static void log_snapshot_timeout(const char *tag,
                                 const lib_vl53l0x_snapshot_config_t *config,
                                 lib_vl53l0x_snapshot_log_t *log_state,
                                 int idx,
                                 uint32_t seq_val)
{
    TickType_t now = xTaskGetTickCount();
    uint32_t suppressed = 0;
    bool should_log = false;

    portENTER_CRITICAL(&log_state->log_mux);
    log_state->timeout_count++;
    if (log_state->timeout_last_log_tick == 0 ||
        (now - log_state->timeout_last_log_tick) >= config->log_interval_ticks) {
        suppressed = log_state->timeout_count - 1;
        log_state->timeout_count = 0;
        log_state->timeout_last_log_tick = now;
        should_log = true;
    }
    portEXIT_CRITICAL(&log_state->log_mux);

    if (!should_log) {
        return;
    }

    if (suppressed > 0) {
        ESP_LOGW(tag,
                 "Snapshot timeout (idx=%d seq=%" PRIu32 ", suppressed=%" PRIu32 ")",
                 idx,
                 seq_val,
                 suppressed);
        return;
    }

    ESP_LOGW(tag, "Snapshot timeout (idx=%d seq=%" PRIu32 ")", idx, seq_val);
}

void lib_vl53l0x_provider_snapshot_read(const char *tag,
                                         const lib_vl53l0x_snapshot_config_t *config,
                                         const lib_vl53l0x_sample_t *samples,
                                         const uint32_t *seq,
                                         lib_vl53l0x_snapshot_log_t *log_state,
                                         lib_vl53l0x_sample_t *out,
                                         uint8_t count)
{
    for (int i = 0; i < count; i++) {
        TickType_t start = xTaskGetTickCount();
        uint32_t spins = 0;
        uint32_t odd_spins = 0;

        while (1) {
            uint32_t seq1 = __atomic_load_n(&seq[i], __ATOMIC_ACQUIRE);
            if (seq1 & 1u) {
                odd_spins++;
                spins++;
                if (odd_spins >= config->odd_yield_threshold) {
                    odd_spins = 0;
                    vTaskDelay(1);
                }
                if (spins >= config->max_spins ||
                    (xTaskGetTickCount() - start) > config->timeout_ticks) {
                    log_snapshot_timeout(tag, config, log_state, i, seq1);
                    out[i] = (lib_vl53l0x_sample_t){
                        .valid = false,
                        .status = config->timeout_status,
                        .range_m = NAN,
                        .seq = seq1,
                    };
                    break;
                }
                if (spins % kSnapshotSpinBackoffInterval == 0) {
                    vTaskDelay(1);
                }
                continue;
            }

            lib_vl53l0x_sample_t sample = samples[i];
            uint32_t seq2 = __atomic_load_n(&seq[i], __ATOMIC_ACQUIRE);
            if (seq1 == seq2 && !(seq2 & 1u)) {
                out[i] = sample;
                break;
            }

            spins++;
            if (spins >= config->max_spins ||
                (xTaskGetTickCount() - start) > config->timeout_ticks) {
                log_snapshot_timeout(tag, config, log_state, i, seq2);
                out[i] = (lib_vl53l0x_sample_t){
                    .valid = false,
                    .status = config->timeout_status,
                    .range_m = NAN,
                    .seq = seq2,
                };
                break;
            }
            if (spins % kSnapshotSpinBackoffInterval == 0) {
                vTaskDelay(1);
            }
            taskYIELD();
        }
    }
}

// ============================================================================
// Utilitaires internes
// ============================================================================

static inline void update_one(lib_vl53l0x_sample_t *samples,
                              uint32_t *seq,
                              portMUX_TYPE *mux,
                              int i,
                              bool valid,
                              uint8_t status,
                              float range_m)
{
    portENTER_CRITICAL(mux);
    uint32_t seq_val = __atomic_load_n(&seq[i], __ATOMIC_RELAXED);
    __atomic_store_n(&seq[i], seq_val + 1u, __ATOMIC_RELAXED);
    samples[i].valid = valid;
    samples[i].status = status;
    samples[i].range_m = range_m;
    samples[i].seq = seq_val + 2u;
    __atomic_store_n(&seq[i], seq_val + 2u, __ATOMIC_RELEASE);
    portEXIT_CRITICAL(mux);
}

static void mark_all_invalid(lib_vl53l0x_provider_t *handle, uint8_t status)
{
    for (int i = 0; i < handle->sensor_count; i++) {
        update_one(handle->samples, handle->seq, &handle->mux, i, false, status, NAN);
    }
}

static void stop_all_measurements(vl53l0x_dev_t *devs, int started)
{
    if (VL53L0X_StopMeasurement == NULL) {
        return;
    }

    for (int i = 0; i < started; i++) {
        VL53L0X_Error st = VL53L0X_StopMeasurement(&devs[i].st);
        if (st != VL53L0X_ERROR_NONE) {
            ESP_LOGW(TAG, "StopMeasurement dev[%d] failed: %d", i, (int)st);
        }
    }
}

static void deinit_i2c_bus(void)
{
    if (vl53l0x_i2c_master_deinit == NULL) {
        return;
    }

    esp_err_t err = vl53l0x_i2c_master_deinit();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C deinit failed: %s", esp_err_to_name(err));
    }
}

// ============================================================================
// Tâche capteur
// ============================================================================

static void sensor_task(void *arg)
{
    sensor_task_ctx_t *ctx = (sensor_task_ctx_t *)arg;
    uint32_t consecutive_errors = 0;
    const uint32_t backoff_base_ms = 5;
    const uint32_t backoff_max_ms = 100;

    while (!ctx->stop_requested) {
        VL53L0X_RangingMeasurementData_t data = {0};

        bool valid = false;
        uint8_t status = 255;
        float range_m = NAN;
        bool gpio_timeout = false;
        bool i2c_timeout = false;

        // Attend l'IRQ "data ready" via GPIO
        esp_err_t err = vl53l0x_wait_gpio_ready(ctx->dev, ctx->timeout);
        if (ctx->stop_requested) {
            break;
        }

        if (err == ESP_OK) {
            if (xSemaphoreTake(ctx->i2c_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                VL53L0X_Error st = VL53L0X_GetRangingMeasurementData(&ctx->dev->st, &data);
                if (st == VL53L0X_ERROR_NONE) {
                    status = data.RangeStatus;
                    if (status == 0) {
                        valid = true;
                        range_m = (float)data.RangeMilliMeter * 0.001f;
                    }
                }
                // Acknowledge IRQ
                (void)VL53L0X_ClearInterruptMask(&ctx->dev->st, 0);
                xSemaphoreGive(ctx->i2c_mutex);
            } else {
                status = 250;
                valid = false;
                range_m = NAN;
                i2c_timeout = true;
            }
        } else {
            status = 251;
            valid = false;
            range_m = NAN;
            gpio_timeout = true;
        }

        update_one(ctx->samples, ctx->seq, ctx->mux, ctx->idx, valid, status, range_m);

        if (gpio_timeout || i2c_timeout) {
            consecutive_errors++;
        } else {
            consecutive_errors = 0;
        }

        TickType_t delay_ticks = pdMS_TO_TICKS(2);
        if (consecutive_errors > 0) {
            uint32_t backoff_ms = backoff_base_ms;
            uint32_t shift = consecutive_errors - 1;
            if (shift < 31) {
                backoff_ms <<= shift;
            } else {
                backoff_ms = backoff_max_ms;
            }
            if (backoff_ms > backoff_max_ms) {
                backoff_ms = backoff_max_ms;
            }
            delay_ticks += pdMS_TO_TICKS(backoff_ms);
        }

        vTaskDelay(delay_ticks);
    }

    vTaskDelete(NULL);
}

// ============================================================================
// API Publique
// ============================================================================

esp_err_t lib_vl53l0x_provider_config_init(lib_vl53l0x_provider_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    config->bus_config = NULL;
    config->hw_configs = NULL;
    config->sensor_count = 0;

    return ESP_OK;
}

esp_err_t lib_vl53l0x_provider_new(const lib_vl53l0x_provider_config_t *config,
                                     lib_vl53l0x_provider_t **out)
{
    if (config == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->bus_config == NULL || config->hw_configs == NULL || config->sensor_count == 0) {
        ESP_LOGE(TAG, "Invalid config: bus_config, hw_configs and sensor_count required");
        return ESP_ERR_INVALID_ARG;
    }

    lib_vl53l0x_provider_t *handle = calloc(1, sizeof(lib_vl53l0x_provider_t));
    if (handle == NULL) {
        return ESP_ERR_NO_MEM;
    }

    handle->sensor_count = config->sensor_count;
    handle->bus_config = *config->bus_config;
    handle->mux = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    handle->snapshot_log.log_mux = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    handle->snapshot_log.timeout_count = 0;
    handle->snapshot_log.timeout_last_log_tick = 0;

    // Allouer hw_configs
    handle->hw_configs = calloc(handle->sensor_count, sizeof(lib_vl53l0x_hw_config_t));
    if (handle->hw_configs == NULL) {
        free(handle);
        return ESP_ERR_NO_MEM;
    }
    memcpy(handle->hw_configs, config->hw_configs,
           handle->sensor_count * sizeof(lib_vl53l0x_hw_config_t));

    // Allouer devices
    handle->devs = calloc(handle->sensor_count, sizeof(vl53l0x_dev_t));
    if (handle->devs == NULL) {
        free(handle->hw_configs);
        free(handle);
        return ESP_ERR_NO_MEM;
    }

    // Allouer samples et seq
    handle->samples = calloc(handle->sensor_count, sizeof(lib_vl53l0x_sample_t));
    handle->seq = calloc(handle->sensor_count, sizeof(uint32_t));
    if (handle->samples == NULL || handle->seq == NULL) {
        free(handle->seq);
        free(handle->samples);
        free(handle->devs);
        free(handle->hw_configs);
        free(handle);
        return ESP_ERR_NO_MEM;
    }

    // Allouer task_handles et contexts
    handle->task_handles = calloc(handle->sensor_count, sizeof(TaskHandle_t));
    handle->task_contexts = calloc(handle->sensor_count, sizeof(sensor_task_ctx_t));
    if (handle->task_handles == NULL || handle->task_contexts == NULL) {
        free(handle->task_contexts);
        free(handle->task_handles);
        free(handle->seq);
        free(handle->samples);
        free(handle->devs);
        free(handle->hw_configs);
        free(handle);
        return ESP_ERR_NO_MEM;
    }

    bool init_ok = false;
    bool i2c_initialized = false;
    int started_devices = 0;

    // Init I2C
    ESP_LOGI(TAG, "Init I2C bus...");
    esp_err_t err = vl53l0x_i2c_master_init(handle->bus_config.sda_gpio,
                                            handle->bus_config.scl_gpio,
                                            handle->bus_config.i2c_freq_hz);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(err));
        goto cleanup;
    }
    i2c_initialized = true;

    // Assign addresses
    vl53l0x_slot_t *slots = calloc(handle->sensor_count, sizeof(vl53l0x_slot_t));
    if (slots == NULL) {
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    for (int i = 0; i < handle->sensor_count; i++) {
        slots[i].xshut_gpio = handle->hw_configs[i].xshut_gpio;
        slots[i].new_addr_7b = handle->hw_configs[i].addr_7b;
    }

    ESP_LOGI(TAG, "Assign addresses (multi XSHUT)...");
    err = vl53l0x_multi_assign_addresses(slots, handle->sensor_count, 10);
    free(slots);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Address assignment failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    // Init devices
    for (int i = 0; i < handle->sensor_count; i++) {
        handle->devs[i].addr_7b = handle->hw_configs[i].addr_7b;
    }

    ESP_LOGI(TAG, "Init %d devices...", handle->sensor_count);
    for (int i = 0; i < handle->sensor_count; i++) {
        err = vl53l0x_init(&handle->devs[i], handle->bus_config.timing_budget_us);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Init dev[%d] failed: %s", i, esp_err_to_name(err));
            goto cleanup;
        }

        err = vl53l0x_enable_gpio_ready(&handle->devs[i], handle->hw_configs[i].int_gpio, true);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "GPIO ready init dev[%d] failed: %s", i, esp_err_to_name(err));
            goto cleanup;
        }

        VL53L0X_Error st = VL53L0X_StartMeasurement(&handle->devs[i].st);
        if (st != VL53L0X_ERROR_NONE) {
            ESP_LOGE(TAG, "StartMeasurement dev[%d] failed: %d", i, (int)st);
            goto cleanup;
        }
        started_devices++;

        update_one(handle->samples, handle->seq, &handle->mux, i, false, 255, NAN);
    }

    // Create mutex
    handle->i2c_mutex = xSemaphoreCreateMutex();
    if (handle->i2c_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create I2C mutex");
        goto cleanup;
    }

    // Create tasks
    for (int i = 0; i < handle->sensor_count; i++) {
        handle->task_contexts[i].idx = i;
        handle->task_contexts[i].dev = &handle->devs[i];
        handle->task_contexts[i].i2c_mutex = handle->i2c_mutex;
        handle->task_contexts[i].timeout = pdMS_TO_TICKS(handle->bus_config.gpio_ready_timeout_ms);
        handle->task_contexts[i].stop_requested = false;
        handle->task_contexts[i].samples = handle->samples;
        handle->task_contexts[i].seq = handle->seq;
        handle->task_contexts[i].mux = &handle->mux;

        char name[16];
        snprintf(name, sizeof(name), "vl53_%d", i);
        BaseType_t ok = xTaskCreate(sensor_task, name, 4096, &handle->task_contexts[i], 5,
                                    &handle->task_handles[i]);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "Failed to create sensor task %s", name);
            goto cleanup;
        }
    }

    ESP_LOGI(TAG, "VL53 provider started (%d sensors)", handle->sensor_count);
    init_ok = true;

cleanup:
    if (!init_ok) {
        if (started_devices > 0) {
            stop_all_measurements(handle->devs, started_devices);
        }
        for (int i = 0; i < handle->sensor_count; i++) {
            handle->task_contexts[i].stop_requested = true;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
        for (int i = 0; i < handle->sensor_count; i++) {
            if (handle->task_handles[i] != NULL) {
                if (eTaskGetState(handle->task_handles[i]) != eDeleted) {
                    vTaskDelete(handle->task_handles[i]);
                }
                handle->task_handles[i] = NULL;
            }
        }
        if (handle->i2c_mutex != NULL) {
            vSemaphoreDelete(handle->i2c_mutex);
            handle->i2c_mutex = NULL;
        }
        if (i2c_initialized) {
            deinit_i2c_bus();
        }
        mark_all_invalid(handle, 255);

        free(handle->task_contexts);
        free(handle->task_handles);
        free(handle->seq);
        free(handle->samples);
        free(handle->devs);
        free(handle->hw_configs);
        free(handle);
        return err;
    }

    *out = handle;
    return ESP_OK;
}

esp_err_t lib_vl53l0x_provider_read_snapshot(lib_vl53l0x_provider_t *handle,
                                               lib_vl53l0x_sample_t *samples)
{
    if (handle == NULL || samples == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lib_vl53l0x_snapshot_config_t config = {
        .timeout_ticks = kSnapshotTimeoutTicks,
        .max_spins = kSnapshotMaxSpins,
        .odd_yield_threshold = kSnapshotOddYieldThreshold,
        .timeout_status = kSnapshotTimeoutStatus,
        .log_interval_ticks = kSnapshotLogIntervalTicks,
    };

    lib_vl53l0x_provider_snapshot_read(TAG, &config, handle->samples, handle->seq,
                                        &handle->snapshot_log, samples, handle->sensor_count);

    return ESP_OK;
}

esp_err_t lib_vl53l0x_provider_del(lib_vl53l0x_provider_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Stop tasks
    for (int i = 0; i < handle->sensor_count; i++) {
        handle->task_contexts[i].stop_requested = true;
    }

    vTaskDelay(pdMS_TO_TICKS(20));

    for (int i = 0; i < handle->sensor_count; i++) {
        if (handle->task_handles[i] != NULL) {
            if (eTaskGetState(handle->task_handles[i]) != eDeleted) {
                vTaskDelete(handle->task_handles[i]);
            }
        }
    }

    // Stop measurements
    stop_all_measurements(handle->devs, handle->sensor_count);

    // Delete mutex
    if (handle->i2c_mutex != NULL) {
        vSemaphoreDelete(handle->i2c_mutex);
    }

    // Deinit I2C
    deinit_i2c_bus();

    // Free memory
    free(handle->task_contexts);
    free(handle->task_handles);
    free(handle->seq);
    free(handle->samples);
    free(handle->devs);
    free(handle->hw_configs);
    free(handle);

    return ESP_OK;
}
