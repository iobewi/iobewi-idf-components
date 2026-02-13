/**
 * @file app_hls_player_http.h
 * @brief Module de téléchargement HTTP/HTTPS pour le player HLS
 *
 * Ce module fournit les fonctions de téléchargement pour :
 * - Playlists M3U8 (master et media)
 * - Segments audio MPEG-TS
 *
 * Caractéristiques :
 * - Alignement automatique TS 188-byte avec carry buffer
 * - Backpressure ringbuffer (blocage producteur sans perte TS)
 * - Support HTTPS avec certificats bundle
 * - Gestion redirections HTTP (détection, pas de suivi auto)
 * - Realloc progressif pour M3U8 (16-64 KB)
 */

#ifndef APP_HLS_PLAYER_HTTP_H
#define APP_HLS_PLAYER_HTTP_H

#include "app_hls_player_internal.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Télécharge un segment audio MPEG-TS dans le ring buffer
 *
 * Cette fonction :
 * - Télécharge le segment depuis l'URL fournie
 * - Aligne automatiquement les données sur 188-byte (MPEG-TS)
 * - Envoie les paquets TS dans le ring buffer du player
 * - Applique la backpressure quand le buffer est haut (CONFIG_APP_HLS_PLAYER_BACKPRESSURE)
 *
 * @param[in] handle Handle du player (accès au ring buffer et stats)
 * @param[in] url    URL complète du segment (HTTP ou HTTPS)
 *
 * @return
 *     - ESP_OK  : Segment téléchargé avec succès (status HTTP 200/206)
 *     - ESP_FAIL : Erreur HTTP, status inattendu, ou échec d'initialisation
 *
 * @note
 *     - Timeout : 5 secondes
 *     - Redirections HTTP : détectées et loggées, mais NON suivies (disable_auto_redirect=true)
 *     - Buffer HTTP : 4 KB (CONFIG via HTTP_BUFFER_SIZE)
 *     - Alignement TS : carry buffer 188-byte dans handle->ts_carry
 */
esp_err_t hls_http_download_segment(app_hls_player_t *handle, const char *url);

/**
 * @brief Libère le client HTTP persistant utilisé pour les segments TS
 *
 * À appeler à l'arrêt/destroy du player, ou lors d'un recreate explicite.
 * Safe si aucun client n'est actif.
 *
 * @param[in] handle Handle du player
 */
void hls_http_ts_client_cleanup(app_hls_player_t *handle);

/**
 * @brief Télécharge une playlist M3U8 et retourne son contenu
 *
 * Cette fonction :
 * - Télécharge la playlist (master ou media)
 * - Alloue dynamiquement un buffer (realloc progressif si chunked)
 * - Retourne le contenu complet (null-terminated)
 *
 * @param[in] url URL complète de la playlist M3U8 (HTTP ou HTTPS)
 *
 * @return
 *     - Pointeur vers buffer alloué contenant la playlist (null-terminated)
 *     - NULL en cas d'erreur (échec HTTP, playlist tronquée, allocation échouée)
 *
 * @note
 *     - Le buffer retourné DOIT être libéré par l'appelant avec free()
 *     - Taille : 16 KB initial, croissance par blocs de 16 KB, max 64 KB
 *     - Content-Length : si connu, realloc immédiat à la taille exacte
 *     - Chunked : realloc progressif, détection EOF avec retry (5 tentatives si read==0)
 *     - Timeout : 5 secondes
 *     - Redirections HTTP : détectées et loggées, mais NON suivies
 *     - Playlist tronquée : rejetée (retourne NULL)
 */
char* hls_http_download_m3u8(const char *url);

#ifdef __cplusplus
}
#endif

#endif // APP_HLS_PLAYER_HTTP_H
