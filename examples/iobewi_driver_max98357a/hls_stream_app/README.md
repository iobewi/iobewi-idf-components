# HLS Stream Player avec MAX98357A

Exemple de streaming HLS/M3U8 utilisant `app_hls_player` et `drv_max98357a` pour diffuser de l'audio internet sur ESP32.

## Vue d'ensemble

Cet exemple démontre comment utiliser le composant `app_hls_player` avec le driver `drv_max98357a` pour créer un lecteur de radio internet. Le code de `main.c` est intentionnellement simple (~150 lignes) pour illustrer l'intégration des composants.

**Architecture:**
```
[WiFi] → [app_hls_player] → [Callback] → [drv_max98357a] → [MAX98357A] → Haut-parleur
           ↓ utilise
      [lib_m3u8_parser]
```

## Fonctionnalités

- ✅ Streaming HLS/M3U8 depuis internet (playlists VOD et Live)
- ✅ Décodage AAC/MPEG-TS automatique
- ✅ Buffering intelligent (100 KB par défaut)
- ✅ Reconnexion WiFi automatique
- ✅ Monitoring temps réel (heap, buffer, téléchargement)
- ✅ Abstraction audio via callback (facile à adapter)

## Limitations

- **Codec** : AAC uniquement (pas MP3, FLAC)
- **Chiffrement** : Pas de support AES-128
- **ABR** : Sélection qualité automatique mais pas adaptatif
- **Contrôle** : Pas de pause/resume (peut être ajouté facilement)

## Prérequis

### Hardware

- **ESP32-S3** (fortement recommandé)
  - Minimum 4 MB Flash
  - **2 MB PSRAM recommandé** (pour buffers et décodage)
- **MAX98357A** amplificateur I2S
- **Haut-parleur** 4-8Ω, 3W

### Software

- ESP-IDF v5.0 ou supérieur (testé avec v6.1)
- Composants (installés automatiquement via idf_component.yml):
  - `iobewi_driver_max98357a`
  - `iobewi_apps_hls_player`
  - `iobewi_libs_m3u8_parser`
  - `espressif/esp_audio_codec` v1.0.0+

## Câblage

| MAX98357A | ESP32-S3 | Description |
|-----------|----------|-------------|
| BCLK      | GPIO 7   | Bit Clock (configurable) |
| LRCLK     | GPIO 8   | Word Select |
| DIN       | GPIO 9   | Data Input |
| SD        | (opt)    | Shutdown (-1 = désactivé) |
| GND       | GND      | Ground |
| VIN       | 3.3V/5V  | Power |

**Note:** Les GPIO sont configurables via `idf.py menuconfig` → `HLS Stream Player Configuration`

## Configuration

```bash
idf.py menuconfig
```

### HLS Stream Player Configuration

#### WiFi Configuration
- **WiFi SSID** : Nom de votre réseau
- **WiFi Password** : Mot de passe
- **Maximum retry** : Tentatives de reconnexion (défaut: 5)

#### Stream Configuration
- **HLS/M3U8 Stream URL** : URL du stream
  - Exemple: `https://icecast.radiofrance.fr/franceinter-midfi.m3u8`

#### I2S Configuration
- **I2S BCLK GPIO** : GPIO pour bit clock (défaut: 7)
- **I2S WS GPIO** : GPIO pour word select (défaut: 8)
- **I2S DOUT GPIO** : GPIO pour données (défaut: 9)
- **SD_MODE GPIO** : GPIO shutdown (-1 = désactivé)
- **Audio Sample Rate** : Fréquence (défaut: 48000 Hz)

## Compilation et Flash

```bash
# Définir la cible
idf.py set-target esp32s3

# Configurer
idf.py menuconfig

# Compiler
idf.py build

# Flasher et monitorer
idf.py -p /dev/ttyUSB0 flash monitor
```

## URLs de Test

### Radios publiques françaises (AAC HLS)

```
# France Inter MIDFI (recommandé)
https://icecast.radiofrance.fr/franceinter-midfi.m3u8

# France Inter HIFI (haute qualité)
https://icecast.radiofrance.fr/franceinter-hifi.m3u8

# France Inter LOFI (basse bande passante)
https://icecast.radiofrance.fr/franceinter-lofi.m3u8
```

