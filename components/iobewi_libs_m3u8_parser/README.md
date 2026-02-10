# lib_m3u8_parser - Parser M3U8/HLS

Parser léger pour les playlists M3U8 (HTTP Live Streaming) pour ESP-IDF.

## Rôle

Ce composant fournit un parser M3U8/HLS réutilisable sans dépendances externes.

**Responsabilités:**
- Parser les playlists M3U8 (media playlists et master playlists)
- Extraire les segments avec leurs URLs et durées
- Résoudre automatiquement les URLs relatives
- Détecter le type de playlist (VOD vs Live, Media vs Master)

**Ce que ce composant NE FAIT PAS:**
- Téléchargement HTTP (délégué à esp_http_client ou autre)
- Décodage audio/vidéo (délégué à esp_audio_codec ou autre)
- Gestion du buffering ou du streaming (délégué aux composants app_*)

## Fonctionnalités

### Supporté
- ✅ Media playlists (segments audio/vidéo)
- ✅ Master playlists (multi-bitrate) - première variante uniquement
- ✅ Playlists VOD (Video On Demand)
- ✅ Playlists Live
- ✅ Résolution URLs relatives (domaine et répertoire)
- ✅ Tags: `#EXTINF`, `#EXT-X-TARGETDURATION`, `#EXT-X-PLAYLIST-TYPE`, `#EXT-X-ENDLIST`, `#EXT-X-STREAM-INF`

### Limitations
- ❌ Chiffrement AES-128 non supporté
- ❌ Master playlists: seule la première variante est retournée
- ❌ Maximum 32 segments par playlist (configurable via `LIB_M3U8_PARSER_MAX_SEGMENTS`)
- ❌ URLs limitées à 256 caractères (configurable via `LIB_M3U8_PARSER_MAX_URL_LEN`)

## API Publique

### Types

```c
// Constantes
#define LIB_M3U8_PARSER_MAX_SEGMENTS 32
#define LIB_M3U8_PARSER_MAX_URL_LEN 256

// Segment individuel
typedef struct {
    char url[LIB_M3U8_PARSER_MAX_URL_LEN];
    float duration;  // En secondes
} lib_m3u8_parser_segment_t;

// Playlist parsée
typedef struct {
    lib_m3u8_parser_segment_t segments[LIB_M3U8_PARSER_MAX_SEGMENTS];
    int segment_count;
    bool is_live;              // true si live, false si VOD
    int target_duration;       // Durée cible des segments (secondes)
    bool is_master_playlist;   // true si master playlist
} lib_m3u8_parser_playlist_t;
```

### Fonctions

```c
// Parser une playlist M3U8
esp_err_t lib_m3u8_parser_parse(const char *content, const char *base_url,
                                 lib_m3u8_parser_playlist_t *playlist);

// Libérer les ressources (actuellement juste un memset)
void lib_m3u8_parser_free(lib_m3u8_parser_playlist_t *playlist);

// Debug: afficher les infos de la playlist
void lib_m3u8_parser_dump(const lib_m3u8_parser_playlist_t *playlist);
```

### Codes de retour

- `ESP_OK` : Parsing réussi
- `ESP_ERR_INVALID_ARG` : Paramètres NULL ou invalides
- `ESP_ERR_NO_MEM` : Échec d'allocation mémoire
- `ESP_FAIL` : Format M3U8 invalide ou aucun segment trouvé

## Exemple d'utilisation

### Cas simple : Playlist VOD

```c
#include "lib_m3u8_parser/lib_m3u8_parser.h"

const char *playlist_content =
    "#EXTM3U\n"
    "#EXT-X-VERSION:3\n"
    "#EXT-X-TARGETDURATION:10\n"
    "#EXT-X-PLAYLIST-TYPE:VOD\n"
    "#EXTINF:9.5,\n"
    "segment0.ts\n"
    "#EXTINF:10.0,\n"
    "segment1.ts\n"
    "#EXT-X-ENDLIST\n";

lib_m3u8_parser_playlist_t playlist;
esp_err_t ret = lib_m3u8_parser_parse(playlist_content, NULL, &playlist);

if (ret == ESP_OK) {
    printf("Segments trouvés: %d\n", playlist.segment_count);
    for (int i = 0; i < playlist.segment_count; i++) {
        printf("  [%d] %.1fs - %s\n", i,
               playlist.segments[i].duration,
               playlist.segments[i].url);
    }
    lib_m3u8_parser_free(&playlist);
}
```

