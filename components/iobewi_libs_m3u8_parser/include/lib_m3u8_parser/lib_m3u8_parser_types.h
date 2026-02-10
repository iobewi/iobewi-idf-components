/**
 * @file lib_m3u8_parser_types.h
 * @brief Types publics pour le parser M3U8/HLS
 *
 * Ce fichier définit les structures et constantes utilisées par le parser M3U8.
 */

#ifndef LIB_M3U8_PARSER_TYPES_H
#define LIB_M3U8_PARSER_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Nombre maximum de segments/variants supportés dans une playlist
 *
 * P2: Configurable via Kconfig (CONFIG_M3U8_MAX_ITEMS)
 * Par défaut : 16 (audio live), jusqu'à 64 (vidéo/VOD)
 */
#ifdef CONFIG_M3U8_MAX_ITEMS
#define LIB_M3U8_PARSER_MAX_SEGMENTS CONFIG_M3U8_MAX_ITEMS
#else
#define LIB_M3U8_PARSER_MAX_SEGMENTS 16  // Défaut si Kconfig non disponible
#endif

/**
 * @brief Longueur maximale d'une URL de segment
 *
 * P2: Configurable via Kconfig (CONFIG_M3U8_MAX_URL_LENGTH)
 * Par défaut : 160 (URLs CDN courtes), jusqu'à 512 (URLs très longues)
 */
#ifdef CONFIG_M3U8_MAX_URL_LENGTH
#define LIB_M3U8_PARSER_MAX_URL_LEN CONFIG_M3U8_MAX_URL_LENGTH
#else
#define LIB_M3U8_PARSER_MAX_URL_LEN 160  // Défaut si Kconfig non disponible
#endif

/**
 * @brief Longueur maximale d'une chaîne de codec
 *
 * P2: Configurable via Kconfig (CONFIG_M3U8_MAX_CODEC_LENGTH)
 * Par défaut : 32 (audio), jusqu'à 128 (vidéo+audio complexe)
 */
#ifdef CONFIG_M3U8_MAX_CODEC_LENGTH
#define LIB_M3U8_PARSER_MAX_CODEC_LEN CONFIG_M3U8_MAX_CODEC_LENGTH
#else
#define LIB_M3U8_PARSER_MAX_CODEC_LEN 32  // Défaut si Kconfig non disponible
#endif

/**
 * @brief Flags pour segment M3U8
 */
#define LIB_M3U8_PARSER_SEGMENT_FLAG_DISCONTINUITY (1 << 0)  /**< Discontinuité avant ce segment */

/**
 * @brief Structure représentant un segment M3U8
 *
 * OPTIMISATION RAM (P1): Types compacts
 * - duration : float → uint16_t (millisecondes, max 65.5s, suffisant pour HLS)
 * - sequence : int64_t → uint32_t (4.3 milliards, plusieurs années de streaming)
 * - discontinuity : bool → flag dans uint8_t (extensible pour futurs flags)
 */
typedef struct {
    char url[LIB_M3U8_PARSER_MAX_URL_LEN];  /**< URL du segment */
    uint16_t duration_ms;                    /**< Durée du segment en millisecondes (0-65535 ms) */
    uint32_t sequence;                       /**< Numéro de séquence (media_sequence + index) */
    uint8_t flags;                           /**< Flags : DISCONTINUITY, etc. */
} lib_m3u8_parser_segment_t;

/**
 * @brief Structure représentant un variant stream (master playlist)
 *
 * OPTIMISATION RAM (P1): bandwidth compact
 * - bandwidth : int64_t (bps) → uint32_t (kbps), max 4.3 Tbps, largement suffisant
 */
typedef struct {
    char url[LIB_M3U8_PARSER_MAX_URL_LEN];    /**< URL de la media playlist */
    uint32_t bandwidth_kbps;                   /**< Bande passante en kilobits/sec (0-4.3M kbps) */
    char codecs[LIB_M3U8_PARSER_MAX_CODEC_LEN]; /**< Codecs (ex: "mp4a.40.2"), P2: configurable */
    int width;                                 /**< Largeur vidéo (0 si audio only) */
    int height;                                /**< Hauteur vidéo (0 si audio only) */
} lib_m3u8_parser_variant_t;

/**
 * @brief Structure représentant une playlist M3U8 parsée
 *
 * OPTIMISATION RAM (P0): Union segments/variants
 * Une playlist est SOIT media (segments) SOIT master (variants), jamais les deux.
 * L'union réduit l'empreinte mémoire de ~50% (~19KB → ~10KB).
 *
 * Les macros de compatibilité permettent d'accéder aux champs comme avant :
 *   playlist->segments[i]       // accès aux segments (media playlist)
 *   playlist->variants[i]       // accès aux variants (master playlist)
 *   playlist->segment_count     // nombre de segments
 *   playlist->variant_count     // nombre de variants
 */
typedef struct {
    // Union : segments OU variants (jamais les deux simultanément)
    union {
        lib_m3u8_parser_segment_t segments[LIB_M3U8_PARSER_MAX_SEGMENTS];  /**< Segments (media playlist) */
        lib_m3u8_parser_variant_t variants[LIB_M3U8_PARSER_MAX_SEGMENTS];  /**< Variants (master playlist) */
    } items;

    int item_count;  /**< Nombre d'items (segments ou variants selon is_master_playlist) */

    // Métadonnées communes
    bool is_live;                 /**< true si playlist live */
    int target_duration;          /**< Durée cible des segments en secondes */
    bool is_master_playlist;      /**< true si master playlist (variants), false si media (segments) */
    uint32_t media_sequence;      /**< Premier numéro de séquence (P1: uint32_t suffit, 4.3 milliards) */
    int version;                  /**< Version HLS (EXT-X-VERSION) */
} lib_m3u8_parser_playlist_t;

// Macros de compatibilité API (permettent d'utiliser l'ancienne syntaxe)
#define segments items.segments
#define variants items.variants
#define segment_count item_count
#define variant_count item_count

#ifdef __cplusplus
}
#endif

#endif // LIB_M3U8_PARSER_TYPES_H
