# TODO : Optimisations avancées M3U8 Parser (P3, P4, P5)

**Document** : Plan d'implémentation optimisations avancées
**Statut** : TODO - Sprint futur
**Prérequis** : P0+P1+P2 implémentés et validés sur hardware (✅ Commit 75583ca)
**Contexte** : ESP32-S3, streaming audio HLS (France Inter), objectif RAM minimal

---

## État actuel (Post P0+P1+P2)

### Gains déjà obtenus ✅

| Optimisation | Impact RAM | Bénéfices |
|--------------|------------|-----------|
| **P0** : Union segments/variants | -45.4% | 19.3 KB → 10.5 KB |
| **P1** : Types compacts (uint16_t, uint32_t, flags) | -0.7% | Suppression float, déterminisme |
| **P2** : Kconfig (16 items, 160 URL, 32 codecs) | -71.1% additionnel | **3.2 KB total (-83.4%)** |

**Résultat** : Playlist RAM réduite de 19.3 KB → 3.2 KB (audio optimisé) ✅

### Zones d'optimisation restantes

Les optimisations P0-P2 ont ciblé la **structure playlist** (heap).
Les optimisations P3-P5 ciblent :
- **P3** : Stack pendant le parsing (buffers temporaires)
- **P4** : Comportement live (window glissante)
- **P5** : URLs (éliminer copies, stocker offsets)

---

## P3 : Réduction stack parsing et copies mémoire

### Problématique

**Stack actuelle du parsing** :
```c
// Dans lib_m3u8_parser_parse()
char line[512];              // Buffer ligne : 512 bytes
char value_buf[128];         // Buffer attributs : 128 bytes
                             // → 640 bytes stack minimum
```

**Opérations coûteuses** :
- `trim_line()` utilise `memmove()` pour décaler les espaces (copie en mémoire)
- Multiples `strncpy()` pour URLs et attributs
- Buffers temporaires empilés

**Impact** :
- Stack élevée (640+ bytes) pour une fonction de parsing
- Copies mémoire inutiles (trim, attributs, URLs)
- Fragmentation CPU (memmove)

---

### Solution P3a : Trim in-place par pointeurs (stack -0 bytes, perf +)

**Concept** : Au lieu de déplacer la mémoire, travailler avec des pointeurs vers la zone utile.

**Avant** :
```c
static void trim_line(char *s) {
    // ltrim - décaler réellement la chaîne avec memmove
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) {
        memmove(s, p, strlen(p) + 1);  // ❌ Copie mémoire
    }
    // rtrim
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t')) {
        s[--len] = '\0';
    }
}
```

**Après** :
```c
// Retourne pointeur vers début + longueur (vue sans copie)
static const char* trim_view(const char *s, size_t *out_len) {
    // ltrim
    while (*s == ' ' || *s == '\t') s++;

    // rtrim
    const char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t')) end--;

    *out_len = end - s;
    return s;  // ✅ Pas de copie, juste un pointeur
}
```

**Gain** : Suppression memmove (CPU), pas de gain stack immédiat mais prépare P3b.

---

### Solution P3b : Parsing avec vues (ptr, len) au lieu de copies

**Concept** : Ne copier que quand strictement nécessaire (stockage final).

**Avant** :
```c
char line[512];
if (read_line(content, &offset, line, sizeof(line))) {
    trim_line(line);  // Copie + memmove
    if (strncmp(line, "#EXTINF:", 8) == 0) {
        float duration_sec = atof(line + 8);  // Parse
        // ...
    }
}
```

**Après** :
```c
// Pas de buffer ligne, juste une vue (ptr + len)
const char *line_start, *line_end;
if (read_line_view(content, &offset, &line_start, &line_end)) {
    size_t line_len;
    const char *trimmed = trim_view(line_start, &line_len);

    if (line_len >= 8 && strncmp(trimmed, "#EXTINF:", 8) == 0) {
        // Parse directement depuis la vue
        float duration_sec = parse_float_view(trimmed + 8, line_len - 8);
    }
}
```

