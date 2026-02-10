# [RAM] Global: Optimiser l'usage mémoire de `app_hls_player` (HLS TS → PCM)

**Document** : Plan d'optimisation RAM app_hls_player
**Statut** : TODO - Sprint futur
**Prérequis** : Instrumentation heap/stack + mesures baseline sur hardware
**Contexte** : ESP32-S3, streaming audio HLS (France Inter), objectif RAM minimal

---

## Contexte

`app_hls_player` implémente un client HLS (M3U8 + segments TS) avec 2 tâches (fetch/play), un ring buffer partagé, et un décodeur TS (`esp_audio_simple_dec`).
En condition terrain, l'empreinte RAM est élevée (buffers + stacks + fragmentation), ce qui limite la marge pour le reste du firmware (Wi-Fi, TLS, audio driver, UI, etc.).

## Objectifs

1. **Réduire la RAM peak** sans dégrader la stabilité du stream live.
2. **Réduire la fragmentation heap** (min heap plus stable sur longues durées).
3. Maintenir un comportement temps réel acceptable (pas d'underrun audio, stop réactif).

### Targets (indicatifs)

* **-100 KB à -180 KB RAM** (selon config actuelle)
* `esp_get_minimum_free_heap_size()` **stable** sur 30–60 min (pas de "descente en escalier")
* **Zéro crash** / zéro stack overflow / streaming stable

---

## Mesures & instrumentation (à ajouter en phase 0)

### Instrumentation stack tasks

```c
// Dans hls_fetch_task et audio_play_task
ESP_LOGI(TAG, "[STACK] %s: HWM = %u words (%u bytes)",
         pcTaskGetName(NULL),
         uxTaskGetStackHighWaterMark(NULL),
         uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t));
```

**Mesure recommandée** :
- Au démarrage de chaque tâche
- Périodiquement (toutes les 30s en mode debug)
- Avant `vTaskDelete(NULL)`

### Instrumentation heap

```c
// Avant app_hls_player_start()
size_t free_before = esp_get_free_heap_size();
size_t min_before = esp_get_minimum_free_heap_size();
ESP_LOGI(TAG, "[HEAP] Avant start: libre=%zu, min=%zu", free_before, min_before);

// Après start (dans les tâches, après 10s)
size_t free_after = esp_get_free_heap_size();
size_t min_after = esp_get_minimum_free_heap_size();
ESP_LOGI(TAG, "[HEAP] Après 10s: libre=%zu, min=%zu", free_after, min_after);

// Périodiquement (toutes les 30-60s en debug)
ESP_LOGI(TAG, "[HEAP] Runtime: libre=%zu, min=%zu",
         esp_get_free_heap_size(),
         esp_get_minimum_free_heap_size());
```

### Stats internes optionnelles

Ajouter à `app_hls_player_stats_t` :
```c
typedef struct {
    size_t bytes_downloaded;
    int buffer_fill_percent;

    // Nouveau (optionnel) :
    uint32_t drop_count;           /**< Nombre total de drops (buffer plein) */
    uint32_t drop_old_count;       /**< Nombre de drops avec stratégie drop-old */
    uint32_t decode_error_count;   /**< Erreurs décodage */
    int buffer_min_fill_percent;   /**< Low water mark */
    int buffer_max_fill_percent;   /**< High water mark */
} app_hls_player_stats_t;
```

---

## Principales sources de consommation RAM (état actuel)

| Source | Taille actuelle | Type | Remarque |
|--------|-----------------|------|----------|
| **Stack fetch_task** | 14336 bytes (14 KB) | Stack | À mesurer avec HWM |
| **Stack play_task** | 6144 bytes (6 KB) | Stack | À mesurer avec HWM |
| **HTTP buffer** | 16 KB | Heap | `esp_http_client` internal |
| **Ringbuffer** | 100 KB (défaut) | Heap | Configurable via config |
| **Encoded buffer** | 144 KB (PSRAM) ou 64 KB | Heap/PSRAM | **GROS GOULOT** ⚠️ |
| **Decoded buffer** | 16 KB | Heap | PCM output |
| **M3U8 buffer** | ~16-32 KB | Heap | malloc + realloc (fragmentation) |

**RAM totale estimée** : ~200-250 KB (hors Wi-Fi, TLS, audio driver)

---

## Plan d'optimisation (priorisé)

### P0 — Gains rapides, faible risque (4-6h)

#### 1) Right-size stacks des tâches

**Problème actuel** :
- `hls_fetch_task` : 14336 bytes (14 KB)
- `audio_play_task` : 6144 bytes (6 KB)
- Stacks probablement surdimensionnés

**Action** :
1. Mesurer HWM (High Water Mark) sur streaming 30+ minutes
2. Réduire stacks avec marge de sécurité (HWM > 800-1000 words)

**Proposition** :
```c
// hls_fetch_task
#define HLS_FETCH_STACK_SIZE  8192  // 8 KB (vs 14 KB actuel)

// audio_play_task
#define AUDIO_PLAY_STACK_SIZE 4096  // 4 KB (vs 6 KB actuel)
```

**Gain estimé** : -8 KB stack total

**Critères d'acceptation** :
- [ ] Aucun stack overflow sur 60 min streaming
- [ ] HWM confortable (marge > 20% du stack)
- [ ] Logs `uxTaskGetStackHighWaterMark()` validés

---

#### 2) Diminuer `HTTP_BUFFER_SIZE`

**Problème actuel** :
- `esp_http_client` utilise buffer RX interne de 16 KB par défaut

**Action** :
```c
esp_http_client_config_t config = {
    .url = segment_url,
    .timeout_ms = 5000,
    .buffer_size = 4096,  // 4 KB au lieu de 16 KB (défaut)
    .event_handler = http_event_handler,
    .user_data = handle,
};
```

**Test graduel** : 16 KB → 8 KB → 4 KB (arrêter si instabilité)

**Gain estimé** : -12 KB (si 4 KB OK), -8 KB (si 8 KB)

**Critères d'acceptation** :
- [ ] Pas d'erreurs HTTP récurrentes
- [ ] Throughput suffisant (buffer pas vide en permanence)
- [ ] Débit réseau stable (mesurer via stats)

---

### P1 — Gros gain RAM, impact modéré (12-16h)

#### 3) Supprimer `encoded_buffer` (144 KB) - **PRIORITÉ #1** ⚠️

**Problème actuel** :
```c
// Dans audio_play_task
const size_t ENC_BUF_SIZE = CONFIG_APP_HLS_PLAYER_ENC_BUFFER_SIZE * 1024;
uint8_t *encoded_buffer = heap_caps_malloc(ENC_BUF_SIZE, MALLOC_CAP_SPIRAM);
size_t buffered_size = 0;

// Data path actuel (INEFFICACE) :
while (true) {
    // 1. Recevoir chunk du ringbuffer
    void *item;
    size_t item_size;
    item = xRingbufferReceive(handle->ring_buffer, &item_size, timeout);

    // 2. COPIE vers encoded_buffer
    memcpy(encoded_buffer + buffered_size, item, item_size);
    buffered_size += item_size;
    vRingbufferReturnItem(handle->ring_buffer, item);

    // 3. Decode
    esp_audio_dec_in_raw_t in_raw = {
        .buffer = encoded_buffer,
        .len = buffered_size,
    };
    ret = esp_audio_simple_dec_process(dec_handle, &in_raw, &out_pcm);

    // 4. MEMMOVE pour compacter (très coûteux !)
    if (in_raw.consumed > 0 && in_raw.consumed < buffered_size) {
        memmove(encoded_buffer,
                encoded_buffer + in_raw.consumed,
                buffered_size - in_raw.consumed);
        buffered_size -= in_raw.consumed;
    }
}
```

**Problèmes** :
- Allocation massive (144 KB) non nécessaire
- Copies multiples : ringbuffer → encoded_buffer → memmove
- Fragmentation mémoire si allocation/désallocation

---

**Solution : Remainder buffer + streaming decode** ✅

**Concept** :
1. Garder uniquement un **petit remainder buffer** (4-8 KB) pour bytes non consommés
2. Décoder directement depuis chunk ringbuffer + remainder assemblé
3. Pas de memmove massif, juste copie du petit remainder

**Implémentation** :

```c
// Configuration
#ifdef CONFIG_APP_HLS_PLAYER_REM_BUFFER_SIZE
#define REM_BUFFER_SIZE (CONFIG_APP_HLS_PLAYER_REM_BUFFER_SIZE * 1024)
#else
#define REM_BUFFER_SIZE (8 * 1024)  // 8 KB par défaut
#endif

// Allocation remainder
uint8_t *remainder_buf = malloc(REM_BUFFER_SIZE);
size_t remainder_size = 0;

// Buffer assemblage temporaire (stack ou petit heap)
uint8_t assembly_buf[REM_BUFFER_SIZE + MAX_CHUNK_SIZE];

while (true) {
    // Recevoir chunk ringbuffer
    void *chunk;
    size_t chunk_size;
    chunk = xRingbufferReceive(handle->ring_buffer, &chunk_size, timeout);
    if (!chunk) continue;

    // Assembler remainder + chunk
    size_t total_size = remainder_size + chunk_size;

    // Option A : Copie temporaire (si chunk_size < 16KB)
    memcpy(assembly_buf, remainder_buf, remainder_size);
    memcpy(assembly_buf + remainder_size, chunk, chunk_size);

    // Option B : Double pointeur (si décodeur supporte scatter-gather)
    // → Pas supporté par esp_audio_simple_dec actuellement

    // Decode
    esp_audio_dec_in_raw_t in_raw = {
        .buffer = assembly_buf,
        .len = total_size,
    };

    esp_audio_dec_out_raw_t out_pcm = {
        .buffer = decoded_buffer,
        .len = DEC_BUF_SIZE,
    };

    ret = esp_audio_simple_dec_process(dec_handle, &in_raw, &out_pcm);
    vRingbufferReturnItem(handle->ring_buffer, chunk);

    // Gérer remainder (bytes non consommés)
    size_t consumed = in_raw.consumed;
    if (consumed < total_size) {
        remainder_size = total_size - consumed;

        if (remainder_size > REM_BUFFER_SIZE) {
            ESP_LOGE(TAG, "Remainder overflow: %zu > %d",
                     remainder_size, REM_BUFFER_SIZE);
            // Truncate ou augmenter REM_BUFFER_SIZE
            remainder_size = REM_BUFFER_SIZE;
        }

        // Copier remainder pour prochain cycle
        memcpy(remainder_buf, assembly_buf + consumed, remainder_size);
    } else {
        remainder_size = 0;
    }

    // Write PCM si décodé
    if (ret == ESP_AUDIO_ERR_OK && out_pcm.decoded_bytes > 0) {
        handle->write_cb(handle->write_ctx,
                         out_pcm.buffer,
                         out_pcm.decoded_bytes,
                         &bytes_written,
                         5000);
    }
}
```

**Gain estimé** : **-136 KB** (144 KB → 8 KB remainder)

**Ajout Kconfig** :
```kconfig
config APP_HLS_PLAYER_REM_BUFFER_SIZE
    int "Remainder buffer size (KB)"
    default 8
    range 4 16
    help
        Small buffer to keep unconsumed bytes between decode cycles.

        Typical values:
        - 4 KB: Minimal, may increase decode errors if TS packets large
        - 8 KB: Recommended (sufficient for AAC/TS)
        - 16 KB: Conservative

        Larger = more resilient to large TS packets, but more RAM.
```

**Critères d'acceptation** :
- [ ] Gain RAM >= 100 KB vs baseline
- [ ] Taux erreur decode stable (pas d'augmentation significative)
- [ ] Streaming stable 60 min (pas d'underrun audio)
- [ ] CPU : pas de surconsommation (suppression memmove devrait compenser)
- [ ] `remainder_size` typiquement < 4 KB (log max observé)

---

#### 4) Ajuster `DEC_BUFFER_SIZE` au pipeline audio

**Problème actuel** :
- `DEC_BUFFER_SIZE` = 16 KB par défaut
- Peut être surdimensionné selon latence `write_cb` (I2S)

**Action** :
1. Mesurer taille typique `out_pcm.decoded_bytes`
2. Ajuster `DEC_BUFFER_SIZE` à 8 KB ou 4 KB si suffisant

**Gain estimé** : -8 KB à -12 KB

**Critères d'acceptation** :
- [ ] `write_cb` ne timeout pas plus souvent
- [ ] Aucun glitch audio
- [ ] PCM décodé ne dépasse jamais buffer (log max)

---

### P2 — Fragmentation / robustesse long terme (6-8h)

#### 5) Réutiliser un buffer M3U8 (éviter realloc répétées)

**Problème actuel** :
```c
// Dans download_m3u8()
char *buffer = malloc(INITIAL_SIZE);
// ... lecture progressive ...
buffer = realloc(buffer, new_capacity);  // Fragmentation !
```

**Action** :
Stocker buffer M3U8 réutilisable dans handle :
```c
typedef struct app_hls_player_s {
    // ... champs existants ...

    char *m3u8_buffer;      /**< Buffer réutilisable M3U8 */
    size_t m3u8_capacity;   /**< Capacité actuelle */
} app_hls_player_t;

// Dans app_hls_player_new()
handle->m3u8_buffer = malloc(32 * 1024);  // 32 KB initial
handle->m3u8_capacity = 32 * 1024;

// Dans download_m3u8()
char* download_m3u8_reusable(const char *url, char *buffer, size_t *capacity) {
    // Remplir buffer existant
    // Si trop petit, realloc (rare)
    // Retourner buffer (même pointeur si pas realloc)
}
```

**Gain** : Pas de gain RAM direct, mais **réduction fragmentation** → min heap plus stable

**Critères d'acceptation** :
- [ ] `esp_get_minimum_free_heap_size()` plus stable sur 60 min
- [ ] Pas de fuite mémoire (même pointeur réutilisé)
- [ ] Realloc rare (log si resize)

---

#### 6) Réduire ringbuffer selon bitrate

**Problème actuel** :
- Ringbuffer 100 KB par défaut (trop pour lofi, insuffisant pour hifi+jitter)

**Action** :
Recommandations Kconfig selon bitrate :
```kconfig
config APP_HLS_PLAYER_RING_BUFFER_SIZE
    int "Ring buffer size (KB)"
    default 64 if !SPIRAM
    default 128 if SPIRAM
    range 32 256
    help
        Size of the ring buffer for streaming data.

        Recommended values by bitrate:
        - lofi  (~64 kbps)  : 32-48 KB (≈4-6s buffer)
        - midfi (~128 kbps) : 64-96 KB (≈4-6s buffer)
        - hifi  (~192 kbps) : 96-128 KB (≈4-5s buffer)

        Larger = more resilient to jitter, but more RAM.
```

**Gain estimé** : -36 KB si 64 KB (vs 100 KB), -68 KB si 32 KB

**Critères d'acceptation** :
- [ ] Buffer fill % ne tombe pas à 0 en Wi-Fi stable
- [ ] Drop rate acceptable (pas de drops permanents)
- [ ] Qualité audio stable

---

### P3 — Micro-optimisations (optionnel, 2-4h)

#### 7) Optimiser allocations RTOS

**Actions possibles** :
- Remplacer `stats_mutex` par section critique `taskENTER_CRITICAL()` si stats best-effort OK
- Utiliser davantage Task Notifications vs sémaphores (déjà fait pour STOP/RESET)

**Gain estimé** : -100 à -200 bytes (négligeable)

---

#### 8) PSRAM si disponible

**Action** :
```c
// Ringbuffer en PSRAM (non DMA, OK pour bufferisation)
handle->ring_buffer = xRingbufferCreateStatic(
    buffer_size,
    RINGBUF_TYPE_BYTEBUF,
    psram_buffer,
    &static_ring_struct
);

// M3U8 buffer en PSRAM
handle->m3u8_buffer = heap_caps_malloc(32*1024, MALLOC_CAP_SPIRAM);

// Remainder en PSRAM (si non critique perf)
remainder_buf = heap_caps_malloc(REM_BUFFER_SIZE, MALLOC_CAP_SPIRAM);

// PCM decoded en internal RAM (si driver audio préfère)
decoded_buffer = heap_caps_malloc(DEC_BUF_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
```

**Gain** : Libère internal RAM pour Wi-Fi/TLS

---

## Récapitulatif gains estimés

| Optimisation | Gain RAM | Complexité | Risque | Effort |
|--------------|----------|------------|--------|--------|
| **P0.1** Stack tasks | -8 KB | Faible | Faible | 1h |
| **P0.2** HTTP buffer | -8 à -12 KB | Faible | Faible | 1h |
| **P1.3** Remainder (vs encoded) | **-136 KB** | Moyenne | Moyen | 12h |
| **P1.4** DEC buffer | -8 à -12 KB | Faible | Faible | 2h |
| **P2.5** M3U8 réutilisable | 0 KB (fragmentation↓) | Faible | Faible | 4h |
| **P2.6** Ringbuffer tuning | -36 à -68 KB | Faible | Moyen | 2h |
| **TOTAL** | **-196 à -244 KB** | - | - | **~24h** |

**Target atteint** : ✅ -100 à -180 KB (objectif dépassé !)

---

## Tâches (checklist implémentation)

### Phase 0 : Instrumentation (4h)
- [ ] Ajouter logs `uxTaskGetStackHighWaterMark()` dans tasks
- [ ] Ajouter logs heap (free + minimum) périodiques
- [ ] Étendre `app_hls_player_stats_t` (drop, errors, buffer watermarks)
- [ ] Tester sur hardware 30 min, capturer baseline

### Phase 1 : P0 + P1 (16h)
- [ ] P0.1 : Mesurer HWM, ajuster stacks (8 KB + 4 KB)
- [ ] P0.2 : Réduire HTTP buffer (16 KB → 4-8 KB)
- [ ] P1.3 : Implémenter remainder buffer streaming decode
  - [ ] Ajouter Kconfig `REM_BUFFER_SIZE`
  - [ ] Refactorer `audio_play_task` data path
  - [ ] Supprimer `encoded_buffer` allocation
  - [ ] Tests stabilité decode
- [ ] P1.4 : Ajuster `DEC_BUFFER_SIZE` (16 KB → 8 KB)
- [ ] Tests intégration 60 min

### Phase 2 : P2 (8h)
- [ ] P2.5 : Implémenter M3U8 buffer réutilisable dans handle
- [ ] P2.6 : Ajuster recommandations ringbuffer Kconfig
- [ ] Tests long-run (2h+), monitoring fragmentation

### Phase 3 : Validation finale (4h)
- [ ] Tests stabilité 60+ min
- [ ] Validation gains RAM (heap logs)
- [ ] Validation qualité audio (écoute subjective)
- [ ] Validation stop réactif (< 1s)
- [ ] Documentation changements

---

## Critères d'acceptation globaux

- [ ] **Gain RAM** : ≥ 100 KB vs baseline (config nominale)
- [ ] **`esp_get_minimum_free_heap_size()`** stable sur 60 min
- [ ] **Streaming stable** : pas de crash, pas de reset, pas d'underrun audio
- [ ] **Stop réactif** : NOTIF_STOP < 1s
- [ ] **Pas de fuite** : heap usage stable sur long-run
- [ ] **Taux erreur decode** : stable (pas d'augmentation > 5%)
- [ ] **Stack overflow** : zéro (HWM confortable)

---

## Risques / Points de vigilance

### Risque 1 : Remainder buffer trop petit
**Symptôme** : Augmentation erreurs decode
**Mitigation** :
- Commencer avec 8 KB (conservateur)
- Logger `remainder_size` max observé
- Augmenter à 16 KB si nécessaire

### Risque 2 : Ringbuffer trop petit
**Symptôme** : Drops fréquents, audio glitches
**Mitigation** :
- Tester progressivement : 100 KB → 64 KB → 48 KB
- Valider sur Wi-Fi réel avec jitter
- Garder profils par bitrate

### Risque 3 : HTTP buffer trop petit
**Symptôme** : Throughput réduit, erreurs HTTP
**Mitigation** :
- Tester progressivement : 16 KB → 8 KB → 4 KB
- Valider sur TLS (overhead)
- Fallback 8 KB si 4 KB instable

### Risque 4 : Stack overflow
**Symptôme** : Crash, reset ESP32
**Mitigation** :
- Mesurer HWM sur 60+ min
- Garder marge 20-25% minimum
- Tests avec conditions charge (Wi-Fi scan, etc.)

---

## Notes d'implémentation

### Kconfig à ajouter/modifier

```kconfig
menu "HLS Player Configuration"

    config APP_HLS_PLAYER_RING_BUFFER_SIZE
        int "Ring buffer size (KB)"
        default 64 if !SPIRAM
        default 128 if SPIRAM
        range 32 256
        # ... help ...

    config APP_HLS_PLAYER_REM_BUFFER_SIZE
        int "Remainder buffer size (KB)"
        default 8
        range 4 16
        help
            Buffer for unconsumed bytes between decode cycles.
            Replaces large encoded_buffer (saves ~136 KB).

    config APP_HLS_PLAYER_DEC_BUFFER_SIZE
        int "Decoded PCM buffer size (KB)"
        default 8
        range 4 32
        # ... help ...

    config APP_HLS_PLAYER_DEBUG_HEAP
        bool "Enable periodic heap logging (debug)"
        default n
        help
            Log heap stats every 30s (for RAM optimization).

    config APP_HLS_PLAYER_DEBUG_STACK
        bool "Enable periodic stack HWM logging (debug)"
        default n
        help
            Log stack high water marks periodically.

endmenu
```

### API publique inchangée

**Important** : Garder `app_hls_player.h` et `app_hls_player_types.h` inchangés.
Toutes les optimisations sont **internes** (implémentation).

Seul ajout : nouveaux champs optionnels dans `app_hls_player_stats_t` (non breaking).

---

## Ordre d'implémentation recommandé

1. **Phase 0** (instrumentation) : Baseline mesures
2. **P0.1 + P0.2** : Gains rapides, validation hardware
3. **P1.3** : Gros gain remainder buffer (priorité #1)
4. **P1.4** : Ajustement DEC buffer
5. **Tests intégration** : 60 min validation
6. **P2.5 + P2.6** : Fragmentation long-terme
7. **Validation finale** : 2h+ streaming

**Total effort estimé** : 24-32h (selon complexité debug)

---

**Document créé** : 2025-02
**Auteur** : Équipe iobewi
**Statut** : TODO - Sprint futur (après validation P0+P1+P2 m3u8_parser)
