/**
 * @file app_hls_player_audio.c
 * @brief Implémentation du module de décodage audio
 */

#include "app_hls_player/app_hls_player_audio.h"
#include "app_hls_player/app_hls_player_internal.h"
#include "app_hls_player/app_hls_player_ts_sync.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "simple_dec/esp_audio_simple_dec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "hls_audio";

static esp_err_t hls_write_all_pcm(app_hls_player_t *handle,
                                   const uint8_t *buf,
                                   size_t len,
                                   uint32_t timeout_ms_total,
                                   size_t *total_written)
{
    if (handle == NULL || handle->write_cb == NULL || buf == NULL || total_written == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *total_written = 0;
    int64_t t0_us = esp_timer_get_time();

    while (*total_written < len) {
        uint32_t elapsed_ms = (uint32_t)((esp_timer_get_time() - t0_us) / 1000);
        if (elapsed_ms >= timeout_ms_total) {
            return ESP_ERR_TIMEOUT;
        }

        uint32_t remaining_ms = timeout_ms_total - elapsed_ms;
        size_t written_now = 0;
        esp_err_t err = handle->write_cb(handle->write_ctx,
                                         buf + *total_written,
                                         len - *total_written,
                                         &written_now,
                                         remaining_ms);

        *total_written += written_now;

        if (err != ESP_OK) {
            if (err == ESP_ERR_TIMEOUT && *total_written < len) {
                return ESP_ERR_TIMEOUT;
            }
            return err;
        }

        if (written_now == 0) {
            // Évite une boucle chaude si le sink est momentanément saturé.
            vTaskDelay(1);
        }
    }

    return ESP_OK;
}

void hls_audio_play_task(void *pvParameters)
{
    app_hls_player_t *handle = (app_hls_player_t *)pvParameters;
    ESP_LOGI(TAG, "Démarrage de la task de lecture audio");

    hls_stack_log_initial(TAG);

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
    int no_data_streak = 0;  // streak underrun (cycles consécutifs)
    uint32_t underrun_count = 0;

    // [FIX AAC error:30] Protection post-resync : évite re-resync immédiat / faux positifs
    // Après un NOTIF_RESYNC, protège N cycles (force remplissage gather_buf)
    int just_resynced_cycles = 0;

    // Initialisé avant toute sortie anticipée (ex: NOTIF_STOP pendant prébuffer)
    // pour garantir que les logs de cleanup restent déterministes.
    int decode_count = 0;
    int error_count = 0;
    bool download_signaled = false;
    bool was_downloading = false;
    uint32_t pcm_short_write_count = 0;
    uint32_t pcm_timeout_count = 0;

    // [FIX UNDERRUN] Prébuffer 70-80% avant démarrage (vs 500ms fixe)
    // Donne >400ms marge pour survivre aux refresh M3U8 (389ms)
    ESP_LOGI(TAG, "Attente prébuffer 70%% avant démarrage audio...");
    const int TARGET_PREBUFFER = 70;  // 70% du buffer
    const int PREBUFFER_TIMEOUT_MS = 10000;  // Timeout sécurité 10s
    int64_t prebuffer_start = esp_timer_get_time();
    int current_level = 0;
    bool stop_requested = false;
    bool stopped_during_prebuffer = false;

    while (true) {
        // Permet un arrêt réactif même pendant la phase de prébuffer
        if (hls_should_stop_now()) {
            ESP_LOGI(TAG, "NOTIF_STOP reçue pendant prébuffer - arrêt audio_play_task");
            stop_requested = true;
            stopped_during_prebuffer = true;
            break;
        }

        current_level = hls_rb_get_level_pct(handle);

        if (current_level >= TARGET_PREBUFFER) {
            ESP_LOGI(TAG, "Prébuffer atteint: %d%% - démarrage lecture", current_level);
            break;
        }

        int64_t elapsed_ms = (esp_timer_get_time() - prebuffer_start) / 1000;
        if (elapsed_ms > PREBUFFER_TIMEOUT_MS) {
            ESP_LOGW(TAG, "Timeout prébuffer après %lld ms (niveau=%d%%) - démarrage forcé",
                     elapsed_ms, current_level);
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(50));  // Check toutes les 50ms
    }

    if (stop_requested) {
        goto task_cleanup;
    }

    // [DIAG] Heartbeat audio loop
    int64_t last_heartbeat_us = 0;
    int last_decode_count = 0;

    while (true) {
#if CONFIG_APP_HLS_PLAYER_STACK_DIAG
        static int64_t last_log_us = 0;
        hls_stack_log_periodic(TAG, "audio_play", &last_log_us, 5 * 1000 * 1000);
#endif

        int64_t now_hb = esp_timer_get_time();
        if (now_hb - last_heartbeat_us > (int64_t)CONFIG_APP_HLS_LOG_SUMMARY_PERIOD_MS * 1000) {
            int frames_decoded = decode_count - last_decode_count;
            float decode_rate = frames_decoded / ((float)CONFIG_APP_HLS_LOG_SUMMARY_PERIOD_MS / 1000.0f);

            if (hls_log_mode_at_least(HLS_LOG_MODE_RUN) &&
                hls_log_throttle_time("audio_summary", CONFIG_APP_HLS_LOG_SUMMARY_PERIOD_MS)) {
#if CONFIG_APP_HLS_PLAYER_RINGBUF_DIAG
                size_t rb_used = 0;
                int rb_fill_pct = 0;
                hls_rb_get_stats(handle, NULL, &rb_used, &rb_fill_pct);
                ESP_LOGI(TAG, "AUDIO_SUMMARY decoded=%.1f f/s rb=%d%% (%zu/%zu KB) leftover=%zu pcm_short=%lu pcm_to=%lu underrun_count=%lu underrun_streak=%d",
                         decode_rate, rb_fill_pct, rb_used / 1024, handle->buffer_size / 1024, leftover_len,
                         (unsigned long)pcm_short_write_count, (unsigned long)pcm_timeout_count,
                         (unsigned long)underrun_count, no_data_streak);
#else
                ESP_LOGI(TAG, "AUDIO_SUMMARY decoded=%.1f f/s leftover=%zu pcm_short=%lu pcm_to=%lu underrun_count=%lu underrun_streak=%d",
                         decode_rate, leftover_len,
                         (unsigned long)pcm_short_write_count, (unsigned long)pcm_timeout_count,
                         (unsigned long)underrun_count, no_data_streak);
#endif
            }

            last_heartbeat_us = now_hb;
            last_decode_count = decode_count;
        }

        // === 1) Gestion notifications STOP/RESET/RESYNC ===
        uint32_t notif = 0;
        if (xTaskNotifyWait(0, NOTIF_STOP | NOTIF_RESET | NOTIF_RESYNC, &notif, 0) == pdTRUE) {
            if (notif & NOTIF_STOP) {
                ESP_LOGI(TAG, "NOTIF_STOP reçue - arrêt play_task");
                break;
            }
            if (notif & NOTIF_RESYNC) {
                // [FIX BUG AAC error:30] Drop-old propre : reprise sur frontière PES
                // Drop jusqu'au prochain PUSI du PID audio (garantit début PES complet)
#if CONFIG_APP_HLS_PLAYER_RINGBUF_DIAG
                hls_rb_log_status(ESP_LOG_WARN,
                                  TAG,
                                  handle,
                                  "NOTIF_RESYNC reçue reason=drop-old - resync propre TS/PES/AAC");
#else
                ESP_LOGW(TAG, "NOTIF_RESYNC reçue reason=drop-old - resync propre TS/PES/AAC");
#endif

                // PID audio typique pour France Inter/FIP : 0x0101 (257 décimal)
                // TODO: rendre configurable via Kconfig CONFIG_APP_HLS_PLAYER_AUDIO_PID
                #ifndef CONFIG_APP_HLS_PLAYER_AUDIO_PID
                #define CONFIG_APP_HLS_PLAYER_AUDIO_PID 0x0101
                #endif
                const uint16_t AUDIO_PID = CONFIG_APP_HLS_PLAYER_AUDIO_PID;

                // [FIX] Solution G1+ : Augmenter RESYNC_MAX_DROP_PID pour trouver audio directement
                // Justification : Logs montrent PID audio trouvé à l'item 156 (150+6) après drop-old
                //   NOTIF_RESYNC s'arrête à 150 → fallback NOTIF_RESET → gap 2.2s ❌
                //   Si scan jusqu'à 300 items → trouve audio directement → gap ~400ms ✅
                // Évite fallback NOTIF_RESET qui vide encore plus le ringbuffer
                // 300 items = 56 KB scan (OK avec ringbuffer 256 KB), couvre cas "audio après PMT burst"
                const int RESYNC_MAX_DROP_PID = 300;  // Max 300 items = ~56 KB scan (augmenté de 150)

                hls_held_ts_packet_t held_pkt = {0};
                int drop_count = hls_drop_until_audio_pusi(handle->ring_buffer, RESYNC_MAX_DROP_PID, AUDIO_PID, &held_pkt);

                if (drop_count < 0) {
                    // Échec : PUSI audio non trouvé dans 300 items
                    // → Trigger NOTIF_RESET complet (plus efficace que fallback PID incorrect)
                    ESP_LOGW(TAG, "NOTIF_RESYNC: PUSI audio (PID=0x%04X) non trouvé dans %d items → fallback reason=resync_failure NOTIF_RESET complet",
                             AUDIO_PID, RESYNC_MAX_DROP_PID);
                    // Trigger reset complet (flush + recréer décodeur + drop-until-PUSI)
                    notif |= NOTIF_RESET;  // Forcer traitement NOTIF_RESET ci-dessous
                } else {
                    ESP_LOGI(TAG, "NOTIF_RESYNC: dropped %d items, reprise sur PUSI audio (PID=0x%04X)",
                             drop_count, held_pkt.pid);

                    // ORDRE CRITIQUE : Calculer should_reset_aac AVANT de reset les états
                    bool should_reset_aac = (drop_count > 5) || (zero_consume_streak > 3);

                    // Flush gather_buf + leftover (données résiduelles potentiellement corrompues)
                    // Reset état TS/PES parser (implicite via gather/leftover + explicite via fonction)
                    gather_len = 0;
                    leftover_len = 0;
                    zero_consume_streak = 0;
                    hls_ts_parser_reset();  // Reset explicite état TS/PES (noop actuellement, future-proof)

                    // Injecter le paquet PUSI held dans gather_buf (début PES propre)
                    // GARDE-FOU : Validation PID audio (défense en profondeur, ne devrait jamais trigger)
                    if (held_pkt.has_packet) {
                        if (held_pkt.pid == AUDIO_PID) {
                            memcpy(gather_buf, held_pkt.data, 188);
                            gather_len = 188;
                            ESP_LOGD(TAG, "NOTIF_RESYNC: paquet PUSI audio injecté dans gather_buf (188 bytes)");
                        } else {
                            // NE DEVRAIT JAMAIS ARRIVER (fallback any PES désactivé)
                            // Gardé pour défense en profondeur si bug futur
                            ESP_LOGE(TAG, "NOTIF_RESYNC: BUG! held_pkt PID=0x%04X not audio (0x%04X) → NOT injected",
                                     held_pkt.pid, AUDIO_PID);
                            // gather_len reste 0 → force refill ou reset au prochain cycle
                        }
                    }

                    // Protection post-resync : évite re-resync immédiat / faux positifs
                    // Force remplissage gather_buf pendant 3 cycles avant toute logique fallback
                    just_resynced_cycles = 3;

                    // Reset décodeur AAC conditionnel (si drop massif OU streak erreurs)
                    // Seuil : 5 drops = 940 bytes (pas un micro-jitter)
                    if (should_reset_aac) {
                        ESP_LOGI(TAG, "Reset décodeur AAC (drop_count=%d, streak_was=%d)",
                                 drop_count, zero_consume_streak);  // Note: streak déjà reset à 0

                        esp_audio_simple_dec_close(dec_handle);

                        dec_ret = esp_audio_simple_dec_open(&dec_cfg, &dec_handle);
                        if (dec_ret != ESP_AUDIO_ERR_OK || dec_handle == NULL) {
                            ESP_LOGE(TAG, "Échec reset décodeur après RESYNC: %d", dec_ret);
                            // Cleanup
                            free(gather_buf);
                            free(decoded_buffer);
                            if (handle->play_done) {
                                xSemaphoreGive(handle->play_done);
                            }
                            vTaskDelete(NULL);
                            return;
                        }

                        ESP_LOGI(TAG, "Décodeur AAC recréé, reprise lecture sur PUSI audio");
                    } else {
                        ESP_LOGD(TAG, "Décodeur AAC conservé (reset non nécessaire)");
                    }
                }

                // Si fallback NOTIF_RESET activé, continuer vers handler NOTIF_RESET ci-dessous
                if ((notif & NOTIF_RESET) == 0) {
                    continue;  // Sinon, reprendre boucle principale
                }
            }
            if (notif & NOTIF_RESET) {
#if CONFIG_APP_HLS_PLAYER_RINGBUF_DIAG
                hls_rb_log_status(ESP_LOG_WARN,
                                  TAG,
                                  handle,
                                  "NOTIF_RESET reçue reason=discontinuity - reset décodeur complet");
#else
                ESP_LOGW(TAG, "NOTIF_RESET reçue reason=discontinuity - reset décodeur complet");
#endif

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
        int buffer_level = hls_rb_get_level_pct(handle);

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

        // [FIX AAC error:30] Protection post-resync : skip logiques fallback resync/drop-old
        // Pendant just_resynced_cycles, force remplissage normal gather_buf
        if (just_resynced_cycles > 0) {
            just_resynced_cycles--;
            ESP_LOGD(TAG, "just_resynced protection active (%d cycles restants), skip fallback resync",
                     just_resynced_cycles);

            // Skip toute logique consumed=0 resync / drop-old pendant protection
            // Continue remplissage gather_buf normalement ci-dessous
        }

        // Défense: leftover ne doit jamais dépasser GATHER_BUF_SIZE
        if (leftover_len > GATHER_BUF_SIZE) {
            // Seulement si NOT protected
            if (just_resynced_cycles == 0) {
                ESP_LOGW(TAG, "leftover overflow: %zu > %zu, flushing", leftover_len, GATHER_BUF_SIZE);
                leftover_len = 0;
            }
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
        int items_copied = 0;
        int receive_fails = 0;
        if (!stall) {
            // Budget temps global au lieu de timeout par item (anti-contention)
            TickType_t t_start = xTaskGetTickCount();
            TickType_t budget = pdMS_TO_TICKS(15);  // 15ms max total

            while (gather_len + 188 <= GATHER_BUF_SIZE) {
                // Calculer timeout restant
                TickType_t elapsed = xTaskGetTickCount() - t_start;
                if (elapsed >= budget) break;  // Budget épuisé

                TickType_t rx_timeout = budget - elapsed;  // Timeout adaptatif
                size_t ilen = 0;
                uint8_t *it = (uint8_t *)xRingbufferReceive(handle->ring_buffer, &ilen, rx_timeout);

                if (!it) {
                    receive_fails++;
                    break;
                }

                // Vérifier alignement TS 188-byte
                if (ilen != 188) {
                    ESP_LOGW(TAG, "Item non-188 bytes: %zu (NOSPLIT échoué!)", ilen);
                    vRingbufferReturnItem(handle->ring_buffer, it);
                    receive_fails++;
                    continue;
                }

                // Copier dans gather_buf APRÈS leftover
                memcpy(gather_buf + gather_len, it, ilen);
                gather_len += ilen;
                items_copied++;

                // IMPORTANT: Rendre immédiatement l'item au ringbuffer (on a copié)
                vRingbufferReturnItem(handle->ring_buffer, it);

                // Seuil max atteint (8-16 KB selon config)
                if (gather_len >= GATHER_BUF_SIZE) break;
            }
        } else {
            // En stall: on travaille uniquement sur leftover (resync + reset)
            ESP_LOGD(TAG, "Stall mode: resync sur leftover uniquement (%zu bytes)", leftover_len);
        }

        // Si pas assez de données, tenter un retry court avant d'abandonner
        if (gather_len < min_gather && !stall) {
            // Retry court (2-3ms) si on est proche (> 50% du seuil)
            if (gather_len >= (min_gather / 2)) {
                size_t ilen = 0;
                uint8_t *it = (uint8_t *)xRingbufferReceive(handle->ring_buffer, &ilen, pdMS_TO_TICKS(3));
                if (it && ilen == 188) {
                    memcpy(gather_buf + gather_len, it, ilen);
                    gather_len += ilen;
                    items_copied++;
                    vRingbufferReturnItem(handle->ring_buffer, it);
                } else if (it) {
                    vRingbufferReturnItem(handle->ring_buffer, it);
                    receive_fails++;
                } else {
                    receive_fails++;
                }
            }
        }

        // Si toujours pas assez de données, attendre
        if (gather_len < min_gather) {
            // Starvation temporaire (pas forcément audible)
            no_data_streak++;

            // [FIX UNDERRUN] Receive secours en tranches (16x 25ms max = 400ms)
            // Évite gros blocage, sort dès qu'un item arrive
            // Permet de survivre aux refresh M3U8 (jusqu'à 400ms TLS/HTTP)
            if (items_copied == 0 && buffer_level >= 40 && no_data_streak < 10) {
                for (int attempt = 0; attempt < 16 && items_copied == 0; attempt++) {
                    size_t ilen = 0;
                    uint8_t *it = (uint8_t *)xRingbufferReceive(handle->ring_buffer, &ilen, pdMS_TO_TICKS(25));
                    if (it) {
                        if (ilen == 188 && gather_len + 188 <= GATHER_BUF_SIZE) {
                            memcpy(gather_buf + gather_len, it, 188);
                            gather_len += 188;
                            items_copied++;
                            // Si on a atteint min_gather, réinitialiser no_data_streak
                            if (gather_len >= min_gather) {
                                no_data_streak = 0;
                            }
                        }
                        vRingbufferReturnItem(handle->ring_buffer, it);
                        break;  // Item récupéré, sortir immédiatement
                    }
                    // Si toujours rien après plusieurs attempts, peut-être produire silence ici
                }
            }

            // Log UNDERRUN seulement si ça dure (>= 10 cycles = ~200-400ms)
            if (no_data_streak >= 10) {
                underrun_count++;
                ESP_LOGW(TAG, "[AUDIO UNDERRUN] gather=%zu/%zu bytes, buffer=%d%%, items=%d, fails=%d, stall=%d",
                         gather_len, min_gather, buffer_level, items_copied, receive_fails, stall);

                // [FIX GLITCH] Insertion silence pour masquer le trou audio
                // Évite clics/pops pendant underrun réseau (TLS timeout, etc.)
                const size_t silence_size = 4096;  // 4 KB silence (~23ms @ 44.1kHz stereo 16-bit)
                memset(decoded_buffer, 0, silence_size);

                size_t bytes_written = 0;
#if CONFIG_APP_HLS_PLAYER_RINGBUF_DIAG
                int64_t t0_wcb = esp_timer_get_time();
#endif
                esp_err_t err = hls_write_all_pcm(handle,
                                                  (const uint8_t *)decoded_buffer,
                                                  silence_size,
                                                  100,
                                                  &bytes_written);
#if CONFIG_APP_HLS_PLAYER_RINGBUF_DIAG
                int64_t dt_wcb = esp_timer_get_time() - t0_wcb;
                // 44.1kHz stereo 16-bit = 176.4 KB/s → expected_us = bytes / 0.1764
                int64_t expected_us = (silence_size * 1000000LL) / 176400;
                int64_t threshold_us = expected_us * 3 / 2;  // 1.5x expected (ou min 30ms)
                if (threshold_us < 30000) threshold_us = 30000;
                if (dt_wcb > threshold_us &&
                    hls_log_mode_at_least(HLS_LOG_MODE_DIAG_LIGHT) &&
                    hls_log_throttle_time("audio_write_slow_silence", CONFIG_APP_HLS_LOG_THROTTLE_MS)) {
                    ESP_LOGW(TAG, "[DIAG] write_cb slow: %lld us (silence %zu bytes, expected ~%lld us)",
                             dt_wcb, bytes_written, expected_us);
                }
#endif
                if (err == ESP_OK) {
                    ESP_LOGD(TAG, "[SILENCE] Inserted %zu bytes to mask underrun", bytes_written);
                } else if (err == ESP_ERR_TIMEOUT) {
                    pcm_timeout_count++;
                }

                if (bytes_written < silence_size) {
                    pcm_short_write_count++;
                }

                no_data_streak = 0;  // Reset pour éviter spam
            }

            // Rollback : revenir au leftover (items déjà rendus au ringbuffer)
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

        if ((decode_count++ < 20 || dec_ret != ESP_AUDIO_ERR_OK) &&
            hls_log_mode_at_least(HLS_LOG_MODE_DIAG_HEAVY) &&
            hls_log_throttle_every_n("decode_detail", CONFIG_APP_HLS_LOG_SAMPLE_N_FAST)) {
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
                    if (hls_ts_find_next_sync(gather_buf, leftover_len, &skip) && skip > 0) {
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

            // [FIX AAC error:30] Protection post-resync : skip fallback resync pendant protection
            if (just_resynced_cycles > 0) {
                ESP_LOGD(TAG, "consumed=0 mais just_resynced protection active, skip fallback resync");
                // Continue, attend prochain cycle (gather_buf devrait se remplir)
                continue;
            }

            // Tenter resync TS intelligent (PID+PUSI+PES) dans leftover
            size_t skip = 0;
            bool resync_ok = false;

            if (leftover_len >= 188 * 3 && hls_ts_resync_smart(gather_buf, leftover_len, leftover_len, 257, &skip)) {
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
                if (leftover_len >= 188 * 2 && hls_ts_find_next_sync(gather_buf, leftover_len, &s)) {
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
#if CONFIG_APP_HLS_PLAYER_RINGBUF_DIAG
            int64_t t0_wcb = esp_timer_get_time();
#endif
            esp_err_t err = hls_write_all_pcm(handle,
                                              (const uint8_t *)decoded_buffer,
                                              out_frame.decoded_size,
                                              200,
                                              &bytes_written);
#if CONFIG_APP_HLS_PLAYER_RINGBUF_DIAG
            int64_t dt_wcb = esp_timer_get_time() - t0_wcb;
            // 44.1kHz stereo 16-bit = 176.4 KB/s → expected_us = bytes / 0.1764
            int64_t expected_us = (out_frame.decoded_size * 1000000LL) / 176400;
            int64_t threshold_us = expected_us * 3 / 2;  // 1.5x expected (ou min 30ms)
            if (threshold_us < 30000) threshold_us = 30000;
            if (dt_wcb > threshold_us &&
                hls_log_mode_at_least(HLS_LOG_MODE_DIAG_LIGHT) &&
                hls_log_throttle_time("audio_write_slow_pcm", CONFIG_APP_HLS_LOG_THROTTLE_MS)) {
                ESP_LOGW(TAG, "[DIAG] write_cb slow: %lld us (PCM %zu bytes, expected ~%lld us)",
                         dt_wcb, bytes_written, expected_us);
            }
#endif
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Erreur d'écriture audio: %s", esp_err_to_name(err));
                if (err == ESP_ERR_TIMEOUT) {
                    pcm_timeout_count++;
                }
            }

            if (bytes_written < out_frame.decoded_size) {
                pcm_short_write_count++;
            }

            if ((decode_count % 100 == 0) &&
                hls_log_mode_at_least(HLS_LOG_MODE_DIAG_HEAVY) &&
                hls_log_throttle_every_n("audio_pcm_detail", CONFIG_APP_HLS_LOG_SAMPLE_N_FAST)) {
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

task_cleanup:
    // Cleanup
    esp_audio_simple_dec_close(dec_handle);

    // Items déjà rendus avec "copy then return" (pas de cleanup nécessaire)
    free(gather_buf);
    free(decoded_buffer);

    ESP_LOGI(TAG, "Arrêt de la task de lecture audio");
    if (stopped_during_prebuffer) {
        ESP_LOGI(TAG, "[STATS GATHER] Arrêt pendant prébuffer (resync=%d, decode=0)", resync_count);
    } else {
        ESP_LOGI(TAG, "[STATS GATHER] Resync count=%d, total decode cycles=%d",
                 resync_count, decode_count);
    }

    const size_t AUDIO_STACK_SIZE = 4096;  // words (from xTaskCreate - P0.1 Phase 1)
    hls_stack_log_final(TAG, AUDIO_STACK_SIZE);

    // FIX: Signaler fin de tâche via sémaphore (guard pour robustesse future)
    if (handle->play_done) {
        xSemaphoreGive(handle->play_done);
    }
    vTaskDelete(NULL);
}
