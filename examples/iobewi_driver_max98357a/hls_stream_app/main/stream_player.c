#include "stream_player.h"
#include "m3u8_parser.h"
#include "wifi_helper.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "simple_dec/esp_audio_simple_dec.h"
#include <string.h>

static const char *TAG = "stream_player";

// Structures internes
typedef struct {
    const char *stream_url;
    drv_max98357a_t *driver;
    RingbufHandle_t ring_buffer;
    size_t buffer_size;
    TaskHandle_t fetch_task;
    TaskHandle_t play_task;
    bool running;
    size_t bytes_downloaded;
    SemaphoreHandle_t download_semaphore;  // Sémaphore pour déclencher le téléchargement
    volatile bool is_downloading;           // Flag pour indiquer qu'un téléchargement est en cours
} stream_player_ctx_t;

static stream_player_ctx_t s_ctx = {0};

// Buffer pour HTTP download (16KB)
#define HTTP_BUFFER_SIZE (16 * 1024)

/**
 * @brief Callback HTTP pour recevoir les données
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // Écrire dans le ring buffer
                if (evt->data_len > 0 && s_ctx.ring_buffer) {
                    if (xRingbufferSend(s_ctx.ring_buffer, evt->data, evt->data_len, pdMS_TO_TICKS(1000)) != pdTRUE) {
                        ESP_LOGW(TAG, "Ring buffer plein, données perdues");
                    } else {
                        s_ctx.bytes_downloaded += evt->data_len;
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
static esp_err_t download_segment(const char *url)
{
    ESP_LOGI(TAG, "Téléchargement: %s", url);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .buffer_size = HTTP_BUFFER_SIZE,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,  // Utilise le bundle de certificats CA
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
    } else {
        ESP_LOGE(TAG, "Erreur HTTP: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

/**
 * @brief Télécharge la playlist M3U8 et retourne son contenu
 */
static char* download_m3u8(const char *url)
{
    ESP_LOGI(TAG, "Téléchargement M3U8: %s", url);

    // Log heap avant allocation
    size_t free_heap = esp_get_free_heap_size();
    size_t min_heap = esp_get_minimum_free_heap_size();
    ESP_LOGI(TAG, "Heap avant M3U8: libre=%u, min=%u", free_heap, min_heap);

    char *buffer = malloc(4 * 1024); // 4KB pour M3U8 (suffisant pour la plupart des playlists)
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Échec d'allocation pour M3U8 (4KB)");
        return NULL;
    }

    int offset = 0;

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,  // Utilise le bundle de certificats CA
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(buffer);
        return NULL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Échec d'ouverture HTTP: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        free(buffer);
        return NULL;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length > 4 * 1024) {
        ESP_LOGW(TAG, "M3U8 trop grand: %d bytes (max 4KB)", content_length);
    }

    while (offset < 4 * 1024 - 1) {
        int read_len = esp_http_client_read(client, buffer + offset, 4096);
        if (read_len <= 0) {
            break;
        }
        offset += read_len;
    }

    buffer[offset] = '\0';
    esp_http_client_cleanup(client);

    if (offset == 0) {
        free(buffer);
        return NULL;
    }

    ESP_LOGI(TAG, "M3U8 téléchargé: %d bytes", offset);
    return buffer;
}

/**
 * @brief Extraire le numéro de séquence d'une URL de segment
 * Ex: "...monpetitfranceinter_aac_lofi_4_1332057_1770542733.ts" → 1332057
 */
static int64_t extract_sequence_number(const char *url)
{
    if (url == NULL) return -1;

    // Chercher ".ts" à la fin
    const char *ts_ext = strstr(url, ".ts");
    if (!ts_ext) return -1;

    // Reculer pour trouver le dernier '_' avant le timestamp
    const char *p = ts_ext - 1;
    while (p > url && *p != '_') p--;
    if (p == url) return -1;

    // Encore un '_' en arrière pour trouver le numéro de séquence
    p--;
    while (p > url && *p != '_') p--;
    if (p == url || *p != '_') return -1;

    // Convertir le numéro
    return atoll(p + 1);
}

/**
 * @brief Task de téléchargement HLS
 */
