# Synthèse Diagnostic Lag/Sauts Audio - ESP32-S3 HLS Player (France Inter/FIP)

**Date** : 2026-02-12
**Contexte** : Streaming HLS AAC/MPEG-TS France Inter/FIP sur ESP32-S3 + MAX98357A I2S
**Qualité** : HiFi (~192 kbps AAC)
**Durée tests** : Multiple sessions (5-60 min)

---

## ✅ PROBLÈMES RÉSOLUS

### 1. Drop-Old Escalation (Spiral de Mort)
**Symptôme initial** :
- Drop-old escaladant de x100 → x2600 (488 KB) en quelques heures
- Dépasse capacité ringbuffer (256 KB) → système "under water"
- 21 événements drop-old par session (vs 0-1 maintenant)

**Cause root** :
- Producer (fetcher HTTP) trop agressif vs consumer (audio task) débordé
- Pas de régulation → boucle : drop-old → NOTIF_RESYNC échoue → drop encore plus

**Solutions implémentées** :
- ✅ **H1 : Ringbuffer 256 KB → 512 KB** (mitigation buffer overflow)
  - Bug fix : `MAX_BUFFER_SIZE` hard-codé à 256 KB (ignorait Kconfig)
  - Commit : 88310d4
- ✅ **Backpressure fetcher** (hystérésis 80%/60%)
  - Pause téléchargement si RB ≥80%, resume si <60%
  - Élimine escalade drop-old : **0-1 événement** (vs 21 avant)
  - Commit : 8572b5e

**Résultats** :
- Drop-old : **0** événements (log 223508, 378s)
- Backpressure : **0** activations nécessaires (RB jamais >80%)
- Système stable ✅

---

### 2. Faux Positifs "write_cb slow"
**Symptôme initial** :
- 6287 warnings "write_cb slow >2ms" (log 220056)
- Fausse alerte : comportement I2S normal confondu avec stall

**Cause** :
- Seuil fixe 2ms trop agressif
- 4096 bytes PCM @ 44.1kHz stéréo = **23ms de données audio**
- `i2s_write()` bloquant DMA jusqu'à 23ms = **comportement attendu**

**Solution** :
- ✅ **Seuil dynamique** basé sur taille PCM
  - Expected : `bytes / 176.4 KB/s`
  - Warn si `>1.5x expected` OU `>30ms`
  - Commit : 88310d4

**Résultats** :
- write_cb slow : **0** warnings (vs 6287 avant)
- I2S fonctionne normalement ✅

---

### 3. Instrumentation Diagnostic (Reason Tags + Occupancy)
**Implémentations** :
- ✅ **Reason tags** NOTIF_RESYNC/RESET (drop-old, discontinuity, resync_failure)
- ✅ **Ringbuffer occupancy** logging (%, KB/KB) aux événements clés
- ✅ **Heartbeat audio** (2s) : decode rate + RB fill% + leftover
- ✅ **write_cb timing** avec seuil dynamique
- Commit : 8572b5e

**Résultats** :
- Diagnostic root cause clarifié (producer/consumer imbalance)
- Métriques stables : decode 137-145 f/s, RB 50-94%

---

## ⚠️ PROBLÈMES RESTANTS

### Symptôme Utilisateur : "Beaucoup de lag/sauts" malgré métriques techniques OK

**Paradoxe observé** (log 223508, 378s HiFi 512KB) :
| Métrique | Valeur | Status |
|----------|--------|--------|
| Drop-old | 0 | ✅ Parfait |
| Backpressure | 0 | ✅ RB jamais saturé |
| UNDERRUN | 0 | ✅ Aucun underrun détecté |
| Silence insertion | 0 | ✅ Pas de gaps |
| Erreurs AAC | 0 | ✅ Décodage propre |
| Decode rate | 137-145 f/s | ✅ Stable |
| RB occupancy | 50-94% | ✅ Jamais vide/plein |

→ **Métriques logicielles PARFAITES, mais perception audio dégradée**

---

## 🔍 HYPOTHÈSES ROOT CAUSE (Lag/Sauts Restants)

### Hypothèse #1 : Micro-Coupures Perceptuelles (<20ms)
**Problème** :
- Coupures trop courtes pour déclencher UNDERRUN (seuil ≥10 cycles = 200-400ms)
- Jitter decode task : vTaskDelay(20ms) entre cycles peut causer micro-gaps
- Non détecté par instrumentation actuelle

**Tests suggérés** :
1. Réduire cycle delay : `vTaskDelay(10ms)` au lieu de 20ms
2. Instrumenter jitter : log delta temps entre cycles decode
3. Ajouter compteur "gap détecté audio" (vs silence insertion actuel ≥10 cycles)

**Impact estimé** : Moyen (si jitter CPU/WiFi simultané)

---

