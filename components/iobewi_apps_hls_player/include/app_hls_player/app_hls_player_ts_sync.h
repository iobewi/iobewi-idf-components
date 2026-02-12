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

/**
 * @brief Structure pour conserver un paquet TS (held packet pattern)
 *
 * Utilisé pour préserver le paquet PUSI trouvé lors du drop-until-PUSI.
 * Le paquet est copié hors du ringbuffer avant libération pour éviter
 * de perdre le point de reprise PES propre.
 */
typedef struct {
    bool has_packet;      ///< True si un paquet est conservé
    uint8_t data[188];    ///< Données du paquet TS (188 bytes MPEG-TS)
    uint16_t pid;         ///< PID du paquet conservé
} hls_held_ts_packet_t;

/**
 * @brief Drop items ringbuffer jusqu'au prochain paquet TS avec PUSI du PID audio
 *
 * Cette fonction garantit une reprise sur frontière PES propre après drop-old,
 * évitant les erreurs AAC error:30 causées par réinjection de PES tronqué.
 *
 * Logique :
 * - Boucle receive(0) jusqu'à trouver paquet TS avec :
 *   * Sync byte 0x47
 *   * PUSI flag = 1 (Payload Unit Start Indicator, début PES/PSI)
 *   * PID = audio_pid
 * - Le paquet PUSI trouvé est CONSERVÉ dans out_held (held packet pattern)
 * - Max max_drop iterations (sécurité anti-boucle infinie si stream corrompu)
 *
 * @param[in]  ring_buffer Ring buffer source (NOSPLIT, items = 188 bytes)
 * @param[in]  max_drop    Nombre max d'items à dropper (ex: 50 = ~9.4 KB)
 * @param[in]  audio_pid   PID audio cible (ex: 0x0101 pour France Inter/FIP)
 * @param[out] out_held    Paquet PUSI conservé (ne pas dropper ce paquet clé!)
 *
 * @return Nombre d'items droppés (SANS compter le paquet held), -1 si PUSI non trouvé
 *
 * @note
 *     - Nécessite RingbufHandle_t (freertos/ringbuf.h)
 *     - Le paquet PUSI est copié dans out_held->data[] puis libéré du ringbuffer
 *     - L'appelant doit injecter out_held->data dans gather_buf pour reprise propre
 */
int hls_drop_until_audio_pusi(void *ring_buffer, int max_drop,
                               uint16_t audio_pid, hls_held_ts_packet_t *out_held);

/**
 * @brief Reset l'état du parser TS/PES (pour resync propre)
 *
 * Reset les états internes qui pourraient garder des traces de PES en cours :
 * - Continuity counters (si implémenté)
 * - PES assembly state (si implémenté)
 * - ES buffer (si implémenté)
 *
 * Note : Dans l'implémentation actuelle (gather_buf + leftover), le reset
 * est implicite via gather_len=0 + leftover_len=0. Cette fonction est un
 * placeholder pour futures extensions (ex: parser PAT/PMT/PES avec état).
 */
void hls_ts_parser_reset(void);

#ifdef __cplusplus
}
#endif

#endif // APP_HLS_PLAYER_TS_SYNC_H
