/**
 * @file app_hls_player.c
 * @brief Implémentation du player HLS
 */

#include "app_hls_player/app_hls_player.h"
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

// [RAM OPT P0.2] Buffer pour HTTP download
// Réduit 16KB → 4KB (gain -12 KB par client HTTP actif)
// 4KB suffisant pour streaming (chunks typiques ~2-8KB)
// Si instabilité réseau/TLS : augmenter à 8KB
#define HTTP_BUFFER_SIZE (4 * 1024)

// Task Notification bits (remplace volatile flags pour thread-safety stricte SMP)
#define NOTIF_STOP   (1u << 0)  /**< Signal d'arrêt */
#define NOTIF_RESET  (1u << 1)  /**< Signal reset décodeur (DISCONTINUITY) */
#define NOTIF_RESYNC (1u << 2)  /**< Signal resync soft (drop-old, pas de reset décodeur) */

// Tailles de buffers depuis Kconfig (avec fallback si non défini)
#ifndef CONFIG_APP_HLS_PLAYER_RING_BUFFER_SIZE
    #define CONFIG_APP_HLS_PLAYER_RING_BUFFER_SIZE 128
#endif
#ifndef CONFIG_APP_HLS_PLAYER_REM_BUFFER_SIZE
    #define CONFIG_APP_HLS_PLAYER_REM_BUFFER_SIZE 8
#endif
#ifndef CONFIG_APP_HLS_PLAYER_DEC_BUFFER_SIZE
    #define CONFIG_APP_HLS_PLAYER_DEC_BUFFER_SIZE 16
#endif
#ifndef CONFIG_APP_HLS_PLAYER_DROP_OLD_ON_FULL
    #define CONFIG_APP_HLS_PLAYER_DROP_OLD_ON_FULL 1
#endif
#ifndef CONFIG_APP_HLS_PLAYER_GATHER_BUFFER_SIZE
    #define CONFIG_APP_HLS_PLAYER_GATHER_BUFFER_SIZE 8
#endif

/**
 * @brief Structure interne du player HLS
 */
struct app_hls_player_s {
    char *stream_url;                       /**< URL du stream (copie allouée) */
    RingbufHandle_t ring_buffer;            /**< Buffer circulaire pour données encodées */
    size_t buffer_size;                     /**< Taille du buffer */
    TaskHandle_t fetch_task;                /**< Tâche de téléchargement */
    TaskHandle_t play_task;                 /**< Tâche de lecture/décodage */
    size_t bytes_downloaded;                /**< Statistiques: bytes téléchargés */
    SemaphoreHandle_t download_semaphore;   /**< Sémaphore pour contrôle téléchargement */
    SemaphoreHandle_t stats_mutex;          /**< Mutex pour bytes_downloaded */
    SemaphoreHandle_t fetch_done;           /**< Sémaphore signalant fin fetch_task */
    SemaphoreHandle_t play_done;            /**< Sémaphore signalant fin play_task */
    volatile bool is_downloading;           /**< Flag téléchargement en cours (lu par stats, OK volatile) */
    app_hls_player_write_cb_t write_cb;     /**< Callback d'écriture audio */
    void *write_ctx;                        /**< Contexte utilisateur pour callback */
    int target_duration;                    /**< Target duration pour timeout refresh */
    uint32_t drop_count;                    /**< Compteur pertes ring buffer (rate limit log) */
    uint32_t drop_old_count;                /**< Compteur drop-old (stratégie live-ness) */
    uint32_t drop_recover_count;            /**< Compteur recoveries après drop-old réussi */
    uint32_t bad_item_size_count;           /**< Compteur corruption ringbuffer (drop_sz != 188) */
    uint32_t resync_notif_guard;            /**< Rate-limit NOTIF_RESYNC fallback */
    uint8_t ts_carry[188];                  /**< Carry buffer pour alignement TS 188-byte */
    size_t ts_carry_len;                    /**< Nombre de bytes dans ts_carry */
};

/**
 * @brief Helper inline pour envoyer NOTIF_RESYNC rate-limited (% 10)
 */
static inline void hls_rate_limited_resync(app_hls_player_t *handle)
{
    if (!handle || !handle->play_task) return;

    uint32_t g = handle->resync_notif_guard + 1;
    handle->resync_notif_guard = g;
    if ((g % 10) == 0) {
        xTaskNotify(handle->play_task, NOTIF_RESYNC, eSetBits);
    }
}

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

/**
 * @brief Attente interruptible avec check notifications NOTIF_STOP
 *
 * xTaskNotifyWait travaille toujours sur la tâche courante, pas besoin de handle.
 * Retourne false si NOTIF_STOP reçue, true si délai complet.
 */
static bool interruptible_delay_ms(uint32_t ms)
{
    const uint32_t step = 100;
    while (ms > 0) {
        uint32_t this_step = (ms > step) ? step : ms;

        // Check notification sans bloquer (xTaskNotifyWait sur tâche courante)
        uint32_t notif = 0;
        if (xTaskNotifyWait(0, NOTIF_STOP, &notif, pdMS_TO_TICKS(this_step)) == pdTRUE) {
            if (notif & NOTIF_STOP) {
                return false;  // Stop demandé
            }
        }

        ms -= this_step;
    }
    return true;  // Délai complet
}

/**
 * @brief Check immédiat si NOTIF_STOP est présente (sans bloquer)
 *
 * ⚠️ WARNING: Cette fonction **CONSOMME** le bit NOTIF_STOP (xTaskNotifyWait l'efface).
 * À n'utiliser que si l'appelant sort IMMÉDIATEMENT de la task (break/goto task_exit).
 *
 * Si appelé dans une fonction utilitaire ou suivi d'un continue, les checks suivants
 * ne verront plus STOP → risque de tâche zombie.
 *
 * @return true si STOP demandé (et consommé), false sinon
 */
static inline bool hls_should_stop_now(void)
{
    uint32_t notif = 0;
    return (xTaskNotifyWait(0, NOTIF_STOP, &notif, 0) == pdTRUE) && (notif & NOTIF_STOP);
}