### Hypothèse #2 : Buffer Underrun I2S/DMA (Hardware)
**Problème** :
- DMA I2S sous-dimensionné : `dma_buf_count` ou `dma_buf_len` trop faibles
- CPU peut pauser audio task (WiFi interrupt, TLS crypto) → DMA I2S underrun
- Underrun I2S **non logué** côté ESP32 (pas de callback erreur configuré)

**Symptômes typiques** :
- Clics/pops audio (replay ancien sample DMA)
- Audible comme "saut" mais pas détecté logiciel

**Tests suggérés** :
1. **Vérifier config I2S actuelle** :
   ```c
   // driver MAX98357A init
   dma_buf_count = ?  // Recommandé : 6-8 (vs défaut 2-4)
   dma_buf_len = ?    // Recommandé : 512-1024 samples
   ```
2. **Augmenter buffers DMA** :
   - `dma_buf_count = 8`
   - `dma_buf_len = 1024`
   - Total buffer : 8 × 1024 × 4 bytes = 32 KB DMA
3. **Activer callback I2S error** (si supporté driver)

**Impact estimé** : Élevé (cause probable si config DMA par défaut)

---

### Hypothèse #3 : Chunk Size PCM Trop Grand (4096 bytes)
**Problème** :
- write_cb bloque 23ms par chunk (4096 bytes)
- Si CPU pause pendant ce temps → gap perceptible
- Latence écriture trop longue pour smooth playback

**Tests suggérés** :
1. **Réduire chunk size** : 4096 → 2048 bytes (11.5ms) ou 1024 bytes (5.8ms)
   - Code : `esp_audio_simple_dec_out_t.len = 2048` au lieu de DEC_BUF_SIZE
2. **Mesurer impact latency** : logs write_cb timing avec chunk réduit

**Impact estimé** : Moyen (améliore smoothness)

---

### Hypothèse #4 : Contention WiFi/CPU (TLS Refresh M3U8)
**Problème** :
- Refresh M3U8 toutes les ~13s (23 fois en 378s)
- TLS handshake + HTTP download peut bloquer CPU 100-400ms
- Audio task préemptée → gap si DMA I2S pas assez de buffer

**Observations actuelles** :
- Prébuffer 70% avant start (OK)
- Mais pas de pause audio pendant refresh M3U8 actif

**Tests suggérés** :
1. **Instrumenter timing refresh M3U8** :
   - Log durée `download_m3u8()` (TLS + HTTP)
   - Corréler avec gaps audio perçus
2. **Augmenter priorité audio task** :
   - Actuellement : quelle priorité ? (à vérifier dans code)
   - Essayer : priorité 10-15 (vs fetcher priorité 5)
3. **Pin audio task sur Core 1** (vs fetcher sur Core 0)
   - Évite contention CPU

**Impact estimé** : Moyen (si WiFi interrupt bloque Core 0)

---

### Hypothèse #5 : Source Upstream (France Inter/FIP)
**Problème** :
- Serveur France Inter lui-même a discontinuités/coupures
- ESP32 ne peut pas compenser si source corrompue
- DISCONTINUITY flag M3U8 devrait trigger NOTIF_RESET, mais 0 détecté dans logs

**Tests suggérés** :
1. **Comparer avec autre client** :
   - VLC desktop même stream HiFi
   - Si VLC aussi des coupures → problème upstream
2. **Logger segments téléchargés** :
   - Sequence numbers, durée, gaps détectés
3. **Tester autre source** (ex: FIP vs France Inter)

**Impact estimé** : Faible (logs propres suggèrent stream clean)

---

## 📊 RÉSUMÉ MÉTRIQUES

### Avant Optimisations (Baseline 256 KB, no backpressure)
- **Drop-old** : 21 événements, escalade x100 → x2600 (488 KB)
- **Backpressure** : N/A
- **write_cb slow** : N/A
- **Perception audio** : Lags/sauts fréquents

### Après H1 + Backpressure (Test 220056, 256 KB + backpressure)
- **Drop-old** : 1 événement (x100) ✅ -95%
- **Backpressure** : 24 activations (RB 99% → 91%)
- **write_cb slow** : 6287 warnings ❌ (faux positifs seuil 2ms)
- **Perception audio** : Amélioration modérée

### Après Fix (Test 223508, 512 KB + backpressure + seuil fixé)
- **Drop-old** : 0 ✅ Parfait
- **Backpressure** : 0 ✅ RB jamais >80%
- **write_cb slow** : 0 ✅ Seuil correct
- **Decode rate** : 137-145 f/s ✅ Stable
- **RB occupancy** : 50-94% ✅ Sain
- **Perception audio** : **Encore lags/sauts** ❌

→ **Métriques software parfaites, mais problème audio persiste**

---

## 🎯 RECOMMANDATIONS PRIORITAIRES

