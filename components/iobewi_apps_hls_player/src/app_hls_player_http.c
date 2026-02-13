/**
 * @file app_hls_player_http.c
 * @brief Implémentation du module de téléchargement HTTP/HTTPS
 */

#include "app_hls_player/app_hls_player_http.h"
#include "app_hls_player/app_hls_player_internal.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "hls_http";

// [RAM OPT P0.2] Buffer pour HTTP download
// Réduit 16KB → 4KB (gain -12 KB par client HTTP actif)
// 4KB suffisant pour streaming (chunks typiques ~2-8KB)
// Si instabilité réseau/TLS : augmenter à 8KB
#define HTTP_BUFFER_SIZE (4 * 1024)

/**
 * @brief Helper pour lire header Location de manière sécurisée
 *
 * COMPAT NOTE: esp_http_client_get_header() ownership/signature peut varier selon IDF.
 * Testé avec ESP-IDF v6.1-dev. Si build fail sur version antérieure, alternative :
 * - Parser manuellement via HTTP_EVENT_ON_HEADER dans event handler
 * - Stocker Location quand evt->header_key == "Location"
 *
 * @param client HTTP client handle
 * @param buf Buffer destination (local, pas de pointeur direct)
 * @param buf_size Taille du buffer
 * @return true si Location trouvé et copié, false sinon
 */
static bool get_location_header(esp_http_client_handle_t client, char *buf, size_t buf_size)
{
    if (!client || !buf || buf_size == 0) {
        return false;
    }

    char *location = NULL;
    esp_err_t err = esp_http_client_get_header(client, "Location", &location);

    if (err == ESP_OK && location != NULL) {
        strncpy(buf, location, buf_size - 1);
        buf[buf_size - 1] = '\0';
        return true;
    }

    return false;
}

static int hls_ringbuf_level_pct(app_hls_player_t *handle)
{
    size_t rb_free = xRingbufferGetCurFreeSize(handle->ring_buffer);
    size_t rb_used = handle->buffer_size - rb_free;
    return (int)((rb_used * 100u) / handle->buffer_size);
}

