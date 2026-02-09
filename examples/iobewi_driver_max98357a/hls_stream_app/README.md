# HLS Stream Player avec MAX98357A

Player de stream internet HLS/M3U8 utilisant le driver MAX98357A pour la lecture audio sur ESP32.

## Fonctionnalités

- ✅ Lecture de streams HLS/M3U8 depuis internet
- ✅ Décodage audio AAC vers PCM
- ✅ Buffering intelligent (ring buffer configurable)
- ✅ Reconnexion WiFi automatique
- ✅ Monitoring en temps réel (heap, buffer, téléchargement)
- ✅ Support streams live et VOD (Video On Demand)

## Limitations

Cette application est conçue comme un exemple didactique. Les limitations suivantes sont documentées :

- **Codec** : AAC uniquement (pas de MP3, FLAC, etc.)
- **Chiffrement** : Pas de support AES-128 (streams non chiffrés uniquement)
- **Variant streams** : Pas de support ABR (Adaptive Bitrate), une seule qualité
- **Contrôle** : Pas de pause/resume, pas de contrôle du volume
- **Playlists** : Playlists simples uniquement (pas de playlists imbriquées)
- **HTTPS** : Vérification de certificat désactivée par défaut (voir section Sécurité)
- **Mémoire** : ESP32-S3 avec minimum 60KB heap libre recommandé

## Prérequis

### Hardware

- **ESP32-S3** (minimum recommandé)
- **4MB Flash** minimum
- **2MB PSRAM** recommandé pour des buffers plus larges et plus de stabilité
- **MAX98357A** amplificateur I2S
- **Haut-parleur** 4-8Ω, 3W

**Note sur la mémoire** : L'application nécessite environ 60KB de heap libre. ESP32-S3 sans PSRAM peut être limité. Voir la section [Optimisations](#optimisations) pour réduire l'usage mémoire.

### Software

- ESP-IDF v5.0 ou supérieur (testé avec v6.1)
- Composant `espressif/esp_audio_codec` v2.3.0+ (installé automatiquement via idf_component.yml)

## Câblage MAX98357A

| MAX98357A Pin | ESP32 GPIO | Description |
|---------------|------------|-------------|
| BCLK          | GPIO 4     | Bit Clock (configurable) |
| LRCLK (WS)    | GPIO 5     | Word Select / Left-Right Clock |
| DIN           | GPIO 18    | Data Input |
| SD            | GPIO -1    | Shutdown (optionnel, -1 = toujours actif) |
| GND           | GND        | Ground |
| VIN           | 3.3V/5V    | Power (3.3V-5.5V) |
| GAIN          | GND/3.3V/Float | Gain configuration (voir datasheet) |

**Note** : Les GPIO sont configurables via `idf.py menuconfig` → `HLS Stream Player Configuration` → `I2S Configuration`

## Configuration

### 1. Configuration WiFi

```bash
idf.py menuconfig
```

Naviguez vers `HLS Stream Player Configuration` → `WiFi Configuration` :

- **WiFi SSID** : Nom de votre réseau WiFi
- **WiFi Password** : Mot de passe WiFi
- **Maximum retry** : Nombre de tentatives de reconnexion (défaut: 5)

### 2. Configuration du Stream

Dans `HLS Stream Player Configuration` → `Stream Configuration` :

- **HLS/M3U8 Stream URL** : URL du stream à lire
- **Stream Buffer Size** : Taille du buffer en KB (défaut: 32KB = ~8 secondes @ 32kbps)

### 3. Configuration I2S

Dans `HLS Stream Player Configuration` → `I2S Configuration` :

- **I2S BCLK GPIO** : GPIO pour le bit clock (défaut: 4)
- **I2S WS GPIO** : GPIO pour le word select (défaut: 5)
- **I2S DOUT GPIO** : GPIO pour les données audio (défaut: 18)
- **SD_MODE GPIO** : GPIO pour shutdown control (défaut: -1 = désactivé)
- **Audio Sample Rate** : Fréquence d'échantillonnage (défaut: 44100 Hz)

## Compilation et Flash

```bash
# Configurer la cible (ESP32-S3 recommandé)
idf.py set-target esp32s3

# Configurer le projet
idf.py menuconfig

# Compiler
idf.py build

# Flasher
idf.py -p /dev/ttyUSB0 flash monitor
```

## URLs de Test

Voici quelques streams publics AAC pour tester :

### Radios publiques (AAC)

```
# BBC World Service (HLS AAC)
http://stream.live.vc.bbcmedia.co.uk/bbc_world_service

# France Inter (peut varier)
http://direct.franceinter.fr/live/franceinter-midfi.mp3
```

**Note** : Les URLs des streams peuvent changer. Vérifiez toujours que :
- Le stream est au format HLS/M3U8
- Le codec est AAC
- Le stream n'est pas chiffré (pas d'AES-128)

### Comment trouver des streams compatibles

1. Cherchez des radios en ligne avec support HLS
2. Utilisez les outils de développement de votre navigateur (Network tab)
3. Cherchez des fichiers `.m3u8`
4. Vérifiez que les segments sont en `.aac` ou `.ts` (Transport Stream avec AAC)

## Architecture

### Flux de données

```
Internet (M3U8)
  ↓
WiFi Stack
  ↓
HTTP Client (télécharge M3U8)
  ↓
M3U8 Parser (extrait URLs segments)
  ↓
HTTP Client (télécharge segments AAC)
  ↓
Ring Buffer (32KB par défaut)
  ↓
Décodeur AAC (esp_audio_codec)
  ↓
drv_max98357a_write()
  ↓
I2S DMA
  ↓
MAX98357A → Haut-parleur
```

