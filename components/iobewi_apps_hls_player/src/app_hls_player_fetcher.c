/**
 * @file app_hls_player_fetcher.c
 * @brief Implémentation du module de téléchargement HLS (fetch task)
 */

#include "app_hls_player/app_hls_player_fetcher.h"
#include "app_hls_player/app_hls_player_internal.h"
#include "app_hls_player/app_hls_player_http.h"
#include "lib_m3u8_parser/lib_m3u8_parser.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "hls_fetcher";

typedef enum {
    HLS_CATCHUP_STATE_STEADY = 0,
    HLS_CATCHUP_STATE_CATCHUP = 1,
} hls_catchup_state_t;

static const char *hls_catchup_state_str(hls_catchup_state_t state)
{
    return (state == HLS_CATCHUP_STATE_CATCHUP) ? "CATCHUP" : "STEADY";
}

static int hls_clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static int64_t hls_clamp_i64(int64_t value, int64_t min_value, int64_t max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static inline void hls_compute_cycle_other_ms(int64_t cycle_start_us,
                                             int64_t master_m3u8_ms,
                                             int64_t media_m3u8_ms,
                                             int64_t ts_sum_ms,
                                             int64_t *cycle_ms,
                                             int64_t *other_ms)
{
    int64_t c = (esp_timer_get_time() - cycle_start_us) / 1000;
    int64_t o = c - master_m3u8_ms - media_m3u8_ms - ts_sum_ms;
    if (o < 0) {
        o = 0;
    }
    *cycle_ms = c;
    *other_ms = o;
}
static int hls_compute_live_edge_backoff(int rb_level_pct)
{
#if CONFIG_APP_HLS_LIVE_CATCHUP_ENABLE
    const int min_backoff = CONFIG_APP_HLS_LIVE_EDGE_BACKOFF_MIN;
    const int max_backoff = CONFIG_APP_HLS_LIVE_EDGE_BACKOFF_MAX;
#else
    const int min_backoff = 1;
    const int max_backoff = 3;
#endif
    int backoff;

    if (rb_level_pct <= 25) {
        backoff = max_backoff;
    } else if (rb_level_pct <= 60) {
        backoff = (min_backoff + max_backoff) / 2;
    } else {
        backoff = min_backoff;
    }

    return hls_clamp_int(backoff, min_backoff, max_backoff);
}

static bool hls_resync_to_live_edge(int rb_level_pct,
                                    int64_t oldest_in_playlist,
                                    int64_t newest_in_playlist,
                                    int64_t *last_sequence_number,
                                    int *selected_backoff,
                                    int64_t *selected_start_seq)
{
    if (last_sequence_number == NULL || newest_in_playlist < oldest_in_playlist) {
        return false;
    }

    const int backoff = hls_compute_live_edge_backoff(rb_level_pct);
    int64_t start_seq = newest_in_playlist - backoff;
    start_seq = hls_clamp_i64(start_seq, oldest_in_playlist, newest_in_playlist);

    int64_t last_before = *last_sequence_number;
    *last_sequence_number = start_seq - 1;

    if (selected_backoff != NULL) {
        *selected_backoff = backoff;
    }
    if (selected_start_seq != NULL) {
        *selected_start_seq = start_seq;
    }

    ESP_LOGW(TAG, "→ RESYNC near-edge: reprise seq=%lld (oldest=%lld newest=%lld backoff=%d last_before=%lld last_after=%lld)",
             (long long)start_seq,
             (long long)oldest_in_playlist,
             (long long)newest_in_playlist,
             backoff,
             (long long)last_before,
             (long long)*last_sequence_number);

    return true;
}

static int hls_compute_segments_per_cycle(size_t buffer_size,
                                          int64_t last_sequence_number,
                                          hls_catchup_state_t catchup_state,
                                          int rb_level_pct)
{
#if CONFIG_APP_HLS_LIVE_CATCHUP_ENABLE
    const int steady_max = CONFIG_APP_HLS_SEG_PER_CYCLE_STEADY_MAX;
    const int catchup_max = CONFIG_APP_HLS_SEG_PER_CYCLE_CATCHUP_MAX;
#else
    const int steady_max = 2;
    const int catchup_max = 2;
#endif

    if (last_sequence_number < 0) {
        const int avg_segment_size = 130 * 1024;
        int max_initial = (int)((buffer_size * 80u / 100u) / avg_segment_size);
        if (max_initial < 1) {
            max_initial = 1;
        }
        if (max_initial > 2) {
            max_initial = 2;
        }
        return max_initial;
    }

    if (catchup_state == HLS_CATCHUP_STATE_CATCHUP) {
        int catchup_spc = 1;
        if (rb_level_pct < 40) {
            catchup_spc = 3;
        } else if (rb_level_pct <= 70) {
            catchup_spc = 2;
        }
        return hls_clamp_int(catchup_spc, 1, catchup_max);
    }

    int steady_spc = (rb_level_pct > 70) ? 1 : 2;
    return hls_clamp_int(steady_spc, 1, steady_max);
}


static void hls_compute_ts_admission_thresholds(const app_hls_player_t *handle,
                                               size_t *start_min_free,
                                               size_t *resume_min_free)
{
    if (start_min_free == NULL || resume_min_free == NULL) {
        return;
    }

    if (handle == NULL || handle->buffer_size <= 188) {
        *start_min_free = 0;
        *resume_min_free = 0;
        return;
    }

    size_t cap = handle->buffer_size - 188;
    size_t start = CONFIG_APP_HLS_RB_START_TS_MIN_FREE_BYTES;
    size_t resume = CONFIG_APP_HLS_RB_RESUME_TS_MIN_FREE_BYTES;

    if (start > cap) {
        start = cap;
    }
    if (resume > cap) {
        resume = cap;
    }
    if (resume < start) {
        resume = start;
    }

    *start_min_free = start;
    *resume_min_free = resume;
}

static bool hls_ts_admission_should_block(app_hls_player_t *handle,
                                          bool *ts_admission_blocked,
                                          size_t rb_free)
{
    if (handle == NULL || ts_admission_blocked == NULL) {
        return false;
    }
#if CONFIG_APP_HLS_RB_GATING_ENABLE
    size_t start_min_free = 0;
    size_t resume_min_free = 0;
    hls_compute_ts_admission_thresholds(handle, &start_min_free, &resume_min_free);

    if (*ts_admission_blocked && rb_free >= resume_min_free) {
        *ts_admission_blocked = false;
        if (hls_log_mode_at_least(HLS_LOG_MODE_DIAG_LIGHT) &&
            hls_log_throttle_time("ts_admission_resume", CONFIG_APP_HLS_LOG_THROTTLE_MS)) {
            ESP_LOGI(TAG, "TS_ADMISSION resume free=%uB start=%uB resume=%uB",
                     (unsigned)rb_free,
                     (unsigned)start_min_free,
                     (unsigned)resume_min_free);
        }
    }

    if ((!*ts_admission_blocked && rb_free < start_min_free) ||
        (*ts_admission_blocked && rb_free < resume_min_free)) {
        const bool was_blocked = *ts_admission_blocked;
        *ts_admission_blocked = true;
        if (!was_blocked && hls_log_throttle_time("ts_admission_block", CONFIG_APP_HLS_LOG_THROTTLE_MS)) {
            ESP_LOGW(TAG, "TS_ADMISSION block free=%uB start=%uB resume=%uB",
                     (unsigned)rb_free,
                     (unsigned)start_min_free,
                     (unsigned)resume_min_free);
        }
        return true;
    }

    return false;
#else
    size_t rb_filled = handle->buffer_size - rb_free;
    int level = (rb_filled * 100) / handle->buffer_size;
    const int admission_high = CONFIG_APP_HLS_PLAYER_TS_ADMISSION_HIGH;
    const int admission_low = CONFIG_APP_HLS_PLAYER_TS_ADMISSION_LOW;

    if (*ts_admission_blocked && level <= admission_low) {
        *ts_admission_blocked = false;
        if (hls_log_mode_at_least(HLS_LOG_MODE_DIAG_LIGHT) &&
            hls_log_throttle_time("ts_admission_resume", CONFIG_APP_HLS_LOG_THROTTLE_MS)) {
            ESP_LOGI(TAG, "TS_ADMISSION resume rb=%d%% high=%d low=%d action=download", level, admission_high, admission_low);
        }
    }

    if ((!*ts_admission_blocked && level >= admission_high) ||
        (*ts_admission_blocked && level > admission_low)) {
        const bool was_blocked = *ts_admission_blocked;
        *ts_admission_blocked = true;
        if (!was_blocked && hls_log_throttle_time("ts_admission_block", CONFIG_APP_HLS_LOG_THROTTLE_MS)) {
            ESP_LOGW(TAG, "TS_ADMISSION block rb=%d%% high=%d low=%d action=wait", level, admission_high, admission_low);
        }
        return true;
    }

    return false;
#endif
}

static void hls_notify_audio_reset_if_needed(const app_hls_player_t *handle,
                                             uint32_t *resets_audio_count,
                                             const char *reason)
{
    if (handle->play_task) {
        xTaskNotify(handle->play_task, NOTIF_RESET, eSetBits);
        if (resets_audio_count != NULL) {
            (*resets_audio_count)++;
        }
        ESP_LOGW(TAG, "NOTIF_RESET envoyé (%s)", reason);
    }
}



/**
 * @brief Download M3U8 avec retry rapide exponentiel en cas d'échec
 *
 * Évite les trous audio longs (~15s) causés par TLS connect timeout.
 * Retry : 250ms, 500ms, 1s, 2s (budget total ~4s) avant abandon.
 *
 * @param url URL de la playlist M3U8
 * @return Contenu M3U8 alloué (free par appelant) ou NULL si échec total
 */
static char *hls_http_download_m3u8_with_retry(const char *url)
{
    const int retry_delays_ms[] = {250, 500, 1000, 2000};
    const int retry_count = sizeof(retry_delays_ms) / sizeof(retry_delays_ms[0]);

    for (int attempt = 0; attempt < retry_count; attempt++) {
        char *m3u8_content = hls_http_download_m3u8(url);

        if (m3u8_content != NULL) {
            if (attempt > 0) {
                ESP_LOGW(TAG, "M3U8 refresh recovered after %d retries", attempt);
            }
            return m3u8_content;
        }

        // Échec : retry après délai exponentiel (sauf au dernier essai)
        if (attempt < retry_count - 1) {
            ESP_LOGW(TAG, "M3U8 refresh failed, retry in %d ms (%d/%d)",
                     retry_delays_ms[attempt], attempt + 1, retry_count);
            vTaskDelay(pdMS_TO_TICKS(retry_delays_ms[attempt]));
        }
    }

    ESP_LOGE(TAG, "M3U8 refresh failed after %d retries", retry_count);
    return NULL;
}

/**
 * @brief Tâche de téléchargement HLS
 */
void hls_fetch_task(void *pvParameters)
{
    app_hls_player_t *handle = (app_hls_player_t *)pvParameters;
    if (hls_log_mode_at_least(HLS_LOG_MODE_RUN)) {
        ESP_LOGI(TAG, "Démarrage de la task de téléchargement HLS");
    }

    // [RAM OPT] Instrumentation stack HWM (P0 phase 0)
#if CONFIG_APP_HLS_PLAYER_STACK_DIAG
    UBaseType_t hwm_initial = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "[STACK] %s: HWM initial = %u words (%u bytes) [sizeof(StackType_t)=%u]",
             pcTaskGetName(NULL), hwm_initial, hwm_initial * sizeof(StackType_t), sizeof(StackType_t));
#endif

    int64_t last_sequence_number = -1;
    bool ts_admission_blocked = false;
    hls_catchup_state_t catchup_state = HLS_CATCHUP_STATE_STEADY;
    int catchup_enter_hits = 0;
    int catchup_exit_hits = 0;
    int64_t last_hard_resync_us = 0;
    int64_t last_soft_resync_us = 0;

    uint32_t catchup_enter_count = 0;
    uint32_t catchup_exit_count = 0;
    uint32_t resync_hard_count = 0;
    uint32_t resync_soft_count = 0;
    uint32_t catchup_cycles = 0;
    uint32_t resets_audio_count = 0;
    uint32_t catchup_admission_block_cycles = 0;
    uint32_t catchup_admission_block_segment_waits = 0;
    uint32_t no_next_segment_cycles = 0;
    uint32_t no_download_cycles = 0;
    int64_t gap_sum = 0;
    uint32_t gap_samples = 0;
    int64_t gap_max = 0;

    // Playlist hors boucle pour cleanup centralisé à task_exit
    lib_m3u8_parser_playlist_t playlist;
    memset(&playlist, 0, sizeof(playlist));
    bool playlist_valid = false;

    // [TODO OPTIMIZATION] Refresh master rare (60s au lieu de 10s)
    // Master change quasi jamais, mais nécessite de cacher variant_url
    // Pour l'instant : refresh à chaque cycle (Fix 1+2 suffisent pour UNDERRUN)
    // int master_refresh_counter = 0;

    while (true) {
#if CONFIG_APP_HLS_PLAYER_STACK_DIAG
        // [P0.1] Log HWM périodique toutes les 5s (debug only)
        // HWM = free min (marge restante), pas usage
        // Usage peak = S_allocated - HWM
        static int64_t last_log_us = 0;
        int64_t now = esp_timer_get_time();
        if (now - last_log_us > 5 * 1000 * 1000) {
            last_log_us = now;
            UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
            ESP_LOGD(TAG, "[STACK] HWM=%u words (%u bytes free min) [hls_fetch]",
                     (unsigned)hwm, (unsigned)(hwm * sizeof(StackType_t)));
        }
#endif

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
        int64_t cycle_start_us = esp_timer_get_time();
        int64_t master_m3u8_ms = 0;
        int64_t media_m3u8_ms = 0;

        // [DIAG LAG + FIX UNDERRUN] Retry rapide exponentiel (250ms, 500ms, 1s, 2s)
        int64_t t0 = esp_timer_get_time();
        char *m3u8_content = hls_http_download_m3u8_with_retry(handle->stream_url);
        int64_t t1 = esp_timer_get_time();
        master_m3u8_ms = (t1 - t0) / 1000;
        ESP_LOGD(TAG, "[REFRESH] m3u8 took %lld ms", (long long)master_m3u8_ms);

        if (m3u8_content == NULL) {
            ESP_LOGE(TAG, "Échec de téléchargement M3U8");
            handle->is_downloading = false;
            if (!hls_interruptible_delay_ms(5000)) {
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
            if (!hls_interruptible_delay_ms(5000)) {
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

        if (hls_log_mode_at_least(HLS_LOG_MODE_DIAG_HEAVY)) {
            lib_m3u8_parser_dump(&playlist);
        }

        // FIX #2: Gestion master playlist avec logique déterministe
        // Utilise maintenant variant_count au lieu de segment_count
        if (playlist.is_master_playlist && playlist.variant_count > 0) {
            if (hls_log_mode_at_least(HLS_LOG_MODE_RUN)) {
                ESP_LOGI(TAG, "Master playlist détectée, sélection de la meilleure qualité...");
            }

            // Politique: hifi > midfi > lofi (par nom OU par bandwidth)
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

            // Sélection par nom (préféré) : HIFI PRIORITAIRE
            if (idx_hifi >= 0) {
                idx_selected = idx_hifi;
                ESP_LOGI(TAG, "Qualité HIFI sélectionnée (~192-320 kbps)");
            } else if (idx_midfi >= 0) {
                idx_selected = idx_midfi;
                ESP_LOGI(TAG, "Qualité MIDFI sélectionnée (~128 kbps)");
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

            // [DIAG LAG + FIX UNDERRUN] Retry rapide exponentiel (250ms, 500ms, 1s, 2s)
            t0 = esp_timer_get_time();
            m3u8_content = hls_http_download_m3u8_with_retry(media_url);
            t1 = esp_timer_get_time();
            media_m3u8_ms = (t1 - t0) / 1000;
            if (hls_log_mode_at_least(HLS_LOG_MODE_DIAG_LIGHT) &&
                hls_log_throttle_time("media_refresh_ms", CONFIG_APP_HLS_LOG_THROTTLE_MS)) {
                ESP_LOGI(TAG, "[REFRESH] media m3u8 took %lld ms", (long long)media_m3u8_ms);
            }

            if (m3u8_content == NULL) {
                ESP_LOGE(TAG, "Échec de téléchargement de la media playlist");
                lib_m3u8_parser_free(&playlist);
                playlist_valid = false;
                if (hls_log_mode_at_least(HLS_LOG_MODE_DIAG_LIGHT) &&
                    hls_log_throttle_time("fetch_timing_early_wait", CONFIG_APP_HLS_LOG_THROTTLE_MS)) {
                    ESP_LOGI(TAG, "[TIMING] fetch cycle early-wait: %lld ms",
                             (long long)((esp_timer_get_time() - cycle_start_us) / 1000));
                }
                handle->is_downloading = false;
                if (!hls_interruptible_delay_ms(5000)) {
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
                if (!hls_interruptible_delay_ms(5000)) {
                    goto task_exit;
                }
                continue;
            }

            playlist_valid = true;  // Parse media réussi
            free(m3u8_content);

            // Mettre à jour target_duration aussi pour media playlist issue d'une master
            if (playlist.target_duration > 0) {
                handle->target_duration = playlist.target_duration;
            }

            if (hls_log_mode_at_least(HLS_LOG_MODE_DIAG_HEAVY)) {
                lib_m3u8_parser_dump(&playlist);
            }
        }

        // FIX #4: Télécharger segments dans l'ordre chronologique
        // FIX CRITIQUE: Le décodeur TS attend les segments dans l'ordre temporel
        bool any_downloaded_this_cycle = false;
        bool hole_detected = false;
        int downloaded_segments = 0;  // Segments HTTP téléchargés avec succès
        int advanced_segments = 0;    // Segments ayant avancé last_sequence_number
        int64_t ts_sum_ms = 0;        // Somme durées download TS du cycle

        // FIX #13: Segments adaptatifs au buffer (évite overflow → drop-old → corruption)
        size_t free_size = xRingbufferGetCurFreeSize(handle->ring_buffer);
        size_t filled = handle->buffer_size - free_size;
        int level = (filled * 100) / handle->buffer_size;

        int segments_per_cycle = hls_compute_segments_per_cycle(handle->buffer_size,
                                                                last_sequence_number,
                                                                catchup_state,
                                                                level);

        // FIX: Log cold start pour faciliter debug terrain
        if (last_sequence_number < 0) {
            if (hls_log_mode_at_least(HLS_LOG_MODE_RUN) &&
                hls_log_throttle_burst("cold_start_segments", CONFIG_APP_HLS_LOG_BURST_COUNT, CONFIG_APP_HLS_LOG_BURST_WINDOW_MS)) {
                ESP_LOGI(TAG, "Cold start: téléchargement de %d segments initiaux (limite anti-overflow)", segments_per_cycle);
            }
        }

        bool was_blocked_cycle = ts_admission_blocked;
        if (hls_ts_admission_should_block(handle, &ts_admission_blocked, free_size)) {
            if (catchup_state == HLS_CATCHUP_STATE_CATCHUP && !was_blocked_cycle) {
                catchup_admission_block_cycles++;
            }
            handle->is_downloading = false;
            if (!hls_interruptible_delay_ms(CONFIG_APP_HLS_RB_GATING_POLL_MS)) {
                goto task_exit;
            }
            continue;
        }

        // Phase 1 (FIX): Collecter segments SEQUENTIELS pour éviter les trous audio
        int to_download[8];  // Max 8 segments (largement suffisant pour cold start)
        int to_download_count = 0;
        int64_t wanted_seq = (last_sequence_number >= 0) ? (last_sequence_number + 1) : -1;

        if (last_sequence_number < 0) {
            // Cold start: prendre les N derniers disponibles mais dans l'ordre chronologique
            int start_idx = playlist.segment_count - segments_per_cycle;
            if (start_idx < 0) {
                start_idx = 0;
            }

            for (int i = start_idx; i < playlist.segment_count && to_download_count < segments_per_cycle; i++) {
                to_download[to_download_count++] = i;
            }
        } else {
            for (int i = 0; i < playlist.segment_count && to_download_count < segments_per_cycle; i++) {
                if (playlist.segments[i].sequence == wanted_seq) {
                    for (int j = i; j < playlist.segment_count && to_download_count < segments_per_cycle; j++) {
                        int64_t expected = wanted_seq + (j - i);
                        if (playlist.segments[j].sequence != expected) {
                            break;
                        }
                        to_download[to_download_count++] = j;
                    }
                    break;
                }
            }
        }

        int64_t oldest_in_playlist = -1;
        int64_t newest_in_playlist = -1;
        if (playlist.segment_count > 0) {
            oldest_in_playlist = playlist.segments[0].sequence;
            newest_in_playlist = playlist.segments[playlist.segment_count - 1].sequence;

#if CONFIG_APP_HLS_LIVE_CATCHUP_ENABLE
            if (last_sequence_number >= 0) {
                int64_t gap = oldest_in_playlist - (last_sequence_number + 1);
                if (gap > 0) {
                    catchup_enter_hits++;
                    if (gap > gap_max) {
                        gap_max = gap;
                    }
                    gap_sum += gap;
                    gap_samples++;
                } else {
                    catchup_enter_hits = 0;
                }

                if (catchup_state == HLS_CATCHUP_STATE_STEADY &&
                    catchup_enter_hits >= CONFIG_APP_HLS_CATCHUP_ENTER_CONSECUTIVE) {
                    catchup_state = HLS_CATCHUP_STATE_CATCHUP;
                    catchup_enter_count++;
                    catchup_exit_hits = 0;
                    ESP_LOGW(TAG, "catchup_enter gap=%lld hits=%d window=[%lld..%lld] last=%lld",
                             (long long)gap, catchup_enter_hits,
                             (long long)oldest_in_playlist,
                             (long long)newest_in_playlist,
                             (long long)last_sequence_number);
                }
            }
#endif
        }

        if (to_download_count > 0) {
            if (hls_log_mode_at_least(HLS_LOG_MODE_DIAG_LIGHT) &&
                hls_log_throttle_time("cycle_download", CONFIG_APP_HLS_LOG_THROTTLE_MS)) {
                ESP_LOGI(TAG, "Cycle download %d seg: seq %lld .. %lld (rb=%d%%)",
                         to_download_count,
                         (long long)playlist.segments[to_download[0]].sequence,
                         (long long)playlist.segments[to_download[to_download_count - 1]].sequence,
                         level);
            }
        } else if (playlist.segment_count > 0 && last_sequence_number >= 0) {
            if (catchup_state == HLS_CATCHUP_STATE_CATCHUP) {
                no_download_cycles++;
            }

            if (oldest_in_playlist <= last_sequence_number + 1) {
                if (catchup_state == HLS_CATCHUP_STATE_CATCHUP) {
                    no_next_segment_cycles++;
                }

                if (hls_log_mode_at_least(HLS_LOG_MODE_DIAG_LIGHT) &&
                    hls_log_throttle_time("no_next_segment", CONFIG_APP_HLS_LOG_THROTTLE_MS)) {
                    ESP_LOGI(TAG, "No next segment yet (wanted=%lld, last=%lld, window=[%lld..%lld], count=%d) - wait",
                             (long long)wanted_seq,
                             (long long)last_sequence_number,
                             (long long)oldest_in_playlist,
                             (long long)newest_in_playlist,
                             playlist.segment_count);
                }
                handle->is_downloading = false;
                if (!hls_interruptible_delay_ms(250)) {
                    goto task_exit;
                }
                continue;
            }
        }

        // Phase 2: Télécharger dans l'ordre chronologique (séquentiel)
        for (int k = 0; k < to_download_count; k++) {
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

            // Admission control au niveau segment: ne jamais démarrer un TS quand RB est haut.
            while (true) {
                size_t seg_free = xRingbufferGetCurFreeSize(handle->ring_buffer);
                bool was_blocked = ts_admission_blocked;
                if (hls_ts_admission_should_block(handle, &ts_admission_blocked, seg_free)) {
                    if (catchup_state == HLS_CATCHUP_STATE_CATCHUP) {
                        if (!was_blocked) {
                            catchup_admission_block_cycles++;
                        }
                        catchup_admission_block_segment_waits++;
                    }
                    if (!hls_interruptible_delay_ms(CONFIG_APP_HLS_RB_GATING_POLL_MS)) {
                        goto task_exit;
                    }
                    continue;
                }
                break;
            }

            if (hls_log_mode_at_least(HLS_LOG_MODE_DIAG_HEAVY) &&
                hls_log_throttle_every_n("segment_download", CONFIG_APP_HLS_LOG_SAMPLE_N_SLOW)) {
                ESP_LOGI(TAG, "Téléchargement segment %lld (%d/%d)%s",
                         (long long)seg->sequence, (k + 1), to_download_count,
                         (seg->flags & LIB_M3U8_PARSER_SEGMENT_FLAG_DISCONTINUITY) ? " [DISCONTINUITY]" : "");
            }

            // Signaler DISCONTINUITY pour reset décodeur via task notification (P1: check flag)
            if (seg->flags & LIB_M3U8_PARSER_SEGMENT_FLAG_DISCONTINUITY) {
                hls_notify_audio_reset_if_needed(handle, &resets_audio_count, "discontinuity");
            }

            int64_t seg_t0 = esp_timer_get_time();
            esp_err_t err = hls_http_download_segment(handle, seg->url);
            int64_t seg_ms = (esp_timer_get_time() - seg_t0) / 1000;
            ts_sum_ms += seg_ms;

            int warn_ts_ms = 2500;
            if (handle->target_duration > 0) {
                int td_ms = handle->target_duration * 1000;
                int td_based = td_ms - 800;
                if (td_based < 500) {
                    td_based = 500;
                }
                if (td_based > warn_ts_ms) {
                    warn_ts_ms = td_based;
                }
            }

            int64_t body_ms = handle->last_seg_metrics.body_read_ms;
            uint32_t kbps = 0;
            if (body_ms > 0) {
                kbps = (uint32_t)(((uint64_t)handle->last_seg_metrics.body_bytes * 8ULL) / (uint64_t)body_ms);
            }

            if (hls_log_mode_at_least(HLS_LOG_MODE_DIAG_LIGHT) &&
                hls_log_throttle_every_n("segment_metrics", CONFIG_APP_HLS_LOG_SAMPLE_N_FAST)) {
                ESP_LOGI(TAG, "SEG seq=%lld reuse=%d open_ms=%lld headers_ms=%lld body_read_ms=%lld max_read_block_ms=%lld bytes=%u kbps=%u rb_wait_ms=%lld gate_wait_ms=%lld rb_retry=%u rb_free_min=%u retry=%d",
                         (long long)seg->sequence,
                         handle->last_seg_metrics.reuse ? 1 : 0,
                         (long long)handle->last_seg_metrics.open_ms,
                         (long long)handle->last_seg_metrics.headers_ms,
                         (long long)handle->last_seg_metrics.body_read_ms,
                         (long long)handle->last_seg_metrics.max_read_block_ms,
                         (unsigned)handle->last_seg_metrics.body_bytes,
                         kbps,
                         (long long)handle->last_seg_metrics.rb_wait_ms,
                         (long long)handle->last_seg_metrics.rb_gating_wait_ms,
                         (unsigned)handle->last_seg_metrics.rb_send_fail_retries,
                         (unsigned)handle->last_seg_metrics.rb_free_min,
                         handle->last_seg_metrics.retried ? 1 : 0);
            }

            if (err == ESP_OK) {
                downloaded_segments++;

                // N'avancer la séquence que sur continuité stricte (ou cold start)
                if (last_sequence_number < 0 || seg->sequence == (last_sequence_number + 1)) {
                    last_sequence_number = seg->sequence;
                    any_downloaded_this_cycle = true;
                    advanced_segments++;
                } else {
                    ESP_LOGW(TAG, "Trou de séquence: last=%lld, got=%lld (pas d'avance)",
                             (long long)last_sequence_number, (long long)seg->sequence);
                    hls_notify_audio_reset_if_needed(handle, &resets_audio_count, "sequence_hole");
                    hole_detected = true;
                    break;
                }

                if (seg_ms >= warn_ts_ms) {
                    if (hls_log_throttle_time("ts_slow", CONFIG_APP_HLS_LOG_THROTTLE_MS) &&
                        hls_log_throttle_burst("ts_slow", CONFIG_APP_HLS_LOG_BURST_COUNT, CONFIG_APP_HLS_LOG_BURST_WINDOW_MS)) {
                        ESP_LOGW(TAG, "[TS SLOW] seq=%lld took=%lld ms (warn=%d ms, open=%lld hdr=%lld body=%lld rb_wait=%lld gate_wait=%lld rb_retry=%u rb_free_min=%u read_max=%lld bytes=%u reads=%d)",
                                 (long long)seg->sequence, (long long)seg_ms, warn_ts_ms,
                                 (long long)handle->last_seg_metrics.open_ms,
                                 (long long)handle->last_seg_metrics.headers_ms,
                                 (long long)handle->last_seg_metrics.body_read_ms,
                                 (long long)handle->last_seg_metrics.rb_wait_ms,
                                 (long long)handle->last_seg_metrics.rb_gating_wait_ms,
                                 (unsigned)handle->last_seg_metrics.rb_send_fail_retries,
                                 (unsigned)handle->last_seg_metrics.rb_free_min,
                                 (long long)handle->last_seg_metrics.max_read_block_ms,
                                 (unsigned)handle->last_seg_metrics.body_bytes,
                                 handle->last_seg_metrics.read_calls);
                    }
                    if (hls_log_mode_at_least(HLS_LOG_MODE_DIAG_LIGHT)) {
                        ESP_LOGI(TAG, "TS_GATING wait_ms=%lld retries=%u free_min=%u",
                                 (long long)handle->last_seg_metrics.rb_gating_wait_ms,
                                 (unsigned)handle->last_seg_metrics.rb_send_fail_retries,
                                 (unsigned)handle->last_seg_metrics.rb_free_min);
                    }
                } else {
                    ESP_LOGD(TAG, "Segment %lld OK [TS took=%lld ms]",
                             (long long)seg->sequence, (long long)seg_ms);
                }
            } else {
                ESP_LOGE(TAG, "Échec téléchargement segment %lld [TS took=%lld ms, open=%lld hdr=%lld body=%lld rb_wait=%lld gate_wait=%lld rb_retry=%u rb_free_min=%u read_max=%lld bytes=%u reads=%d]",
                         (long long)seg->sequence, (long long)seg_ms,
                         (long long)handle->last_seg_metrics.open_ms,
                         (long long)handle->last_seg_metrics.headers_ms,
                         (long long)handle->last_seg_metrics.body_read_ms,
                         (long long)handle->last_seg_metrics.rb_wait_ms,
                         (long long)handle->last_seg_metrics.rb_gating_wait_ms,
                         (unsigned)handle->last_seg_metrics.rb_send_fail_retries,
                         (unsigned)handle->last_seg_metrics.rb_free_min,
                         (long long)handle->last_seg_metrics.max_read_block_ms,
                         (unsigned)handle->last_seg_metrics.body_bytes,
                         handle->last_seg_metrics.read_calls);

                handle->is_downloading = false;
                break;
            }
        }

        if (hole_detected) {
            if (wanted_seq >= 0) {
                ESP_LOGW(TAG, "Cycle interrompu: trou de séquence (last=%lld, wanted=%lld)",
                         (long long)last_sequence_number, (long long)wanted_seq);
            } else {
                ESP_LOGW(TAG, "Cycle interrompu: trou de séquence (last=%lld, cold_start)",
                         (long long)last_sequence_number);
            }

            int64_t cycle_ms = 0;
            int64_t other_ms = 0;
            hls_compute_cycle_other_ms(cycle_start_us, master_m3u8_ms, media_m3u8_ms, ts_sum_ms, &cycle_ms, &other_ms);

            if (hls_log_mode_at_least(HLS_LOG_MODE_DIAG_LIGHT) &&
                hls_log_throttle_time("fetch_cycle_hole", CONFIG_APP_HLS_LOG_THROTTLE_MS)) {
                ESP_LOGI(TAG, "[TIMING] fetch cycle hole: %lld ms (ok=%d advanced=%d rb=%d%% spc=%d last=%lld ts_sum=%lld other=%lld m3u8_top=%lld m3u8_media=%lld)",
                         (long long)cycle_ms,
                         downloaded_segments, advanced_segments, level, segments_per_cycle,
                         (long long)last_sequence_number,
                         (long long)ts_sum_ms, (long long)other_ms,
                         (long long)master_m3u8_ms, (long long)media_m3u8_ms);
            }

            // Hardening live: relancer immédiatement un refresh playlist sans attendre le timer.
            lib_m3u8_parser_free(&playlist);
            playlist_valid = false;
            handle->is_downloading = false;
            // Petit backoff pour éviter une boucle serrée si la playlist reste invalide.
            hls_interruptible_delay_ms(50);
            continue;
        }

        // FIX #12: Détection décrochage et resynchronisation
        // Si aucun segment téléchargé alors que la playlist en contient, vérifier si on est trop en retard
        if (!any_downloaded_this_cycle && playlist.segment_count > 0 && last_sequence_number >= 0) {
            // Décrochage détecté : notre dernier segment est plus vieux que le plus ancien disponible
            int64_t gap = oldest_in_playlist - (last_sequence_number + 1);
            if (gap > 0) {
                ESP_LOGW(TAG, "DÉCROCHAGE DÉTECTÉ: gap=%lld last_seq=%lld fenêtre=[%lld..%lld] state=%s",
                         (long long)gap,
                         (long long)last_sequence_number,
                         (long long)oldest_in_playlist,
                         (long long)newest_in_playlist,
                         hls_catchup_state_str(catchup_state));

                bool did_hard_resync = false;
                bool did_soft_resync = false;
                int backoff_used = hls_compute_live_edge_backoff(level);
                int64_t start_seq_used = -1;

#if CONFIG_APP_HLS_LIVE_CATCHUP_ENABLE
                if (catchup_state != HLS_CATCHUP_STATE_CATCHUP) {
                    catchup_state = HLS_CATCHUP_STATE_CATCHUP;
                    catchup_enter_hits = CONFIG_APP_HLS_CATCHUP_ENTER_CONSECUTIVE;
                    catchup_enter_count++;
                    catchup_exit_hits = 0;
                    ESP_LOGW(TAG, "catchup_enter (direct) gap=%lld window=[%lld..%lld]",
                             (long long)gap,
                             (long long)oldest_in_playlist,
                             (long long)newest_in_playlist);
                }

                const int64_t now_us = esp_timer_get_time();
                const int64_t hard_cooldown_us = (int64_t)CONFIG_APP_HLS_CATCHUP_RESYNC_COOLDOWN_MS * 1000LL;
                const int64_t soft_cooldown_us = (hard_cooldown_us > 1500LL * 1000LL) ? (1500LL * 1000LL) : hard_cooldown_us;
                const bool hard_cooldown_ok = (last_hard_resync_us == 0) || ((now_us - last_hard_resync_us) >= hard_cooldown_us);
                const bool soft_cooldown_ok = (last_soft_resync_us == 0) || ((now_us - last_soft_resync_us) >= soft_cooldown_us);
                const bool allow_hard = (gap >= CONFIG_APP_HLS_CATCHUP_HARD_GAP) && hard_cooldown_ok;

                if (allow_hard) {
                    did_hard_resync = hls_resync_to_live_edge(level,
                                                              oldest_in_playlist,
                                                              newest_in_playlist,
                                                              &last_sequence_number,
                                                              &backoff_used,
                                                              &start_seq_used);
                    if (did_hard_resync) {
                        last_hard_resync_us = now_us;
                    }
                } else if (gap > CONFIG_APP_HLS_CATCHUP_SOFT_GAP && soft_cooldown_ok) {
                    did_soft_resync = hls_resync_to_live_edge(level,
                                                              oldest_in_playlist,
                                                              newest_in_playlist,
                                                              &last_sequence_number,
                                                              &backoff_used,
                                                              &start_seq_used);
                    if (did_soft_resync) {
                        last_soft_resync_us = now_us;
                    }
                } else {
                    ESP_LOGW(TAG, "Soft catchup sans resync: gap=%lld soft_gap=%d cooldown_ms=%lld state=%s",
                             (long long)gap,
                             CONFIG_APP_HLS_CATCHUP_SOFT_GAP,
                             (long long)(soft_cooldown_us / 1000LL),
                             hls_catchup_state_str(catchup_state));
                }
#else
                did_hard_resync = hls_resync_to_live_edge(level,
                                                          oldest_in_playlist,
                                                          newest_in_playlist,
                                                          &last_sequence_number,
                                                          &backoff_used,
                                                          &start_seq_used);
#endif

                if (did_hard_resync) {
                    resync_hard_count++;
                    hls_notify_audio_reset_if_needed(handle, &resets_audio_count, "hard_resync");
                } else if (did_soft_resync) {
                    resync_soft_count++;
                    ESP_LOGW(TAG, "Soft catchup near-edge: sans reset audio (gap=%lld start_seq=%lld backoff=%d new_last=%lld)",
                             (long long)gap,
                             (long long)start_seq_used,
                             backoff_used,
                             (long long)last_sequence_number);
                }

#if CONFIG_APP_HLS_LIVE_CATCHUP_ENABLE
                catchup_cycles++;
#endif

                int64_t cycle_ms = 0;
                int64_t other_ms = 0;
                hls_compute_cycle_other_ms(cycle_start_us, master_m3u8_ms, media_m3u8_ms, ts_sum_ms,
                                           &cycle_ms, &other_ms);

                if (hls_log_mode_at_least(HLS_LOG_MODE_DIAG_LIGHT) &&
                    hls_log_throttle_time("fetch_cycle_resync", CONFIG_APP_HLS_LOG_THROTTLE_MS)) {
                    ESP_LOGI(TAG, "[TIMING] fetch cycle resync: %lld ms (ok=%d advanced=%d rb=%d%% spc=%d last=%lld ts_sum=%lld other=%lld m3u8_top=%lld m3u8_media=%lld)",
                             (long long)cycle_ms,
                             downloaded_segments, advanced_segments, level, segments_per_cycle,
                             (long long)last_sequence_number,
                             (long long)ts_sum_ms, (long long)other_ms,
                             (long long)master_m3u8_ms, (long long)media_m3u8_ms);
                }

                // Forcer un nouveau cycle immédiatement pour télécharger les segments récents.
                lib_m3u8_parser_free(&playlist);
                playlist_valid = false;
                handle->is_downloading = false;
                if (!hls_interruptible_delay_ms(100)) {
                    goto task_exit;
                }
                xSemaphoreGive(handle->download_semaphore);  // Trigger immédiat
                continue;
            }

            ESP_LOGD(TAG, "Aucun nouveau segment (dernier: %lld)", (long long)last_sequence_number);
        }

#if CONFIG_APP_HLS_LIVE_CATCHUP_ENABLE
        if (playlist.segment_count > 0 && last_sequence_number >= 0 && newest_in_playlist >= oldest_in_playlist) {
            const int exit_backoff = hls_compute_live_edge_backoff(level);
            const int64_t near_live_threshold = newest_in_playlist - exit_backoff;

            if (catchup_state == HLS_CATCHUP_STATE_CATCHUP) {
                if (last_sequence_number >= near_live_threshold) {
                    catchup_exit_hits++;
                    if (catchup_exit_hits >= CONFIG_APP_HLS_CATCHUP_EXIT_CONSECUTIVE) {
                        catchup_state = HLS_CATCHUP_STATE_STEADY;
                        catchup_exit_count++;
                        catchup_enter_hits = 0;
                        catchup_exit_hits = 0;
                        ESP_LOGI(TAG, "catchup_exit last=%lld threshold=%lld newest=%lld",
                                 (long long)last_sequence_number,
                                 (long long)near_live_threshold,
                                 (long long)newest_in_playlist);
                    }
                } else {
                    catchup_exit_hits = 0;
                }
            }
        }
#endif

        int64_t cycle_ms = 0;
        int64_t other_ms = 0;
        hls_compute_cycle_other_ms(cycle_start_us, master_m3u8_ms, media_m3u8_ms, ts_sum_ms, &cycle_ms, &other_ms);

        if (hls_log_mode_at_least(HLS_LOG_MODE_RUN) &&
            hls_log_throttle_time("fetch_cycle_summary", CONFIG_APP_HLS_LOG_SUMMARY_PERIOD_MS)) {
            ESP_LOGI(TAG, "FETCH_SUMMARY cycle=%lldms ok=%d advanced=%d rb=%d%% spc=%d last=%lld ts_sum=%lldms other=%lldms m3u8_top=%lldms m3u8_media=%lldms",
                     (long long)cycle_ms,
                     downloaded_segments, advanced_segments, level, segments_per_cycle,
                     (long long)last_sequence_number,
                     (long long)ts_sum_ms, (long long)other_ms,
                     (long long)master_m3u8_ms, (long long)media_m3u8_ms);

#if CONFIG_APP_HLS_LIVE_CATCHUP_ENABLE
            int64_t gap_avg = (gap_samples > 0) ? (gap_sum / (int64_t)gap_samples) : 0;
            ESP_LOGI(TAG, "CATCHUP_SUMMARY state=%s enter=%u exit=%u catchup_cycles=%u resync_hard=%u resync_soft=%u gap_avg=%lld gap_max=%lld resets_audio=%u",
                     hls_catchup_state_str(catchup_state),
                     (unsigned)catchup_enter_count,
                     (unsigned)catchup_exit_count,
                     (unsigned)catchup_cycles,
                     (unsigned)resync_hard_count,
                     (unsigned)resync_soft_count,
                     (long long)gap_avg,
                     (long long)gap_max,
                     (unsigned)resets_audio_count);
            ESP_LOGI(TAG, "CATCHUP_DIAG no_download_cycles=%u no_next_segment_cycles=%u admission_block_cycles=%u admission_block_segment_waits=%u",
                     (unsigned)no_download_cycles,
                     (unsigned)no_next_segment_cycles,
                     (unsigned)catchup_admission_block_cycles,
                     (unsigned)catchup_admission_block_segment_waits);
#endif
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
    if (hls_log_mode_at_least(HLS_LOG_MODE_RUN)) {
        ESP_LOGI(TAG, "Arrêt de la task de téléchargement HLS");
    }

    // [RAM OPT] Log HWM final avant sortie (P0 phase 0)
#if CONFIG_APP_HLS_PLAYER_STACK_DIAG
    UBaseType_t hwm_final = uxTaskGetStackHighWaterMark(NULL);
    const size_t FETCH_STACK_SIZE = 11264;  // words (from xTaskCreate - P0.1 Phase 1)
    ESP_LOGI(TAG, "[STACK] %s: HWM final = %u words (%u bytes) - utilisation max = %u bytes",
             pcTaskGetName(NULL), hwm_final, hwm_final * sizeof(StackType_t),
             (FETCH_STACK_SIZE * sizeof(StackType_t)) - (hwm_final * sizeof(StackType_t)));
#endif

    // Signaler fin de tâche via sémaphore
    if (handle->fetch_done) {
        xSemaphoreGive(handle->fetch_done);
    }
    vTaskDelete(NULL);
}




// ============================================================================
// API Publique
// ============================================================================
