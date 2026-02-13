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
    ESP_LOGI(TAG, "Démarrage de la task de téléchargement HLS");

    // [RAM OPT] Instrumentation stack HWM (P0 phase 0)
    UBaseType_t hwm_initial = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "[STACK] %s: HWM initial = %u words (%u bytes) [sizeof(StackType_t)=%u]",
             pcTaskGetName(NULL), hwm_initial, hwm_initial * sizeof(StackType_t), sizeof(StackType_t));

    int64_t last_sequence_number = -1;
    int resync_cooldown = 0;  // Cycles en mode prudent après décrochage

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

        lib_m3u8_parser_dump(&playlist);

        // FIX #2: Gestion master playlist avec logique déterministe
        // Utilise maintenant variant_count au lieu de segment_count
        if (playlist.is_master_playlist && playlist.variant_count > 0) {
            ESP_LOGI(TAG, "Master playlist détectée, sélection de la meilleure qualité...");

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
            ESP_LOGI(TAG, "[REFRESH] media m3u8 took %lld ms", (long long)media_m3u8_ms);

            if (m3u8_content == NULL) {
                ESP_LOGE(TAG, "Échec de téléchargement de la media playlist");
                lib_m3u8_parser_free(&playlist);
                playlist_valid = false;
                ESP_LOGI(TAG, "[TIMING] fetch cycle early-wait: %lld ms",
                         (long long)((esp_timer_get_time() - cycle_start_us) / 1000));
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

            lib_m3u8_parser_dump(&playlist);
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

        int segments_per_cycle;
        if (last_sequence_number < 0) {
            // Cold start: limite dynamique pour éviter overflow ringbuffer pendant le 1er cycle
            const int avg_segment_size = 130 * 1024;
            int max_initial = (int)((handle->buffer_size * 80u / 100u) / avg_segment_size);
            if (max_initial < 1) max_initial = 1;
            if (max_initial > 2) max_initial = 2;
            segments_per_cycle = max_initial;
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

        // En régime établi, limiter l'agressivité pour éviter de décrocher la fenêtre live.
        if (last_sequence_number >= 0 && segments_per_cycle > 2) {
            segments_per_cycle = 2;
        }

        // Après un décrochage, imposer temporairement 1 segment/cycle pour se recaler proprement.
        if (last_sequence_number >= 0 && resync_cooldown > 0) {
            segments_per_cycle = 1;
            resync_cooldown--;
        }

        // FIX: Log cold start pour faciliter debug terrain
        if (last_sequence_number < 0) {
            ESP_LOGI(TAG, "Cold start: téléchargement de %d segments initiaux (limite anti-overflow)", segments_per_cycle);
        }

#if CONFIG_APP_HLS_PLAYER_BACKPRESSURE
        // Gate backpressure au niveau cycle: éviter un cycle partiel (source de sauts audio)
        // si le buffer est déjà haut, on reporte tout le cycle plutôt que télécharger 1 segment puis couper.
        const int BACKPRESSURE_HIGH = CONFIG_APP_HLS_PLAYER_BACKPRESSURE_HIGH;
        if (level >= BACKPRESSURE_HIGH) {
            ESP_LOGW(TAG, "[BACKPRESSURE] RB %d%% ≥ %d%% - report cycle complet", level, BACKPRESSURE_HIGH);
            handle->is_downloading = false;
            if (!hls_interruptible_delay_ms(500)) {
                goto task_exit;
            }
            continue;
        }
#endif

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

        if (to_download_count > 0) {
            ESP_LOGI(TAG, "Cycle download %d seg: seq %lld .. %lld (rb=%d%%)",
                     to_download_count,
                     (long long)playlist.segments[to_download[0]].sequence,
                     (long long)playlist.segments[to_download[to_download_count - 1]].sequence,
                     level);
        } else if (playlist.segment_count > 0 && last_sequence_number >= 0) {
            int64_t oldest_in_playlist = playlist.segments[0].sequence;
            int64_t newest_in_playlist = playlist.segments[playlist.segment_count - 1].sequence;
            if (oldest_in_playlist <= last_sequence_number + 1) {
                ESP_LOGI(TAG, "No next segment yet (wanted=%lld, last=%lld, window=[%lld..%lld], count=%d) - wait",
                         (long long)wanted_seq,
                         (long long)last_sequence_number,
                         (long long)oldest_in_playlist,
                         (long long)newest_in_playlist,
                         playlist.segment_count);
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

            ESP_LOGI(TAG, "Téléchargement segment %lld (%d/%d)%s",
                     (long long)seg->sequence, (k + 1), to_download_count,
                     (seg->flags & LIB_M3U8_PARSER_SEGMENT_FLAG_DISCONTINUITY) ? " [DISCONTINUITY]" : "");

            // Signaler DISCONTINUITY pour reset décodeur via task notification (P1: check flag)
            if ((seg->flags & LIB_M3U8_PARSER_SEGMENT_FLAG_DISCONTINUITY) && handle->play_task) {
                ESP_LOGW(TAG, "DISCONTINUITY détectée → notification NOTIF_RESET vers play_task");
                xTaskNotify(handle->play_task, NOTIF_RESET, eSetBits);
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
                    if (handle->play_task) {
                        ESP_LOGW(TAG, "Trou de séquence -> notification NOTIF_RESET vers play_task");
                        xTaskNotify(handle->play_task, NOTIF_RESET, eSetBits);
                    }
                    hole_detected = true;
                    break;
                }

                if (seg_ms >= warn_ts_ms) {
                    ESP_LOGW(TAG, "[TS SLOW] seq=%lld took=%lld ms (warn=%d ms)",
                             (long long)seg->sequence, (long long)seg_ms, warn_ts_ms);
                } else {
                    ESP_LOGD(TAG, "Segment %lld OK [TS took=%lld ms]",
                             (long long)seg->sequence, (long long)seg_ms);
                }
            } else {
                ESP_LOGE(TAG, "Échec téléchargement segment %lld [TS took=%lld ms]",
                         (long long)seg->sequence, (long long)seg_ms);
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

            ESP_LOGI(TAG, "[TIMING] fetch cycle hole: %lld ms (ok=%d advanced=%d rb=%d%% spc=%d last=%lld ts_sum=%lld other=%lld m3u8_top=%lld m3u8_media=%lld)",
                     (long long)cycle_ms,
                     downloaded_segments, advanced_segments, level, segments_per_cycle,
                     (long long)last_sequence_number,
                     (long long)ts_sum_ms, (long long)other_ms,
                     (long long)master_m3u8_ms, (long long)media_m3u8_ms);

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
            // Vérifier si tous les segments disponibles ont été skippés
            int64_t oldest_in_playlist = playlist.segments[0].sequence;
            int64_t newest_in_playlist = playlist.segments[playlist.segment_count - 1].sequence;

            // Décrochage détecté : notre dernier segment est plus vieux que le plus ancien disponible
            if (oldest_in_playlist > last_sequence_number + 1) {
                ESP_LOGW(TAG, "DÉCROCHAGE DÉTECTÉ: last_seq=%lld, fenêtre=[%lld..%lld]",
                         (long long)last_sequence_number,
                         (long long)oldest_in_playlist,
                         (long long)newest_in_playlist);
                // Resync near live edge: backoff adaptatif selon niveau buffer.
                int64_t catchup_backoff = 2;
                if (level > 75) {
                    catchup_backoff = 4;
                } else if (level > 60) {
                    catchup_backoff = 3;
                }
                int64_t catchup_seq = newest_in_playlist - catchup_backoff;
                if (catchup_seq < oldest_in_playlist) {
                    catchup_seq = oldest_in_playlist;
                }
                int64_t last_before = last_sequence_number;
                last_sequence_number = catchup_seq - 1;

                ESP_LOGW(TAG, "→ RESYNC near-edge: reprise depuis seq=%lld (oldest=%lld newest=%lld backoff=%lld last_before=%lld last_after=%lld)",
                         (long long)catchup_seq,
                         (long long)oldest_in_playlist,
                         (long long)newest_in_playlist,
                         (long long)catchup_backoff,
                         (long long)last_before,
                         (long long)last_sequence_number);

                if (handle->play_task) {
                    xTaskNotify(handle->play_task, NOTIF_RESET, eSetBits);
                }

                int64_t cycle_ms = 0;
                int64_t other_ms = 0;
                hls_compute_cycle_other_ms(cycle_start_us, master_m3u8_ms, media_m3u8_ms, ts_sum_ms,
                                           &cycle_ms, &other_ms);

                ESP_LOGI(TAG, "[TIMING] fetch cycle resync: %lld ms (ok=%d advanced=%d rb=%d%% spc=%d last=%lld ts_sum=%lld other=%lld m3u8_top=%lld m3u8_media=%lld)",
                         (long long)cycle_ms,
                         downloaded_segments, advanced_segments, level, segments_per_cycle,
                         (long long)last_sequence_number,
                         (long long)ts_sum_ms, (long long)other_ms,
                         (long long)master_m3u8_ms, (long long)media_m3u8_ms);

                // Forcer un nouveau cycle immédiatement pour télécharger les segments récents
                // avec un micro-backoff pour éviter les rafales refresh/resync.
                resync_cooldown = 3;
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

        int64_t cycle_ms = 0;
        int64_t other_ms = 0;
        hls_compute_cycle_other_ms(cycle_start_us, master_m3u8_ms, media_m3u8_ms, ts_sum_ms, &cycle_ms, &other_ms);

        ESP_LOGI(TAG, "[TIMING] fetch cycle done: %lld ms (ok=%d advanced=%d rb=%d%% spc=%d last=%lld ts_sum=%lld other=%lld m3u8_top=%lld m3u8_media=%lld)",
                 (long long)cycle_ms,
                 downloaded_segments, advanced_segments, level, segments_per_cycle,
                 (long long)last_sequence_number,
                 (long long)ts_sum_ms, (long long)other_ms,
                 (long long)master_m3u8_ms, (long long)media_m3u8_ms);

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
    const size_t FETCH_STACK_SIZE = 11264;  // words (from xTaskCreate - P0.1 Phase 1)
    ESP_LOGI(TAG, "[STACK] %s: HWM final = %u words (%u bytes) - utilisation max = %u bytes",
             pcTaskGetName(NULL), hwm_final, hwm_final * sizeof(StackType_t),
             (FETCH_STACK_SIZE * sizeof(StackType_t)) - (hwm_final * sizeof(StackType_t)));

    // Signaler fin de tâche via sémaphore
    if (handle->fetch_done) {
        xSemaphoreGive(handle->fetch_done);
    }
    vTaskDelete(NULL);
}




// ============================================================================
// API Publique
// ============================================================================