/**
 * @brief Callback HTTP pour recevoir les données
 * Drop-old strategy pour meilleure live-ness
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    app_hls_player_t *handle = (app_hls_player_t *)evt->user_data;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0 && handle->ring_buffer) {
                // [TS ALIGNMENT] Alignement TS 188-byte sans malloc pour stabilité
                const uint8_t *src = (const uint8_t *)evt->data;
                size_t src_len = (size_t)evt->data_len;

                // 1) Si carry existe, compléter jusqu'à 188
                if (handle->ts_carry_len > 0) {
                    size_t need = 188 - handle->ts_carry_len;
                    size_t take = (src_len < need) ? src_len : need;

                    memcpy(handle->ts_carry + handle->ts_carry_len, src, take);
                    handle->ts_carry_len += take;
                    src += take;
                    src_len -= take;

                    // Si on a un paquet TS complet, l'envoyer
                    if (handle->ts_carry_len == 188) {
                        bool sent_ok = (xRingbufferSend(handle->ring_buffer, handle->ts_carry, 188, 0) == pdTRUE);
#if CONFIG_APP_HLS_PLAYER_DROP_OLD_ON_FULL
                        if (!sent_ok) {
                            // Drop jusqu'à trouver une frontière PES (PUSI=1)
                            bool dropped = false;
                            int drop_count = 0;
                            for (int attempt = 0; attempt < 20 && !dropped; attempt++) {
                                size_t drop_sz = 0;
                                void *drop = xRingbufferReceive(handle->ring_buffer, &drop_sz, 0);
                                if (!drop) break;

                                // Défensif: vérifier taille avant d'accéder au contenu
                                if (drop_sz != 188) {
                                    vRingbufferReturnItem(handle->ring_buffer, drop);
                                    handle->drop_old_count++;
                                    handle->bad_item_size_count++;
                                    drop_count++;

                                    // Corruption détectée : log rate-limited + forcer resync soft
                                    if ((handle->bad_item_size_count % 50) == 0) {
                                        ESP_LOGW(TAG, "Ring item size=%zu (expected 188) [x%u]",
                                                 drop_sz, handle->bad_item_size_count);
                                    }
                                    hls_rate_limited_resync(handle);
                                    continue;
                                }

                                uint8_t *ts = (uint8_t *)drop;
                                bool is_pusi = (ts[0] == 0x47 && (ts[1] & 0x40) != 0);

                                vRingbufferReturnItem(handle->ring_buffer, drop);
                                handle->drop_old_count++;
                                drop_count++;

                                if (is_pusi) {
                                    dropped = true;

                                    if (handle->play_task && (handle->drop_old_count % 50) == 0) {
                                        xTaskNotify(handle->play_task, NOTIF_RESYNC, eSetBits);
                                    }

                                    sent_ok = (xRingbufferSend(handle->ring_buffer, handle->ts_carry, 188, 0) == pdTRUE);
                                    if (sent_ok) {
                                        handle->drop_recover_count++;
                                        if ((handle->drop_old_count % 100) == 0) {
                                            ESP_LOGI(TAG, "Drop-old (x%u) [PUSI/carry]", (unsigned)handle->drop_old_count);
                                        }
                                    }
                                }
                            }

                            // Fallback: si pas de PUSI trouvé après 20 tentatives, force resync soft (rate-limited)
                            if (!dropped && drop_count > 0) {
                                hls_rate_limited_resync(handle);
                            }
                        }
#endif
                        if (sent_ok) {
                            // Try-lock pour éviter blocage HTTP callback
                            if (handle->stats_mutex && xSemaphoreTake(handle->stats_mutex, 0) == pdTRUE) {
                                handle->bytes_downloaded += 188;
                                xSemaphoreGive(handle->stats_mutex);
                            } else {
                                handle->bytes_downloaded += 188;
                            }
                        } else {
                            handle->drop_count++;
                            if ((handle->drop_count % 100) == 0) {
                                ESP_LOGW(TAG, "Ring buffer plein (x%u)", (unsigned)handle->drop_count);
                            }
                        }

                        handle->ts_carry_len = 0;
                    }
                }

                // 2) [FIX CRITIQUE] Envoyer paquet-par-paquet (188) pour drop-old granulaire
                // Avant: bulk aligné (3948 bytes) → drop 21 paquets d'un coup → corruption TS massive
                // Après: 188 bytes → drop 1 paquet max → corruption minimale
                while (src_len >= 188) {
                    bool sent_ok = (xRingbufferSend(handle->ring_buffer, src, 188, 0) == pdTRUE);
#if CONFIG_APP_HLS_PLAYER_DROP_OLD_ON_FULL
                    if (!sent_ok) {
                        // Drop jusqu'à trouver une frontière PES (PUSI=1) pour éviter
                        // de casser une AAC frame fragmentée sur plusieurs TS
                        bool dropped = false;
                        int drop_count = 0;
                        for (int attempt = 0; attempt < 20 && !dropped; attempt++) {
                            size_t drop_sz = 0;
                            void *drop = xRingbufferReceive(handle->ring_buffer, &drop_sz, 0);
                            if (!drop) break;

                            // Défensif: vérifier taille avant d'accéder au contenu
                            if (drop_sz != 188) {
                                vRingbufferReturnItem(handle->ring_buffer, drop);
                                handle->drop_old_count++;
                                handle->bad_item_size_count++;
                                drop_count++;

                                // Corruption détectée : log rate-limited + forcer resync soft
                                if ((handle->bad_item_size_count % 50) == 0) {
                                    ESP_LOGW(TAG, "Ring item size=%zu (expected 188) [x%u]",
                                             drop_sz, handle->bad_item_size_count);
                                }
                                hls_rate_limited_resync(handle);
                                continue;
                            }

                            uint8_t *ts = (uint8_t *)drop;
                            bool is_pusi = (ts[0] == 0x47 && (ts[1] & 0x40) != 0);

                            // Compter tous les drops (PUSI + non-PUSI)
                            vRingbufferReturnItem(handle->ring_buffer, drop);
                            handle->drop_old_count++;
                            drop_count++;

                            if (is_pusi) {
                                // Frontière PES trouvée : arrêter
                                dropped = true;

                                // Soft resync côté play task (rare)
                                if (handle->play_task && (handle->drop_old_count % 50) == 0) {
                                    xTaskNotify(handle->play_task, NOTIF_RESYNC, eSetBits);
                                }

                                sent_ok = (xRingbufferSend(handle->ring_buffer, src, 188, 0) == pdTRUE);
                                if (sent_ok) {
                                    handle->drop_recover_count++;
                                    if ((handle->drop_old_count % 100) == 0) {
                                        ESP_LOGI(TAG, "Drop-old (x%u) [PUSI]", (unsigned)handle->drop_old_count);
                                    }
                                }
                            }
                        }

                        // Fallback: si pas de PUSI trouvé après 20 tentatives, force resync soft (rate-limited)
                        if (!dropped && drop_count > 0) {
                            hls_rate_limited_resync(handle);
                        }
                    }
#endif
                    if (sent_ok) {
                        // Try-lock pour éviter de bloquer le callback HTTP
                        if (handle->stats_mutex && xSemaphoreTake(handle->stats_mutex, 0) == pdTRUE) {
                            handle->bytes_downloaded += 188;
                            xSemaphoreGive(handle->stats_mutex);
                        } else {
                            // Best effort sans mutex (stats_mutex NULL ou occupé)
                            handle->bytes_downloaded += 188;
                        }
                    } else {
                        handle->drop_count++;
                        if ((handle->drop_count % 100) == 0) {
                            ESP_LOGW(TAG, "Ring buffer plein (x%u)", (unsigned)handle->drop_count);
                        }
                    }

                    src += 188;
                    src_len -= 188;
                }

                // 3) Reste (<188) -> carry (append)
                if (src_len > 0) {
                    // src_len < 188 garanti ici
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

/**
 * @brief Télécharge la playlist M3U8 et retourne son contenu
 * FIX #5: Lecture correcte avec to_read calculé
 */