**Gain stack** : 512 bytes (line) + 128 bytes (value_buf) = **-640 bytes** ✅

**Complexité** : Moyenne (refactoring complet du parsing)

---

### Plan d'implémentation P3

1. **Phase 1** : Implémenter `trim_view()` et helpers de parsing par vue
   - `trim_view(const char *s, size_t *len) → const char*`
   - `parse_float_view(const char *s, size_t len) → float`
   - `parse_int_view(const char *s, size_t len) → int`

2. **Phase 2** : Remplacer `read_line()` par `read_line_view()`
   - Retourne `(const char *start, const char *end)` au lieu de copier dans buffer

3. **Phase 3** : Adapter `get_attribute_value()` pour travailler avec vues
   - Retourner `(const char *value_start, size_t value_len)` au lieu de copier dans dest

4. **Phase 4** : Tests de validation
   - Parser playlists complexes (master + variants)
   - Vérifier pas de buffer overflow (vues hors limites)
   - Comparer résultats avec version P2

**Durée estimée** : 4-6h
**Risque** : Moyen (beaucoup de changements parsing, attention bornes)
**Gain** : -640 bytes stack + suppression memmove/strncpy

---

## P4 : Sliding window pour playlists live

### Problématique

**Comportement actuel** :
- Le parser stocke TOUS les segments de la playlist (16 par défaut avec P2)
- Pour un flux live, seuls les **N derniers segments** sont pertinents
- Les anciens segments ne seront jamais rejoués

**Exemple France Inter** :
- Playlist rafraîchie toutes les ~10s
- Contient ~10 segments (1 minute de buffer)
- On ne rejoue JAMAIS les segments > 30s en arrière

**Gaspillage** :
- Stocker 16 segments alors que seuls 8-10 sont utiles
- Parser et copier des segments obsolètes

---

### Solution P4 : Ring buffer segments (window glissante)

**Concept** : Garder uniquement une fenêtre glissante des N derniers segments.

**Implémentation** :
```c
typedef struct {
    lib_m3u8_parser_segment_t segments[CONFIG_M3U8_MAX_ITEMS];
    int segment_count;        // Nombre de segments actuellement stockés
    int write_index;          // Index d'écriture (circulaire)
    uint32_t base_sequence;   // Plus ancienne sequence dans le ring
} lib_m3u8_parser_media_playlist_t;

// Ajouter un segment (écrase le plus ancien si plein)
void add_segment_sliding(lib_m3u8_parser_media_playlist_t *pl,
                         const lib_m3u8_parser_segment_t *seg) {
    if (pl->segment_count < CONFIG_M3U8_MAX_ITEMS) {
        // Buffer pas encore plein, ajouter normalement
        pl->segments[pl->segment_count++] = *seg;
    } else {
        // Buffer plein, écraser le plus ancien (ring)
        pl->segments[pl->write_index] = *seg;
        pl->write_index = (pl->write_index + 1) % CONFIG_M3U8_MAX_ITEMS;
        pl->base_sequence = seg->sequence - CONFIG_M3U8_MAX_ITEMS + 1;
    }
}
```

**Workflow live** :
1. Parser playlist avec 10 segments (seq 1000-1009)
2. Stocker les 10 dans le ring
3. 10s plus tard, parser nouvelle playlist (seq 1002-1011)
4. **Identifier nouveaux segments** : 1010, 1011
5. **Ajouter au ring** : écrase segments 1000, 1001 (obsolètes)
6. Ring contient maintenant 1002-1011 (fenêtre glissante)

**Gain RAM** : Pas de gain structure (même MAX_ITEMS), mais permet de réduire MAX_ITEMS pour live
- Avec sliding : 8 items suffisent (vs 16 sans)
- **Gain indirect** : 3.2 KB → 1.6 KB (-50%)

**Avantages supplémentaires** :
- Moins de segments à parser/traiter chaque cycle
- Détection automatique nouveaux segments (diff séquences)
- Résilience si playlist serveur garde anciens segments

