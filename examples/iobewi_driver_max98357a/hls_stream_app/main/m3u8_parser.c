#include "m3u8_parser.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "m3u8_parser";

/**
 * @brief Résout une URL relative par rapport à une URL de base
 */
static void resolve_url(const char *base_url, const char *relative_url, char *output, size_t output_size)
{
    if (base_url == NULL || relative_url == NULL || output == NULL) {
        return;
    }

    // Si l'URL relative est déjà absolue (commence par http:// ou https://)
    if (strncmp(relative_url, "http://", 7) == 0 || strncmp(relative_url, "https://", 8) == 0) {
        strncpy(output, relative_url, output_size - 1);
        output[output_size - 1] = '\0';
        return;
    }

    // Si l'URL relative commence par /, c'est une URL absolue sur le même domaine
    if (relative_url[0] == '/') {
        // Extraire le protocole et le domaine de base_url
        const char *proto_end = strstr(base_url, "://");
        if (proto_end) {
            proto_end += 3; // Passer "://"
            const char *path_start = strchr(proto_end, '/');
            if (path_start) {
                size_t domain_len = path_start - base_url;
                snprintf(output, output_size, "%.*s%s", (int)domain_len, base_url, relative_url);
                return;
            }
        }
        // Fallback
        snprintf(output, output_size, "%s%s", base_url, relative_url);
        return;
    }

    // URL relative au chemin courant
    // Trouver le dernier / dans base_url
    const char *last_slash = strrchr(base_url, '/');
    if (last_slash) {
        size_t base_len = last_slash - base_url + 1;
        snprintf(output, output_size, "%.*s%s", (int)base_len, base_url, relative_url);
    } else {
        // Pas de slash, juste concaténer
        snprintf(output, output_size, "%s/%s", base_url, relative_url);
    }
}

bool m3u8_parse(const char *content, const char *base_url, m3u8_playlist_t *playlist)
{
    if (content == NULL || playlist == NULL) {
        ESP_LOGE(TAG, "Paramètres NULL");
        return false;
    }

    memset(playlist, 0, sizeof(m3u8_playlist_t));

    // Vérifier l'en-tête M3U8
    if (strncmp(content, "#EXTM3U", 7) != 0) {
        ESP_LOGE(TAG, "En-tête M3U8 invalide");
        return false;
    }

    char *content_copy = strdup(content);
    if (content_copy == NULL) {
        ESP_LOGE(TAG, "Échec d'allocation mémoire");
        return false;
    }

    char *line = strtok(content_copy, "\n\r");
    float current_duration = 0.0f;
    int segment_idx = 0;
    bool is_after_stream_inf = false;  // Flag pour les master playlists

    while (line != NULL && segment_idx < M3U8_MAX_SEGMENTS) {
        // Supprimer les espaces au début et à la fin
        while (*line == ' ' || *line == '\t') line++;
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == ' ' || line[len-1] == '\t')) {
            line[--len] = '\0';
        }

        // Ignorer les lignes vides
        if (len == 0) {
            line = strtok(NULL, "\n\r");
            continue;
        }

        // Parser les tags M3U8
        if (strncmp(line, "#EXT-X-STREAM-INF:", 18) == 0) {
            // C'est une master playlist (variant streams)
            playlist->is_master_playlist = true;
            is_after_stream_inf = true;
            ESP_LOGI(TAG, "Master playlist détectée");
        }
        else if (strncmp(line, "#EXT-X-TARGETDURATION:", 21) == 0) {
            playlist->target_duration = atoi(line + 21);
        }
        else if (strncmp(line, "#EXT-X-PLAYLIST-TYPE:", 21) == 0) {
            // VOD ou EVENT
            playlist->is_live = (strstr(line, "VOD") == NULL);
        }
        else if (strncmp(line, "#EXTINF:", 8) == 0) {
            // Extraire la durée
            current_duration = atof(line + 8);
        }
        else if (strncmp(line, "#EXT-X-ENDLIST", 14) == 0) {
            playlist->is_live = false;
        }
        else if (line[0] != '#') {
            // Ligne d'URL de segment (ou de media playlist si master)
            if (is_after_stream_inf) {
                // Master playlist : c'est une URL de media playlist
                m3u8_segment_t *seg = &playlist->segments[segment_idx];
                seg->duration = 0.0f;

                // Résoudre l'URL
                if (base_url) {
                    resolve_url(base_url, line, seg->url, M3U8_MAX_URL_LEN);
                } else {
                    strncpy(seg->url, line, M3U8_MAX_URL_LEN - 1);
                    seg->url[M3U8_MAX_URL_LEN - 1] = '\0';
                }

                segment_idx++;
                is_after_stream_inf = false;

                // Pour simplifier, on prend seulement la première variante
                if (playlist->is_master_playlist) {
                    ESP_LOGI(TAG, "Media playlist trouvée: %s", seg->url);
                    break;  // On s'arrête à la première variante
                }
            }
            else if (current_duration > 0.0f || segment_idx == 0) {
                // Media playlist : c'est un vrai segment
                m3u8_segment_t *seg = &playlist->segments[segment_idx];
                seg->duration = current_duration;

                // Résoudre l'URL
                if (base_url) {
                    resolve_url(base_url, line, seg->url, M3U8_MAX_URL_LEN);
                } else {
                    strncpy(seg->url, line, M3U8_MAX_URL_LEN - 1);
                    seg->url[M3U8_MAX_URL_LEN - 1] = '\0';
                }

                segment_idx++;
                current_duration = 0.0f;
            }
        }

        line = strtok(NULL, "\n\r");
    }

    playlist->segment_count = segment_idx;
    free(content_copy);

    if (playlist->segment_count == 0) {
        ESP_LOGE(TAG, "Aucun segment trouvé dans la playlist");
        return false;
    }

    ESP_LOGI(TAG, "Playlist parsée: %d segments, live=%d, target_duration=%d",
             playlist->segment_count, playlist->is_live, playlist->target_duration);

    return true;
}

void m3u8_free(m3u8_playlist_t *playlist)
{
    if (playlist) {
        memset(playlist, 0, sizeof(m3u8_playlist_t));
    }
}

void m3u8_dump(const m3u8_playlist_t *playlist)
{
    if (playlist == NULL) {
        ESP_LOGW(TAG, "Playlist NULL");
        return;
    }

    ESP_LOGI(TAG, "=== Playlist M3U8 ===");
    ESP_LOGI(TAG, "Type: %s", playlist->is_master_playlist ? "Master" : "Media");
    ESP_LOGI(TAG, "Segments: %d", playlist->segment_count);
    ESP_LOGI(TAG, "Live: %s", playlist->is_live ? "oui" : "non");
    ESP_LOGI(TAG, "Target duration: %d s", playlist->target_duration);

    for (int i = 0; i < playlist->segment_count; i++) {
        ESP_LOGI(TAG, "  [%d] %.1fs - %s",
                 i,
                 playlist->segments[i].duration,
                 playlist->segments[i].url);
    }
}