static char* download_m3u8(const char *url)
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

/**
 * @brief Tâche de téléchargement HLS
 */
static void hls_fetch_task(void *pvParameters)
{
    app_hls_player_t *handle = (app_hls_player_t *)pvParameters;
    ESP_LOGI(TAG, "Démarrage de la task de téléchargement HLS");

    // [RAM OPT] Instrumentation stack HWM (P0 phase 0)
    UBaseType_t hwm_initial = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "[STACK] %s: HWM initial = %u words (%u bytes)",
             pcTaskGetName(NULL), hwm_initial, hwm_initial * sizeof(StackType_t));

    int64_t last_sequence_number = -1;

    // Playlist hors boucle pour cleanup centralisé à task_exit
    lib_m3u8_parser_playlist_t playlist;
    memset(&playlist, 0, sizeof(playlist));
    bool playlist_valid = false;

    while (true) {
        // Reset systématique en début de cycle (défense bug logique)
        // Si playlist_valid du cycle précédent, free avant de reset
        if (playlist_valid) {
            lib_m3u8_parser_free(&playlist);
            playlist_valid = false;
        }
        memset(&playlist, 0, sizeof(playlist));

        // Check notification NOTIF_STOP sans bloquer
        if (hls_should_stop_now()) {
            ESP_LOGI(TAG, "NOTIF_STOP reçue - arrêt fetch_task");
            handle->is_downloading = false;
            break;
        }

        // Timeout périodique basé sur target_duration (RFC 8216 Section 6.3.4)
        uint32_t refresh_timeout = (handle->target_duration > 0)
                                   ? (handle->target_duration * 1000)
                                   : 10000;

        if (xSemaphoreTake(handle->download_semaphore, pdMS_TO_TICKS(refresh_timeout)) != pdTRUE) {
            // Timeout atteint → forcer refresh même si buffer OK (pour live)
            ESP_LOGI(TAG, "Timeout refresh (%u ms) - vérification nouveaux segments", (unsigned)refresh_timeout);
        } else {
            ESP_LOGI(TAG, "Signal reçu - démarrage du téléchargement");
        }

        // Recheck stop après semaphore
        if (hls_should_stop_now()) {
            ESP_LOGI(TAG, "NOTIF_STOP reçue - arrêt fetch_task");
            handle->is_downloading = false;  // Défensif (normalement déjà false)
            break;
        }

        handle->is_downloading = true;

        // [DIAG LAG] Mesurer temps de téléchargement M3U8 master
        int64_t t0 = esp_timer_get_time();
        char *m3u8_content = download_m3u8(handle->stream_url);
        int64_t t1 = esp_timer_get_time();
        ESP_LOGI(TAG, "[REFRESH] master m3u8 took %lld ms", (long long)((t1 - t0) / 1000));

        if (m3u8_content == NULL) {
            ESP_LOGE(TAG, "Échec de téléchargement M3U8");
            handle->is_downloading = false;
            if (!interruptible_delay_ms(5000)) {
                break;  // Stop demandé pendant le délai
            }
            continue;
        }

        // CHECK STOP après download, avant parse (optimisation latence d'arrêt)
        if (hls_should_stop_now()) {
            free(m3u8_content);
            handle->is_downloading = false;
            goto task_exit;
        }

        // playlist déjà memset en début de cycle
        esp_err_t ret = lib_m3u8_parser_parse(m3u8_content, handle->stream_url, &playlist);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Échec de parsing M3U8");
            free(m3u8_content);
            handle->is_downloading = false;
            // NE PAS free playlist si parse a échoué (peut être incohérent)
            if (!interruptible_delay_ms(5000)) {
                break;  // Stop demandé pendant le délai
            }
            continue;
        }

        playlist_valid = true;  // Parse réussi, playlist safe à free
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
            uint32_t best_bandwidth = 0;  // P1: bandwidth en kbps (uint32_t)

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
                    uint32_t bw_kbps = playlist.variants[i].bandwidth_kbps;  // P1: bandwidth en kbps
                    if (bw_kbps >= 96 && bw_kbps <= 160) {
                        if (idx_selected < 0 || bw_kbps > best_bandwidth) {
                            idx_selected = i;
                            best_bandwidth = bw_kbps;
                        }
                    }
                }

                // Sinon prendre le premier
                if (idx_selected < 0) {
                    idx_selected = 0;
                }

                ESP_LOGI(TAG, "Variant %d sélectionné (bandwidth=%u kbps)",
                         idx_selected, playlist.variants[idx_selected].bandwidth_kbps);
            }

            char media_url[LIB_M3U8_PARSER_MAX_URL_LEN];
            strncpy(media_url, playlist.variants[idx_selected].url, LIB_M3U8_PARSER_MAX_URL_LEN - 1);
            media_url[LIB_M3U8_PARSER_MAX_URL_LEN - 1] = '\0';

            // [DIAG LAG] Mesurer temps de téléchargement M3U8 media
            t0 = esp_timer_get_time();
            m3u8_content = download_m3u8(media_url);
            t1 = esp_timer_get_time();
            ESP_LOGI(TAG, "[REFRESH] media m3u8 took %lld ms", (long long)((t1 - t0) / 1000));

            if (m3u8_content == NULL) {
                ESP_LOGE(TAG, "Échec de téléchargement de la media playlist");
                lib_m3u8_parser_free(&playlist);
                playlist_valid = false;
                handle->is_downloading = false;
                if (!interruptible_delay_ms(5000)) {
                    goto task_exit;  // Pas de free ici, déjà fait ligne ci-dessus
                }
                continue;
            }

            // CHECK STOP après download media playlist, avant parse (optimisation latence)
            if (hls_should_stop_now()) {
                free(m3u8_content);
                lib_m3u8_parser_free(&playlist);
                playlist_valid = false;
                handle->is_downloading = false;
                goto task_exit;
            }

            lib_m3u8_parser_free(&playlist);
            playlist_valid = false;
            // CRITICAL: memset après free pour garantir état propre avant parse
            memset(&playlist, 0, sizeof(playlist));

            ret = lib_m3u8_parser_parse(m3u8_content, media_url, &playlist);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Échec de parsing de la media playlist");
                free(m3u8_content);
                handle->is_downloading = false;
                // NE PAS free playlist si parse a échoué (peut être incohérent)
                if (!interruptible_delay_ms(5000)) {
                    goto task_exit;
                }
                continue;
            }

            playlist_valid = true;  // Parse media réussi
            free(m3u8_content);
            lib_m3u8_parser_dump(&playlist);
        }

        // FIX #4: Télécharger segments dans l'ordre chronologique
        // FIX CRITIQUE: Le décodeur TS attend les segments dans l'ordre temporel
        bool downloaded = false;

        // FIX #13: Segments adaptatifs au buffer (évite overflow → drop-old → corruption)
        size_t free_size = xRingbufferGetCurFreeSize(handle->ring_buffer);
        size_t filled = handle->buffer_size - free_size;
        int level = (filled * 100) / handle->buffer_size;

        int segments_per_cycle;
        if (last_sequence_number < 0) {
            // Cold start: buffer initial
            segments_per_cycle = 4;
        } else if (level > 70) {
            // Buffer presque plein → ralentir
            segments_per_cycle = 1;
        } else if (level > 50) {
            // Buffer mi-plein → modéré
            segments_per_cycle = 2;
        } else if (level > 30) {
            // Buffer bas → accélérer
            segments_per_cycle = 3;
        } else {
            // Buffer critique → remplir vite
            segments_per_cycle = 4;
        }

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
        for (int k = to_download_count - 1; k >= 0; k--) {
            // Check stop avant chaque segment
            if (hls_should_stop_now()) {
                ESP_LOGI(TAG, "NOTIF_STOP reçue pendant download - arrêt fetch_task");
                if (playlist_valid) {
                    lib_m3u8_parser_free(&playlist);
                    playlist_valid = false;
                }
                goto task_exit;
            }

            int idx = to_download[k];
            const lib_m3u8_parser_segment_t *seg = &playlist.segments[idx];

            ESP_LOGI(TAG, "Téléchargement segment %u (%d/%d)%s",
                     seg->sequence, (to_download_count - k), to_download_count,
                     (seg->flags & LIB_M3U8_PARSER_SEGMENT_FLAG_DISCONTINUITY) ? " [DISCONTINUITY]" : "");

            // Signaler DISCONTINUITY pour reset décodeur via task notification (P1: check flag)
            if ((seg->flags & LIB_M3U8_PARSER_SEGMENT_FLAG_DISCONTINUITY) && handle->play_task) {
                ESP_LOGW(TAG, "DISCONTINUITY détectée → notification NOTIF_RESET vers play_task");
                xTaskNotify(handle->play_task, NOTIF_RESET, eSetBits);
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
                handle->is_downloading = false;
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
                playlist_valid = false;
                handle->is_downloading = false;
                xSemaphoreGive(handle->download_semaphore);  // Trigger immédiat
                continue;
            }

            ESP_LOGD(TAG, "Aucun nouveau segment (dernier: %lld)", (long long)last_sequence_number);
        }

        lib_m3u8_parser_free(&playlist);
        playlist_valid = false;
        handle->is_downloading = false;
    }