static bool rb_wait_for_space(app_hls_player_t *handle, int timeout_ms)
{
#if CONFIG_APP_HLS_PLAYER_BACKPRESSURE
    const int low_wm = CONFIG_APP_HLS_PLAYER_BACKPRESSURE_LOW;
#else
    (void)timeout_ms;
    (void)handle;
    return true;
#endif

#if CONFIG_APP_HLS_PLAYER_BACKPRESSURE
    int64_t t0_us = esp_timer_get_time();
    while (true) {
        if (hls_ringbuf_level_pct(handle) <= low_wm) {
            return true;
        }

        if (timeout_ms >= 0) {
            int64_t elapsed_ms = (esp_timer_get_time() - t0_us) / 1000;
            if (elapsed_ms >= timeout_ms) {
                return false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }
#endif
}

static esp_err_t rb_send_ts_backpressure(app_hls_player_t *handle, const uint8_t *pkt188)
{
    if (handle == NULL || handle->ring_buffer == NULL || pkt188 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

#if CONFIG_APP_HLS_PLAYER_BACKPRESSURE
    const int high_wm = CONFIG_APP_HLS_PLAYER_BACKPRESSURE_HIGH;
#endif

    while (true) {
#if CONFIG_APP_HLS_PLAYER_BACKPRESSURE
        if (hls_ringbuf_level_pct(handle) >= high_wm) {
            if (!rb_wait_for_space(handle, -1)) {
                return ESP_ERR_TIMEOUT;
            }
        }
#endif

        if (xRingbufferSend(handle->ring_buffer, pkt188, 188, pdMS_TO_TICKS(200)) == pdTRUE) {
            __atomic_fetch_add(&handle->bytes_downloaded, 188, __ATOMIC_RELAXED);
            return ESP_OK;
        }

        // Ringbuffer momentanément saturé: céder puis réessayer (no-drop)
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

/**
 * @brief Callback HTTP pour recevoir les données
 * Envoie TS avec backpressure (sans stratégie drop-old)
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    app_hls_player_t *handle = (app_hls_player_t *)evt->user_data;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0 && handle->ring_buffer) {
                const uint8_t *src = (const uint8_t *)evt->data;
                size_t src_len = (size_t)evt->data_len;

                if (handle->ts_carry_len > 0) {
                    size_t need = 188 - handle->ts_carry_len;
                    size_t take = (src_len < need) ? src_len : need;

                    memcpy(handle->ts_carry + handle->ts_carry_len, src, take);
                    handle->ts_carry_len += take;
                    src += take;
                    src_len -= take;

                    if (handle->ts_carry_len == 188) {
                        esp_err_t err = rb_send_ts_backpressure(handle, handle->ts_carry);
                        if (err != ESP_OK) {
                            ESP_LOGE(TAG, "TS push failed [carry]: %s", esp_err_to_name(err));
                            return err;
                        }
                        handle->ts_carry_len = 0;
                    }
                }

                while (src_len >= 188) {
                    esp_err_t err = rb_send_ts_backpressure(handle, src);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "TS push failed: %s", esp_err_to_name(err));
                        return err;
                    }

                    src += 188;
                    src_len -= 188;
                }

                if (src_len > 0) {
                    memcpy(handle->ts_carry, src, src_len);
                    handle->ts_carry_len = src_len;
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t hls_http_download_segment(app_hls_player_t *handle, const char *url)
{
    ESP_LOGI(TAG, "Téléchargement: %s", url);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = handle,
        .buffer_size = HTTP_BUFFER_SIZE,
        .timeout_ms = 5000,  // FIX: 5s pour aligner avec timeout stop (évite timeout warnings)
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true,  // FIX: Log "non suivie" cohérent
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

        // Traiter status != 200/206 comme erreur (évite dérive silencieuse)
        if (status != 200 && status != 206) {
            // Logger Location si redirection (debug terrain) - buffer local pour compat IDF
            if (status >= 300 && status < 400) {
                char location_buf[256];
                if (get_location_header(client, location_buf, sizeof(location_buf))) {
                    ESP_LOGW(TAG, "HTTP redirection %d → %s (non suivie)", status, location_buf);
                } else {
                    ESP_LOGW(TAG, "HTTP redirection %d → (Location manquant)", status);
                }
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

char* hls_http_download_m3u8(const char *url)
{
    ESP_LOGI(TAG, "Téléchargement M3U8: %s", url);

    size_t free_heap = esp_get_free_heap_size();
    size_t min_heap = esp_get_minimum_free_heap_size();
    ESP_LOGI(TAG, "Heap avant M3U8: libre=%zu, min=%zu", free_heap, min_heap);

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
        .buffer_size = HTTP_BUFFER_SIZE,  // [RAM OPT P0.2] 4 KB (vs 16 KB défaut)
        .timeout_ms = 5000,  // FIX: 5s pour aligner avec timeout stop (évite timeout warnings)
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true,  // FIX: Log "non suivie" cohérent
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

    // CRITIQUE: Vérifier le status HTTP avant de lire le corps (évite parser HTML d'erreur)
    int status = esp_http_client_get_status_code(client);
    if (status != 200 && status != 206) {
        if (status >= 300 && status < 400) {
            char location_buf[256];
            if (get_location_header(client, location_buf, sizeof(location_buf))) {
                ESP_LOGW(TAG, "M3U8 HTTP redirection %d → %s (non suivie)", status, location_buf);
            } else {
                ESP_LOGW(TAG, "M3U8 HTTP redirection %d → (Location manquant)", status);
            }
        } else {
            ESP_LOGE(TAG, "M3U8 HTTP status inattendu: %d (échec)", status);
        }
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        free(buffer);
        return NULL;
    }

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

            // [FIX LAG] Early exit si content_length connu et complet
            if (content_length > 0 && offset >= content_length) {
                break;
            }
        } else if (read_len == 0) {
            // FIX: read==0 peut être temporaire (pas forcément EOF)
            if (++zero_read_streak > 5) {  // ~5 tentatives
                ESP_LOGW(TAG, "M3U8: read_len==0 persistant (%d tentatives), fin lecture", zero_read_streak);

                // FIX CHUNKED: Accepter playlist si on a reçu des données (offset > 0)
                if (offset > 0) {
                    // Si content_length connu : incomplet seulement si offset < content_length
                    if (expect_length && offset < content_length) {
                        ESP_LOGW(TAG, "M3U8: playlist incomplète (attendu: %d, reçu: %d)", content_length, offset);
                        truncated = true;
                    } else {
                        // Chunked/CL=0 : on accepte ce qu'on a
                        ESP_LOGI(TAG, "M3U8: chunked/CL=0, acceptation des %d bytes reçus", offset);
                        truncated = false;
                    }
                } else {
                    // Rien reçu
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

    ESP_LOGI(TAG, "M3U8 téléchargé: %d bytes (capacité: %zu)", offset, buffer_capacity);

    // Log heap après pour tracker fragmentation
    free_heap = esp_get_free_heap_size();
    min_heap = esp_get_minimum_free_heap_size();
    ESP_LOGI(TAG, "Heap après M3U8: libre=%zu, min=%zu", free_heap, min_heap);

    return buffer;
}