**Vérifications avant utilisation:**
- Format : HLS/M3U8
- Codec : AAC (ADTS) ou MPEG-TS+AAC
- Chiffrement : Non (pas d'AES-128)

### Comment trouver d'autres streams

1. Ouvrir les outils développeur du navigateur (F12)
2. Onglet "Network" pendant la lecture
3. Filtrer par `.m3u8`
4. Copier l'URL de la playlist
5. Vérifier que les segments sont `.aac` ou `.ts`

## Architecture de l'exemple

### Composants utilisés

```
main.c (~150 lignes)
  ↓ utilise
┌─────────────────────────────────┐
│ app_hls_player                  │ ← Orchestration HLS
│  - Téléchargement M3U8          │
│  - Téléchargement segments      │
│  - Décodage AAC → PCM           │
│  - Buffering intelligent        │
└────────────┬────────────────────┘
             │ callback: audio_write_callback()
             ↓
┌─────────────────────────────────┐
│ drv_max98357a                   │ ← Driver I2S
│  - Configuration I2S            │
│  - Écriture DMA                 │
│  - Contrôle amplificateur       │
└─────────────────────────────────┘
             ↓
         MAX98357A
```

### Tâches FreeRTOS (créées par app_hls_player)

1. **hls_fetch_task** (prio 5, 14 KB stack)
   - Télécharge et parse playlists M3U8
   - Télécharge segments audio
   - Écrit dans ring buffer interne

2. **audio_play_task** (prio 8, 6 KB stack)
   - Décode AAC/TS vers PCM
   - Appelle callback audio
   - Resynchronisation automatique

3. **monitor (main.c)** : Affiche statistiques toutes les 10s

## Code principal

Le code de `main.c` est volontairement simple pour illustrer l'usage des composants :

```c
// 1. Callback d'écriture audio
static esp_err_t audio_write_callback(void *user_ctx, const void *data,
                                       size_t size, size_t *bytes_written,
                                       uint32_t timeout_ms)
{
    drv_max98357a_t *driver = (drv_max98357a_t *)user_ctx;
    return drv_max98357a_write(driver, data, size, bytes_written, timeout_ms);
}

void app_main(void) {
    // 2. Init NVS + WiFi
    // ...

    // 3. Enregistrer décodeurs (IMPORTANT)
    esp_aac_dec_register();
    esp_ts_dec_register();

    // 4. Init driver audio
    drv_max98357a_t *driver;
    drv_max98357a_new(&driver_config, &driver);
    drv_max98357a_enable(driver);

    // 5. Créer et démarrer HLS player
    app_hls_player_config_t cfg;
    app_hls_player_config_init(&cfg);
    cfg.stream_url = HLS_STREAM_URL;
    cfg.buffer_size = 100 * 1024;      // 100 KB
    cfg.write_cb = audio_write_callback;
    cfg.write_ctx = driver;            // Passer driver au callback

    app_hls_player_t *player;
    app_hls_player_new(&cfg, &player);
    app_hls_player_start(player);

    // 6. Monitoring loop
    while (1) {
        app_hls_player_get_stats(player, &stats);
        // ...
    }
}
```

**Points clés:**
- Le callback `audio_write_callback()` fait le pont entre `app_hls_player` et `drv_max98357a`
- Cette abstraction permet de changer facilement le driver audio (PCM5102, I2S générique, fichier, etc.)
- Le player gère automatiquement buffering, décodage, et resynchronisation

## Logs

### Démarrage normal

```
I (1234) hls_stream_app: WiFi connecté, IP: 192.168.1.42
I (1240) app_hls_player: Ring buffer créé: 102400 bytes
I (1245) app_hls_player: Démarrage de la task de téléchargement HLS
I (1250) app_hls_player: Démarrage de la task de lecture audio
I (2100) lib_m3u8_parser: Playlist parsée: 10 segments, live=1
I (2500) app_hls_player: Téléchargement segment 1332057
I (3200) app_hls_player: Décodeur TS créé avec succès
I (5000) hls_stream_app: Stream: 512 KB téléchargés, buffer: 75%
```

### Monitoring

```
I (15000) hls_stream_app: === État ===
I (15000) hls_stream_app: Stream: 1234 KB téléchargés, buffer: 68%
I (15000) hls_stream_app: Heap: 45672 bytes libre (min: 38924 bytes)
```

## Troubleshooting

| Problème | Solution |
|----------|----------|
| `Échec de connexion WiFi` | Vérifier SSID/password dans menuconfig |
| `Échec de téléchargement M3U8` | Vérifier l'URL, tester dans un navigateur |
| `Decoder not registered` | Appeler `esp_aac_dec_register()` **AVANT** `app_hls_player_new()` |
| `Échec de création du décodeur` | Vérifier que `esp_audio_codec` est dans idf_component.yml |
| `Buffer vide, attente...` | Réseau lent → Augmenter buffer_size |
| `Ring buffer plein` | Augmenter buffer_size ou vérifier CPU |
| `Memory allocation failed` | ESP32-S3 avec PSRAM fortement recommandé |
| `Erreur d'écriture audio` | Vérifier configuration I2S (sample_rate, GPIO) |

## Mémoire

### Consommation typique (avec buffer 100 KB)

- Ring buffer : 100 KB
- Buffers décodage : 160 KB (144 KB + 16 KB)
- Stacks tâches : 20 KB
- Handle + divers : ~10 KB
- **Total : ~290 KB**

**Heap libre minimum recommandé : 40 KB** (après démarrage)

### Avec ESP32-S3 (8 MB PSRAM)
- Heap libre : ~150-200 KB
- Minimum observé : ~100 KB
- **Confortable** ✅

### Avec ESP32-S3 (sans PSRAM)
- Heap libre : ~50-80 KB
- Minimum observé : ~35 KB
- **Limite** ⚠️

## Optimisations

### Pour économiser la mémoire
```c
// Réduire buffer à 50 KB (au lieu de 100 KB)
cfg.buffer_size = 50 * 1024;
```

### Pour améliorer la stabilité réseau
```c
// Augmenter buffer à 150 KB (requiert PSRAM)
cfg.buffer_size = 150 * 1024;
```

### Pour changer de driver audio

Exemple avec PCM5102 (ou tout autre driver I2S) :

```c
// 1. Modifier le callback
static esp_err_t audio_write_callback(void *user_ctx, const void *data,
                                       size_t size, size_t *bytes_written,
                                       uint32_t timeout_ms)
{
    // Remplacer drv_max98357a par votre driver
    my_i2s_driver_t *driver = (my_i2s_driver_t *)user_ctx;
    return my_i2s_write(driver, data, size, bytes_written, timeout_ms);
}

// 2. Passer votre driver au contexte
cfg.write_ctx = my_driver;
```

### Pour enregistrer vers fichier

```c
// Callback vers fichier
static FILE *g_record_file = NULL;

static esp_err_t file_write_callback(void *user_ctx, const void *data,
                                      size_t size, size_t *bytes_written,
                                      uint32_t timeout_ms)
{
    size_t written = fwrite(data, 1, size, g_record_file);
    *bytes_written = written;
    return (written == size) ? ESP_OK : ESP_FAIL;
}

void app_main(void) {
    g_record_file = fopen("/sdcard/recording.raw", "wb");

    cfg.write_cb = file_write_callback;
    cfg.write_ctx = NULL;
    // ...
}
```

## Extensions possibles

- ✨ Interface boutons (play/pause, changement station)
- ✨ Liste de stations prédéfinies
- ✨ Écran OLED (affichage station, buffer, etc.)
- ✨ Contrôle volume via GPIO
- ✨ WebServer pour configuration WiFi
- ✨ Enregistrement programmé sur carte SD
- ✨ Equalizer DSP

## Composants connexes

- **app_hls_player** : `/components/iobewi_apps_hls_player/README.md`
  - Documentation complète de l'API
  - Architecture détaillée (2 tâches, ring buffer, resynchronisation)
  - Exemples d'utilisation avancés

- **drv_max98357a** : `/components/iobewi_driver_max98357a/README.md`
  - Configuration I2S
  - Contrôle gain/shutdown
  - Exemples de test

- **lib_m3u8_parser** : `/components/iobewi_libs_m3u8_parser/README.md`
  - Parser M3U8/HLS réutilisable
  - Support master playlists
  - Résolution URLs relatives

## Références

- [MAX98357A Datasheet](https://datasheets.maximintegrated.com/en/ds/MAX98357A-MAX98357B.pdf)
- [ESP-IDF I2S Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2s.html)
- [HLS Specification (RFC 8216)](https://datatracker.ietf.org/doc/html/rfc8216)
- [esp_audio_codec Component](https://components.espressif.com/components/espressif/esp_audio_codec)

## Support

Pour les bugs et questions :
- Issues : https://github.com/iobewi/iobewi-idf-components/issues