---

### Plan d'implémentation P4

1. **Phase 1** : Nouvelle structure avec ring buffer
   - Ajouter `write_index`, `base_sequence` à la playlist
   - Créer `add_segment_sliding()` et `get_segment_by_seq()`

2. **Phase 2** : Option Kconfig pour activer sliding window
   ```kconfig
   config M3U8_LIVE_SLIDING_WINDOW
       bool "Use sliding window for live playlists"
       default y
       help
           Keep only the N most recent segments in memory (ring buffer).
           Older segments are automatically overwritten.

           Recommended for live streaming (reduces RAM and CPU).
           Disable for VOD if you need access to all segments.
   ```

3. **Phase 3** : Adapter le parsing pour remplir le ring
   - Détecter nouveaux segments (seq > dernière seq stockée)
   - Ajouter uniquement les nouveaux au ring

4. **Phase 4** : Adapter `app_hls_player` pour consommer depuis ring
   - Chercher segments par séquence au lieu d'index
   - Gérer cas où segment demandé a été écrasé (skip ou re-fetch)

5. **Phase 5** : Tests validation
   - Simuler playlist live avec séquences croissantes
   - Vérifier que fenêtre glisse correctement
   - Tester comportement si segment manquant

**Durée estimée** : 6-8h
**Risque** : Moyen-Élevé (changement logique consommation segments)
**Gain** : -50% RAM si MAX_ITEMS réduit (8 vs 16)

---

## P5 : URLs sans copie (offsets dans buffer)

### Problématique

**Goulot RAM actuel** : Les URLs représentent 93% de la taille d'un segment.

```
sizeof(segment_t) = 172 bytes avec P2 (16 items, 160 URL)
  ├─ url[160]       : 160 bytes (93.0%)  ← GOULOT
  ├─ duration_ms    : 2 bytes
  ├─ sequence       : 4 bytes
  ├─ flags          : 1 byte
  └─ padding        : 5 bytes
```

**Constat** :
- Chaque segment copie son URL complète (160 bytes)
- L'URL source existe déjà dans `m3u8_content` (buffer HTTP)
- On copie inutilement 160 × 16 = **2.5 KB d'URLs**

---

### Solution P5 : Stocker offsets au lieu de copier

**Concept** : Au lieu de copier l'URL, stocker un offset + longueur vers `m3u8_content`.

**Avant** :
```c
typedef struct {
    char url[160];        // 160 bytes, COPIÉ depuis m3u8_content
    uint16_t duration_ms;
    uint32_t sequence;
    uint8_t flags;
} lib_m3u8_parser_segment_t;
```

**Après** :
```c
typedef struct {
    uint16_t url_offset;  // Offset dans m3u8_content (2 bytes)
    uint16_t url_len;     // Longueur URL (2 bytes)
    uint16_t duration_ms;
    uint32_t sequence;
    uint8_t flags;
} lib_m3u8_parser_segment_t;  // Maintenant : 13 bytes vs 172 bytes !
```

**Workflow** :
```c
// Parsing
char *m3u8_content = download_m3u8(url);
lib_m3u8_parser_playlist_t playlist;
lib_m3u8_parser_parse_zero_copy(m3u8_content, &playlist);

// Usage ultérieur (résoudre l'URL quand nécessaire)
for (int i = 0; i < playlist.segment_count; i++) {
    lib_m3u8_parser_segment_t *seg = &playlist.segments[i];

    // Reconstruire l'URL uniquement au moment de l'utiliser
    char full_url[256];
    lib_m3u8_parser_resolve_segment_url(&playlist, seg, m3u8_content,
                                         full_url, sizeof(full_url));

    // Télécharger le segment
    download_segment(full_url, ...);
}
```

**Gain RAM structure** :
- segment_t : 172 bytes → **13 bytes** (-92.4%) ✅
- Playlist 16 items : 3.2 KB → **0.5 KB** (-84%) ✅

**MAIS attention** : Le buffer `m3u8_content` doit rester vivant !