### Priority 1 : Vérifier Config I2S/DMA (Impact Élevé)
**Action** :
```c
// Dans init MAX98357A, vérifier/modifier :
i2s_driver_config_t i2s_config = {
    // ...
    .dma_buf_count = 8,        // Augmenter (défaut souvent 2-4)
    .dma_buf_len = 1024,       // Augmenter (défaut souvent 64-512)
};
```
**Justification** : Underrun I2S/DMA = cause #1 clics/pops audio non détectés logiciel

---

### Priority 2 : Réduire Chunk Size PCM (Impact Moyen)
**Action** :
```c
// Dans audio.c, ligne ~57 :
const size_t DEC_BUF_SIZE = 2048;  // Au lieu de CONFIG * 1024 (16KB défaut)
```
**Justification** : 4096 bytes = 23ms bloquant → 2048 = 11.5ms plus smooth

---

### Priority 3 : Instrumenter Jitter Audio Loop (Impact Faible-Moyen)
**Action** :
```c
// Dans audio_play_task(), ajouter :
static int64_t last_cycle_us = 0;
int64_t now = esp_timer_get_time();
int64_t delta = now - last_cycle_us;
if (delta > 30000) {  // >30ms entre cycles
    ESP_LOGW(TAG, "Audio loop jitter: %lld us", delta);
}
last_cycle_us = now;
```
**Justification** : Détecte pauses CPU non détectées actuellement

---

### Priority 4 : Pin Audio Task Core 1 + Priorité (Impact Moyen)
**Action** :
```c
// Dans app_hls_player_start() :
xTaskCreatePinnedToCore(hls_audio_play_task, "audio_play",
                        6144, handle,
                        15,  // Priorité haute (vs 10 actuel ?)
                        &handle->play_task,
                        1);  // Core 1 (vs APP_CPU_NUM actuel)
```
**Justification** : Évite contention WiFi/TLS sur Core 0

---

### Priority 5 : Test Comparatif VLC Desktop (Validation Upstream)
**Action** :
1. VLC desktop → stream France Inter HiFi (même URL)
2. Écoute 10-15 min
3. Si VLC aussi des coupures → problème upstream
4. Si VLC propre → problème ESP32 confirmé

**Justification** : Élimine hypothèse source corrompue

---

## 📁 FICHIERS MODIFIÉS (Commits Récents)

### Commit 9fbeebd : Priorité variant hifi
- `app_hls_player_fetcher.c` : hifi > midfi > lofi

### Commit 88310d4 : Fix MAX_BUFFER_SIZE + seuil write_cb
- `app_hls_player.c` : MAX_BUFFER_SIZE 256 KB → 1024 KB
- `app_hls_player_audio.c` : Seuil write_cb dynamique (1.5x expected)

### Commit 8572b5e : H1 + instrumentation + backpressure
- `Kconfig.projbuild` : RING_BUFFER_SIZE défaut 512 KB, backpressure config
- `app_hls_player_audio.c` : Reason tags, heartbeat, RB occupancy logs
- `app_hls_player_fetcher.c` : Backpressure hystérésis 80%/60%
- `app_hls_player_http.c` : RB occupancy @ drop-old events

---

## 🔗 LOGS TESTS

### Test Baseline (avant fix)
- `hls_stream_app_20260212_180554.log` (6.9 MB, version 300 scan)
  - 21 drop-old (x100 → x2600)
  - Système "under water"

### Test H1 Partiel (256 KB, bug MAX_BUFFER_SIZE)
- `hls_stream_app_20260212_220056.log` (606 KB)
  - 1 drop-old (x100) ✅
  - 24 backpressure ✅
  - 6287 write_cb slow ❌ (faux positifs)
  - **Ringbuffer resté 256 KB** (bug)

### Test H1 Complet (512 KB, seuil fixé, HiFi)
- `hls_stream_app_20260212_223508.log` (169 KB, 378s)
  - 0 drop-old ✅
  - 0 backpressure ✅
  - 0 write_cb slow ✅
  - 0 underrun ✅
  - **Métriques parfaites MAIS lag/sauts perçus** ❌

---

## ✅ CONCLUSION

**Progrès majeurs** :
- Drop-old spiral **éliminée** (0 vs 21 événements)
- Backpressure **fonctionne** (prévient saturation)
- Instrumentation **complète** (reason tags, occupancy, heartbeat)
- Métriques software **parfaites**

**Problème restant** :
- **Lag/sauts audio perçus** malgré métriques OK
- **Cause probable** : Underrun I2S/DMA hardware (config buffers DMA trop faibles)
- **Tests prioritaires** : Augmenter `dma_buf_count=8` + `dma_buf_len=1024`

**Next steps** :
1. Vérifier config I2S/DMA actuelle (fichier init MAX98357A)
2. Tester augmentation buffers DMA
3. Si persiste : réduire chunk size PCM (4096 → 2048)
4. Si persiste : instrumenter jitter audio loop + pin Core 1

---

**Document préparé pour** : Équipe développement
**Contact technique** : [Votre contact]
**Repo** : iobewi-idf-components (branch: iobewi_driver_max98357a)
