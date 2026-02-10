/**
 * @file app_hls_player.c
 * @brief Implémentation du player HLS
 */

#include "app_hls_player/app_hls_player.h"
#include "lib_m3u8_parser/lib_m3u8_parser.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "simple_dec/esp_audio_simple_dec.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "app_hls_player";

// Buffer pour HTTP download (16KB)
#define HTTP_BUFFER_SIZE (16 * 1024)

/**
 * @brief Structure interne du player HLS
 */
struct app_hls_player_s {
    char *stream_url;                       /**< URL du stream (copie allouée) */
    RingbufHandle_t ring_buffer;            /**< Buffer circulaire pour données encodées */
    size_t buffer_size;                     /**< Taille du buffer */
    TaskHandle_t fetch_task;                /**< Tâche de téléchargement */
    TaskHandle_t play_task;                 /**< Tâche de lecture/décodage */
    volatile bool running;                  /**< FIX: volatile pour thread-safety (lu/écrit depuis 2 tasks) */
    size_t bytes_downloaded;                /**< Statistiques: bytes téléchargés */
    SemaphoreHandle_t download_semaphore;   /**< Sémaphore pour contrôle téléchargement */
    SemaphoreHandle_t stats_mutex;          /**< Mutex pour bytes_downloaded */
    SemaphoreHandle_t fetch_done;           /**< FIX: Sémaphore signalant fin fetch_task */
    SemaphoreHandle_t play_done;            /**< FIX: Sémaphore signalant fin play_task */
    volatile bool is_downloading;           /**< Flag téléchargement en cours */
    app_hls_player_write_cb_t write_cb;     /**< Callback d'écriture audio */
    void *write_ctx;                        /**< Contexte utilisateur pour callback */
    int target_duration;                    /**< FIX #11: Target duration pour timeout refresh */
    volatile bool needs_decoder_reset;      /**< FIX #14: Flag reset décodeur sur DISCONTINUITY */
    uint32_t drop_count;                    /**< Compteur pertes ring buffer (rate limit log) */
};

/**
 * @brief Attente interruptible (check running toutes les 100ms max)
 */
static void interruptible_delay_ms(app_hls_player_t *handle, uint32_t ms)
{
    const uint32_t step = 100;
    while (ms > 0 && handle->running) {
        uint32_t this_step = (ms > step) ? step : ms;
        vTaskDelay(pdMS_TO_TICKS(this_step));
        ms -= this_step;
    }
}

/**
 * @brief Callback HTTP pour recevoir les données
 * FIX #1: Accepte chunked ET non-chunked
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    app_hls_player_t *handle = (app_hls_player_t *)evt->user_data;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            // FIX #1: Suppression du check is_chunked - accepter toutes les réponses
            if (evt->data_len > 0 && handle->ring_buffer) {
                // FIX: Timeout 0 (non-blocking) pour ne jamais bloquer la stack HTTP
                if (xRingbufferSend(handle->ring_buffer, evt->data, evt->data_len, 0) != pdTRUE) {
                    // FIX: Rate limit log 1/100 (compteur dans handle pour multi-instance)
                    if ((++handle->drop_count % 100) == 0) {
                        ESP_LOGW(TAG, "Ring buffer plein, données perdues (x%u)", (unsigned)handle->drop_count);
                    }
                } else {
                    // FIX #6: Protection concurrent access
                    if (handle->stats_mutex) {
                        xSemaphoreTake(handle->stats_mutex, portMAX_DELAY);
                        handle->bytes_downloaded += evt->data_len;
                        xSemaphoreGive(handle->stats_mutex);
                    }
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

/**
 * @brief Télécharge un segment audio dans le ring buffer
 */
static esp_err_t download_segment(app_hls_player_t *handle, const char *url)
{
    ESP_LOGI(TAG, "Téléchargement: %s", url);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = handle,
        .buffer_size = HTTP_BUFFER_SIZE,
        .timeout_ms = 5000,  // FIX: 5s pour aligner avec timeout stop (évite timeout warnings)
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Échec d'initialisation du client HTTP");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        int length = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "HTTP Status=%d, Length=%d", status, length);

        // FIX: Traiter status != 200/206 comme erreur (évite dérive silencieuse)
        if (status != 200 && status != 206) {
            // FIX: Logger Location si redirection (debug terrain)
            if (status >= 300 && status < 400) {
                char *location = NULL;
                esp_http_client_get_header(client, "Location", &location);
                ESP_LOGW(TAG, "HTTP redirection %d → %s (non suivie)", status,
                         location ? location : "unknown");
            } else {
                ESP_LOGE(TAG, "HTTP status inattendu: %d (échec)", status);
            }
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "Erreur HTTP: %s", esp_err_to_name(err));
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err;
}

