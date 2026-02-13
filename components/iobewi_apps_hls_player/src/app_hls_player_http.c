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
        if (!handle->is_downloading) {
            return false;
        }

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
        if (!handle->is_downloading) {
            return ESP_ERR_INVALID_STATE;
        }

#if CONFIG_APP_HLS_PLAYER_BACKPRESSURE
        if (hls_ringbuf_level_pct(handle) >= high_wm) {
            if (!rb_wait_for_space(handle, -1)) {
                return ESP_ERR_INVALID_STATE;
            }
        }
#endif

        if (xRingbufferSend(handle->ring_buffer, pkt188, 188, pdMS_TO_TICKS(20)) == pdTRUE) {
            __atomic_fetch_add(&handle->bytes_downloaded, 188, __ATOMIC_RELAXED);
            return ESP_OK;
        }

        // Ringbuffer momentanément saturé: céder puis réessayer (no-drop)
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

/**
 * @brief Envoi TS avec mesure du temps passé en backpressure
 */
static esp_err_t rb_send_ts_backpressure_timed(app_hls_player_t *handle,
                                               const uint8_t *pkt188,
                                               int64_t *rb_wait_us)
{
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = rb_send_ts_backpressure(handle, pkt188);
    if (rb_wait_us != NULL) {
        *rb_wait_us += (esp_timer_get_time() - t0);
    }
    return err;
}

static bool hls_rb_wait_budget_exceeded(int64_t rb_wait_us)
{
    if (CONFIG_APP_HLS_PLAYER_RB_WAIT_BUDGET_MS <= 0) {
        return false;
    }

    int64_t budget_us = (int64_t)CONFIG_APP_HLS_PLAYER_RB_WAIT_BUDGET_MS * 1000;
    return rb_wait_us >= budget_us;
}