### Tasks FreeRTOS

L'application utilise 3 tasks principales :

1. **hls_fetch_task** (prio 4, 16KB stack)
   - Télécharge la playlist M3U8
   - Parse les segments
   - Télécharge les segments audio
   - Écrit dans le ring buffer

2. **audio_play_task** (prio 6, 8KB stack)
   - Lit le ring buffer
   - Décode AAC vers PCM
   - Écrit vers le driver I2S

3. **monitor_task** (prio 2, 3KB stack)
   - Affiche l'état du système toutes les 10s
   - Heap, buffer fill, statistiques

## Logs

### Logs normaux

```
I (1234) main: WiFi connecté avec succès
I (1240) stream_player: Ring buffer créé: 32768 bytes
I (1245) stream_player: Démarrage de la task de téléchargement HLS
I (1250) stream_player: Démarrage de la task de lecture audio
I (2100) m3u8_parser: Playlist parsée: 10 segments, live=1, target_duration=6
I (2500) stream_player: Téléchargement: http://example.com/segment001.aac
I (3200) stream_player: Décodeur AAC créé avec succès
I (5000) main: Stream: 512 KB téléchargés, buffer: 75%
```

### Troubleshooting

| Problème | Cause possible | Solution |
|----------|---------------|----------|
| `Échec de connexion WiFi` | SSID/Password incorrect | Vérifier la config menuconfig |
| `Échec de téléchargement M3U8` | URL invalide ou réseau down | Vérifier l'URL, tester dans un navigateur |
| `Échec de parsing M3U8` | Format non supporté | Vérifier que c'est un M3U8 simple (pas d'AES) |
| `Decoder AAC not registered` | Décodeur non enregistré | Appeler `esp_aac_dec_register()` dans main |
| `Échec de création du décodeur AAC` | Composant manquant | Vérifier `idf_component.yml` |
| `Erreur de décodage: X` | Codec incompatible | Utiliser un stream AAC uniquement |
| `No server verification option set` | HTTPS non configuré | Voir section Sécurité HTTPS |
| `Failed to open new connection` | Erreur TLS/certificat | Vérifier config HTTPS ou utiliser HTTP |
| `Échec d'allocation pour M3U8` | Heap insuffisant | Réduire buffer size ou utiliser PSRAM |
| `Échec d'allocation des buffers` | Mémoire trop basse | ESP32-S3 avec PSRAM recommandé |
| `Buffer vide, attente...` | Réseau lent ou stream coupé | Augmenter `STREAM_BUFFER_SIZE` |
| `Ring buffer plein` | Décodage trop lent | Vérifier CPU, augmenter buffer |
| `Memory allocation failed` | Heap insuffisant | ESP32-S3 avec PSRAM recommandé |

## Sécurité HTTPS

⚠️ **IMPORTANT** : Par défaut, la vérification des certificats HTTPS est **désactivée** pour simplifier l'exemple. Cela signifie que les connexions HTTPS ne sont **pas sécurisées** contre les attaques man-in-the-middle.

### Pour activer la vérification des certificats (recommandé en production)

1. Dans `sdkconfig.defaults`, ajoutez :
```kconfig
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y
```

2. Dans `stream_player.c`, modifiez :
```c
#include "esp_crt_bundle.h"

esp_http_client_config_t config = {
    .url = url,
    .crt_bundle_attach = esp_crt_bundle_attach,  // Au lieu de NULL
    .skip_cert_common_name_check = false,        // Au lieu de true
    // ...
};
```

**Note** : Cela augmente la taille du binaire de ~100KB.

## Optimisations

### Pour économiser la mémoire (ESP32-S3 sans PSRAM)
- Réduire `STREAM_BUFFER_SIZE` à 16KB (au lieu de 32KB)
- Désactiver les fonctionnalités non utilisées dans menuconfig
- Compiler avec `CONFIG_COMPILER_OPTIMIZATION_SIZE=y`

### Pour réduire la latence
- Diminuer `STREAM_BUFFER_SIZE` (minimum 16KB)
- Réduire le délai initial dans `audio_play_task` (actuellement 3s)

### Pour améliorer la stabilité
- Augmenter `STREAM_BUFFER_SIZE` (jusqu'à 64KB avec PSRAM)
- Activer PSRAM pour plus de mémoire disponible
- Utiliser ESP32-S3 avec PSRAM pour des performances optimales

### Pour les réseaux lents
- Augmenter `HTTP timeout` dans `stream_player.c`
- Augmenter la taille du buffer (requiert PSRAM)

## Extensions possibles

Ce projet est un exemple de base. Voici des améliorations possibles :

- ✨ Support HTTPS avec certificats
- ✨ Multi-codec (MP3, FLAC)
- ✨ Variant streams (ABR)
- ✨ Contrôle du volume via GPIO
- ✨ Interface boutons (play/pause, next/prev station)
- ✨ Affichage métadonnées (titre, artiste)
- ✨ Equalizer DSP
- ✨ Enregistrement sur carte SD

## Licence

Ce projet fait partie de `iobewi-idf-components`.
Consultez le fichier LICENSE à la racine du dépôt.

## Support

Pour les bugs et questions :
- Issues : https://github.com/iobewi/iobewi-idf-components/issues
- Documentation driver : `/components/iobewi_driver_max98357a/README.md`

## Références

- [MAX98357A Datasheet](https://datasheets.maximintegrated.com/en/ds/MAX98357A-MAX98357B.pdf)
- [ESP-IDF I2S Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2s.html)
- [HLS Specification (RFC 8216)](https://tools.ietf.org/html/rfc8216)
- [esp_audio_codec Component](https://components.espressif.com/components/espressif/esp_audio_codec)
