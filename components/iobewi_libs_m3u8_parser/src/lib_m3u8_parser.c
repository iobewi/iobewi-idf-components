/**
 * @file lib_m3u8_parser.c
 * @brief Implémentation du parser M3U8/HLS
 */

#include "lib_m3u8_parser/lib_m3u8_parser.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

static const char *TAG = "lib_m3u8_parser";

/**
 * @brief Extrait une valeur d'attribut depuis une chaîne
 * Ex: "BANDWIDTH=800000,..." → copie "800000" dans dest
 * FIX: Buffer destination + matching correct avec délimiteurs
 *
 * @return true si trouvé, false sinon
 */
static bool get_attribute_value(const char *line, const char *attr_name, char *dest, size_t dest_size)
{
    if (line == NULL || attr_name == NULL || dest == NULL || dest_size == 0) {
        return false;
    }

    // FIX: Chercher attr_name avec délimiteurs pour éviter AVERAGE-BANDWIDTH vs BANDWIDTH
    size_t name_len = strlen(attr_name);
    const char *p = line;

    while ((p = strstr(p, attr_name)) != NULL) {
        // FIX #2: Remonter en arrière en sautant les espaces pour trouver le vrai délimiteur
        const char *q = p;
        while (q > line && (q[-1] == ' ' || q[-1] == '\t')) {
            q--;
        }

        // Vérifier délimiteur avant (début de ligne, ':', ou ',')
        bool ok_prev = (q == line) || (q[-1] == ':') || (q[-1] == ',');
        // Vérifier '=' après
        bool ok_next = (p[name_len] == '=');

        if (ok_prev && ok_next) {
            // Trouvé !
            const char *attr_pos = p + name_len + 1;  // Après "NAME="

            // Copier jusqu'à la virgule ou fin de ligne
            const char *end = attr_pos;
            while (*end && *end != ',' && *end != '\r' && *end != '\n') {
                end++;
            }

            size_t len = end - attr_pos;
            if (len >= dest_size) {
                len = dest_size - 1;
            }

            strncpy(dest, attr_pos, len);
            dest[len] = '\0';

            // FIX #1: Trim quotes basé sur strlen(dest) pour gérer troncature
            size_t dlen = strlen(dest);
            if (dlen >= 2 && dest[0] == '"' && dest[dlen-1] == '"') {
                memmove(dest, dest + 1, dlen - 2);
                dest[dlen - 2] = '\0';
            }

            return true;
        }

        // Pas le bon match, continuer la recherche
        p += name_len;
    }

    return false;  // Pas trouvé
}

/**
 * @brief Résout une URL relative par rapport à une URL de base
 * FIX: Amélioration du cas base_url sans path
 */
static void resolve_url(const char *base_url, const char *relative_url, char *output, size_t output_size)
{
    if (relative_url == NULL || output == NULL) {
        return;
    }

    // Si l'URL relative est déjà absolue (commence par http:// ou https://)
    if (strncmp(relative_url, "http://", 7) == 0 || strncmp(relative_url, "https://", 8) == 0) {
        strncpy(output, relative_url, output_size - 1);
        output[output_size - 1] = '\0';
        return;
    }

    // Si pas de base_url, utiliser l'URL relative telle quelle
    if (base_url == NULL) {
        strncpy(output, relative_url, output_size - 1);
        output[output_size - 1] = '\0';
        return;
    }

    // Si l'URL relative commence par /, c'est une URL absolue sur le même domaine
    if (relative_url[0] == '/') {
        // Extraire le protocole et le domaine de base_url (origin)
        const char *proto_end = strstr(base_url, "://");
        if (proto_end) {
            proto_end += 3; // Passer "://"
            const char *path_start = strchr(proto_end, '/');
            size_t origin_len;

            if (path_start) {
                origin_len = path_start - base_url;
            } else {
                // Pas de path dans base_url (ex: https://host)
                origin_len = strlen(base_url);
            }

            snprintf(output, output_size, "%.*s%s", (int)origin_len, base_url, relative_url);
            return;
        }
        // Fallback
        snprintf(output, output_size, "%s%s", base_url, relative_url);
        return;
    }

    // URL relative au chemin courant
    // Trouver le dernier / dans base_url, mais seulement après l'origin (scheme://host/)
    const char *proto_end = strstr(base_url, "://");
    const char *origin_end = NULL;

    if (proto_end) {
        proto_end += 3;  // Après "://"
        origin_end = strchr(proto_end, '/');
    }

    const char *last_slash = strrchr(base_url, '/');

    // last_slash doit être après l'origin pour être un path slash
    if (last_slash && origin_end && last_slash > origin_end) {
        size_t base_len = last_slash - base_url + 1;
        snprintf(output, output_size, "%.*s%s", (int)base_len, base_url, relative_url);
    } else {
        // Pas de path dans base_url, ajouter /
        snprintf(output, output_size, "%s/%s", base_url, relative_url);
    }
}