static esp_err_t hls_process_ts_chunk(app_hls_player_t *handle,
                                      const uint8_t *src,
                                      size_t src_len,
                                      int64_t *rb_wait_us)
{
    if (handle == NULL || src == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (handle->ts_carry_len > 0) {
        size_t need = 188 - handle->ts_carry_len;
        size_t take = (src_len < need) ? src_len : need;

        memcpy(handle->ts_carry + handle->ts_carry_len, src, take);
        handle->ts_carry_len += take;
        src += take;
        src_len -= take;

        if (handle->ts_carry_len == 188) {
            esp_err_t err = rb_send_ts_backpressure_timed(handle, handle->ts_carry, rb_wait_us);
            if (err != ESP_OK) {
                return err;
            }
            handle->ts_carry_len = 0;
        }
    }

    while (src_len >= 188) {
        esp_err_t err = rb_send_ts_backpressure_timed(handle, src, rb_wait_us);
        if (err != ESP_OK) {
            return err;
        }

        src += 188;
        src_len -= 188;
    }

    if (src_len > 0) {
        memcpy(handle->ts_carry, src, src_len);
        handle->ts_carry_len = src_len;
    }

    return ESP_OK;
}

static bool hls_parse_ts_url(const char *url,
                             char *host,
                             size_t host_size,
                             int *port,
                             esp_http_client_transport_t *transport)
{
    if (!url || !host || host_size == 0 || !port || !transport) {
        return false;
    }

    const char *scheme_end = strstr(url, "://");
    if (!scheme_end) {
        return false;
    }

    size_t scheme_len = (size_t)(scheme_end - url);
    const bool is_https = (scheme_len == 5) && (strncmp(url, "https", 5) == 0);
    const bool is_http = (scheme_len == 4) && (strncmp(url, "http", 4) == 0);
    if (!is_http && !is_https) {
        return false;
    }

    const char *authority = scheme_end + 3;
    const char *path = strpbrk(authority, "/?#");
    size_t authority_len = path ? (size_t)(path - authority) : strlen(authority);
    if (authority_len == 0) {
        return false;
    }

    const char *host_start = authority;
    if (*host_start == '[') {
        const char *close_br = memchr(host_start, ']', authority_len);
        if (!close_br) {
            return false;
        }

        // IPv6 host retourné sans crochets pour compat avec config.host
        size_t host_len = (size_t)(close_br - (host_start + 1));
        if (host_len == 0 || host_len >= host_size) {
            return false;
        }
        memcpy(host, host_start + 1, host_len);
        host[host_len] = '\0';

        const char *port_ptr = close_br + 1;
        if ((size_t)(port_ptr - authority) < authority_len && *port_ptr == ':') {
            *port = atoi(port_ptr + 1);
        } else {
            *port = is_https ? 443 : 80;
        }
    } else {
        const char *colon = memchr(host_start, ':', authority_len);
        size_t host_len = colon ? (size_t)(colon - host_start) : authority_len;
        if (host_len == 0 || host_len >= host_size) {
            return false;
        }

        memcpy(host, host_start, host_len);
        host[host_len] = '\0';

        if (colon) {
            *port = atoi(colon + 1);
        } else {
            *port = is_https ? 443 : 80;
        }
    }

    if (*port <= 0) {
        *port = is_https ? 443 : 80;
    }

    *transport = is_https ? HTTP_TRANSPORT_OVER_SSL : HTTP_TRANSPORT_OVER_TCP;
    return true;
}

void hls_http_ts_client_cleanup(app_hls_player_t *handle)
{
    if (!handle) {
        return;
    }

    if (handle->ts_http_client) {
        esp_http_client_close(handle->ts_http_client);
        esp_http_client_cleanup(handle->ts_http_client);
        handle->ts_http_client = NULL;
    }

    handle->ts_client_ready = false;
    handle->ts_host[0] = '\0';
    handle->ts_port = 0;
    handle->ts_transport = HTTP_TRANSPORT_UNKNOWN;
}

static esp_http_client_handle_t hls_http_ts_client_get_or_create(app_hls_player_t *handle,
                                                                  const char *url,
                                                                  bool *reuse_hit)
{
    if (!handle || !url) {
        return NULL;
    }

    char parsed_host[sizeof(handle->ts_host)] = {0};
    int parsed_port = 0;
    esp_http_client_transport_t parsed_transport = HTTP_TRANSPORT_UNKNOWN;
    if (!hls_parse_ts_url(url, parsed_host, sizeof(parsed_host), &parsed_port, &parsed_transport)) {
        ESP_LOGE(TAG, "TS_CLIENT parse_url failed: %s", url);
        return NULL;
    }

    bool needs_recreate = false;
    const char *reason = "none";
    if (!handle->ts_http_client || !handle->ts_client_ready) {
        needs_recreate = true;
        reason = "init";
    } else if ((handle->ts_port != parsed_port) ||
               (handle->ts_transport != parsed_transport) ||
               (strcmp(handle->ts_host, parsed_host) != 0)) {
        needs_recreate = true;
        reason = "host_change";
    }

    if (reuse_hit) {
        *reuse_hit = !needs_recreate;
    }

    if (!needs_recreate) {
        return handle->ts_http_client;
    }

    if (handle->ts_http_client) {
        ESP_LOGI(TAG, "TS_CLIENT recreate reason=%s old=%s:%d", reason, handle->ts_host, handle->ts_port);
        hls_http_ts_client_cleanup(handle);
    }

    // Copie persistante avant init: évite de pointer sur parsed_host (stack)
    strlcpy(handle->ts_host, parsed_host, sizeof(handle->ts_host));

    esp_http_client_config_t config = {
        .url = url,
        .host = handle->ts_host,
        .port = parsed_port,
        .buffer_size = HTTP_BUFFER_SIZE,
        .timeout_ms = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true,
        .keep_alive_enable = true,
        .transport_type = parsed_transport,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "TS_CLIENT recreate reason=init_err url=%s", url);
        handle->ts_client_ready = false;
        return NULL;
    }

    handle->ts_http_client = client;
    handle->ts_client_ready = true;
    handle->ts_port = parsed_port;
    handle->ts_transport = parsed_transport;

    ESP_LOGI(TAG, "TS_CLIENT recreate reason=%s new=%s:%d transport=%d", reason,
             handle->ts_host, handle->ts_port, (int)handle->ts_transport);
    return handle->ts_http_client;
}

