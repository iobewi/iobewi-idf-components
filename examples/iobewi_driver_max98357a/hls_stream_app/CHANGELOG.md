# Changelog - HLS Stream Player

## [1.0.0] - 2026-02-07

### Ajouté
- Player de stream HLS/M3U8 complet
- Support décodage AAC via esp_audio_codec
- Parser M3U8 simple (playlists non chiffrées)
- Gestion WiFi avec reconnexion automatique
- Ring buffer pour streaming continu
- Monitoring système (heap, buffer, statistiques)
- Configuration complète via menuconfig
- Documentation complète (README, WIRING, exemples)
- Support ESP32-S3

### Limitations documentées
- Codec: AAC uniquement
- Pas de support chiffrement AES-128
- Pas de variant streams (ABR)
- Pas de contrôle pause/resume
- Playlists simples uniquement

### Fichiers créés
- main/main.c : Point d'entrée et orchestration
- main/wifi_helper.c/h : Gestion WiFi
- main/m3u8_parser.c/h : Parser de playlists M3U8
- main/stream_player.c/h : Pipeline audio (fetch + decode + play)
- sdkconfig.defaults : Configuration par défaut
- README.md : Documentation complète
- WIRING.txt : Schéma de câblage

### Dépendances
- ESP-IDF >= 5.0
- esp_audio_codec >= 2.3.0 (téléchargé automatiquement)
- iobewi_driver_max98357a (composant local)

### Testé sur
- ESP-IDF v6.1-dev
- ESP32-S3
- Compilation réussie: 906KB binaire