/**
 * @brief Lit une ligne depuis un buffer
 * FIX #1: Remplace strtok pour un parsing propre ligne par ligne
 *
 * @param buffer Buffer source
 * @param offset Offset actuel (sera mis à jour)
 * @param line Buffer destination pour la ligne
 * @param line_size Taille du buffer destination
 * @return true si ligne lue, false si fin
 */
static bool read_line(const char *buffer, size_t *offset, char *line, size_t line_size)
{
    if (buffer == NULL || offset == NULL || line == NULL) {
        return false;
    }

    const char *start = buffer + *offset;
    const char *end = start;

    // Chercher fin de ligne (\n)
    while (*end && *end != '\n') {
        end++;
    }

    if (start == end && *end == '\0') {
        return false;  // Fin du buffer
    }

    // Copier la ligne
    size_t len = end - start;
    if (len >= line_size) {
        len = line_size - 1;
        ESP_LOGW(TAG, "Ligne tronquée à %d caractères", (int)len);
    }

    if (len > 0) {
        strncpy(line, start, len);
    }
    line[len] = '\0';

    // Trim trailing \r si présent
    if (len > 0 && line[len - 1] == '\r') {
        line[len - 1] = '\0';
    }

    // Mettre à jour offset
    *offset += (end - start);
    if (*end == '\n') {
        (*offset)++;
    }

    return true;
}

/**
 * @brief Trim espaces au début et fin d'une ligne
 * FIX: Vrai ltrim avec memmove
 */
static void trim_line(char *s)
{
    if (s == NULL) return;

    // ltrim - décaler réellement la chaîne
    char *p = s;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (p != s) {
        memmove(s, p, strlen(p) + 1);
    }

    // rtrim
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t')) {
        s[--len] = '\0';
    }
}