/**
 * @brief Télécharge la playlist M3U8 et retourne son contenu
 * FIX #5: Lecture correcte avec to_read calculé
 */
static char* download_m3u8(const char *url)
{
    ESP_LOGI(TAG, "Téléchargement M3U8: %s", url);

    size_t free_heap = esp_get_free_heap_size();
    size_t min_heap = esp_get_minimum_free_heap_size();
    ESP_LOGI(TAG, "Heap avant M3U8: libre=%u, min=%u", free_heap, min_heap);

    // FIX: Realloc progressif pour éviter overflow si chunked > 16KB
    // Allocation initiale 16KB, croissance par blocs de 16KB, max 64KB
    const size_t M3U8_INITIAL_SIZE = 16 * 1024;
    const size_t M3U8_GROW_SIZE = 16 * 1024;
    const size_t M3U8_MAX_SIZE = 64 * 1024;

    size_t buffer_capacity = M3U8_INITIAL_SIZE;
    char *buffer = malloc(buffer_capacity);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Échec d'allocation pour M3U8 (%u bytes)", (unsigned)buffer_capacity);
        return NULL;
    }

    int offset = 0;
    bool truncated = false;

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,  // FIX: 5s pour aligner avec timeout stop (évite timeout warnings)
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(buffer);
        return NULL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Échec d'ouverture HTTP: %s", esp_err_to_name(err));
        // FIX: Pas de close() si open() a échoué
        esp_http_client_cleanup(client);
        free(buffer);
        return NULL;
    }

    int content_length = esp_http_client_fetch_headers(client);

    // Si content-length connu et > capacité initiale, realloc immédiatement
    if (content_length > 0 && (size_t)content_length >= buffer_capacity) {
        size_t new_capacity = content_length + 1;  // +1 pour '\0'
        if (new_capacity > M3U8_MAX_SIZE) {
            ESP_LOGE(TAG, "M3U8 trop grand: %d bytes (max %u)", content_length, (unsigned)M3U8_MAX_SIZE);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            free(buffer);
            return NULL;
        }
        char *new_buffer = realloc(buffer, new_capacity);
        if (new_buffer == NULL) {
            ESP_LOGE(TAG, "Échec realloc M3U8 à %u bytes", (unsigned)new_capacity);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            free(buffer);
            return NULL;
        }
        buffer = new_buffer;
        buffer_capacity = new_capacity;
        ESP_LOGI(TAG, "M3U8 realloc à %u bytes (content-length=%d)", (unsigned)buffer_capacity, content_length);
    } else if (content_length < 0) {
        ESP_LOGW(TAG, "M3U8 content-length inconnu (chunked), realloc progressif activé");
    } else {
        ESP_LOGI(TAG, "M3U8 content-length: %d bytes", content_length);
    }

    // Lecture avec realloc progressif si nécessaire
    // FIX: zero_read_streak pour gérer read_len==0 temporaire (certaines stacks/proxy)
    int zero_read_streak = 0;
    bool expect_length = (content_length > 0);  // FIX: Détecter EOF normal vs incomplet

    while (offset < (int)buffer_capacity - 1) {
        int to_read = (buffer_capacity - 1) - offset;
        int read_len = esp_http_client_read(client, buffer + offset, to_read);

        if (read_len > 0) {
            offset += read_len;
            zero_read_streak = 0;  // Reset streak sur succès
        } else if (read_len == 0) {
            // FIX: read==0 peut être temporaire (pas forcément EOF)
            if (++zero_read_streak > 5) {  // ~5 tentatives
                ESP_LOGW(TAG, "M3U8: read_len==0 persistant (%d tentatives), fin lecture", zero_read_streak);
                // FIX: Si content_length connu et offset < attendu → potentiellement incomplet
                if (!expect_length || offset < content_length) {
                    if (expect_length) {
                        ESP_LOGW(TAG, "M3U8: playlist incomplète (attendu: %d, reçu: %d)",
                                 content_length, offset);
                    } else {
                        ESP_LOGW(TAG, "M3U8: playlist potentiellement incomplète (chunked, reçu: %d)", offset);
                    }
                    truncated = true;
                }
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));  // Petit délai avant retry
            continue;
        } else {
            // read_len < 0 : erreur HTTP
            ESP_LOGE(TAG, "M3U8: erreur read_len=%d", read_len);
            truncated = true;
            break;
        }

        // Si buffer plein et lecture continue, realloc
        if (offset >= (int)buffer_capacity - 1) {
            if (buffer_capacity >= M3U8_MAX_SIZE) {
                ESP_LOGE(TAG, "M3U8 atteint limite max %u bytes, playlist tronquée (échec)", (unsigned)M3U8_MAX_SIZE);
                truncated = true;
                break;
            }

            size_t new_capacity = buffer_capacity + M3U8_GROW_SIZE;
            if (new_capacity > M3U8_MAX_SIZE) {
                new_capacity = M3U8_MAX_SIZE;
            }

            char *new_buffer = realloc(buffer, new_capacity);
            if (new_buffer == NULL) {
                ESP_LOGE(TAG, "Échec realloc progressif à %u bytes", (unsigned)new_capacity);
                truncated = true;  // FIX: Forcer NULL si realloc échoue
                break;
            }
            buffer = new_buffer;
            buffer_capacity = new_capacity;
            ESP_LOGI(TAG, "M3U8 realloc progressif: %u bytes", (unsigned)buffer_capacity);
        }
    }

    // FIX: Sécuriser offset avant write (défense contre évolution future du while)
    if (offset < 0) {
        offset = 0;
    }
    if ((size_t)offset >= buffer_capacity) {
        truncated = true;
        offset = (int)buffer_capacity - 1;
    }
    buffer[offset] = '\0';

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    // FIX: Ne pas retourner une playlist tronquée (parsing échouera aléatoirement)
    if (offset == 0 || truncated) {
        free(buffer);
        return NULL;
    }

    ESP_LOGI(TAG, "M3U8 téléchargé: %d bytes (capacité: %u)", offset, (unsigned)buffer_capacity);
    return buffer;
}