static void hls_fetch_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Démarrage de la task de téléchargement HLS - avec sémaphore");

    // Tracker le dernier segment téléchargé par numéro de séquence
    int64_t last_sequence_number = -1;

    while (s_ctx.running) {
        // Attendre le signal du sémaphore (déclencheé par la task de lecture)
        // Timeout de 10 secondes pour vérifier périodiquement si on doit arrêter
        if (xSemaphoreTake(s_ctx.download_semaphore, pdMS_TO_TICKS(10000)) != pdTRUE) {
            // Timeout - vérifier si on doit continuer
            if (!s_ctx.running) {
                break;
            }
            // Pas de signal, continuer à attendre
            continue;
        }

        ESP_LOGI(TAG, "Signal reçu - démarrage du téléchargement");

        // Marquer qu'un téléchargement est en cours
        s_ctx.is_downloading = true;

        // Télécharger la playlist M3U8
        char *m3u8_content = download_m3u8(s_ctx.stream_url);
        if (m3u8_content == NULL) {
            ESP_LOGE(TAG, "Échec de téléchargement M3U8");
            s_ctx.is_downloading = false;
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        // Parser la playlist
        m3u8_playlist_t playlist;
        if (!m3u8_parse(m3u8_content, s_ctx.stream_url, &playlist)) {
            ESP_LOGE(TAG, "Échec de parsing M3U8");
            free(m3u8_content);
            s_ctx.is_downloading = false;
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        free(m3u8_content);
        m3u8_dump(&playlist);

        // Si c'est une master playlist, télécharger la media playlist
        if (playlist.is_master_playlist && playlist.segment_count > 0) {
            ESP_LOGI(TAG, "Master playlist détectée, sélection de la meilleure qualité...");

            // Chercher la meilleure qualité : midfi > hifi > lofi
            // Note: MIDFI est préféré car plus stable sur ESP32-S3
            int selected_index = 0;
            for (int i = 0; i < playlist.segment_count; i++) {
                // Prioriser MIDFI pour équilibre qualité/performance
                if (strstr(playlist.segments[i].url, "_midfi.m3u8") != NULL) {
                    selected_index = i;
                    ESP_LOGI(TAG, "Qualité MIDFI sélectionnée (~128 kbps)");
                    break;
                } else if (strstr(playlist.segments[i].url, "_hifi.m3u8") != NULL) {
                    selected_index = i;
                    ESP_LOGI(TAG, "Qualité HIFI disponible (~192-320 kbps)");
                    // Continue pour chercher midfi
                } else if (strstr(playlist.segments[i].url, "_lofi.m3u8") != NULL) {
                    if (selected_index == 0) {  // Seulement si rien trouvé
                        selected_index = i;
                        ESP_LOGI(TAG, "Qualité LOFI sélectionnée (par défaut ~64 kbps)");
                    }
                }
            }

            // Copier l'URL avant de libérer la structure
            char media_url[M3U8_MAX_URL_LEN];
            strncpy(media_url, playlist.segments[selected_index].url, M3U8_MAX_URL_LEN - 1);
            media_url[M3U8_MAX_URL_LEN - 1] = '\0';

            m3u8_content = download_m3u8(media_url);
            if (m3u8_content == NULL) {
                ESP_LOGE(TAG, "Échec de téléchargement de la media playlist");
                m3u8_free(&playlist);
                s_ctx.is_downloading = false;
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }

            // Re-parser avec la media playlist
            m3u8_free(&playlist);
            if (!m3u8_parse(m3u8_content, media_url, &playlist)) {
                ESP_LOGE(TAG, "Échec de parsing de la media playlist");
                free(m3u8_content);
                s_ctx.is_downloading = false;
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }

            free(m3u8_content);
            m3u8_dump(&playlist);
        }

        // Télécharger 2 segments consécutifs pour avoir une avance
        bool downloaded = false;
        int segments_downloaded = 0;
        const int SEGMENTS_PER_CYCLE = 2;  // Télécharger 2 segments d'avance

        for (int i = 0; i < playlist.segment_count && s_ctx.running && segments_downloaded < SEGMENTS_PER_CYCLE; i++) {
            // Extraire le numéro de séquence de ce segment
            int64_t seq_num = extract_sequence_number(playlist.segments[i].url);

            if (seq_num < 0) {
                ESP_LOGW(TAG, "Impossible d'extraire le numéro de séquence du segment %d", i);
                continue;
            }

            // SKIP si déjà téléchargé
            if (seq_num <= last_sequence_number) {
                continue;
            }

            // Nouveau segment trouvé - télécharger immédiatement (sémaphore contrôle le timing)
            ESP_LOGI(TAG, "Téléchargement segment %lld (%d/%d)", (long long)seq_num, segments_downloaded + 1, SEGMENTS_PER_CYCLE);

            esp_err_t err = download_segment(playlist.segments[i].url);
            if (err == ESP_OK) {
                last_sequence_number = seq_num;
                downloaded = true;
                segments_downloaded++;
                ESP_LOGI(TAG, "Segment %lld OK (%d/%d téléchargés)", (long long)seq_num, segments_downloaded, SEGMENTS_PER_CYCLE);
            } else {
                ESP_LOGE(TAG, "Échec téléchargement segment %lld", (long long)seq_num);
                break;  // Arrêter en cas d'erreur
            }
        }

        if (!downloaded && last_sequence_number >= 0) {
            ESP_LOGD(TAG, "Aucun téléchargement (dernier: %lld)", (long long)last_sequence_number);
        }

        m3u8_free(&playlist);

        // Téléchargement terminé - réinitialiser le flag
        s_ctx.is_downloading = false;

        // Pas de délai - on reboucle pour attendre le prochain signal du sémaphore
    }

    ESP_LOGI(TAG, "Arrêt de la task de téléchargement HLS");
    vTaskDelete(NULL);
}

/**
 * @brief Task de lecture audio
 */
static void audio_play_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Démarrage de la task de lecture audio");

    // Créer le décodeur TS simple (gère le parsing MPEG-TS + décodage AAC)
    esp_audio_simple_dec_handle_t dec_handle = NULL;
    esp_audio_simple_dec_cfg_t dec_cfg = {
        .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_TS,
        .dec_cfg = NULL,
        .cfg_size = 0,
        .use_frame_dec = false,  // Parsing automatique activé
    };
    esp_audio_err_t dec_ret = esp_audio_simple_dec_open(&dec_cfg, &dec_handle);

    if (dec_ret != ESP_AUDIO_ERR_OK || dec_handle == NULL) {
        ESP_LOGE(TAG, "Échec de création du décodeur TS: %d", dec_ret);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Décodeur TS créé avec succès");

    // Buffers de décodage avec PSRAM : optimisé pour éviter pertes et watchdog                      
    const size_t ENC_BUF_SIZE = 147456;  // 144KB = compromis performance/stabilité  
    const size_t DEC_BUF_SIZE = 16384;   // 16KB pour PCM décodé

    uint8_t *encoded_buffer = malloc(ENC_BUF_SIZE);
    int16_t *decoded_buffer = malloc(DEC_BUF_SIZE);

    if (encoded_buffer == NULL || decoded_buffer == NULL) {
        ESP_LOGE(TAG, "Échec d'allocation des buffers de décodage");
        esp_audio_simple_dec_close(dec_handle);
        free(encoded_buffer);
        free(decoded_buffer);
        vTaskDelete(NULL);
        return;
    }

    // Buffer pour accumuler les données non consommées
    size_t buffered_size = 0;

    // Délai minimal pour laisser le premier segment commencer à arriver
    ESP_LOGI(TAG, "Démarrage de la lecture audio...");
    vTaskDelay(pdMS_TO_TICKS(500));

    static int decode_count = 0;
    bool download_signaled = false;  // Pour éviter de signaler trop souvent
    bool was_downloading = false;    // Pour détecter la fin d'un téléchargement

    while (s_ctx.running) {
        // Vérifier le niveau du buffer et signaler si besoin de télécharger
        UBaseType_t items_waiting = 0;
        vRingbufferGetInfo(s_ctx.ring_buffer, NULL, NULL, NULL, NULL, &items_waiting);
        int buffer_level = (items_waiting * 100) / s_ctx.buffer_size;

        // Détecter la fin d'un téléchargement et permettre un nouveau signal
        if (was_downloading && !s_ctx.is_downloading) {
            download_signaled = false;  // Téléchargement terminé, autoriser un nouveau signal
            ESP_LOGD(TAG, "Téléchargement terminé, signal réinitialisé");
        }
        was_downloading = s_ctx.is_downloading;

        // Si buffer < 40% et qu'aucun téléchargement en cours, déclencher le téléchargement
        // Seuil augmenté de 20% à 40% pour anticiper et éviter les coupures audio
        if (buffer_level < 40 && !download_signaled && !s_ctx.is_downloading) {
            ESP_LOGI(TAG, "Buffer bas (%d%%) - signal pour télécharger", buffer_level);
            xSemaphoreGive(s_ctx.download_semaphore);
            download_signaled = true;  // Éviter de signaler plusieurs fois
        } else if (buffer_level >= 60) {
            // Réinitialiser le flag quand le buffer se remplit (60% pour hysteresis)
            download_signaled = false;
        }

        // Lire les données encodées du ring buffer seulement si on a de la place
        if (buffered_size < ENC_BUF_SIZE / 4) {
            size_t item_size = 0;
            uint8_t *item = (uint8_t *)xRingbufferReceive(s_ctx.ring_buffer, &item_size, pdMS_TO_TICKS(100));

            if (item != NULL) {
                // Copier dans le buffer en préservant les données existantes
                size_t space_left = ENC_BUF_SIZE - buffered_size;
                size_t to_copy = (item_size < space_left) ? item_size : space_left;

                // Warning si on perd des données (ne devrait plus arriver avec buffer 100KB)
                if (to_copy < item_size) {
                    ESP_LOGW(TAG, "PERTE: %zu bytes perdus (buffer plein: %zu/%d)",
                             item_size - to_copy, buffered_size, ENC_BUF_SIZE);
                }

                memcpy(encoded_buffer + buffered_size, item, to_copy);
                buffered_size += to_copy;

                vRingbufferReturnItem(s_ctx.ring_buffer, item);
            }
        }

        if (buffered_size == 0) {
            ESP_LOGW(TAG, "Buffer vide, attente...");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Décoder avec le simple decoder
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

        // Log de décodage pour diagnostic (premiers 20 + erreurs)
        if (decode_count++ < 20 || dec_ret != ESP_AUDIO_ERR_OK) {
            ESP_LOGI(TAG, "Décodage #%d: ret=%d, in=%zu, consumed=%lu, out=%lu",
                     decode_count, dec_ret, buffered_size, raw.consumed, out_frame.decoded_size);
        }

        // CRITIQUE: Gérer les données consommées correctement
        if (raw.consumed > 0) {
            // Déplacer les données non consommées au début du buffer
            if (raw.consumed < buffered_size) {
                memmove(encoded_buffer, encoded_buffer + raw.consumed, buffered_size - raw.consumed);
            }
            buffered_size -= raw.consumed;
        }

        if (dec_ret == ESP_AUDIO_ERR_OK && out_frame.decoded_size > 0) {
            // Réduction de volume logicielle (25% du volume = -12dB)
            int16_t *samples = (int16_t *)decoded_buffer;
            size_t num_samples = out_frame.decoded_size / sizeof(int16_t);
            for (size_t i = 0; i < num_samples; i++) {
                samples[i] = samples[i] / 4;  // Diviser par 4 = 25% volume
            }

            // Écrire vers le driver I2S
            size_t bytes_written = 0;
            esp_err_t err = drv_max98357a_write(s_ctx.driver, decoded_buffer,
                                                 out_frame.decoded_size, &bytes_written,
                                                 portMAX_DELAY);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Erreur d'écriture I2S: %s", esp_err_to_name(err));
            }
            // Log réduit : seulement tous les 100 appels
            if (decode_count % 100 == 0) {
                ESP_LOGI(TAG, "Audio: %zu bytes PCM (#%d, buf=%zu)", bytes_written, decode_count, buffered_size);
            }
        } else if (dec_ret != ESP_AUDIO_ERR_OK) {
            // Log seulement les erreurs persistantes (1 sur 50)
            static int error_count = 0;
            if (++error_count % 50 == 0) {
                ESP_LOGW(TAG, "Erreur décodage: ret=%d, consumed=%lu, decoded=%lu (count=%d)",
                         dec_ret, raw.consumed, out_frame.decoded_size, error_count);
            }

            // Stratégie agressive de resynchronisation
            // 1. Chercher le prochain sync word AAC (0xFFF) ou TS (0x47)
            bool found_sync = false;
            for (size_t i = 1; i < buffered_size - 1; i++) {
                // AAC ADTS sync word: 0xFFF (12 bits)
                if ((encoded_buffer[i] == 0xFF) && ((encoded_buffer[i+1] & 0xF0) == 0xF0)) {
                    // Trouvé un sync word AAC
                    memmove(encoded_buffer, encoded_buffer + i, buffered_size - i);
                    buffered_size -= i;
                    found_sync = true;
                    if (error_count % 50 == 0) {
                        ESP_LOGI(TAG, "Resync AAC trouvé à offset %zu", i);
                    }
                    break;
                }
                // TS sync byte: 0x47
                if (encoded_buffer[i] == 0x47 && i + 188 < buffered_size && encoded_buffer[i + 188] == 0x47) {
                    // Trouvé un sync TS (2 paquets consécutifs)
                    memmove(encoded_buffer, encoded_buffer + i, buffered_size - i);
                    buffered_size -= i;
                    found_sync = true;
                    if (error_count % 50 == 0) {
                        ESP_LOGI(TAG, "Resync TS trouvé à offset %zu", i);
                    }
                    break;
                }
            }

            // Si pas de sync trouvé, purger agressivement 2KB ou tout
            if (!found_sync) {
                if (buffered_size > 2048) {
                    buffered_size -= 2048;
                    memmove(encoded_buffer, encoded_buffer + 2048, buffered_size);
                } else {
                    buffered_size = 0;  // Vider complètement
                }
            }
        }
    }

    // Cleanup
    esp_audio_simple_dec_close(dec_handle);
    free(encoded_buffer);
    free(decoded_buffer);

    ESP_LOGI(TAG, "Arrêt de la task de lecture audio");
    vTaskDelete(NULL);
}

esp_err_t stream_player_start(const stream_player_config_t *config)
{
    if (config == NULL || config->stream_url == NULL || config->driver == NULL) {
        ESP_LOGE(TAG, "Configuration invalide");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_ctx.running) {
        ESP_LOGW(TAG, "Le player est déjà démarré");
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.stream_url = config->stream_url;
    s_ctx.driver = config->driver;
    s_ctx.buffer_size = config->buffer_size;
    s_ctx.running = true;

    // Créer le ring buffer
    s_ctx.ring_buffer = xRingbufferCreate(config->buffer_size, RINGBUF_TYPE_BYTEBUF);
    if (s_ctx.ring_buffer == NULL) {
        ESP_LOGE(TAG, "Échec de création du ring buffer");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Ring buffer créé: %d bytes", config->buffer_size);

    // Créer le sémaphore de téléchargement
    s_ctx.download_semaphore = xSemaphoreCreateBinary();
    if (s_ctx.download_semaphore == NULL) {
        ESP_LOGE(TAG, "Échec de création du sémaphore");
        vRingbufferDelete(s_ctx.ring_buffer);
        return ESP_ERR_NO_MEM;
    }

    // Donner le sémaphore immédiatement pour déclencher le premier téléchargement
    xSemaphoreGive(s_ctx.download_semaphore);
    ESP_LOGI(TAG, "Sémaphore de téléchargement créé");

    // Créer les tasks (stack optimisée pour économiser RAM)
    // Priorité 5 pour fetch (équilibrée avec sémaphore)
    BaseType_t ret = xTaskCreate(hls_fetch_task, "hls_fetch", 14336, NULL, 5, &s_ctx.fetch_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Échec de création de la task de téléchargement");
        vSemaphoreDelete(s_ctx.download_semaphore);
        vRingbufferDelete(s_ctx.ring_buffer);
        return ESP_FAIL;
    }

    // Priorité 8 pour audio (haute mais pas excessive)
    ret = xTaskCreate(audio_play_task, "audio_play", 6144, NULL, 8, &s_ctx.play_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Échec de création de la task de lecture");
        s_ctx.running = false;
        vTaskDelete(s_ctx.fetch_task);
        vSemaphoreDelete(s_ctx.download_semaphore);
        vRingbufferDelete(s_ctx.ring_buffer);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Stream player démarré");
    return ESP_OK;
}

esp_err_t stream_player_stop(void)
{
    if (!s_ctx.running) {
        return ESP_OK;
    }

    s_ctx.running = false;

    // Donner le sémaphore pour débloquer la task de téléchargement si elle attend
    if (s_ctx.download_semaphore) {
        xSemaphoreGive(s_ctx.download_semaphore);
    }

    // Attendre que les tasks se terminent
    vTaskDelay(pdMS_TO_TICKS(2000));

    if (s_ctx.ring_buffer) {
        vRingbufferDelete(s_ctx.ring_buffer);
        s_ctx.ring_buffer = NULL;
    }

    if (s_ctx.download_semaphore) {
        vSemaphoreDelete(s_ctx.download_semaphore);
        s_ctx.download_semaphore = NULL;
    }

    ESP_LOGI(TAG, "Stream player arrêté");
    return ESP_OK;
}

esp_err_t stream_player_get_stats(size_t *bytes_downloaded, int *buffer_fill)
{
    if (bytes_downloaded) {
        *bytes_downloaded = s_ctx.bytes_downloaded;
    }

    if (buffer_fill && s_ctx.ring_buffer) {
        UBaseType_t items_waiting;
        vRingbufferGetInfo(s_ctx.ring_buffer, NULL, NULL, NULL, NULL, &items_waiting);
        *buffer_fill = (items_waiting * 100) / s_ctx.buffer_size;
    }

    return ESP_OK;
}