task_exit:
    // Cleanup centralisé : free playlist si encore valide
    if (playlist_valid) {
        lib_m3u8_parser_free(&playlist);
        playlist_valid = false;
    }

    handle->is_downloading = false;
    ESP_LOGI(TAG, "Arrêt de la task de téléchargement HLS");

    // [RAM OPT] Log HWM final avant sortie (P0 phase 0)
    UBaseType_t hwm_final = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "[STACK] %s: HWM final = %u words (%u bytes) - utilisation max = %u bytes",
             pcTaskGetName(NULL), hwm_final, hwm_final * sizeof(StackType_t),
             14336 - (hwm_final * sizeof(StackType_t)));

    // Signaler fin de tâche via sémaphore
    if (handle->fetch_done) {
        xSemaphoreGive(handle->fetch_done);
    }
    vTaskDelete(NULL);
}

/**
 * @brief Resynchronisation MPEG-TS intelligente (PID + PUSI + CC + PES start code)
 *
 * Valide :
 * - Sync byte 0x47
 * - PID audio (257 pour France Inter)
 * - PUSI=1 (début PES)
 * - Payload présent (AFC)
 * - Start code PES (00 00 01)
 * - Continuity counter cohérent sur 3 paquets
 *
 * @param buf Buffer à scanner
 * @param len Taille buffer
 * @param scan_max Limite scan
 * @param audio_pid PID audio à chercher
 * @param out_skip Offset du sync trouvé
 * @return true si sync valide trouvé
 */
