/**
 * @file app_hls_player_audio.h
 * @brief Module de décodage audio pour le player HLS
 *
 * Ce module fournit la task de décodage audio qui :
 * - Récupère les paquets MPEG-TS depuis le ring buffer
 * - Assemble les paquets via gather buffer (4-8 KB)
 * - Décode TS → PCM via esp_audio_simple_dec
 * - Envoie les frames PCM via callback write_cb
 * - Gère la resynchronisation TS en cas d'erreurs
 *
 * Architecture :
 * - Gather buffer : accumule N paquets TS (188 bytes) avant décodage
 * - Leftover : bytes non consommés persistants entre cycles
 * - Resync smart : PID + PUSI + PES pour points de resync valides
 * - Notifications : NOTIF_STOP, NOTIF_RESET, NOTIF_RESYNC
 */

#ifndef APP_HLS_PLAYER_AUDIO_H
#define APP_HLS_PLAYER_AUDIO_H

#include "app_hls_player_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Task de lecture audio (zéro-copie avec gather buffer)
 *
 * Cette task :
 * - Récupère les items du ring buffer (188 bytes par item)
 * - Assemble N items dans un gather buffer (CONFIG_APP_HLS_PLAYER_GATHER_BUFFER_SIZE KB)
 * - Décode les données TS → PCM via esp_audio_simple_dec
 * - Appelle write_cb() pour chaque frame PCM décodée
 * - Gère les notifications (STOP, RESET, RESYNC)
 * - Applique la resynchronisation smart en cas d'erreurs AAC
 *
 * Configuration stack/priority :
 * - Stack : 6144 bytes (CONFIG ou par défaut)
 * - Priority : 5 (tskIDLE_PRIORITY + 5)
 * - CPU : Core 1 (pinning pour réduire jitter)
 *
 * @param[in] pvParameters Pointeur vers app_hls_player_t (handle)
 *
 * @note
 *     - Utilise hls_ts_resync_smart() et hls_ts_find_next_sync() du module TS_SYNC
 *     - Gère leftover entre cycles (bytes non consommés par décodeur)
 *     - Stratégie anti-glitch : copy-then-return (pas de held items)
 *     - Stall mode : si consumed==0 répété, resync uniquement sur leftover
 *     - Signale completion via handle->play_done (semaphore)
 */
void hls_audio_play_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // APP_HLS_PLAYER_AUDIO_H