---

### Contraintes et trade-offs P5

#### ⚠️ Contrainte 1 : Durée de vie du buffer m3u8_content

**Problème** : Le buffer `m3u8_content` ne peut plus être libéré immédiatement après parsing.

**Avant (P0-P2)** :
```c
char *m3u8_content = download_m3u8(url);
lib_m3u8_parser_parse(m3u8_content, &playlist);
free(m3u8_content);  // ✅ OK, URLs copiées dans playlist
```

**Après (P5)** :
```c
char *m3u8_content = download_m3u8(url);
lib_m3u8_parser_parse_zero_copy(m3u8_content, &playlist);
// ❌ NE PAS free(m3u8_content) tant que playlist est utilisée !

// Utiliser les segments...
for (...) { resolve_segment_url(&playlist, seg, m3u8_content, ...); }

free(m3u8_content);  // ✅ Seulement après usage des URLs
```

**Solutions** :
1. **Référence explicite** : Stocker `m3u8_content` dans playlist
   ```c
   typedef struct {
       char *m3u8_buffer;  // Pointeur vers buffer (à libérer plus tard)
       lib_m3u8_parser_segment_t segments[16];
       // ...
   } lib_m3u8_parser_playlist_t;
   ```

2. **API contractuelle** : Documenter clairement que l'utilisateur DOIT garder buffer vivant
   ```c
   /**
    * @brief Parse M3U8 sans copier les URLs (zero-copy)
    *
    * ⚠️ ATTENTION : Le buffer 'content' DOIT rester valide tant que
    * vous utilisez la playlist (URLs stockées par offset).
    *
    * @param content   Buffer M3U8 (NE PAS FREE avant usage playlist)
    * @param playlist  Playlist de sortie (contient offsets dans content)
    */
   esp_err_t lib_m3u8_parser_parse_zero_copy(const char *content,
                                               lib_m3u8_parser_playlist_t *playlist);
   ```

---

#### ⚠️ Contrainte 2 : URLs relatives doivent être résolues

**Problème** : Les URLs dans M3U8 peuvent être relatives.

**Exemple playlist** :
```m3u8
#EXTM3U
#EXTINF:10.0
segment001.ts     ← URL relative
#EXTINF:10.0
segment002.ts
```

**Avant (P0-P2)** :
```c
// resolve_url() est appelée pendant le parsing
resolve_url(base_url, "segment001.ts", seg->url, 160);
// seg->url = "https://cdn.example.com/stream/segment001.ts"
```

**Après (P5)** :
```c
// On ne peut pas stocker l'URL résolue (seulement offset)
// Il faut résoudre à la demande :
lib_m3u8_parser_resolve_segment_url(&playlist, seg, m3u8_content,
                                     full_url, sizeof(full_url));
```

**Solution** : Fonction helper dédiée
```c
/**
 * @brief Résout l'URL complète d'un segment (gère URLs relatives)
 *
 * @param playlist     Playlist contenant le segment
 * @param seg          Segment dont on veut l'URL
 * @param m3u8_content Buffer M3U8 source (pour extraire URL)
 * @param output       Buffer pour URL résolue
 * @param output_size  Taille du buffer output
 */
void lib_m3u8_parser_resolve_segment_url(
    const lib_m3u8_parser_playlist_t *playlist,
    const lib_m3u8_parser_segment_t *seg,
    const char *m3u8_content,
    char *output,
    size_t output_size
);
```

---

### Plan d'implémentation P5

#### Phase 1 : Nouvelle structure zero-copy (4h)

1. Définir nouvelle structure segment avec offsets :
   ```c
   typedef struct {
       uint16_t url_offset;  // Offset dans buffer M3U8
       uint16_t url_len;     // Longueur URL
       uint16_t duration_ms;
       uint32_t sequence;
       uint8_t flags;
   } lib_m3u8_parser_segment_zerocopy_t;
   ```

2. Ajouter champ `m3u8_buffer` à playlist pour garder référence

3. Créer fonction `lib_m3u8_parser_resolve_segment_url()`