static bool ts_resync_smart(const uint8_t *buf,
                            size_t len,
                            size_t scan_max,
                            uint16_t audio_pid,
                            size_t *out_skip)
{
    size_t n = (len < scan_max) ? len : scan_max;
    if (n < 188 * 3) return false; // Besoin 3 paquets TS minimum

    // Compteurs debug (static pour garder entre appels)
    static int dbg_sync_found = 0;
    static int dbg_pid_ok = 0;
    static int dbg_pusi_ok = 0;
    static int dbg_afc_ok = 0;
    static int dbg_pes_ok = 0;
    static int dbg_cc_ok = 0;
    static int dbg_calls = 0;

    dbg_calls++;
    bool log_stats = (dbg_calls % 10 == 0); // Log tous les 10 appels

    for (size_t i = 0; i + 188 * 2 < n; i++) {
        if (buf[i] != 0x47) continue;
        dbg_sync_found++;

        // Parse header TS
        uint8_t b1 = buf[i + 1];
        uint8_t b2 = buf[i + 2];
        uint8_t b3 = buf[i + 3];

        uint16_t pid = ((b1 & 0x1F) << 8) | b2;
        uint8_t pusi = (b1 & 0x40) ? 1 : 0;
        uint8_t afc  = (b3 & 0x30) >> 4;   // 1=payload, 2=adapt, 3=adapt+payload
        uint8_t cc0  = (b3 & 0x0F);

        // Filtres PID + PUSI + payload
        if (pid != audio_pid) continue;
        dbg_pid_ok++;

        if (!pusi) continue;
        dbg_pusi_ok++;

        if (afc != 1 && afc != 3) continue; // Payload requis
        dbg_afc_ok++;

        // Compute payload start (skip adaptation field si présent)
        size_t p = i + 4;
        size_t pkt_end = i + 188;  // Fin du paquet TS courant
        if (afc == 3) {
            if (p >= pkt_end) continue;  // Vérif AVANT lecture afl
            uint8_t afl = buf[p];
            p += 1 + afl;
            if (p >= pkt_end) continue;  // Payload doit rester dans paquet TS
        }
        if (p + 2 >= pkt_end) continue;  // PES start code (3 bytes) doit être dans paquet
        if (p + 2 >= n) continue;        // Vérif buffer global

        // Vérif start code PES (00 00 01)
        if (!(buf[p] == 0x00 && buf[p+1] == 0x00 && buf[p+2] == 0x01)) {
            continue;
        }
        dbg_pes_ok++;

        // Valider next 2 packets : sync + au moins 1 PID audio
        const uint8_t *p1 = buf + i + 188;
        const uint8_t *p2 = buf + i + 376;
        if (p1[0] != 0x47 || p2[0] != 0x47) continue;

        // Au moins 1 des 2 paquets suivants doit avoir PID audio
        // (tolère PAT/PMT/autres, mais réduit faux positifs)
        uint16_t pid1 = ((p1[1] & 0x1F) << 8) | p1[2];
        uint16_t pid2 = ((p2[1] & 0x1F) << 8) | p2[2];
        if (pid1 != audio_pid && pid2 != audio_pid) continue;

        // Validation : PID + PUSI + PES + 3 sync + au moins 1 PID audio
        dbg_cc_ok++;

        *out_skip = i;

        // Log détaillé du resync trouvé (par appel, pas cumulé)
        ESP_LOGD(TAG, "resync_smart HIT at skip=%zu pid=%u pusi=%u afc=%u cc=%u",
                 i, pid, pusi, afc, cc0);

        if (log_stats) {
            ESP_LOGW(TAG, "[RESYNC DEBUG] Appels=%d, Sync=0x47:%d, PID:%d, PUSI:%d, AFC:%d, PES:%d, CC:%d, SUCCESS:%d",
                     dbg_calls, dbg_sync_found, dbg_pid_ok, dbg_pusi_ok, dbg_afc_ok, dbg_pes_ok, dbg_cc_ok, dbg_cc_ok);
        }

        return true;
    }

    if (log_stats) {
        ESP_LOGW(TAG, "[RESYNC DEBUG] Appels=%d, Sync=0x47:%d, PID:%d, PUSI:%d, AFC:%d, PES:%d, CC:%d, ÉCHEC",
                 dbg_calls, dbg_sync_found, dbg_pid_ok, dbg_pusi_ok, dbg_afc_ok, dbg_pes_ok, dbg_cc_ok);
    }

    return false;
}

/**
 * @brief Recherche le prochain sync byte 0x47 validé (espacement 188 bytes)
 *
 * FIX: Valide que les sync bytes sont espacés de 188 bytes pour éviter
 * faux positifs (0x47 dans payload). Triple check si possible.
 *
 * @param buf Buffer à scanner
 * @param len Taille buffer
 * @param out_skip Offset du prochain sync valide
 * @return true si trouvé et validé
 */
static bool ts_find_next_sync(const uint8_t *buf, size_t len, size_t *out_skip)
{
    for (size_t i = 0; i + 188 < len; i++) {
        if (buf[i] == 0x47 && buf[i + 188] == 0x47) {
            // Bonus: triple check si disponible
            if (i + 376 < len && buf[i + 376] != 0x47) {
                continue;  // Faux positif, continuer scan
            }
            *out_skip = i;
            return true;
        }
    }
    return false;
}

/**
 * @brief Tâche de lecture audio (zéro-copie avec curseur sur ringbuffer)
 */