### Avec résolution d'URLs relatives

```c
const char *playlist_content =
    "#EXTM3U\n"
    "#EXT-X-TARGETDURATION:10\n"
    "#EXTINF:10.0,\n"
    "segment0.ts\n"
    "#EXTINF:10.0,\n"
    "/absolute/segment1.ts\n"
    "#EXTINF:10.0,\n"
    "https://cdn.example.com/segment2.ts\n"
    "#EXT-X-ENDLIST\n";

const char *base_url = "https://example.com/streams/playlist.m3u8";

lib_m3u8_parser_playlist_t playlist;
esp_err_t ret = lib_m3u8_parser_parse(playlist_content, base_url, &playlist);

if (ret == ESP_OK) {
    // Résultat:
    //   segments[0].url = "https://example.com/streams/segment0.ts"
    //   segments[1].url = "https://example.com/absolute/segment1.ts"
    //   segments[2].url = "https://cdn.example.com/segment2.ts"
}
```

### Master playlist (multi-bitrate)

```c
const char *master_content =
    "#EXTM3U\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360\n"
    "low.m3u8\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=1400000,RESOLUTION=1280x720\n"
    "high.m3u8\n";

const char *base_url = "https://example.com/master.m3u8";

lib_m3u8_parser_playlist_t playlist;
esp_err_t ret = lib_m3u8_parser_parse(master_content, base_url, &playlist);

if (ret == ESP_OK) {
    if (playlist.is_master_playlist) {
        // Prend la première variante
        const char *media_playlist_url = playlist.segments[0].url;
        // "https://example.com/low.m3u8"

        // Télécharger et parser la media playlist...
    }
}
```

## Résolution d'URLs

Le parser gère trois types d'URLs :

1. **URLs absolues** : Utilisées telles quelles
   ```
   https://cdn.example.com/segment.ts
   ```

2. **URLs relatives au domaine** (commencent par `/`) : Protocole + domaine de `base_url` + URL
   ```
   base_url = "https://example.com/streams/playlist.m3u8"
   relative = "/absolute/path/segment.ts"
   → "https://example.com/absolute/path/segment.ts"
   ```

3. **URLs relatives au répertoire** : Répertoire de `base_url` + URL
   ```
   base_url = "https://example.com/streams/playlist.m3u8"
   relative = "segment.ts"
   → "https://example.com/streams/segment.ts"
   ```

## Workflow typique

Pour un streaming HLS complet :

1. **Télécharger** la playlist principale (master ou media)
2. **Parser** avec `lib_m3u8_parser_parse()`
3. Si `is_master_playlist == true` :
   - Télécharger la media playlist (première variante)
   - Parser à nouveau
4. **Itérer** sur les segments :
   - Télécharger chaque `segments[i].url`
   - Décoder le contenu (AAC, TS, etc.)
   - Jouer l'audio/vidéo
5. Si `is_live == true` : Rafraîchir périodiquement la playlist

## Dépendances

- ESP-IDF >= 5.0
- Aucune dépendance externe

## Tests

Tests unitaires disponibles dans `test/test_lib_m3u8_parser.c` :
- ✅ Validation arguments NULL
- ✅ Parsing playlists VOD et Live
- ✅ Résolution URLs relatives (3 types)
- ✅ Master playlists
- ✅ Limites (32 segments max)

Pour exécuter les tests :
```bash
cd components/iobewi_libs_m3u8_parser
idf.py build
idf.py -p PORT flash monitor
```

## Notes techniques

### Mémoire
- Structure playlist : ~8.5 KB (32 segments × 260 bytes + métadonnées)
- Allocation dynamique : ~1 KB temporaire pendant le parsing (copie du contenu)

### Performance
- Parsing typique : < 5ms pour une playlist de 10 segments
- Complexité : O(n) avec n = nombre de lignes

### Thread-safety
- ❌ Les fonctions ne sont pas thread-safe
- L'utilisateur doit gérer la synchronisation si accès concurrent

## Voir aussi

- Composant `iobewi_apps_hls_player` : Utilise ce parser pour le streaming HLS complet
- Exemple `hls_stream_app` : Démo de streaming HLS avec MAX98357A
- [RFC 8216 - HTTP Live Streaming](https://datatracker.ietf.org/doc/html/rfc8216)
