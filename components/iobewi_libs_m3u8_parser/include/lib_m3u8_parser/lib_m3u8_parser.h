/**
 * @file lib_m3u8_parser.h
 * @brief Parser M3U8/HLS pour ESP-IDF
 *
 * Ce composant fournit un parser léger pour les playlists M3U8 (HLS).
 * Il supporte les playlists media (segments audio/video) et les master playlists (multi-bitrate).
 *
 * Limitations actuelles :
 * - Pas de support pour le chiffrement AES-128
 * - Les master playlists retournent uniquement la première variante
 * - Maximum 32 segments par playlist
 */

#ifndef LIB_M3U8_PARSER_H
#define LIB_M3U8_PARSER_H

#include "lib_m3u8_parser_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parse le contenu d'une playlist M3U8
 *
 * Cette fonction parse un fichier M3U8 et extrait les informations sur les segments.
 * Les URLs relatives sont automatiquement résolues par rapport à base_url si fournie.
 *
 * Exemples d'URLs :
 * - URL absolue : "https://example.com/segment0.ts" (utilisée telle quelle)
 * - URL relative au domaine : "/path/segment0.ts" (préfixée avec protocole://domaine)
 * - URL relative au répertoire : "segment0.ts" (résolue par rapport au répertoire de base_url)
 *
 * @param[in]  content  Contenu de la playlist M3U8 (chaîne null-terminated)
 * @param[in]  base_url URL de base pour résoudre les URLs relatives (peut être NULL)
 * @param[out] playlist Pointeur vers la structure de sortie (doit être allouée par l'appelant)
 *
 * @return
 *     - ESP_OK : Parsing réussi
 *     - ESP_ERR_INVALID_ARG : Paramètres NULL ou invalides
 *     - ESP_ERR_NO_MEM : Échec d'allocation mémoire
 *     - ESP_FAIL : Format M3U8 invalide ou aucun segment trouvé
 */
esp_err_t lib_m3u8_parser_parse(const char *content, const char *base_url,
                                 lib_m3u8_parser_playlist_t *playlist);

/**
 * @brief Libère les ressources d'une playlist
 *
 * Note : Actuellement cette fonction ne fait que réinitialiser la structure car
 * tous les champs sont alloués statiquement. Elle est fournie pour compatibilité
 * future si des allocations dynamiques sont ajoutées.
 *
 * @param[in] playlist Playlist à libérer (peut être NULL)
 */
void lib_m3u8_parser_free(lib_m3u8_parser_playlist_t *playlist);

/**
 * @brief Affiche les informations de la playlist pour debug
 *
 * Utilise ESP_LOGI pour afficher le type de playlist, le nombre de segments,
 * et les URLs de tous les segments avec leur durée.
 *
 * @param[in] playlist Playlist à afficher (peut être NULL)
 */
void lib_m3u8_parser_dump(const lib_m3u8_parser_playlist_t *playlist);

#ifdef __cplusplus
}
#endif

#endif // LIB_M3U8_PARSER_H
