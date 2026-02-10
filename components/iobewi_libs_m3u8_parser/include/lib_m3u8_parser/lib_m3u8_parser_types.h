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
 */
#define LIB_M3U8_PARSER_MAX_SEGMENTS 32

/**
 * @brief Longueur maximale d'une URL de segment
 */
#define LIB_M3U8_PARSER_MAX_URL_LEN 256

/**
 * @brief Structure représentant un segment M3U8
 */
typedef struct {
    char url[LIB_M3U8_PARSER_MAX_URL_LEN];  /**< URL du segment */
    float duration;                          /**< Durée du segment en secondes */
    int64_t sequence;                        /**< Numéro de séquence (media_sequence + index) */
    bool discontinuity;                      /**< true si discontinuité avant ce segment */
} lib_m3u8_parser_segment_t;

/**
 * @brief Structure représentant un variant stream (master playlist)
 */
typedef struct {
    char url[LIB_M3U8_PARSER_MAX_URL_LEN];  /**< URL de la media playlist */
    int64_t bandwidth;                       /**< Bande passante en bits/sec */
    char codecs[64];                         /**< Codecs (ex: "mp4a.40.2") */
    int width;                               /**< Largeur vidéo (0 si audio only) */
    int height;                              /**< Hauteur vidéo (0 si audio only) */
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
    int64_t media_sequence;       /**< Premier numéro de séquence (live, media playlist uniquement) */
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
