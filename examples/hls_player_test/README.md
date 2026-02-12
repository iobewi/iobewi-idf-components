# HLS Player Test Harness (Mock Sink)

Test harness minimal pour `app_hls_player` permettant de valider le player **sans consumer I2S**.

## Fonctionnalités

### Mock Sink Write Callback
- Comptabilise bytes PCM décodés sans jouer l'audio
- Stats périodiques toutes les 10s (frames, bytes, débit)
- Permet de tester la chaîne complète : download → decode → callback

### Modes de Test

1. **Simple Run** (défaut) : Run 60s simple
2. **Start/Stop Cycles** : 3 cycles de 30s avec pause 2s
3. **Long Run** : Durée configurable (600-7200s)

## Usage

### 1. Configuration Wi-Fi

Éditer `sdkconfig.defaults` ou via menuconfig :

```bash
idf.py menuconfig
# → Example Connection Configuration
#   → WiFi SSID / Password
```

### 2. Sélectionner le mode de test

```bash
idf.py menuconfig
# → HLS Test Configuration
#   → Test mode (Simple/Cycles/Long run)
```

### 3. Build + Flash

```bash
idf.py build flash monitor
```

## P0.1 Stack Reduction (HWM Baseline)

Pour établir le baseline HWM avant réduction des stacks :

1. Activer stack diagnostics :
   ```bash
   idf.py menuconfig
   # → HLS Player Configuration
   #   → Enable stack diagnostics (HWM logging) = Y
   ```

2. Activer log level DEBUG :
   ```bash
   idf.py menuconfig
   # → Component config → Log output
   #   → Default log verbosity = Debug
   ```

3. Run long-run test (10 min minimum) :
   ```bash
   idf.py menuconfig
   # → HLS Test Configuration
   #   → Test mode = Long run
   #   → Long run duration = 600 (10 min)
   ```

4. Analyser logs HWM :
   ```bash
   idf.py flash monitor | tee hwm_baseline.log
   # Après test :
   grep "\[STACK\] HWM" hwm_baseline.log
   ```

5. Identifier HWM minimal :
   - hls_fetch : rechercher minimum dans logs `[hls_fetch]`
   - audio_play : rechercher minimum dans logs `[audio_play]`

6. Calculer nouvelle taille stack :
   - **FORMULE CORRECTE** : `S_new = (S_old - F_min) / 0.75`
     - S_old = stack allouée actuelle (bytes)
     - F_min = HWM_min observé (bytes)
     - (S_old - F_min) = usage au pic
   - **ATTENTION** : HWM = free min (marge), PAS usage
   - Voir détails : `components/iobewi_apps_hls_player/docs/P0.1_STACK_REDUCTION.md`
   - Arrondir au KB supérieur

## Tests Attendus

### Simple Run (60s)
- Heap min ≥ 1.85 MB
- Aucun crash/watchdog
- Frames PCM > 0
- Logs des 5 modules visibles

### Start/Stop Cycles (3x)
- Pas de fuite mémoire entre cycles
- Heap min stable entre cycles
- Stats bytes_downloaded croissants

### Long Run (600s+)
- Heap min stable sur durée complète
- Pas de fragmentation progressive
- Mock sink stats cohérentes (débit ~128 kbps AAC décodé)

## Architecture Testée

```
Wi-Fi → HTTP/TLS → M3U8 Parser → Segment Download
                                       ↓
                                  Ring Buffer (128 KB)
                                       ↓
                        [TS Demux] → [AAC Decode] → Mock Sink (compteurs)
```

## Logs Attendus

```
I (12345) hls_test: Wi-Fi connected
I (12350) app_hls_player: Player HLS créé avec succès
I (12360) hls_fetcher: Démarrage de la task de téléchargement HLS
I (12370) hls_audio: Démarrage de la task de lecture audio
I (15000) hls_http: M3U8 téléchargé: 1234 bytes
I (16000) hls_fetcher: Variant sélectionné: MIDFI (bitrate 128 kbps)
I (22000) hls_test: [MOCK SINK] Frames=450, Bytes=192000, Rate=576 KB/min | Heap: libre=1950000, min=1910000
...
```

## Troubleshooting

### Crash TLS/HTTP
- Vérifier stack fetch ≥ 14 KB (TLS handshake consomme beaucoup)
- Activer `CONFIG_MBEDTLS_DEBUG` si nécessaire

### Heap min < 1.85 MB
- Réduire ring buffer (128 → 96 KB)
- Réduire gather buffer (8 → 4 KB)

### Pas de frames PCM
- Vérifier Wi-Fi stable
- Vérifier URL M3U8 accessible
- Check logs `hls_audio` pour erreurs décodage
