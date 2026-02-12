/**
 * @file app_hls_player_fetcher.c
 * @brief Implémentation du module de téléchargement HLS (fetch task)
 */

#include "app_hls_player_fetcher.h"
#include "app_hls_player_internal.h"
#include "app_hls_player_http.h"
#include "lib_m3u8_parser/lib_m3u8_parser.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "hls_fetcher";

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

    // Playlist hors boucle pour cleanup centralisé à task_exit
    lib_m3u8_parser_playlist_t playlist;
    memset(&playlist, 0, sizeof(playlist));
    bool playlist_valid = false;

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

        // [DIAG LAG] Mesurer temps de téléchargement M3U8 master
        int64_t t0 = esp_timer_get_time();
        char *m3u8_content = hls_http_download_m3u8(handle->stream_url);
        int64_t t1 = esp_timer_get_time();
        ESP_LOGI(TAG, "[REFRESH] master m3u8 took %lld ms", (long long)((t1 - t0) / 1000));

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
            m3u8_content = hls_http_download_m3u8(media_url);
            t1 = esp_timer_get_time();
            ESP_LOGI(TAG, "[REFRESH] media m3u8 took %lld ms", (long long)((t1 - t0) / 1000));

            if (m3u8_content == NULL) {
                ESP_LOGE(TAG, "Échec de téléchargement de la media playlist");
                lib_m3u8_parser_free(&playlist);
                playlist_valid = false;
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

            esp_err_t err = hls_http_download_segment(handle, seg->url);
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