#### Phase 2 : Parser zero-copy (4h)

1. Implémenter `lib_m3u8_parser_parse_zero_copy()`
   - Stocker offsets au lieu de copier URLs
   - Garder référence au buffer m3u8_content

2. Adapter extraction URLs pour utiliser vues (ptr + len)

#### Phase 3 : Adapter app_hls_player (6h)

1. Modifier `hls_fetch_task` pour :
   - Garder `m3u8_content` vivant jusqu'à fin téléchargement segments
   - Résoudre URLs à la demande avant `download_segment()`

2. Gestion mémoire :
   ```c
   char *m3u8_content = download_m3u8(url);
   lib_m3u8_parser_parse_zero_copy(m3u8_content, &playlist);

   // Télécharger tous les segments
   for (int i = 0; i < playlist.segment_count; i++) {
       char full_url[256];
       lib_m3u8_parser_resolve_segment_url(&playlist, &playlist.segments[i],
                                            m3u8_content, full_url, sizeof(full_url));
       download_segment(full_url, ...);
   }

   // Maintenant on peut libérer
   free(m3u8_content);
   lib_m3u8_parser_free(&playlist);
   ```

#### Phase 4 : Option Kconfig pour activer zero-copy (2h)

```kconfig
config M3U8_ZERO_COPY_URLS
    bool "Use zero-copy URLs (advanced, save ~2 KB RAM)"
    default n
    help
        Store URL offsets instead of copying URLs into playlist structure.

        Pros: Saves ~2 KB RAM (93% of segment size is URL)
        Cons: m3u8_content buffer must stay alive until URLs are used

        ⚠️ ADVANCED OPTION: Changes API contract, requires careful
        buffer lifetime management. Only enable if you understand
        the implications.
```

#### Phase 5 : Tests exhaustifs (4h)

