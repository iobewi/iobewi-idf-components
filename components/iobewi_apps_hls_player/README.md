# app_hls_player - Player HTTP Live Streaming

Player HLS (HTTP Live Streaming) complet pour ESP-IDF avec abstraction audio via callback.

## Rôle

Ce composant fournit une orchestration complète pour le streaming HLS/M3U8 sur ESP32.

**Responsabilités:**
- Téléchargement et parsing de playlists M3U8 (master et media)
- Téléchargement séquentiel des segments audio
- Décodage AAC/MPEG-TS vers PCM stéréo 16-bit
- Buffering intelligent avec resynchronisation automatique
- Abstraction de la sortie audio via callback (découplage du driver)

**Ce que ce composant NE FAIT PAS:**
- Accès matériel audio (délégué au callback utilisateur)
- Gestion WiFi/réseau (délégué à l'application)
- Support vidéo (audio uniquement)

## Architecture

### Vue d'ensemble

```
┌─────────────────────────────────────────────────────────────┐
│                      app_hls_player                         │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────┐         Ring Buffer          ┌─────────┐ │
│  │ hls_fetch    │──────► [Données encodées] ──►│  audio  │ │
│  │ task         │           (100 KB)            │  play   │ │
│  │              │                               │  task   │ │
│  │ - Download   │         Sémaphore             │         │ │
│  │   M3U8       │◄──────────────────────────────│ - Déco- │ │
│  │ - Parse      │   (Signal si buffer < 40%)    │   dage  │ │
│  │ - Download   │                               │ - write_│ │
│  │   segments   │                               │   cb()  │ │
│  └──────────────┘                               └─────────┘ │
│         │                                             │     │
│         └─────────── esp_http_client ─────────────────┘     │
│                                                             │
│  Dépendances:                                               │
│  - lib_m3u8_parser (parsing playlists)                      │
│  - esp_audio_codec (décodage AAC/TS)                        │
└─────────────────────────────────────────────────────────────┘
                              │
                              │ write_cb(user_ctx, pcm_data, ...)
                              ▼
                    ┌──────────────────────┐
                    │  Driver Audio        │
                    │  (MAX98357A, etc.)   │
                    └──────────────────────┘
```

### Tâches concurrentes

1. **hls_fetch_task** (priorité 5, 14 KB stack)
   - Télécharge la playlist M3U8
   - Parse avec `lib_m3u8_parser`
   - Gère master playlists (sélection qualité: midfi > hifi > lofi)
   - Télécharge 2 segments d'avance
   - Évite les doublons via numéro de séquence
   - Attend signal du sémaphore (contrôle par niveau buffer)

2. **audio_play_task** (priorité 8, 6 KB stack)
   - Décode MPEG-TS + AAC vers PCM 16-bit stéréo
   - Applique réduction volume logicielle (25%)
   - Écrit via `write_cb()` fourni par l'utilisateur
   - Resynchronisation automatique en cas d'erreur
   - Signale fetch_task si buffer < 40%

## API Publique

### Types

```c
// Handle opaque
typedef struct app_hls_player_s app_hls_player_t;

// Callback d'écriture audio
typedef esp_err_t (*app_hls_player_write_cb_t)(
    void *user_ctx,           // Contexte utilisateur (ex: driver)
    const void *data,         // Données PCM stéréo 16-bit LE
    size_t size,              // Taille en bytes
    size_t *bytes_written,    // Bytes effectivement écrits
    uint32_t timeout_ms       // Timeout
);

// Configuration
typedef struct {
    const char *stream_url;                 // URL M3U8 (master ou media)
    size_t buffer_size;                     // Taille buffer (recommandé: 100 KB)
    app_hls_player_write_cb_t write_cb;     // Callback écriture (obligatoire)
    void *write_ctx;                        // Contexte utilisateur
} app_hls_player_config_t;

// Statistiques
typedef struct {
    size_t bytes_downloaded;    // Total bytes téléchargés
    int buffer_fill_percent;    // Niveau buffer (0-100%)
    bool is_playing;            // État actif
} app_hls_player_stats_t;
```

### Fonctions

```c
// Initialiser config avec valeurs par défaut
esp_err_t app_hls_player_config_init(app_hls_player_config_t *config);

// Créer player (alloue ressources, ne démarre PAS)
esp_err_t app_hls_player_new(const app_hls_player_config_t *config, app_hls_player_t **out);

// Démarrer streaming (crée tâches)
esp_err_t app_hls_player_start(app_hls_player_t *handle);

// Arrêter streaming (détruit tâches, vide buffer)
esp_err_t app_hls_player_stop(app_hls_player_t *handle);

// Récupérer statistiques
esp_err_t app_hls_player_get_stats(app_hls_player_t *handle, app_hls_player_stats_t *stats);

// Détruire player (libère toutes ressources)
esp_err_t app_hls_player_del(app_hls_player_t *handle);
```

### Lifecycle

```
new() → start() → [RUNNING] → stop() → start() → ... → stop() → del()
  ↓       ↓                      ↓                         ↓       ↓
Alloc   Tâches              Arrêt tâches              Arrêt    Free
ressources créées            Buffer vidé               si actif  tout
```

## Exemple d'utilisation

### Cas typique avec MAX98357A

```c
#include "app_hls_player/app_hls_player.h"
#include "drv_max98357a/drv_max98357a.h"

// 1. Callback d'écriture audio
static esp_err_t audio_write_callback(void *user_ctx, const void *data,
                                       size_t size, size_t *bytes_written,
                                       uint32_t timeout_ms)
{
    drv_max98357a_t *driver = (drv_max98357a_t *)user_ctx;
    return drv_max98357a_write(driver, data, size, bytes_written, timeout_ms);
}

void app_main(void)
{
    // 2. Initialiser driver audio
    drv_max98357a_t *driver;
    drv_max98357a_config_t drv_cfg = {
        .bclk_io = GPIO_NUM_7,
        .lrc_io = GPIO_NUM_8,
        .din_io = GPIO_NUM_9,
        .sample_rate = 48000,
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
    };
    drv_max98357a_new(&drv_cfg, &driver);
    drv_max98357a_enable(driver);

    // 3. Configurer HLS player
    app_hls_player_config_t player_cfg;
    app_hls_player_config_init(&player_cfg);
    player_cfg.stream_url = "https://example.com/stream.m3u8";
    player_cfg.buffer_size = 100 * 1024;  // 100 KB
    player_cfg.write_cb = audio_write_callback;
    player_cfg.write_ctx = driver;  // Passer driver comme contexte

    // 4. Créer et démarrer player
    app_hls_player_t *player;
    app_hls_player_new(&player_cfg, &player);
    app_hls_player_start(player);

    // 5. Monitoring loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));

        app_hls_player_stats_t stats;
        app_hls_player_get_stats(player, &stats);
        ESP_LOGI("main", "Stats: %u KB téléchargés, buffer: %d%%",
                 stats.bytes_downloaded / 1024, stats.buffer_fill_percent);
    }

    // 6. Cleanup (si nécessaire)
    app_hls_player_stop(player);
    app_hls_player_del(player);
    drv_max98357a_del(driver);
}
```

### Cas avancé : Enregistrement vers fichier

```c
// Callback pour enregistrer dans un fichier
static FILE *g_record_file = NULL;

static esp_err_t file_write_callback(void *user_ctx, const void *data,
                                      size_t size, size_t *bytes_written,
                                      uint32_t timeout_ms)
{
    (void)user_ctx;
    (void)timeout_ms;

    if (g_record_file == NULL) {
        return ESP_FAIL;
    }

    size_t written = fwrite(data, 1, size, g_record_file);
    if (bytes_written) {
        *bytes_written = written;
    }

    return (written == size) ? ESP_OK : ESP_FAIL;
}

void app_main(void)
{
    // Ouvrir fichier d'enregistrement
    g_record_file = fopen("/sdcard/recording.raw", "wb");

    app_hls_player_config_t cfg;
    app_hls_player_config_init(&cfg);
    cfg.stream_url = "https://example.com/stream.m3u8";
    cfg.write_cb = file_write_callback;
    cfg.write_ctx = NULL;

    app_hls_player_t *player;
    app_hls_player_new(&cfg, &player);
    app_hls_player_start(player);

    // Enregistrer pendant 60 secondes
    vTaskDelay(pdMS_TO_TICKS(60000));

    app_hls_player_stop(player);
    app_hls_player_del(player);

    fclose(g_record_file);
    // Fichier .raw = PCM 16-bit stéréo 48 kHz (peut être converti en WAV)
}
```

## Fonctionnalités

### Supporté
- ✅ Playlists M3U8 (master et media)
- ✅ Codec AAC (ADTS)
- ✅ Container MPEG-TS
- ✅ Playlists VOD et Live
- ✅ HTTPS avec validation certificats (esp_crt_bundle)
- ✅ Sélection automatique qualité (midfi préféré)
- ✅ Resynchronisation automatique
- ✅ Réduction volume logicielle (25% par défaut)
- ✅ Statistiques temps réel

### Limitations
- ❌ Vidéo non supportée (audio uniquement)
- ❌ Codec MP3 non supporté (AAC uniquement)
- ❌ Chiffrement AES-128 non supporté
- ❌ Master playlists: première variante valide uniquement
- ❌ Pas de support ABR (Adaptive Bitrate)
- ❌ Segments limités à 32 par playlist (limite lib_m3u8_parser)

## Configuration

### Tailles de buffer recommandées

| Scénario | buffer_size | Latence | Robustesse |
|----------|-------------|---------|------------|
| WiFi stable | 50 KB | ~3s | Moyenne |
| WiFi normal | **100 KB** | ~6s | **Recommandé** |
| WiFi instable | 150 KB | ~9s | Haute |
| Réseau mobile | 200 KB | ~12s | Très haute |

### Tailles de stack des tâches

- **hls_fetch_task** : 14336 bytes (14 KB)
  - Utilisé pour HTTP client + parsing M3U8
  - Ne PAS réduire sous risque de stack overflow

- **audio_play_task** : 6144 bytes (6 KB)
  - Utilisé pour décodage AAC/TS
  - Peut être réduit à 5 KB si mémoire critique

### Mémoire totale requise

- Handle : ~60 bytes
- Ring buffer : `buffer_size` (100 KB par défaut)
- Stacks tâches : 14 KB + 6 KB = 20 KB
- Buffers décodage : 144 KB + 16 KB = 160 KB
- **Total : ~280 KB** (avec buffer 100 KB)

Note : Les buffers de décodage sont alloués dynamiquement au démarrage.

## Resynchronisation

Le player gère automatiquement les erreurs de décodage :

1. **Recherche sync word AAC** : `0xFFF` (12 bits)
2. **Recherche sync word TS** : `0x47` (2 paquets consécutifs)
3. **Purge agressive** : 2 KB si aucun sync trouvé

Stratégie équilibrée entre :
- Récupération rapide après erreur
- Minimisation pertes audio
- Évitement watchdog timer

## Dépendances

### Composants requis
- `iobewi_libs_m3u8_parser` : Parsing playlists M3U8
- `espressif/esp_audio_codec` : Décodage AAC/TS (version ^1.0.0)
- `esp_http_client` : Téléchargement HTTP/HTTPS
- `esp_https_ota` : Bundle certificats CA

### ESP-IDF
- Version minimale : 5.0
- Composants IDF : `esp_common`, `freertos`, `esp_http_client`

### Enregistrement décodeurs

**Important** : Les décodeurs doivent être enregistrés AVANT de créer le player :

```c
#include "decoder/impl/esp_aac_dec.h"
#include "simple_dec/impl/esp_ts_dec.h"

void app_main(void) {
    // CRITIQUE : Enregistrer décodeurs en premier
    esp_aac_dec_register();
    esp_ts_dec_register();

    // Ensuite créer le player
    app_hls_player_t *player;
    // ...
}
```

## Notes techniques

### Gestion master playlists

Lors de la détection d'une master playlist, le player :
1. Parse la master playlist
2. Sélectionne une variante selon priorité :
   - **midfi** (préféré) : ~128 kbps, équilibre optimal
   - **hifi** (fallback) : ~192-320 kbps, qualité maximale
   - **lofi** (dernier recours) : ~64 kbps, bande passante minimale
3. Télécharge et parse la media playlist correspondante
4. Démarre le streaming

### Contrôle du téléchargement

Le système utilise un **sémaphore binaire** pour synchroniser fetch et play :
- `audio_play_task` surveille le niveau du buffer
- Si buffer < 40% : signal via `xSemaphoreGive()`
- `hls_fetch_task` télécharge 2 segments d'avance
- Hysteresis : réinitialisation du signal si buffer > 60%

Avantages :
- Évite polling CPU-intensif
- Télécharge uniquement quand nécessaire
- Économise bande passante
- Prévient buffer underrun

### Thread-safety
- ❌ Les fonctions ne sont pas thread-safe
- ✅ Les tâches internes sont synchronisées (sémaphore + ring buffer thread-safe)
- L'utilisateur doit gérer la synchronisation pour les appels API externes

## Performance

### Temps de démarrage typique
1. Téléchargement master playlist : ~200-500 ms
2. Téléchargement media playlist : ~200-500 ms
3. Téléchargement premier segment : ~500-2000 ms
4. Buffering initial : ~500 ms
5. **Total first audio : ~1.5-3.5 secondes**

### Utilisation CPU (ESP32-S3 @ 240 MHz)
- fetch_task : 5-10% (pics à 20% pendant téléchargement)
- play_task : 15-25% (décodage AAC)
- **Total : ~20-35%**

### Latence
- Buffer 100 KB ≈ 6 secondes d'audio à 128 kbps
- Live streaming : délai ~6-12s par rapport au direct

## Troubleshooting

### Erreur "Échec de création du décodeur TS"
→ Vérifier que `esp_aac_dec_register()` et `esp_ts_dec_register()` sont appelés

### Erreur "Ring buffer plein, données perdues"
→ Augmenter `buffer_size` ou vérifier que le callback audio ne bloque pas

### Coupures audio fréquentes
→ Augmenter `buffer_size` ou vérifier la stabilité WiFi

### Stack overflow dans hls_fetch_task
→ NE PAS réduire la taille de stack (14 KB minimum requis)

### Audio distordu
→ Vérifier la configuration I2S du driver (sample_rate, bits_per_sample)

## Tests

Tests unitaires disponibles dans `test/test_app_hls_player.c` :
- ✅ Validation arguments NULL (toutes fonctions)
- ✅ Initialisation configuration par défaut
- ✅ Lifecycle complet (new → del)
- ✅ Idempotence stop()
- ✅ Statistiques initiales

Tests d'intégration (manuels) :
- Streaming radio France Inter (MIDFI)
- Streaming 30+ minutes
- Stabilité mémoire (heap minimum)

## Exemples

- `examples/iobewi_driver_max98357a/hls_stream_app` : Exemple complet de streaming HLS avec MAX98357A

## Voir aussi

- Composant `iobewi_libs_m3u8_parser` : Parser M3U8 utilisé en interne
- Composant `iobewi_driver_max98357a` : Driver I2S pour amplificateur MAX98357A
- [RFC 8216 - HTTP Live Streaming](https://datatracker.ietf.org/doc/html/rfc8216)
- [esp_audio_codec documentation](https://components.espressif.com/components/espressif/esp_audio_codec)
