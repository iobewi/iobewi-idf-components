/**
 * @file app_hls_player.c
 * @brief Implémentation du player HLS
 */

#include "app_hls_player/app_hls_player.h"
#include "app_hls_player_fetcher.h"
#include "app_hls_player_audio.h"
#include "app_hls_player_http.h"
#include "app_hls_player_ts_sync.h"
#include "app_hls_player_internal.h"
#include "lib_m3u8_parser/lib_m3u8_parser.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "simple_dec/esp_audio_simple_dec.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "app_hls_player";


// ============================================================================
// API Publique
// ============================================================================

esp_err_t app_hls_player_config_init(app_hls_player_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(config, 0, sizeof(app_hls_player_config_t));
    config->buffer_size = 0;  // 0 = utilise CONFIG_APP_HLS_PLAYER_RING_BUFFER_SIZE du Kconfig

    return ESP_OK;
}

esp_err_t app_hls_player_new(const app_hls_player_config_t *config, app_hls_player_t **out)
{
    if (config == NULL || out == NULL) {
        ESP_LOGE(TAG, "Arguments NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (config->stream_url == NULL || config->write_cb == NULL) {
        ESP_LOGE(TAG, "stream_url ou write_cb NULL");
        return ESP_ERR_INVALID_ARG;
    }

    app_hls_player_t *handle = calloc(1, sizeof(app_hls_player_t));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Échec d'allocation du handle");
        return ESP_ERR_NO_MEM;
    }

    // Copier l'URL
    handle->stream_url = strdup(config->stream_url);
    if (handle->stream_url == NULL) {
        free(handle);
        return ESP_ERR_NO_MEM;
    }

    // Déterminer buffer_size (Kconfig si 0, sinon config utilisateur)
    const size_t MIN_BUFFER_SIZE = 16 * 1024;   // 16 KB minimum
    const size_t MAX_BUFFER_SIZE = 256 * 1024;  // 256 KB maximum
    size_t buffer_size = config->buffer_size;

    if (buffer_size == 0) {
        buffer_size = CONFIG_APP_HLS_PLAYER_RING_BUFFER_SIZE * 1024;
        ESP_LOGI(TAG, "buffer_size=0 → utilisation Kconfig: %zu KB", CONFIG_APP_HLS_PLAYER_RING_BUFFER_SIZE);
    }

    if (buffer_size < MIN_BUFFER_SIZE) {
        ESP_LOGW(TAG, "buffer_size %zu trop petit, clamping à %zu", buffer_size, MIN_BUFFER_SIZE);
        buffer_size = MIN_BUFFER_SIZE;
    } else if (buffer_size > MAX_BUFFER_SIZE) {
        ESP_LOGW(TAG, "buffer_size %zu trop grand, clamping à %zu", buffer_size, MAX_BUFFER_SIZE);
        buffer_size = MAX_BUFFER_SIZE;
    }

    handle->buffer_size = buffer_size;
    handle->write_cb = config->write_cb;
    handle->write_ctx = config->write_ctx;

    // Créer le ring buffer (NOSPLIT pour préserver alignement TS 188-byte)
    // FIX: RINGBUF_TYPE_NOSPLIT garantit que chaque send = item atomique
    // → pas de coupure arbitraire des paquets TS alignés 188-byte
    handle->ring_buffer = xRingbufferCreate(buffer_size, RINGBUF_TYPE_NOSPLIT);
    if (handle->ring_buffer == NULL) {
        ESP_LOGE(TAG, "Échec de création du ring buffer");
        free(handle->stream_url);
        free(handle);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Ring buffer créé: %zu bytes", buffer_size);

    // Créer le sémaphore de téléchargement
    handle->download_semaphore = xSemaphoreCreateBinary();
    if (handle->download_semaphore == NULL) {
        ESP_LOGE(TAG, "Échec de création du sémaphore download");
        vRingbufferDelete(handle->ring_buffer);
        free(handle->stream_url);
        free(handle);
        return ESP_ERR_NO_MEM;
    }

    // FIX #6: Créer mutex pour stats
    handle->stats_mutex = xSemaphoreCreateMutex();
    if (handle->stats_mutex == NULL) {
        ESP_LOGE(TAG, "Échec de création du mutex stats");
        vSemaphoreDelete(handle->download_semaphore);
        vRingbufferDelete(handle->ring_buffer);
        free(handle->stream_url);
        free(handle);
        return ESP_ERR_NO_MEM;
    }

    // FIX: Créer sémaphores de synchronisation stop
    handle->fetch_done = xSemaphoreCreateBinary();
    handle->play_done = xSemaphoreCreateBinary();
    if (handle->fetch_done == NULL || handle->play_done == NULL) {
        ESP_LOGE(TAG, "Échec de création des sémaphores done");
        if (handle->fetch_done) vSemaphoreDelete(handle->fetch_done);
        if (handle->play_done) vSemaphoreDelete(handle->play_done);
        vSemaphoreDelete(handle->stats_mutex);
        vSemaphoreDelete(handle->download_semaphore);
        vRingbufferDelete(handle->ring_buffer);
        free(handle->stream_url);
        free(handle);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Player HLS créé avec succès");

    *out = handle;
    return ESP_OK;
}

esp_err_t app_hls_player_start(app_hls_player_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (handle->fetch_task != NULL || handle->play_task != NULL) {
        ESP_LOGW(TAG, "Le player est déjà démarré");
        return ESP_ERR_INVALID_STATE;
    }

    // Vider sémaphores done AVANT création tâches (évite races si stop appelé pendant start)
    xSemaphoreTake(handle->fetch_done, 0);
    xSemaphoreTake(handle->play_done, 0);

    // Vider ring buffer pour garantir démarrage propre (pas de données résiduelles)
    if (handle->ring_buffer) {
        size_t item_size = 0;
        void *item;
        while ((item = xRingbufferReceive(handle->ring_buffer, &item_size, 0)) != NULL) {
            vRingbufferReturnItem(handle->ring_buffer, item);
        }
    }

    // Réinitialiser états avant démarrage
    handle->bytes_downloaded = 0;
    handle->is_downloading = false;
    handle->target_duration = 0;
    handle->drop_count = 0;
    handle->drop_old_count = 0;
    handle->drop_recover_count = 0;
    handle->bad_item_size_count = 0;
    handle->resync_notif_guard = 0;
    handle->ts_carry_len = 0;  // Reset carry pour alignement TS

    // Signal initial pour déclencher le premier téléchargement
    xSemaphoreGive(handle->download_semaphore);

    // Créer les tâches
    // Core pinning : fetch sur CPU0 (Wi-Fi/TLS), play sur CPU1 (isolation audio)
    // NOTE: xTaskCreatePinnedToCore() attend usStackDepth en WORDS (pas bytes)
    // Sur Xtensa: 1 word = 4 bytes, donc 14336 words = 57 KB, 6144 words = 24 KB
    // IMPORTANT: Vérifier sizeof(StackType_t) dans les logs HWM pour confirmation
    BaseType_t ret = xTaskCreatePinnedToCore(hls_fetch_task, "hls_fetch", 14336, handle, 5, &handle->fetch_task, 0);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Échec de création de la task de téléchargement");
        return ESP_FAIL;
    }

    ret = xTaskCreatePinnedToCore(hls_audio_play_task, "audio_play", 6144, handle, 8, &handle->play_task, 1);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Échec de création de la task de lecture");
        // FIX CRITIQUE: Ne pas vTaskDelete brutal (peut laisser stack HTTP sale)
        // Utiliser même logique que stop() : notify NOTIF_STOP + attendre fetch_done
        if (handle->fetch_task) {
            xTaskNotify(handle->fetch_task, NOTIF_STOP, eSetBits);
        }
        if (handle->download_semaphore) {
            xSemaphoreGive(handle->download_semaphore);  // Débloquer fetch_task
        }
        // Attendre terminaison propre de fetch_task
        if (handle->fetch_done) {
            xSemaphoreTake(handle->fetch_done, pdMS_TO_TICKS(5000));
        }
        handle->fetch_task = NULL;
        handle->play_task = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Player HLS démarré");
    return ESP_OK;
}

esp_err_t app_hls_player_stop(app_hls_player_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (handle->fetch_task == NULL && handle->play_task == NULL) {
        return ESP_OK;  // Déjà arrêté
    }

    ESP_LOGI(TAG, "Arrêt du player HLS...");
    handle->is_downloading = false;

    // Envoyer NOTIF_STOP aux deux tâches
    if (handle->fetch_task) {
        xTaskNotify(handle->fetch_task, NOTIF_STOP, eSetBits);
    }
    if (handle->play_task) {
        xTaskNotify(handle->play_task, NOTIF_STOP, eSetBits);
    }

    // Débloquer la tâche de téléchargement
    if (handle->download_semaphore) {
        xSemaphoreGive(handle->download_semaphore);
    }

    // Attendre la terminaison des tâches via sémaphores done
    if (handle->fetch_task) {
        ESP_LOGI(TAG, "Attente terminaison fetch_task...");
        if (xSemaphoreTake(handle->fetch_done, pdMS_TO_TICKS(5000)) != pdTRUE) {
            ESP_LOGW(TAG, "Timeout attente fetch_task");
        }
        handle->fetch_task = NULL;
    }

    if (handle->play_task) {
        ESP_LOGI(TAG, "Attente terminaison play_task...");
        if (xSemaphoreTake(handle->play_done, pdMS_TO_TICKS(5000)) != pdTRUE) {
            ESP_LOGW(TAG, "Timeout attente play_task");
        }
        handle->play_task = NULL;
    }

    ESP_LOGI(TAG, "Player HLS arrêté");
    return ESP_OK;
}

esp_err_t app_hls_player_get_stats(app_hls_player_t *handle, app_hls_player_stats_t *stats)
{
    if (handle == NULL || stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Protection concurrent access
    if (handle->stats_mutex) {
        xSemaphoreTake(handle->stats_mutex, portMAX_DELAY);
        stats->bytes_downloaded = handle->bytes_downloaded;
        xSemaphoreGive(handle->stats_mutex);
    } else {
        stats->bytes_downloaded = handle->bytes_downloaded;
    }

    stats->is_playing = (handle->fetch_task != NULL) || (handle->play_task != NULL);

    if (handle->ring_buffer) {
        // FIX #9: Calcul correct du niveau de buffer
        size_t free_size = xRingbufferGetCurFreeSize(handle->ring_buffer);
        size_t filled_size = handle->buffer_size - free_size;
        stats->buffer_fill_percent = (filled_size * 100) / handle->buffer_size;
    } else {
        stats->buffer_fill_percent = 0;
    }

    return ESP_OK;
}

esp_err_t app_hls_player_del(app_hls_player_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Arrêter si nécessaire (avec synchronisation)
    if (handle->fetch_task != NULL || handle->play_task != NULL) {
        app_hls_player_stop(handle);
    }

    // Libérer les ressources
    if (handle->ring_buffer) {
        vRingbufferDelete(handle->ring_buffer);
    }

    if (handle->download_semaphore) {
        vSemaphoreDelete(handle->download_semaphore);
    }

    // FIX #6: Libérer mutex stats
    if (handle->stats_mutex) {
        vSemaphoreDelete(handle->stats_mutex);
    }

    // FIX: Libérer sémaphores done
    if (handle->fetch_done) {
        vSemaphoreDelete(handle->fetch_done);
    }
    if (handle->play_done) {
        vSemaphoreDelete(handle->play_done);
    }

    if (handle->stream_url) {
        free(handle->stream_url);
    }

    free(handle);

    ESP_LOGI(TAG, "Player HLS détruit");
    return ESP_OK;
}