/**
 * @brief Tâche de téléchargement HLS
 */
static void hls_fetch_task(void *pvParameters)
{
    app_hls_player_t *handle = (app_hls_player_t *)pvParameters;
    ESP_LOGI(TAG, "Démarrage de la task de téléchargement HLS");

    int64_t last_sequence_number = -1;

    while (handle->running) {
        // FIX #11: Timeout périodique basé sur target_duration (RFC 8216 Section 6.3.4)
        // "client MUST wait for at least the target duration before attempting to reload"
        uint32_t refresh_timeout = (handle->target_duration > 0)
                                   ? (handle->target_duration * 1000)  // En ms
                                   : 10000;  // Fallback 10s si pas encore connu

        if (xSemaphoreTake(handle->download_semaphore, pdMS_TO_TICKS(refresh_timeout)) != pdTRUE) {
            // Timeout atteint → forcer refresh même si buffer OK (pour live)
            ESP_LOGI(TAG, "Timeout refresh (%u ms) - vérification nouveaux segments", refresh_timeout);
            if (!handle->running) {
                break;
            }
            // Continuer vers download playlist (pas de continue ici)
        } else {
            ESP_LOGI(TAG, "Signal reçu - démarrage du téléchargement");
        }

        if (!handle->running) {
            break;
        }

        handle->is_downloading = true;

        char *m3u8_content = download_m3u8(handle->stream_url);
        if (m3u8_content == NULL) {
            ESP_LOGE(TAG, "Échec de téléchargement M3U8");
            handle->is_downloading = false;
            // FIX: Attente interruptible de 5s
            interruptible_delay_ms(handle, 5000);
            continue;
        }

        lib_m3u8_parser_playlist_t playlist;
        esp_err_t ret = lib_m3u8_parser_parse(m3u8_content, handle->stream_url, &playlist);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Échec de parsing M3U8");
            free(m3u8_content);
            handle->is_downloading = false;
            // FIX: Attente interruptible de 5s
            interruptible_delay_ms(handle, 5000);
            continue;
        }

        free(m3u8_content);

        // FIX #11: Stocker target_duration pour timeout refresh périodique
        if (playlist.target_duration > 0) {
            handle->target_duration = playlist.target_duration;
        }

        lib_m3u8_parser_dump(&playlist);

        // FIX #2: Gestion master playlist avec logique déterministe
        // Utilise maintenant variant_count au lieu de segment_count
        if (playlist.is_master_playlist && playlist.variant_count > 0) {
            ESP_LOGI(TAG, "Master playlist détectée, sélection de la meilleure qualité...");

            // Politique: midfi > hifi > lofi (par nom OU par bandwidth)
            int idx_selected = -1;
            int64_t best_bandwidth = 0;

            // D'abord chercher par nom (France Inter)
            int idx_hifi = -1, idx_midfi = -1, idx_lofi = -1;
            for (int i = 0; i < playlist.variant_count; i++) {
                const char *u = playlist.variants[i].url;
                if (strstr(u, "_hifi.m3u8")) {
                    idx_hifi = i;
                } else if (strstr(u, "_midfi.m3u8")) {
                    idx_midfi = i;
                } else if (strstr(u, "_lofi.m3u8")) {
                    idx_lofi = i;
                }
            }

            // Sélection par nom (préféré)
            if (idx_midfi >= 0) {
                idx_selected = idx_midfi;
                ESP_LOGI(TAG, "Qualité MIDFI sélectionnée (~128 kbps)");
            } else if (idx_hifi >= 0) {
                idx_selected = idx_hifi;
                ESP_LOGI(TAG, "Qualité HIFI sélectionnée (~192-320 kbps)");
            } else if (idx_lofi >= 0) {
                idx_selected = idx_lofi;
                ESP_LOGI(TAG, "Qualité LOFI sélectionnée (~64 kbps)");
            } else {
                // Fallback: sélectionner par bandwidth (milieu de gamme)
                // Chercher bandwidth entre 96kbps et 160kbps si possible
                for (int i = 0; i < playlist.variant_count; i++) {
                    int64_t bw = playlist.variants[i].bandwidth;
                    if (bw >= 96000 && bw <= 160000) {
                        if (idx_selected < 0 || bw > best_bandwidth) {
                            idx_selected = i;
                            best_bandwidth = bw;
                        }
                    }
                }

                // Sinon prendre le premier
                if (idx_selected < 0) {
                    idx_selected = 0;
                }

                ESP_LOGI(TAG, "Variant %d sélectionné (bandwidth=%lld bps)",
                         idx_selected, (long long)playlist.variants[idx_selected].bandwidth);
            }

            char media_url[LIB_M3U8_PARSER_MAX_URL_LEN];
            strncpy(media_url, playlist.variants[idx_selected].url, LIB_M3U8_PARSER_MAX_URL_LEN - 1);
            media_url[LIB_M3U8_PARSER_MAX_URL_LEN - 1] = '\0';

            m3u8_content = download_m3u8(media_url);
            if (m3u8_content == NULL) {
                ESP_LOGE(TAG, "Échec de téléchargement de la media playlist");
                lib_m3u8_parser_free(&playlist);
                handle->is_downloading = false;
                // FIX: Attente interruptible de 5s
                interruptible_delay_ms(handle, 5000);
                continue;
            }

            lib_m3u8_parser_free(&playlist);
            ret = lib_m3u8_parser_parse(m3u8_content, media_url, &playlist);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Échec de parsing de la media playlist");
                free(m3u8_content);
                handle->is_downloading = false;
                // FIX: Attente interruptible de 5s
                interruptible_delay_ms(handle, 5000);
                continue;
            }

            free(m3u8_content);
            lib_m3u8_parser_dump(&playlist);
        }

        // FIX #4: Télécharger segments dans l'ordre chronologique
        // FIX CRITIQUE: Le décodeur TS attend les segments dans l'ordre temporel
        bool downloaded = false;

        // FIX #13: Cold start avec buffer initial plus grand
        // RFC recommande 3× target_duration au démarrage pour stabilité
        const int segments_per_cycle = (last_sequence_number < 0) ? 4 : 2;

        // FIX: Log cold start pour faciliter debug terrain
        if (last_sequence_number < 0) {
            ESP_LOGI(TAG, "Cold start: téléchargement de %d segments initiaux", segments_per_cycle);
        }

        // Phase 1: Collecter indices des N segments les plus récents non téléchargés
        int to_download[8];  // Max 8 segments (largement suffisant pour cold start)
        int to_download_count = 0;

        for (int i = playlist.segment_count - 1; i >= 0 && to_download_count < segments_per_cycle; i--) {
            if (playlist.segments[i].sequence > last_sequence_number) {
                to_download[to_download_count++] = i;
            }
        }

        // Phase 2: Télécharger dans l'ordre inverse = ordre chronologique croissant
        for (int k = to_download_count - 1; k >= 0 && handle->running; k--) {
            int idx = to_download[k];
            const lib_m3u8_parser_segment_t *seg = &playlist.segments[idx];

            ESP_LOGI(TAG, "Téléchargement segment %lld (%d/%d)%s",
                     (long long)seg->sequence, (to_download_count - k), to_download_count,
                     seg->discontinuity ? " [DISCONTINUITY]" : "");

            // FIX #14: Signaler DISCONTINUITY pour reset décodeur
            if (seg->discontinuity) {
                ESP_LOGW(TAG, "DISCONTINUITY détectée → flag reset décodeur");
                handle->needs_decoder_reset = true;
            }

            esp_err_t err = download_segment(handle, seg->url);
            if (err == ESP_OK) {
                // Mettre à jour avec le segment le plus récent téléchargé
                if (seg->sequence > last_sequence_number) {
                    last_sequence_number = seg->sequence;
                }
                downloaded = true;
                ESP_LOGI(TAG, "Segment %lld OK (%d/%d téléchargés)",
                         (long long)seg->sequence, (to_download_count - k), to_download_count);
            } else {
                ESP_LOGE(TAG, "Échec téléchargement segment %lld", (long long)seg->sequence);
                break;
            }
        }

        // FIX #12: Détection décrochage et resynchronisation
        // Si aucun segment téléchargé alors que la playlist en contient, vérifier si on est trop en retard
        if (!downloaded && playlist.segment_count > 0 && last_sequence_number >= 0) {
            // Vérifier si tous les segments disponibles ont été skippés
            int64_t oldest_in_playlist = playlist.segments[0].sequence;
            int64_t newest_in_playlist = playlist.segments[playlist.segment_count - 1].sequence;

            // Décrochage détecté : notre dernier segment est plus vieux que le plus ancien disponible
            if (oldest_in_playlist > last_sequence_number + 1) {
                ESP_LOGW(TAG, "DÉCROCHAGE DÉTECTÉ: last_seq=%lld, fenêtre=[%lld..%lld]",
                         (long long)last_sequence_number,
                         (long long)oldest_in_playlist,
                         (long long)newest_in_playlist);
                ESP_LOGW(TAG, "→ RESYNC: saut vers début de fenêtre actuelle");

                // Resync : repositionner juste avant le segment le plus ancien disponible
                // Au prochain cycle, on téléchargera depuis oldest_in_playlist
                last_sequence_number = oldest_in_playlist - 1;

                // Forcer un nouveau cycle immédiatement pour télécharger les segments récents
                lib_m3u8_parser_free(&playlist);
                handle->is_downloading = false;
                xSemaphoreGive(handle->download_semaphore);  // Trigger immédiat
                continue;
            }

            ESP_LOGD(TAG, "Aucun nouveau segment (dernier: %lld)", (long long)last_sequence_number);
        }

        lib_m3u8_parser_free(&playlist);
        handle->is_downloading = false;
    }

    ESP_LOGI(TAG, "Arrêt de la task de téléchargement HLS");

    // FIX: Signaler fin de tâche via sémaphore (guard pour robustesse future)
    if (handle->fetch_done) {
        xSemaphoreGive(handle->fetch_done);
    }
    vTaskDelete(NULL);
}

