/**
 * @file app_hls_player_fetcher.h
 * @brief Module de téléchargement HLS (fetch task)
 *
 * Ce module fournit la task de téléchargement HLS qui :
 * - Télécharge et parse la playlist M3U8 (master et media)
 * - Sélectionne le variant approprié (midfi/hifi/lofi ou bandwidth)
 * - Télécharge les segments audio en boucle
 * - Gère les DISCONTINUITY (reset décodeur via NOTIF_RESET)
 * - Rafraîchit périodiquement la playlist (target_duration * 0.8)
 * - Signale fetch_done à la fin
 *
 * Architecture :
 * - Sélection variant : préfère "midfi", fallback "hifi" > "lofi" > bandwidth
 * - Téléchargement interruptible via hls_interruptible_delay_ms()
 * - Notifications : NOTIF_RESET sur DISCONTINUITY
 * - Helpers : hls_http_download_m3u8(), hls_http_download_segment()
 */

#ifndef APP_HLS_PLAYER_FETCHER_H
#define APP_HLS_PLAYER_FETCHER_H

#include "app_hls_player_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Task de téléchargement HLS
 *
 * Cette task :
 * - Télécharge la playlist M3U8 initiale (master ou media)
 * - Parse avec lib_m3u8_parser
 * - Sélectionne variant (midfi préféré, sinon hifi/lofi/bandwidth)
 * - Télécharge les segments en boucle
 * - Rafraîchit la playlist périodiquement (target_duration * 0.8)
 * - Détecte DISCONTINUITY et envoie NOTIF_RESET
 * - Gère download_semaphore (signale quand buffer < 40%)
 *
 * Configuration stack/priority :
 * - Stack : 14336 bytes (CONFIG ou par défaut)
 * - Priority : 4 (tskIDLE_PRIORITY + 4)
 * - CPU : Core 0 (séparé de la task audio sur Core 1)
 *
 * @param[in] pvParameters Pointeur vers app_hls_player_t (handle)
 *
 * @note
 *     - Utilise hls_http_download_m3u8() et hls_http_download_segment() du module HTTP
 *     - Utilise hls_interruptible_delay_ms() et hls_should_stop_now() (internal.h)
 *     - Signale completion via handle->fetch_done (semaphore)
 *     - Arrêt interruptible via NOTIF_STOP
 */
void hls_fetch_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // APP_HLS_PLAYER_FETCHER_H