1. Tests unitaires :
   - URLs relatives (segment.ts)
   - URLs absolutes (https://...)
   - URLs avec query params (?token=...)

2. Tests intégration :
   - Cycle complet parsing → resolve → download
   - Vérifier pas de dangling pointer (buffer freed trop tôt)
   - Valgrind / AddressSanitizer sur x86

3. Tests hardware ESP32-S3 :
   - Streaming France Inter 30+ minutes
   - Vérifier stabilité mémoire
   - Monitoring heap minimum

**Durée totale estimée** : 20h
**Risque** : **ÉLEVÉ** (changement API, gestion lifetime complexe)
**Gain** : **-84% RAM playlist** (3.2 KB → 0.5 KB)

---

## Récapitulatif P3-P4-P5

| Optimisation | Cible | Gain estimé | Complexité | Risque | Durée |
|--------------|-------|-------------|------------|--------|-------|
| **P3** : Stack réduite (vues) | Stack parsing | -640 bytes stack | Moyenne | Moyen | 4-6h |
| **P4** : Sliding window live | Heap indirect | -50% si MAX_ITEMS↓ | Moyenne | Moyen | 6-8h |
| **P5** : Zero-copy URLs | Heap direct | -84% (-2.5 KB) | Élevée | **Élevé** | 20h |

### Gains cumulés potentiels

```
État post-P2        : 3.2 KB playlist
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
+ P3 (stack)        : 3.2 KB playlist, -640B stack
+ P4 (sliding, 8→16): 1.6 KB playlist (-50%)
+ P5 (zero-copy)    : 0.5 KB playlist (-84% vs P2)

GAIN TOTAL P2→P5 : 2.7 KB saved sur playlist
GAIN GLOBAL vs baseline : 19.3 KB → 0.5 KB (-97.4%) 🚀
```

---

## Recommandations

### Ordre d'implémentation conseillé

1. **P3 d'abord** : Risque moyen, gain immédiat stack, prépare P5
2. **P4 si besoin live** : Seulement si streaming live pur (pas VOD)
3. **P5 en dernier** : Complexe, réservé si RAM critique (< 100 KB libre)

### Quand implémenter

- **P3** : Si stack overflow détecté ou si profiling montre hotspot dans parsing
- **P4** : Si RAM critique ET flux live uniquement (pas VOD)
- **P5** : **Seulement si RAM < 100 KB disponible** et après validation P0-P2 sur hardware

### Alternatives à considérer avant P5

Avant de faire P5 (complexe, risqué), considérer :

1. **Réduire MAX_ITEMS** : 16 → 8 items = -1.6 KB (via P2 Kconfig)
2. **Réduire MAX_URL_LEN** : 160 → 128 bytes = -512 bytes
3. **Combiner P2 + P4** : 8 items + sliding window = -50% vs 16 items

→ **P0+P1+P2 avec 8 items = 1.6 KB**, probablement suffisant pour la plupart des cas !

---

## Critères d'acceptation (si implémentation)

### P3 : Stack réduite
- [ ] `line[512]` supprimé, remplacé par vues (ptr + len)
- [ ] `value_buf[128]` supprimé, parsing direct avec vues
- [ ] Stack fonction parsing < 100 bytes
- [ ] Aucune régression parsing (même résultats que P2)
- [ ] Tests playlists complexes (master, variants, segments)

### P4 : Sliding window
- [ ] Option Kconfig `M3U8_LIVE_SLIDING_WINDOW`
- [ ] Ring buffer segments fonctionnel
- [ ] Détection nouveaux segments par séquence
- [ ] Pas de leaks si segments écrasés
- [ ] Tests simulation live (séquences croissantes)
- [ ] Intégration app_hls_player (recherche par seq)

### P5 : Zero-copy URLs
- [ ] Option Kconfig `M3U8_ZERO_COPY_URLS`
- [ ] Structure segment avec offsets (13 bytes)
- [ ] `lib_m3u8_parser_resolve_segment_url()` implémenté
- [ ] Documentation claire contraintes lifetime buffer
- [ ] Tests URLs relatives/absolues/avec params
- [ ] Valgrind clean (pas de dangling pointers)
- [ ] Stabilité 1h+ streaming sur ESP32-S3

---

## Fichiers à modifier (si implémentation)

### P3
- `lib_m3u8_parser.c` : Refactoring parsing avec vues
- Nouveaux helpers : `trim_view()`, `parse_float_view()`, `read_line_view()`

### P4
- `lib_m3u8_parser_types.h` : Ajout `write_index`, `base_sequence`
- `lib_m3u8_parser.c` : Logique sliding window
- `lib_m3u8_parser.h` : Fonctions `add_segment_sliding()`, `get_segment_by_seq()`
- `app_hls_player.c` : Adaptation recherche segments par séquence
- `Kconfig` : Option `M3U8_LIVE_SLIDING_WINDOW`

### P5
- `lib_m3u8_parser_types.h` : Nouvelle structure segment avec offsets
- `lib_m3u8_parser.c` : Parser zero-copy
- `lib_m3u8_parser.h` : `lib_m3u8_parser_resolve_segment_url()`
- `app_hls_player.c` : Gestion lifetime m3u8_content, résolution URLs à la demande
- `Kconfig` : Option `M3U8_ZERO_COPY_URLS`

---

## Conclusion

**État actuel (P0+P1+P2)** : Excellent, -83.4% RAM ✅

**P3-P4-P5** : Optimisations **avancées** pour cas extrêmes.

**Recommandation** :
1. Valider P0+P1+P2 sur hardware réel d'abord
2. Si RAM suffisante (> 100 KB libre), **ne pas implémenter P3-P5**
3. Si RAM critique, considérer **P2 tuning** (8 items) avant P5
4. Si vraiment nécessaire : **P3 → P4 → P5** dans cet ordre

**Le gain P0+P1+P2 est déjà exceptionnel** : 19.3 KB → 3.2 KB (-83.4%) 🎉

---

**Document créé** : 2025-02
**Auteur** : Équipe iobewi
**Statut** : TODO - Sprint futur (après validation P0+P1+P2 hardware)