esp_err_t lib_m3u8_parser_parse(const char *content, const char *base_url,
                                 lib_m3u8_parser_playlist_t *playlist)
{
    // Validation des arguments
    if (content == NULL || playlist == NULL) {
        ESP_LOGE(TAG, "Arguments NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Initialiser la structure de sortie
    memset(playlist, 0, sizeof(lib_m3u8_parser_playlist_t));
    playlist->media_sequence = 0;  // Défaut si non spécifié
    playlist->is_live = true;       // Défaut live, mis à false si #EXT-X-ENDLIST

    // Vérifier l'en-tête M3U8
    if (strncmp(content, "#EXTM3U", 7) != 0) {
        ESP_LOGE(TAG, "En-tête M3U8 invalide");
        return ESP_FAIL;
    }

    // Parser ligne par ligne (FIX #1)
    char line[512];
    size_t offset = 0;
    float current_duration = 0.0f;
    int segment_idx = 0;
    int variant_idx = 0;
    int media_pos = 0;  // FIX #5: Position dans la playlist (pour sequence)
    bool next_is_discontinuity = false;

    // Pour master playlists
    lib_m3u8_parser_variant_t current_variant;
    memset(&current_variant, 0, sizeof(current_variant));
    bool has_pending_variant = false;

    while (read_line(content, &offset, line, sizeof(line))) {
        trim_line(line);

        // Ignorer les lignes vides
        if (strlen(line) == 0) {
            continue;
        }

        // Parser les tags M3U8
        if (strncmp(line, "#EXT-X-VERSION:", 15) == 0) {
            // FIX: Ajouter version
            playlist->version = atoi(line + 15);
        }
        else if (strncmp(line, "#EXT-X-STREAM-INF:", 18) == 0) {
            // FIX #3: Parser les attributs de STREAM-INF
            playlist->is_master_playlist = true;

            char value_buf[128];

            if (get_attribute_value(line, "BANDWIDTH", value_buf, sizeof(value_buf))) {
                current_variant.bandwidth = atoll(value_buf);
            }

            if (get_attribute_value(line, "CODECS", value_buf, sizeof(value_buf))) {
                strncpy(current_variant.codecs, value_buf, sizeof(current_variant.codecs) - 1);
                current_variant.codecs[sizeof(current_variant.codecs) - 1] = '\0';
            }

            if (get_attribute_value(line, "RESOLUTION", value_buf, sizeof(value_buf))) {
                sscanf(value_buf, "%dx%d", &current_variant.width, &current_variant.height);
            }

            has_pending_variant = true;
        }
        else if (strncmp(line, "#EXT-X-TARGETDURATION:", 22) == 0) {
            playlist->target_duration = atoi(line + 22);
        }
        else if (strncmp(line, "#EXT-X-MEDIA-SEQUENCE:", 22) == 0) {
            // FIX #2: Parser MEDIA-SEQUENCE
            playlist->media_sequence = atoll(line + 22);
        }
        else if (strncmp(line, "#EXT-X-PLAYLIST-TYPE:", 21) == 0) {
            // Juste pour info, is_live sera déterminé par ENDLIST
            if (strstr(line, "VOD")) {
                ESP_LOGI(TAG, "PLAYLIST-TYPE: VOD");
            } else if (strstr(line, "EVENT")) {
                ESP_LOGI(TAG, "PLAYLIST-TYPE: EVENT");
            }
        }
        else if (strncmp(line, "#EXTINF:", 8) == 0) {
            // Extraire la durée
            current_duration = atof(line + 8);
        }
        else if (strncmp(line, "#EXT-X-DISCONTINUITY", 20) == 0) {
            // FIX: Support DISCONTINUITY
            next_is_discontinuity = true;
        }
        else if (strncmp(line, "#EXT-X-ENDLIST", 14) == 0) {
            playlist->is_live = false;
        }
        else if (line[0] != '#') {
            // Ligne d'URL

            if (has_pending_variant) {
                // FIX #3: Master playlist - stocker le variant avec ses infos
                if (variant_idx < LIB_M3U8_PARSER_MAX_SEGMENTS) {
                    resolve_url(base_url, line, current_variant.url, LIB_M3U8_PARSER_MAX_URL_LEN);

                    playlist->variants[variant_idx] = current_variant;
                    variant_idx++;

                    ESP_LOGI(TAG, "Variant %d: bandwidth=%lld, url=%s",
                             variant_idx - 1, (long long)current_variant.bandwidth, current_variant.url);
                }

                // Reset pour le prochain variant
                memset(&current_variant, 0, sizeof(current_variant));
                has_pending_variant = false;
            }
            else {
                // Media playlist
                // FIX #5: Incrémenter media_pos pour TOUTES les URLs (même rejetées)
                if (current_duration > 0.0f) {
                    // Accepter le segment
                    if (segment_idx < LIB_M3U8_PARSER_MAX_SEGMENTS) {
                        lib_m3u8_parser_segment_t *seg = &playlist->segments[segment_idx];
                        seg->duration = current_duration;
                        seg->sequence = playlist->media_sequence + media_pos;  // FIX #5: Utiliser media_pos
                        seg->discontinuity = next_is_discontinuity;

                        // Résoudre l'URL
                        resolve_url(base_url, line, seg->url, LIB_M3U8_PARSER_MAX_URL_LEN);

                        segment_idx++;
                    }

                    current_duration = 0.0f;
                    next_is_discontinuity = false;
                } else {
                    ESP_LOGW(TAG, "URL de segment sans EXTINF: %s (ignoré)", line);
                }

                media_pos++;  // FIX #5: Incrémenter même si segment rejeté
            }
        }
    }

    // FIX #1: Vérifier variant incomplet
    if (has_pending_variant) {
        ESP_LOGW(TAG, "STREAM-INF sans URI associée (variant incomplet ignoré)");
    }

    if (playlist->is_master_playlist) {
        playlist->variant_count = variant_idx;
        if (playlist->variant_count == 0) {
            ESP_LOGE(TAG, "Aucun variant trouvé dans la master playlist");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Master playlist parsée: %d variants", playlist->variant_count);
    } else {
        playlist->segment_count = segment_idx;
        if (playlist->segment_count == 0) {
            ESP_LOGE(TAG, "Aucun segment trouvé dans la playlist");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Media playlist parsée: %d segments, live=%d, media_seq=%lld, target_duration=%d",
                 playlist->segment_count, playlist->is_live, (long long)playlist->media_sequence,
                 playlist->target_duration);
    }

    return ESP_OK;
}

void lib_m3u8_parser_free(lib_m3u8_parser_playlist_t *playlist)
{
    if (playlist) {
        memset(playlist, 0, sizeof(lib_m3u8_parser_playlist_t));
    }
}

void lib_m3u8_parser_dump(const lib_m3u8_parser_playlist_t *playlist)
{
    if (playlist == NULL) {
        ESP_LOGW(TAG, "Playlist NULL");
        return;
    }

    ESP_LOGI(TAG, "=== Playlist M3U8 ===");
    ESP_LOGI(TAG, "Type: %s", playlist->is_master_playlist ? "Master" : "Media");
    ESP_LOGI(TAG, "Version: %d", playlist->version);

    if (playlist->is_master_playlist) {
        ESP_LOGI(TAG, "Variants: %d", playlist->variant_count);
        for (int i = 0; i < playlist->variant_count; i++) {
            const lib_m3u8_parser_variant_t *v = &playlist->variants[i];
            ESP_LOGI(TAG, "  [%d] BW=%lld bps, %dx%d, codecs=%s",
                     i, (long long)v->bandwidth, v->width, v->height,
                     v->codecs[0] ? v->codecs : "none");
            ESP_LOGI(TAG, "      %s", v->url);
        }
    } else {
        ESP_LOGI(TAG, "Segments: %d", playlist->segment_count);
        ESP_LOGI(TAG, "Live: %s", playlist->is_live ? "oui" : "non");
        ESP_LOGI(TAG, "Media sequence: %lld", (long long)playlist->media_sequence);
        ESP_LOGI(TAG, "Target duration: %d s", playlist->target_duration);

        for (int i = 0; i < playlist->segment_count; i++) {
            const lib_m3u8_parser_segment_t *seg = &playlist->segments[i];
            ESP_LOGI(TAG, "  [%d] seq=%lld, %.1fs%s - %s",
                     i, (long long)seg->sequence, seg->duration,
                     seg->discontinuity ? " [DISC]" : "",
                     seg->url);
        }
    }
}