static esp_err_t hls_http_download_segment_once(app_hls_player_t *handle,
                                                esp_http_client_handle_t client,
                                                const char *url,
                                                bool *network_error)
{
    if (network_error) {
        *network_error = false;
    }

    esp_err_t err = esp_http_client_set_url(client, url);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TS_CLIENT set_url failed: %s", esp_err_to_name(err));
        return err;
    }

    int64_t open_t0 = esp_timer_get_time();
    err = esp_http_client_open(client, 0);
    handle->last_seg_metrics.open_ms += (esp_timer_get_time() - open_t0) / 1000;
    if (err != ESP_OK) {
        if (network_error) {
            *network_error = true;
        }
        ESP_LOGW(TAG, "TS_CLIENT open failed: %s", esp_err_to_name(err));
        return err;
    }

    int64_t hdr_t0 = esp_timer_get_time();
    int length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    handle->last_seg_metrics.headers_ms += (esp_timer_get_time() - hdr_t0) / 1000;
    ESP_LOGI(TAG, "HTTP Status=%d, Length=%d", status, length);

    if (status != 200 && status != 206) {
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
        esp_http_client_close(client);
        return ESP_FAIL;
    }

    int64_t body_t0 = esp_timer_get_time();
    int64_t rb_wait_us = 0;
    uint8_t buffer[HTTP_BUFFER_SIZE];

    while (true) {
        int64_t read_t0 = esp_timer_get_time();
        int read = esp_http_client_read(client, (char *)buffer, sizeof(buffer));
        int64_t read_block_ms = (esp_timer_get_time() - read_t0) / 1000;

        handle->last_seg_metrics.read_calls++;
        if (read_block_ms > handle->last_seg_metrics.max_read_block_ms) {
            handle->last_seg_metrics.max_read_block_ms = read_block_ms;
        }

        if (read < 0) {
            if (network_error) {
                *network_error = true;
            }
            ESP_LOGW(TAG, "Erreur read segment: %d", read);
            err = ESP_FAIL;
            break;
        }
        if (read == 0) {
            if (length > 0 && handle->last_seg_metrics.body_bytes < (size_t)length) {
                if (network_error) {
                    *network_error = true;
                }
                ESP_LOGW(TAG, "EOF précoce segment: status=%d got=%u expected=%d url=%s", status,
                         (unsigned)handle->last_seg_metrics.body_bytes, length, url ? url : "(null)");
                err = ESP_FAIL;
            } else {
                err = ESP_OK;
            }
            break;
        }

        handle->last_seg_metrics.body_bytes += (size_t)read;
        err = hls_process_ts_chunk(handle, buffer, (size_t)read, &rb_wait_us);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "TS push stopped: %s", esp_err_to_name(err));
            break;
        }

        if (hls_rb_wait_budget_exceeded(rb_wait_us)) {
            handle->last_seg_metrics.rb_budget_abort = true;
            ESP_LOGW(TAG, "RB_BUDGET exceeded: rb_wait_ms=%lld budget_ms=%d url=%s",
                     (long long)(rb_wait_us / 1000), CONFIG_APP_HLS_PLAYER_RB_WAIT_BUDGET_MS,
                     url ? url : "(null)");
            err = ESP_ERR_TIMEOUT;
            break;
        }
    }

    handle->last_seg_metrics.body_read_ms += (esp_timer_get_time() - body_t0) / 1000;
    handle->last_seg_metrics.rb_wait_ms += rb_wait_us / 1000;
    esp_http_client_close(client);
    return err;
}

esp_err_t hls_http_download_segment(app_hls_player_t *handle, const char *url)
{
    int64_t seg_t0 = esp_timer_get_time();
    memset(&handle->last_seg_metrics, 0, sizeof(handle->last_seg_metrics));

    ESP_LOGI(TAG, "Téléchargement: %s", url);

    bool reuse_hit = false;
    esp_http_client_handle_t client = hls_http_ts_client_get_or_create(handle, url, &reuse_hit);
    if (!client) {
        handle->last_seg_metrics.total_ms = (esp_timer_get_time() - seg_t0) / 1000;
        return ESP_FAIL;
    }
    handle->last_seg_metrics.reuse = reuse_hit;

    bool network_error = false;
    esp_err_t err = hls_http_download_segment_once(handle, client, url, &network_error);
    bool partial_segment_written = (handle->last_seg_metrics.body_bytes > 0) || (handle->ts_carry_len > 0);
    if (err != ESP_OK && network_error && !partial_segment_written) {
        ESP_LOGW(TAG, "TS_CLIENT recreate reason=socket_err retry=1 err=%s", esp_err_to_name(err));
        handle->last_seg_metrics.retried = true;
        handle->last_seg_metrics.reuse = false;
        hls_http_ts_client_cleanup(handle);

        client = hls_http_ts_client_get_or_create(handle, url, NULL);
        if (!client) {
            handle->last_seg_metrics.total_ms = (esp_timer_get_time() - seg_t0) / 1000;
            return err;
        }

        err = hls_http_download_segment_once(handle, client, url, NULL);
    } else if (err != ESP_OK && network_error && partial_segment_written) {
        ESP_LOGW(TAG,
                 "TS_CLIENT skip retry reason=partial_segment bytes=%u carry=%u err=%s",
                 (unsigned)handle->last_seg_metrics.body_bytes,
                 (unsigned)handle->ts_carry_len,
                 esp_err_to_name(err));
    }

    handle->last_seg_metrics.total_ms = (esp_timer_get_time() - seg_t0) / 1000;
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
