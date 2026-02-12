/**
 * @file app_hls_player_ts_sync.h
 * @brief Module de synchronisation MPEG-TS pour le player HLS
 *
 * Ce module fournit des fonctions pures (sans dépendances externes) pour :
 * - Recherche de sync byte 0x47 validé (espacement 188-byte)
 * - Resynchronisation intelligente basée sur PID + PUSI + PES
 *
 * Ces fonctions sont optimisées pour ESP32-S3 et réduisent les faux positifs
 * lors de la resynchronisation MPEG-TS.
 */

#ifndef APP_HLS_PLAYER_TS_SYNC_H
#define APP_HLS_PLAYER_TS_SYNC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Resynchronisation intelligente MPEG-TS avec validation PID+PUSI+PES
 *
 * Cette fonction recherche un point de resynchronisation valide dans un buffer
 * en validant :
 * - Sync byte 0x47
 * - PID audio correspondant
 * - PUSI (Payload Unit Start Indicator) actif
 * - Start code PES (00 00 01)
 * - 3 paquets TS consécutifs avec sync bytes valides
 * - Au moins 1 paquet audio dans les 2 suivants
 *
 * @param[in]  buf        Buffer à scanner
 * @param[in]  len        Taille du buffer (bytes)
 * @param[in]  scan_max   Limite de scan (bytes, 0 = pas de limite)
 * @param[in]  audio_pid  PID audio à rechercher (typique : 0x100-0x1FFF)
 * @param[out] out_skip   Offset du point de resync trouvé (si succès)
 *
 * @return
 *     - true  : Point de resync valide trouvé, *out_skip contient l'offset
 *     - false : Aucun point valide trouvé dans scan_max bytes
 *
 * @note
 *     - Nécessite au minimum 188*3 = 564 bytes dans le buffer
 *     - Logs détaillés activés tous les 10 appels (ESP_LOGW)
 *     - Compteurs de debug statiques (thread-safe pour usage mono-task)
 */
bool hls_ts_resync_smart(const uint8_t *buf,
                         size_t len,
                         size_t scan_max,
                         uint16_t audio_pid,
                         size_t *out_skip);

/**
 * @brief Recherche le prochain sync byte 0x47 validé (espacement 188-byte)
 *
 * Cette fonction valide que les sync bytes sont espacés de 188 bytes pour
 * éliminer les faux positifs (0x47 dans le payload MPEG-TS).
 *
 * Validation :
 * - Double check : sync[i] == 0x47 && sync[i+188] == 0x47
 * - Triple check : si disponible, sync[i+376] == 0x47
 *
 * @param[in]  buf        Buffer à scanner
 * @param[in]  len        Taille du buffer (bytes)
 * @param[out] out_skip   Offset du prochain sync valide (si trouvé)
 *
 * @return
 *     - true  : Sync valide trouvé, *out_skip contient l'offset
 *     - false : Aucun sync valide trouvé (buffer < 188 bytes ou pas de match)
 *
 * @note
 *     - Nécessite au minimum 188 bytes dans le buffer
 *     - Si disponible, utilise triple check (188*2 = 376 bytes)
 *     - Pur (pas de logs, pas d'effets de bord), testable offline
 */
bool hls_ts_find_next_sync(const uint8_t *buf, size_t len, size_t *out_skip);

#ifdef __cplusplus
}
#endif

#endif // APP_HLS_PLAYER_TS_SYNC_H
