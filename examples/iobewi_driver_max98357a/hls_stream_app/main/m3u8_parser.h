#ifndef M3U8_PARSER_H
#define M3U8_PARSER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define M3U8_MAX_SEGMENTS 32
#define M3U8_MAX_URL_LEN  256

/**
 * @brief Structure représentant un segment M3U8
 */
typedef struct {
    char url[M3U8_MAX_URL_LEN];  /**< URL du segment */
    float duration;               /**< Durée du segment en secondes */
} m3u8_segment_t;

/**
 * @brief Structure représentant une playlist M3U8 parsée
 */
typedef struct {
    m3u8_segment_t segments[M3U8_MAX_SEGMENTS];  /**< Tableau de segments */
    int segment_count;                            /**< Nombre de segments */
    bool is_live;                                 /**< true si playlist live */
    int target_duration;                          /**< Durée cible des segments */
    bool is_master_playlist;                      /**< true si master playlist (variant streams) */
} m3u8_playlist_t;

/**
 * @brief Parse le contenu d'une playlist M3U8
 *
 * Cette fonction parse un fichier M3U8 simple. Limitations :
 * - Pas de support pour le chiffrement AES
 * - Pas de support pour les variant streams (multi-bitrate)
 * - URLs relatives résolues par rapport à base_url
 *
 * @param content Contenu de la playlist M3U8 (null-terminated)
 * @param base_url URL de base pour résoudre les URLs relatives (peut être NULL)
 * @param playlist Pointeur vers la structure de sortie
 * @return true en cas de succès, false sinon
 */
bool m3u8_parse(const char *content, const char *base_url, m3u8_playlist_t *playlist);

/**
 * @brief Libère les ressources d'une playlist
 *
 * @param playlist Playlist à libérer
 */
void m3u8_free(m3u8_playlist_t *playlist);

/**
 * @brief Affiche les informations de la playlist pour debug
 *
 * @param playlist Playlist à afficher
 */
void m3u8_dump(const m3u8_playlist_t *playlist);

#ifdef __cplusplus
}
#endif

#endif // M3U8_PARSER_H