/**
 * @brief Tâche de lecture audio
 */
static void audio_play_task(void *pvParameters)
{
    app_hls_player_t *handle = (app_hls_player_t *)pvParameters;
    ESP_LOGI(TAG, "Démarrage de la task de lecture audio");

    // Créer le décodeur TS
    esp_audio_simple_dec_handle_t dec_handle = NULL;
    esp_audio_simple_dec_cfg_t dec_cfg = {
        .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_TS,
        .dec_cfg = NULL,
        .cfg_size = 0,
        .use_frame_dec = false,
    };

    esp_audio_err_t dec_ret = esp_audio_simple_dec_open(&dec_cfg, &dec_handle);
    if (dec_ret != ESP_AUDIO_ERR_OK || dec_handle == NULL) {
        ESP_LOGE(TAG, "Échec de création du décodeur TS: %d", dec_ret);
        if (handle->play_done) {
            xSemaphoreGive(handle->play_done);
        }
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Décodeur TS créé avec succès");

    const size_t ENC_BUF_SIZE = 147456;  // 144KB
    const size_t DEC_BUF_SIZE = 16384;   // 16KB

    uint8_t *encoded_buffer = malloc(ENC_BUF_SIZE);
    int16_t *decoded_buffer = malloc(DEC_BUF_SIZE);

    if (encoded_buffer == NULL || decoded_buffer == NULL) {
        ESP_LOGE(TAG, "Échec d'allocation des buffers de décodage");
        esp_audio_simple_dec_close(dec_handle);
        free(encoded_buffer);
        free(decoded_buffer);
        if (handle->play_done) {
            xSemaphoreGive(handle->play_done);
        }
        vTaskDelete(NULL);
        return;
    }

    size_t buffered_size = 0;

    ESP_LOGI(TAG, "Démarrage de la lecture audio...");
    vTaskDelay(pdMS_TO_TICKS(500));

    int decode_count = 0;  // FIX: non-static pour reset à chaque start
    int error_count = 0;   // FIX: non-static pour reset à chaque start
    bool download_signaled = false;
    bool was_downloading = false;

    while (handle->running) {
        // FIX #9: Calcul correct du niveau de buffer
        size_t free_size = xRingbufferGetCurFreeSize(handle->ring_buffer);
        size_t filled_size = handle->buffer_size - free_size;
        int buffer_level = (filled_size * 100) / handle->buffer_size;

        // FIX #14: Gestion DISCONTINUITY - reset complet décodeur + flush buffers
        if (handle->needs_decoder_reset) {
            ESP_LOGW(TAG, "Reset décodeur suite DISCONTINUITY");
            // FIX: Clear flag immédiatement pour éviter double reset si DISC arrive pendant reset
            handle->needs_decoder_reset = false;

            // Vider le ring buffer pour éviter données corrompues
            size_t item_size = 0;
            void *item;
            while ((item = xRingbufferReceive(handle->ring_buffer, &item_size, 0)) != NULL) {
                vRingbufferReturnItem(handle->ring_buffer, item);
            }

            // Reset buffer encodé local
            buffered_size = 0;

            // FIX: Recréer le décodeur pour reset état interne (PES, PAT/PMT, timestamps)
            ESP_LOGI(TAG, "Fermeture décodeur...");
            esp_audio_simple_dec_close(dec_handle);

            ESP_LOGI(TAG, "Réouverture décodeur...");
            dec_ret = esp_audio_simple_dec_open(&dec_cfg, &dec_handle);
            if (dec_ret != ESP_AUDIO_ERR_OK || dec_handle == NULL) {
                ESP_LOGE(TAG, "Échec réouverture décodeur après DISCONTINUITY: %d", dec_ret);
                // Arrêt fatal, la tâche ne peut pas continuer
                if (handle->play_done) {
                    xSemaphoreGive(handle->play_done);
                }
                vTaskDelete(NULL);
                return;
            }

            ESP_LOGI(TAG, "Décodeur recréé avec succès");
        }

        if (was_downloading && !handle->is_downloading) {
            download_signaled = false;
            ESP_LOGD(TAG, "Téléchargement terminé, signal réinitialisé");
        }
        was_downloading = handle->is_downloading;

        if (buffer_level < 40 && !download_signaled && !handle->is_downloading) {
            ESP_LOGI(TAG, "Buffer bas (%d%%) - signal pour télécharger", buffer_level);
            xSemaphoreGive(handle->download_semaphore);
            download_signaled = true;
        } else if (buffer_level >= 60) {
            download_signaled = false;
        }

        // Lire les données encodées
        if (buffered_size < ENC_BUF_SIZE / 4) {
            size_t item_size = 0;
            uint8_t *item = (uint8_t *)xRingbufferReceive(handle->ring_buffer, &item_size, pdMS_TO_TICKS(100));

            if (item != NULL) {
                size_t space_left = ENC_BUF_SIZE - buffered_size;
                size_t to_copy = (item_size < space_left) ? item_size : space_left;

                if (to_copy < item_size) {
                    ESP_LOGW(TAG, "PERTE: %zu bytes perdus (buffer plein: %zu/%d)",
                             item_size - to_copy, buffered_size, ENC_BUF_SIZE);
                }

                memcpy(encoded_buffer + buffered_size, item, to_copy);
                buffered_size += to_copy;

                vRingbufferReturnItem(handle->ring_buffer, item);
            }
        }

        if (buffered_size == 0) {
            ESP_LOGW(TAG, "Buffer vide, attente...");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Décoder
        esp_audio_simple_dec_raw_t raw = {
            .buffer = encoded_buffer,
            .len = buffered_size,
            .eos = false,
            .consumed = 0,
        };

        esp_audio_simple_dec_out_t out_frame = {
            .buffer = (uint8_t *)decoded_buffer,
            .len = DEC_BUF_SIZE,
            .decoded_size = 0,
        };

        dec_ret = esp_audio_simple_dec_process(dec_handle, &raw, &out_frame);

        if (decode_count++ < 20 || dec_ret != ESP_AUDIO_ERR_OK) {
            ESP_LOGI(TAG, "Décodage #%d: ret=%d, in=%zu, consumed=%lu, out=%lu",
                     decode_count, dec_ret, buffered_size, raw.consumed, out_frame.decoded_size);
        }

        if (raw.consumed > 0) {
            if (raw.consumed < buffered_size) {
                memmove(encoded_buffer, encoded_buffer + raw.consumed, buffered_size - raw.consumed);
            }
            buffered_size -= raw.consumed;
        }

        if (dec_ret == ESP_AUDIO_ERR_OK && out_frame.decoded_size > 0) {
            // Réduction de volume logicielle (25%)
            int16_t *samples = (int16_t *)decoded_buffer;
            size_t num_samples = out_frame.decoded_size / sizeof(int16_t);
            for (size_t i = 0; i < num_samples; i++) {
                samples[i] = samples[i] / 4;
            }

            // FIX #10: Timeout court au lieu de portMAX_DELAY pour permettre arrêt propre
            // FIX: Timeout en ticks (pas en ms) pour compatibilité I2S/stream
            // FIX: 200ms pour stop réactif (au lieu de 1s)
            size_t bytes_written = 0;
            uint32_t timeout = handle->running ? pdMS_TO_TICKS(200) : 0;  // 200ms en ticks si running, 0 sinon
            esp_err_t err = handle->write_cb(handle->write_ctx, decoded_buffer,
                                             out_frame.decoded_size, &bytes_written,
                                             timeout);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Erreur d'écriture audio: %s", esp_err_to_name(err));
            }

            if (decode_count % 100 == 0) {
                ESP_LOGI(TAG, "Audio: %zu bytes PCM (#%d, buf=%zu)", bytes_written, decode_count, buffered_size);
            }
        } else if (dec_ret != ESP_AUDIO_ERR_OK) {
            // FIX #8: Resync TS uniquement (pas AAC car on décode du TS)
            if (++error_count % 50 == 0) {
                ESP_LOGW(TAG, "Erreur décodage: ret=%d, consumed=%lu, decoded=%lu (count=%d)",
                         dec_ret, raw.consumed, out_frame.decoded_size, error_count);
            }

            // Resynchronisation TS (0x47 à intervalle 188)
            bool found_sync = false;
            for (size_t i = 1; i < buffered_size - 188; i++) {
                if (encoded_buffer[i] == 0x47 && encoded_buffer[i + 188] == 0x47) {
                    memmove(encoded_buffer, encoded_buffer + i, buffered_size - i);
                    buffered_size -= i;
                    found_sync = true;
                    if (error_count % 50 == 0) {
                        ESP_LOGI(TAG, "Resync TS trouvé à offset %zu", i);
                    }
                    break;
                }
            }

            if (!found_sync) {
                if (buffered_size > 2048) {
                    buffered_size -= 2048;
                    memmove(encoded_buffer, encoded_buffer + 2048, buffered_size);
                } else {
                    buffered_size = 0;
                }
            }
        }
    }

    // Cleanup
    esp_audio_simple_dec_close(dec_handle);
    free(encoded_buffer);
    free(decoded_buffer);

    ESP_LOGI(TAG, "Arrêt de la task de lecture audio");

    // FIX: Signaler fin de tâche via sémaphore (guard pour robustesse future)
    if (handle->play_done) {
        xSemaphoreGive(handle->play_done);
    }
    vTaskDelete(NULL);
}

// ============================================================================
// API Publique
// ============================================================================

esp_err_t app_hls_player_config_init(app_hls_player_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(config, 0, sizeof(app_hls_player_config_t));
    config->buffer_size = 100 * 1024;  // 100 KB par défaut

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

    handle->buffer_size = config->buffer_size;
    handle->write_cb = config->write_cb;
    handle->write_ctx = config->write_ctx;
    handle->running = false;

    // Créer le ring buffer
    handle->ring_buffer = xRingbufferCreate(config->buffer_size, RINGBUF_TYPE_BYTEBUF);
    if (handle->ring_buffer == NULL) {
        ESP_LOGE(TAG, "Échec de création du ring buffer");
        free(handle->stream_url);
        free(handle);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Ring buffer créé: %d bytes", config->buffer_size);

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

    if (handle->running) {
        ESP_LOGW(TAG, "Le player est déjà démarré");
        return ESP_ERR_INVALID_STATE;
    }

    // FIX: Vider sémaphores done AVANT running=true (évite races si stop appelé pendant start)
    xSemaphoreTake(handle->fetch_done, 0);
    xSemaphoreTake(handle->play_done, 0);

    // FIX: Vider ring buffer pour garantir silence au démarrage (pas de données résiduelles)
    if (handle->ring_buffer) {
        size_t item_size = 0;
        void *item;
        while ((item = xRingbufferReceive(handle->ring_buffer, &item_size, 0)) != NULL) {
            vRingbufferReturnItem(handle->ring_buffer, item);
        }
    }

    // FIX: Réinitialiser états avant démarrage
    handle->bytes_downloaded = 0;
    handle->is_downloading = false;
    handle->needs_decoder_reset = false;
    handle->target_duration = 0;  // Sera mis à jour par premier parsing
    handle->drop_count = 0;  // Reset compteur pertes ring buffer
    handle->running = true;

    // Signal initial pour déclencher le premier téléchargement
    xSemaphoreGive(handle->download_semaphore);

    // Créer les tâches
    BaseType_t ret = xTaskCreate(hls_fetch_task, "hls_fetch", 14336, handle, 5, &handle->fetch_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Échec de création de la task de téléchargement");
        handle->running = false;
        return ESP_FAIL;
    }

    ret = xTaskCreate(audio_play_task, "audio_play", 6144, handle, 8, &handle->play_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Échec de création de la task de lecture");
        handle->running = false;
        vTaskDelete(handle->fetch_task);
        handle->fetch_task = NULL;  // FIX: Cleanup pour stop/del idempotent
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

    if (!handle->running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Arrêt du player HLS...");
    handle->running = false;
    handle->is_downloading = false;  // FIX: Reset état download immédiatement

    // Débloquer la tâche de téléchargement
    if (handle->download_semaphore) {
        xSemaphoreGive(handle->download_semaphore);
    }

    // FIX: Attendre la terminaison des tâches via sémaphores done
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

    // FIX #6: Protection concurrent access
    if (handle->stats_mutex) {
        xSemaphoreTake(handle->stats_mutex, portMAX_DELAY);
        stats->bytes_downloaded = handle->bytes_downloaded;
        xSemaphoreGive(handle->stats_mutex);
    } else {
        stats->bytes_downloaded = 0;
    }

    stats->is_playing = handle->running;

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
    if (handle->running) {
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
