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

### Organisation modulaire

Le composant est organisé en modules séparés pour améliorer la maintenabilité :

```
app_hls_player/
├── include/app_hls_player/
│   ├── app_hls_player.h              # API publique
│   ├── app_hls_player_types.h        # Types publics
│   ├── app_hls_player_internal.h     # Structures internes + helpers
│   ├── app_hls_player_fetcher.h      # Module téléchargement
│   ├── app_hls_player_audio.h        # Module décodage audio
│   ├── app_hls_player_http.h         # Module helpers HTTP
│   └── app_hls_player_ts_sync.h      # Module resynchronisation TS
├── src/
│   ├── app_hls_player.c              # Orchestration (new/start/stop/del)
│   ├── app_hls_player_fetcher.c      # Task téléchargement
│   ├── app_hls_player_audio.c        # Task décodage (gather buffer)
│   ├── app_hls_player_http.c         # Helpers HTTP
│   └── app_hls_player_ts_sync.c      # Resynchronisation TS smart
└── test/
    ├── test_app_hls_player.c         # Tests unitaires API publique
    └── test_ts_sync.c                # Tests unitaires resync TS
```

### Vue d'ensemble

```
┌─────────────────────────────────────────────────────────────┐
│                      app_hls_player                         │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────┐         Ring Buffer          ┌─────────┐ │
│  │ hls_fetch    │──────► [Données encodées] ──►│  audio  │ │
│  │ task         │        (188 bytes/item)       │  play   │ │
│  │ (fetcher.c)  │         (256 KB total)        │  task   │ │
│  │              │                               │ (audio.c)│ │
│  │ - Download   │         Sémaphore             │         │ │
│  │   M3U8       │◄──────────────────────────────│ Gather  │ │
│  │ - Parse      │   (Signal si buffer < 40%)    │ Buffer  │ │
│  │ - Download   │                               │  (8 KB) │ │
│  │   segments   │         Notifications         │         │ │
│  │ (http.c)     │────► RESET/RESYNC/STOP ──────►│ - Déco- │ │
│  │              │                               │   dage  │ │
│  │              │                               │ - Resync│ │
│  │              │                               │ (ts_sync)│
│  │              │                               │ - write_│ │
│  │              │                               │   cb()  │ │
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

1. **hls_fetch_task** (priorité 4, stack 14 KB, Core 0)
   - Module : `app_hls_player_fetcher.c`
   - Télécharge la playlist M3U8 (via `app_hls_player_http.c`)
   - Parse avec `lib_m3u8_parser`
   - Gère master playlists (sélection qualité: midfi > hifi > lofi)
   - Télécharge les segments en boucle (items de 188 bytes)
   - Évite les doublons via numéro de séquence
   - Rafraîchit périodiquement (target_duration * 0.8)
   - Détecte DISCONTINUITY → envoie `NOTIF_RESET`
   - Attend signal du sémaphore (contrôle par niveau buffer)

2. **hls_audio_play_task** (priorité 5, stack 6 KB, Core 1)
   - Module : `app_hls_player_audio.c`
   - **Gather buffer** : assemble N paquets TS (188 bytes) → 4-8 KB contiguë
   - **Leftover** : bytes non consommés persistants entre cycles
   - Décode MPEG-TS + AAC vers PCM 16-bit stéréo
   - Applique réduction volume logicielle (25%)
   - Écrit via `write_cb()` fourni par l'utilisateur
   - **Resynchronisation smart** (module `app_hls_player_ts_sync.c`) :
     - Recherche points de resync valides (PID audio, PUSI, PES)
     - Triple validation sync bytes TS (0x47 espacés 188 bytes)
     - Stall mode : resync sur leftover uniquement si consumed==0 répété
   - Gère notifications : `NOTIF_STOP`, `NOTIF_RESET`, `NOTIF_RESYNC`
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

## Détails techniques

### Gather Buffer + Leftover

L'architecture audio utilise un **gather buffer** pour améliorer la fiabilité du décodage :

**Problème résolu** : Les items du ring buffer (188 bytes = 1 paquet TS) sont trop petits pour certains décodeurs AAC, causant des `consumed=0` en boucle et des erreurs de décodage massives.

**Solution** :
1. **Gather buffer** (4-8 KB configurable) : assemble N paquets TS en zone contiguë
2. **Leftover** : bytes non consommés persistants entre cycles de décodage
3. **Copy-then-return** : items copiés puis retournés immédiatement au ring buffer
4. **Resync smart** : appliqué effectivement sur le leftover

**Avantages** :
- ✅ Décodeur reçoit 4-8 KB contiguë (vs 188 bytes)
- ✅ Continuité stream (leftover entre cycles)
- ✅ Libération sélective (seulement items consommés)
- ✅ Resync efficace (skip appliqué + reset décodeur)
- ✅ Pas de fuite ring buffer (items retournés immédiatement)

**Configuration** : `CONFIG_APP_HLS_PLAYER_GATHER_BUFFER_SIZE` (4-16 KB, défaut 8 KB)

### Resynchronisation TS Smart

Module `app_hls_player_ts_sync.c` fournit une resynchronisation intelligente :

**Stratégie multi-niveaux** :
1. **Validation sync bytes** : triple check 0x47 espacés 188 bytes (élimine faux positifs)
2. **PID + PUSI** : recherche points de resync valides (début de PES audio)
3. **Stall mode** : si `consumed==0` répété, resync sur leftover uniquement (pas de recopie)
4. **Drop aggressif** : 30-50 items max sur NOTIF_RESET (vs tout vider)

**Gardes défensives** :
- Leftover overflow (`> GATHER_BUF_SIZE`) → flush
- Rate-limiting notifications (`NOTIF_RESYNC` % 10)
- Validation alignement TS avant décodage

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
- ❌ Codec MP3 non supporté (AAC uniquement, MPEG-TS container)
- ❌ Chiffrement AES-128 non supporté
- ❌ Master playlists: sélection variant par nom (midfi/hifi/lofi) ou bandwidth
- ❌ Pas de support ABR (Adaptive Bitrate) dynamique
- ❌ Segments limités à 32 par playlist (limite lib_m3u8_parser)
- ⚠️ Ring buffer : items doivent être 188 bytes (NOSPLIT garanti par téléchargement)
- ⚠️ Gather buffer : latence additionnelle ~20-50 ms (négligeable)
- ⚠️ Resync smart : peut skip jusqu'à 8 KB si corruption massive (< 1s audio)

## Configuration

### Tailles de buffer recommandées

**Ring Buffer** (`CONFIG_APP_HLS_PLAYER_RING_BUFFER_SIZE`) :

| Scénario | buffer_size | Latence | Robustesse |
|----------|-------------|---------|------------|
| WiFi stable | 128 KB | ~6s | Moyenne |
| WiFi normal | **256 KB** | ~12s | **Recommandé** |
| WiFi instable | 512 KB | ~24s | Haute |
| Réseau mobile | 1024 KB | ~48s | Très haute |

**Gather Buffer** (`CONFIG_APP_HLS_PLAYER_GATHER_BUFFER_SIZE`) :

| Scénario | gather_size | RAM | Fiabilité décodage |
|----------|-------------|-----|-------------------|
| Minimum | 4 KB | Économie | Acceptable |
| **Standard** | **8 KB** | **Standard** | **Recommandé** |
| Robuste | 12 KB | +4 KB | Maximale |
| Maximum | 16 KB | +8 KB | Overkill |

Note : Gather buffer < 4 KB non recommandé (risque `consumed=0` en boucle)

### Tailles de stack des tâches

- **hls_fetch_task** : 14336 bytes (14 KB)
  - Utilisé pour HTTP client + parsing M3U8
  - Ne PAS réduire sous risque de stack overflow

- **audio_play_task** : 6144 bytes (6 KB)
  - Utilisé pour décodage AAC/TS
  - Peut être réduit à 5 KB si mémoire critique

### Mémoire totale requise

- Handle : ~256 bytes (structure complète)
- Ring buffer : `buffer_size` (256 KB recommandé, configurable)
- Gather buffer : `CONFIG_APP_HLS_PLAYER_GATHER_BUFFER_SIZE` (8 KB par défaut)
- TS carry buffer : 188 bytes (alignement paquets TS)
- Stacks tâches : 14 KB + 6 KB = 20 KB
- Buffers décodage : 16 KB (CONFIG_APP_HLS_PLAYER_DEC_BUFFER_SIZE)
- **Total : ~300 KB** (avec ring buffer 256 KB, gather 8 KB)

**Configuration Kconfig** :
```
CONFIG_APP_HLS_PLAYER_RING_BUFFER_SIZE=256     # KB, défaut 128
CONFIG_APP_HLS_PLAYER_GATHER_BUFFER_SIZE=8     # KB, défaut 8
CONFIG_APP_HLS_PLAYER_DEC_BUFFER_SIZE=16       # KB, défaut 16
```

Note : Tous les buffers sont alloués dynamiquement au démarrage.

## Resynchronisation

Le player gère automatiquement les erreurs de décodage via le module `app_hls_player_ts_sync.c` :

### Fonction `hls_ts_find_next_sync()`

Recherche robuste du prochain sync byte TS :
1. **Sync byte** : `0x47` (début paquet TS)
2. **Triple validation** : si ≥ 564 bytes disponibles, vérifie 3 sync bytes espacés 188 bytes
3. **Élimination faux positifs** : 0x47 peut apparaître dans payload AAC

### Fonction `hls_ts_resync_smart()`

Resynchronisation intelligente multi-critères :
1. **Audio PID** : extrait PID depuis paquet TS
2. **PUSI flag** : vérifie Payload Unit Start Indicator (début PES)
3. **PES header** : valide 0x000001 (start code PES)
4. **Points valides** : accepte uniquement PID audio + PUSI=1 + PES valide

### Stratégie notifications

- **NOTIF_RESET** : DISCONTINUITY détecté → flush leftover + reset décodeur
- **NOTIF_RESYNC** : drop-old ring buffer → resync soft (pas de reset décodeur)
- **Rate-limiting** : `NOTIF_RESYNC` envoyé tous les 10 événements uniquement

### Équilibre

Stratégie optimisée pour :
- ✅ Récupération rapide après erreur
- ✅ Minimisation pertes audio (skip sélectif vs flush total)
- ✅ Évitement watchdog timer (pas de boucles infinies)
- ✅ Réduction faux positifs (validation multi-critères)

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

Le système utilise **sémaphores et notifications** pour synchroniser fetch et play :

**Sémaphore download** :
- `audio_play_task` surveille le niveau du buffer
- Si buffer < 40% : signal via `xSemaphoreGive(download_semaphore)`
- `hls_fetch_task` télécharge segments en boucle
- Hysteresis : réinitialisation du signal si buffer > 60%

**Notifications task** (via `xTaskNotify`) :
- **NOTIF_STOP** : arrêt immédiat demandé (stop interruptible)
- **NOTIF_RESET** : DISCONTINUITY détecté → flush leftover + reset décodeur
- **NOTIF_RESYNC** : drop-old ring buffer → resync soft (rate-limited % 10)

Avantages :
- ✅ Évite polling CPU-intensif
- ✅ Télécharge uniquement quand nécessaire
- ✅ Économise bande passante
- ✅ Prévient buffer underrun
- ✅ Arrêt rapide (< 100 ms via notifications)
- ✅ Thread-safety stricte SMP (pas de volatile flags)

### Thread-safety

**API publique** :
- ❌ Les fonctions ne sont PAS thread-safe (appeler depuis un seul thread)
- L'utilisateur doit gérer la synchronisation pour appels externes

**Synchronisation interne** :
- ✅ Ring buffer : thread-safe (FreeRTOS ringbuf)
- ✅ Statistiques : protégées par mutex (`stats_mutex`)
- ✅ Sémaphores : `download_semaphore`, `fetch_done`, `play_done`
- ✅ Notifications task : **remplacent volatile flags** (thread-safety stricte SMP)
  - `NOTIF_STOP`, `NOTIF_RESET`, `NOTIF_RESYNC` via `xTaskNotify()`
  - Évite data races sur ESP32-S3 dual-core

**Pinning CPU** :
- `hls_fetch_task` : Core 0 (téléchargement + parsing)
- `hls_audio_play_task` : Core 1 (décodage, réduit jitter audio)

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
→ Vérifier que `esp_aac_dec_register()` et `esp_ts_dec_register()` sont appelés AVANT `app_hls_player_new()`

### Erreur "Ring buffer plein, données perdues"
→ Augmenter `CONFIG_APP_HLS_PLAYER_RING_BUFFER_SIZE` ou vérifier que le callback audio ne bloque pas

### Coupures audio fréquentes
→ Augmenter `buffer_size` ou vérifier la stabilité WiFi
→ Vérifier logs "consumed=0" répétés : peut indiquer problème décodeur

### Erreurs AAC élevées (> 5/minute)
→ Vérifier qualité réseau (paquets corrompus)
→ Augmenter `CONFIG_APP_HLS_PLAYER_GATHER_BUFFER_SIZE` (8 → 12 KB)
→ Vérifier logs resync : fréquence excessive peut indiquer stream corrompu

### Stack overflow dans hls_fetch_task
→ NE PAS réduire la taille de stack (14 KB minimum requis pour HTTP + parsing M3U8)

### Audio distordu
→ Vérifier la configuration I2S du driver (sample_rate=48000, bits_per_sample=16)
→ Vérifier que `write_cb()` ne modifie pas les données PCM

### "Leftover overflow" dans les logs
→ Problème rare : leftover dépasse gather buffer (corruption stream)
→ Le player flush automatiquement (perte < 1 seconde audio)
→ Si fréquent : vérifier qualité réseau ou augmenter GATHER_BUFFER_SIZE

### Latence audio excessive (> 10 secondes)
→ Réduire `RING_BUFFER_SIZE` (256 → 128 KB)
→ Note : risque underrun si WiFi instable

## Tests

### Tests unitaires

**`test/test_app_hls_player.c`** : API publique
- ✅ Validation arguments NULL (toutes fonctions)
- ✅ Initialisation configuration par défaut
- ✅ Lifecycle complet (new → del)
- ✅ Idempotence stop()
- ✅ Statistiques initiales

**`test/test_ts_sync.c`** : Module resynchronisation TS
- ✅ `hls_ts_find_next_sync()` : détection sync byte
- ✅ `hls_ts_resync_smart()` : validation PID + PUSI + PES
- ✅ Robustesse faux positifs (0x47 dans payload)
- ✅ Triple validation sync bytes espacés 188 bytes

### Tests d'intégration (manuels)

- ✅ Streaming radio France Inter (MIDFI, AAC 128 kbps)
- ✅ Streaming 30+ minutes sans coupure
- ✅ Stabilité mémoire (heap minimum ~1.9 MB)
- ✅ Gather buffer : < 1% activations stitch
- ✅ Erreurs AAC : < 1/minute (vs 10+/min sans gather buffer)
- ✅ Frames PCM : 4500+/minute
- ✅ Resync smart : activations ≥ 15% (points valides uniquement)

## Exemples

- `examples/iobewi_driver_max98357a/hls_stream_app` : Exemple complet de streaming HLS avec MAX98357A

## Documentation technique

Documentations détaillées disponibles dans `docs/` :

- **`TODO_RAM_OPTIMIZATION.md`** : Historique complet des optimisations RAM
  - Architecture gather buffer + leftover
  - Stratégie resync smart (PID + PUSI + PES)
  - Bugs corrigés (double memmove, fuite ringbuffer, etc.)
  - Tests et métriques de performance

- **`P0.1_STACK_REDUCTION.md`** : Réduction stacks des tâches (TODO)
  - Analyse utilisation stack actuelle
  - Stratégies de réduction sécurisées

- **`SYNTHESE_ERREUR_AAC.md`** : Diagnostic erreurs AAC
  - Root cause analysis (items 188 bytes trop petits)
  - Solutions implémentées (gather buffer)
  - Métriques avant/après

## Voir aussi

- Composant `iobewi_libs_m3u8_parser` : Parser M3U8 utilisé en interne
- Composant `iobewi_driver_max98357a` : Driver I2S pour amplificateur MAX98357A
- [RFC 8216 - HTTP Live Streaming](https://datatracker.ietf.org/doc/html/rfc8216)
- [esp_audio_codec documentation](https://components.espressif.com/components/espressif/esp_audio_codec)