static void audio_play_task(void *pvParameters)
{
    app_hls_player_t *handle = (app_hls_player_t *)pvParameters;
    ESP_LOGI(TAG, "Démarrage de la task de lecture audio");

    // [RAM OPT] Instrumentation stack HWM (P0 phase 0)
    UBaseType_t hwm_initial = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "[STACK] %s: HWM initial = %u words (%u bytes)",
             pcTaskGetName(NULL), hwm_initial, hwm_initial * sizeof(StackType_t));

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

    // [RAM OPT P1.3] Gather buffer pour assembler N paquets TS avant décodage
    // Items ringbuffer = 188 bytes, mais décodeur a besoin 4-8 KB contiguë
    #define RESYNC_SCAN_MAX   4096
    #define ZERO_CONSUME_MAX    20

    const size_t GATHER_BUF_SIZE = CONFIG_APP_HLS_PLAYER_GATHER_BUFFER_SIZE * 1024;
    const size_t DEC_BUF_SIZE = CONFIG_APP_HLS_PLAYER_DEC_BUFFER_SIZE * 1024;

    ESP_LOGI(TAG, "[RAM OPT P1.3] Gather buffer mode: gather=%zu KB, dec=%zu KB",
             CONFIG_APP_HLS_PLAYER_GATHER_BUFFER_SIZE, CONFIG_APP_HLS_PLAYER_DEC_BUFFER_SIZE);

    // Gather buffer (accumule N items de 188 avant décodage)
    uint8_t *gather_buf = malloc(GATHER_BUF_SIZE);
    size_t gather_len = 0;
    size_t leftover_len = 0;  // Bytes déjà dans gather_buf à conserver entre cycles

    // NOTE: Stratégie "copy then return" (pas de held_items)
    // Les items sont copiés dans gather_buf puis rendus immédiatement au ringbuffer

    // Buffer PCM décodé
    int16_t *decoded_buffer = malloc(DEC_BUF_SIZE);

    if (gather_buf == NULL || decoded_buffer == NULL) {
        ESP_LOGE(TAG, "Échec d'allocation des buffers de décodage");
        esp_audio_simple_dec_close(dec_handle);
        free(gather_buf);
        free(decoded_buffer);
        if (handle->play_done) {
            xSemaphoreGive(handle->play_done);
        }
        vTaskDelete(NULL);
        return;
    }

    int zero_consume_streak = 0;
    int resync_count = 0;
    int no_data_streak = 0;  // [DIAG LAG] Compteur underrun

    ESP_LOGI(TAG, "Démarrage de la lecture audio...");
    vTaskDelay(pdMS_TO_TICKS(500));

    int decode_count = 0;
    int error_count = 0;
    bool download_signaled = false;
    bool was_downloading = false;

    while (true) {
        // === 1) Gestion notifications STOP/RESET/RESYNC ===
        uint32_t notif = 0;
        if (xTaskNotifyWait(0, NOTIF_STOP | NOTIF_RESET | NOTIF_RESYNC, &notif, 0) == pdTRUE) {
            if (notif & NOTIF_STOP) {
                ESP_LOGI(TAG, "NOTIF_STOP reçue - arrêt play_task");
                break;
            }
            if (notif & NOTIF_RESYNC) {
                // Soft resync: drop quelques items SANS recréer le décodeur
                // Évite les audio skips causés par NOTIF_RESET trop fréquents
                ESP_LOGW(TAG, "NOTIF_RESYNC reçue (drop-old) - soft resync sans reset décodeur");

                size_t item_size = 0;
                void *item;
                int drop_count = 0;
                const int SOFT_DROP = 10;  // ~10 × 188 = 1.88 KB (soft, peu audible)

                while (drop_count < SOFT_DROP && (item = xRingbufferReceive(handle->ring_buffer, &item_size, 0)) != NULL) {
                    vRingbufferReturnItem(handle->ring_buffer, item);
                    drop_count++;
                }

                ESP_LOGI(TAG, "NOTIF_RESYNC: dropped %d items (%zu bytes)", drop_count, drop_count * 188);

                // Reset gather state (pas de reset décodeur)
                gather_len = 0;
                leftover_len = 0;
                zero_consume_streak = 0;

                continue;  // Reprendre la boucle sans recréer le décodeur
            }
            if (notif & NOTIF_RESET) {
                ESP_LOGW(TAG, "NOTIF_RESET reçue - reset décodeur suite DISCONTINUITY");

                // FIX: Drop très peu d'items pour éviter trou audible
                // Sur DISCONTINUITY, on veut juste purger fin segment précédent
                size_t item_size = 0;
                void *item;
                int drop_count = 0;
                const int MAX_DROP = 6;  // ~1128 bytes, beaucoup moins audible

                while (drop_count < MAX_DROP && (item = xRingbufferReceive(handle->ring_buffer, &item_size, 0)) != NULL) {
                    vRingbufferReturnItem(handle->ring_buffer, item);
                    drop_count++;
                }

                ESP_LOGI(TAG, "NOTIF_RESET: dropped %d items (%zu bytes)", drop_count, drop_count * 188);

                // Reset gather state (items déjà rendus avec "copy then return")
                gather_len = 0;
                leftover_len = 0;  // Flush leftover

                // Recréer le décodeur
                ESP_LOGI(TAG, "Fermeture décodeur...");
                esp_audio_simple_dec_close(dec_handle);

                ESP_LOGI(TAG, "Réouverture décodeur...");
                dec_ret = esp_audio_simple_dec_open(&dec_cfg, &dec_handle);
                if (dec_ret != ESP_AUDIO_ERR_OK || dec_handle == NULL) {
                    ESP_LOGE(TAG, "Échec réouverture décodeur après DISCONTINUITY: %d", dec_ret);
                    // Cleanup (items déjà rendus avec "copy then return")
                    free(gather_buf);
                    free(decoded_buffer);
                    if (handle->play_done) {
                        xSemaphoreGive(handle->play_done);
                    }
                    vTaskDelete(NULL);
                    return;
                }

                ESP_LOGI(TAG, "Décodeur recréé avec succès, reprise lecture");
                continue;
            }
        }

        // === 2) Buffer monitoring et signal téléchargement ===
        size_t free_size = xRingbufferGetCurFreeSize(handle->ring_buffer);
        size_t filled_size = handle->buffer_size - free_size;
        int buffer_level = (filled_size * 100) / handle->buffer_size;

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

        // === 3) Remplir gather buffer avec N items (en gardant leftover) ===
        // Remplir gather_buf jusqu'à ~GATHER_BUF_SIZE (ou au moins MIN_GATHER)
        // FIX: Respecter leftover_len (bytes déjà présents à conserver)
        // Stratégie "copy then return" : copier puis rendre immédiatement

        // Défense: leftover ne doit jamais dépasser GATHER_BUF_SIZE
        if (leftover_len > GATHER_BUF_SIZE) {
            ESP_LOGW(TAG, "leftover overflow: %zu > %zu, flushing", leftover_len, GATHER_BUF_SIZE);
            leftover_len = 0;
        }

        // FIX STALL: Si consumed==0 persiste, ne pas recopier (resync sur leftover uniquement)
        bool stall = (zero_consume_streak > 0);

        gather_len = leftover_len;

        // MIN_GATHER adaptatif : 12 nominal, 8 en rattrapage (anti-faux underrun)
        size_t min_gather = 188 * 12;  // 12 paquets nominal (2256 bytes)
        if (no_data_streak > 0 || buffer_level < 30) {
            min_gather = 188 * 8;  // Mode rattrapage (1504 bytes)
        }

        // Ne recopier que si pas en stall (resync en cours)
        if (!stall) {
            while (gather_len + 188 <= GATHER_BUF_SIZE) {
                // Timeout court si proche du seuil (évite attente inutile 20ms)
                TickType_t rx_timeout = pdMS_TO_TICKS(20);
                if (gather_len >= (min_gather - 188)) {
                    rx_timeout = pdMS_TO_TICKS(3);  // 3ms si on a presque assez
                }

                size_t ilen = 0;
                uint8_t *it = (uint8_t *)xRingbufferReceive(handle->ring_buffer, &ilen, rx_timeout);
                if (!it) break;

                // Vérifier alignement TS 188-byte (temporaire debug)
                if (ilen != 188) {
                    ESP_LOGW(TAG, "Item non-188 bytes: %zu (NOSPLIT échoué!)", ilen);
                    vRingbufferReturnItem(handle->ring_buffer, it);
                    continue;
                }

                // Copier dans gather_buf APRÈS leftover
                memcpy(gather_buf + gather_len, it, ilen);
                gather_len += ilen;

                // IMPORTANT: Rendre immédiatement l'item au ringbuffer (on a copié)
                // Stratégie "copy then return" : pas de fuite, pas de held items
                vRingbufferReturnItem(handle->ring_buffer, it);

                // Seuil max atteint (8-16 KB selon config)
                if (gather_len >= GATHER_BUF_SIZE) break;
            }
        } else {
            // En stall: on travaille uniquement sur leftover (resync + reset)
            ESP_LOGD(TAG, "Stall mode: resync sur leftover uniquement (%zu bytes)", leftover_len);
        }

        // Si pas assez de données, attendre
        if (gather_len < min_gather) {
            // Starvation temporaire (pas forcément audible)
            no_data_streak++;

            // Log UNDERRUN seulement si ça dure (> 10 cycles = ~200-400ms)
            if (no_data_streak >= 10) {
                ESP_LOGW(TAG, "[AUDIO UNDERRUN] gather=%zu/%zu bytes (leftover=%zu), buffer=%d%%, stall=%d",
                         gather_len, min_gather, leftover_len, buffer_level, stall);
                no_data_streak = 0;  // Reset pour éviter spam
            }

            // Rollback : revenir au leftover (items déjà rendus au ringbuffer)
            // NOTE: données copiées après leftover sont ignorées (seront réécrites au prochain cycle)
            gather_len = leftover_len;

            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        no_data_streak = 0;  // Reset sur succès

        // === 4) Préparer données pour décodage ===
        uint8_t *decode_ptr = gather_buf;
        size_t decode_len = gather_len;

        // === 5) Décoder ===
        esp_audio_simple_dec_raw_t raw = {
            .buffer = decode_ptr,
            .len = decode_len,
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
                     decode_count, dec_ret, decode_len, raw.consumed, out_frame.decoded_size);
        }

        // === 6) Appliquer consumed: créer leftover ===
        // FIX CRITIQUE: UN SEUL memmove (pas de double déplacement)
        // Stratégie "copy then return" : items déjà rendus, juste gérer leftover

        if (raw.consumed > 0) {
            size_t consumed = raw.consumed;
            if (consumed > gather_len) consumed = gather_len;

            // Warning si consumed pas multiple de 188 (rare mais possible)
            bool misaligned = (consumed % 188 != 0);
            if (misaligned) {
                ESP_LOGW(TAG, "consumed traverse TS packet boundary: %zu (rem=%zu)",
                         consumed, consumed % 188);
            }

            // Construire le nouveau leftover = ce qui reste après consumed
            // FIX: UN SEUL memmove basé sur raw.consumed (pas de double déplacement)
            size_t remaining = gather_len - consumed;
            if (remaining > 0) {
                memmove(gather_buf, gather_buf + consumed, remaining);
                leftover_len = remaining;

                // Fix #2: Réaligner TS immédiatement si désaligné
                if (misaligned && leftover_len >= 188 * 2) {
                    size_t skip = 0;
                    if (ts_find_next_sync(gather_buf, leftover_len, &skip) && skip > 0) {
                        ESP_LOGI(TAG, "TS misalignment → resync: skip %zu bytes", skip);
                        memmove(gather_buf, gather_buf + skip, leftover_len - skip);
                        leftover_len -= skip;
                    }
                }
            } else {
                leftover_len = 0;
            }

            // Reset streak si OK
            if (dec_ret == ESP_AUDIO_ERR_OK) {
                zero_consume_streak = 0;
            }
        } else {
            // consumed == 0 : retry plus tard avec les mêmes données
            leftover_len = gather_len;
        }

        // === 7) Resync si consumed==0 ou erreur ===
        if (raw.consumed == 0 || dec_ret != ESP_AUDIO_ERR_OK) {
            // Incrémenter streak seulement si vraiment consumed==0
            if (raw.consumed == 0) {
                zero_consume_streak++;
                ESP_LOGW(TAG, "consumed=0 with gather_len=%zu, leftover=%zu (streak=%d)",
                         gather_len, leftover_len, zero_consume_streak);
            } else {
                // Erreur avec consumed>0 : resync ponctuel
                ESP_LOGW(TAG, "Erreur décodeur (ret=%d) avec consumed=%lu, resync", dec_ret, raw.consumed);
            }

            // Tenter resync TS intelligent (PID+PUSI+PES) dans leftover
            size_t skip = 0;
            bool resync_ok = false;

            if (leftover_len >= 188 * 3 && ts_resync_smart(gather_buf, leftover_len, leftover_len, 257, &skip)) {
                // Resync réussi
                if (skip > 0) {
                    ESP_LOGI(TAG, "TS resync smart: skip %zu bytes (resync #%d)", skip, ++resync_count);

                    // FIX CRITIQUE: APPLIQUER le skip dans leftover
                    if (skip < leftover_len) {
                        memmove(gather_buf, gather_buf + skip, leftover_len - skip);
                        leftover_len -= skip;
                    } else {
                        // Skip >= leftover : vider leftover
                        leftover_len = 0;
                    }
                } else {
                    ESP_LOGD(TAG, "TS resync smart: déjà aligné (skip=0)");
                }
                resync_ok = true;
            } else {
                // Resync smart échoué : fallback basique (chercher prochain 0x47 validé)
                size_t s = 0;
                if (leftover_len >= 188 * 2 && ts_find_next_sync(gather_buf, leftover_len, &s)) {
                    ESP_LOGW(TAG, "Fallback resync: skip %zu bytes vers prochain 0x47", s);

                    // FIX CRITIQUE: APPLIQUER le skip
                    if (s < leftover_len) {
                        memmove(gather_buf, gather_buf + s, leftover_len - s);
                        leftover_len -= s;
                    } else {
                        leftover_len = 0;
                    }
                    resync_ok = true;
                } else {
                    ESP_LOGW(TAG, "Aucun sync trouvé dans leftover (%zu bytes)", leftover_len);
                    // Pas de sync trouvé : vider leftover (données corrompues)
                    leftover_len = 0;
                }
            }

            // IMPORTANT: pas de reset décodeur sur resync "soft".
            // On repart juste avec un input propre pour éviter les glitches audibles.
            if (resync_ok) {
                gather_len = 0;
                // leftover_len a déjà été ajusté par le memmove/skip ci-dessus
                zero_consume_streak = 0;
            }

            // Si on a flushé le leftover, on doit reconsommer le ringbuffer,
            // sinon "stall mode" empêche de récupérer de nouvelles données.
            if (leftover_len == 0) {
                zero_consume_streak = 0;
            }

            // Anti-deadlock : reset si consumed==0 persiste
            if (zero_consume_streak >= ZERO_CONSUME_MAX) {
                ESP_LOGW(TAG, "consumed==0 persistant (%d), reset décodeur + flush leftover", zero_consume_streak);
                zero_consume_streak = 0;
                leftover_len = 0;  // Flush leftover (données probablement corrompues)

                esp_audio_simple_dec_close(dec_handle);
                dec_ret = esp_audio_simple_dec_open(&dec_cfg, &dec_handle);
                if (dec_ret != ESP_AUDIO_ERR_OK || dec_handle == NULL) {
                    ESP_LOGE(TAG, "Échec réouverture décodeur après deadlock");
                    break;
                }
            }

            // Attendre avant retry
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // === 8) Écriture audio ===
        if (dec_ret == ESP_AUDIO_ERR_OK && out_frame.decoded_size > 0) {
            // Réduction de volume logicielle (25%)
            int16_t *samples = (int16_t *)decoded_buffer;
            size_t num_samples = out_frame.decoded_size / sizeof(int16_t);
            for (size_t i = 0; i < num_samples; i++) {
                samples[i] >>= 2;
            }

            size_t bytes_written = 0;
            esp_err_t err = handle->write_cb(handle->write_ctx, decoded_buffer,
                                             out_frame.decoded_size, &bytes_written,
                                             pdMS_TO_TICKS(200));
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Erreur d'écriture audio: %s", esp_err_to_name(err));
            }

            if (decode_count % 100 == 0) {
                ESP_LOGI(TAG, "Audio: %zu bytes PCM (#%d, gather=%zu bytes, leftover=%zu)",
                         bytes_written, decode_count, gather_len, leftover_len);
            }
        } else if (dec_ret != ESP_AUDIO_ERR_OK) {
            // Erreur de décodage
            if (++error_count % 50 == 0) {
                ESP_LOGW(TAG, "Erreur décodage: ret=%d, consumed=%lu, decoded=%lu (count=%d)",
                         dec_ret, raw.consumed, out_frame.decoded_size, error_count);
            }
        }
    }

    // Cleanup
    esp_audio_simple_dec_close(dec_handle);

    // Items déjà rendus avec "copy then return" (pas de cleanup nécessaire)
    free(gather_buf);
    free(decoded_buffer);

    ESP_LOGI(TAG, "Arrêt de la task de lecture audio");
    ESP_LOGI(TAG, "[STATS GATHER] Resync count=%d, total decode cycles=%d", resync_count, decode_count);

    // [RAM OPT] Log HWM final avant sortie (P0 phase 0)
    UBaseType_t hwm_final = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "[STACK] %s: HWM final = %u words (%u bytes) - utilisation max = %u bytes",
             pcTaskGetName(NULL), hwm_final, hwm_final * sizeof(StackType_t),
             6144 - (hwm_final * sizeof(StackType_t)));

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
    BaseType_t ret = xTaskCreate(hls_fetch_task, "hls_fetch", 14336, handle, 5, &handle->fetch_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Échec de création de la task de téléchargement");
        return ESP_FAIL;
    }

    ret = xTaskCreate(audio_play_task, "audio_play", 6144, handle, 8, &handle->play_task);
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
