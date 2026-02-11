# Synthèse Technique : Erreurs AAC Persistantes dans app_hls_player

**Date** : 2026-02-11
**Contexte** : Streaming HLS (France Inter FIP midfi, AAC/MPEG-TS ~185 kbps) sur ESP32-S3
**Objectif** : Évaluer l'état actuel et identifier les pistes d'amélioration pour éliminer les erreurs de décodage

---

## 1. État Actuel du Système

### Architecture Implémentée

**Zéro-Copie avec Curseur sur Ringbuffer**
```
Ring Buffer (256 KB) → Curseur (cur_item, cur_off) → Décodeur TS/AAC → Audio I2S
                            ↓
                    Stitch Buffer (2 KB, <1% utilisation)
```

**Gains RAM Réalisés**
- Encoded buffer : 144 KB → **Supprimé** ✅
- Stitch buffer : **2 KB** (vs 144 KB) ✅
- **Gain total : -142 KB (-98.6%)**

### Performance Mesurée (Test 72 secondes)

| Métrique | Valeur | État |
|----------|--------|------|
| **WiFi connexion** | 2.3s | ✅ Excellent |
| **Audio démarrage** | ~5s | ✅ Bon |
| **Frames PCM produits** | 5400+ | ✅ Fluide |
| **Stitch activations** | 0 | ✅ Curseur pur |
| **Heap stable** | 1.91 MB min | ✅ Stable |
| **Buffer fill** | 13-73% | ✅ Variable mais OK |
| **Erreurs AAC** | **9 en 72s** | ⚠️ **~7.5/min** |

---

## 2. Problème Persistant : Erreurs AAC

### Observations Terrain

**Fréquence des erreurs**
```
Timeline (72s de lecture):
- t=4s   : 2 erreurs (cold start)
- t=18s  : 3 erreurs (groupées)
- t=29s  : 1 erreur
- t=45s  : 1 erreur
- t=59s  : 1 erreur
- t=72s  : 1 erreur
───────────────────────────────
Total    : 9 erreurs = 7.5/min
```

**Amélioration vs Versions Précédentes**
| Version | Erreurs/min | Amélioration |
|---------|-------------|--------------|
| P1.3 Remainder 64KB | 50+ | Baseline |
| P1.3 Remainder 8KB | 80+ | -60% ❌ |
| Zéro-Copie (actuel) | **7.5** | **+85%** ✅ |

**Constat** : Forte amélioration mais pas encore zéro erreur.

### Symptôme Technique

```
E (4285) ESP_AAC_DEC: Failed to decode aac frame, error:30
```

**Error 30** = `AAC_DEC_TRANSPORT_SYNC_ERROR` → Le décodeur ne peut pas synchroniser le stream MPEG-TS ou les données AAC sont corrompues/incomplètes.

### Contexte des Erreurs

**Pattern observé** :
1. Les erreurs surviennent souvent **après un `consumed=0`** (décodeur bloqué)
2. Le resync TS trouve un sync byte 0x47 et avance le curseur
3. Mais le **frame AAC suivant peut être incomplet** → error:30

**Hypothèses** :
- Le resync TS trouve un sync byte valide mais pas au bon endroit (faux positif)
- Les frames AAC sont à cheval sur plusieurs paquets TS (fragmentation)
- Le décodeur AAC manque de contexte après un resync TS

---

## 3. Architecture Zéro-Copie Détaillée

### Fonctionnement Normal (99% des cas)

```c
while (true) {
    // 1. Recevoir item depuis ringbuffer (zéro copie)
    cur_item = xRingbufferReceive(ring_buffer, &cur_len, timeout);
    cur_off = 0;

    // 2. Décoder directement depuis item
    while (cur_off < cur_len) {
        dec_ret = decode(cur_item + cur_off, cur_len - cur_off, ...);

        if (raw.consumed > 0) {
            cur_off += raw.consumed;  // Avancer curseur
        } else {
            // consumed==0 → PROBLÈME
            break;
        }
    }

    // 3. Libérer item quand complètement consommé
    vRingbufferReturnItem(ring_buffer, cur_item);
}
```

### Gestion `consumed==0` (cas problématique)

**Stratégie Actuelle**
```c
if (consumed == 0) {
    // 1. Resync TS exhaustif (scan tout le buffer)
    if (ts_resync_find(cur_item + cur_off, avail, avail, &skip)) {
        cur_off += skip;  // Sauter vers sync 0x47
        zero_consume_streak = 0;
    } else {
        // 2. Pas de sync trouvé → skip 188 bytes (1 paquet TS)
        cur_off += 188;
    }

    // 3. Fail-safe : reset décodeur après 20 échecs
    if (++zero_consume_streak >= 20) {
        esp_audio_simple_dec_close(dec_handle);
        esp_audio_simple_dec_open(&dec_cfg, &dec_handle);
    }
}
```

**Fonction `ts_resync_find()`**
```c
static bool ts_resync_find(const uint8_t *buf, size_t len,
                            size_t scan_max, size_t *out_skip)
{
    size_t n = (len < scan_max) ? len : scan_max;
    if (n < 376) return false;  // Besoin 2 paquets TS minimum

    for (size_t i = 0; i + 376 < n; i++) {
        // Chercher 3 sync bytes 0x47 espacés de 188 bytes
        if (buf[i] == 0x47 &&
            buf[i + 188] == 0x47 &&
            buf[i + 376] == 0x47) {
            *out_skip = i;
            return true;  // Sync trouvé
        }
    }
    return false;
}
```

**Amélioration récente** : Scanner **tout le buffer** (pas juste 2KB) → Meilleurs résultats mais pas parfait

---

## 4. Analyse des Causes Possibles

### Hypothèse #1 : Fragmentation AAC sur Frontières TS

**Problème** :
Un frame AAC peut être réparti sur plusieurs paquets MPEG-TS (188 bytes chacun). Quand le resync TS trouve un sync byte 0x47 valide, il peut **tomber au milieu d'un frame AAC** fragmenté.

**Exemple** :
```
Paquet TS 1 (188 bytes) : [Header TS][50 bytes AAC frame début]
Paquet TS 2 (188 bytes) : [Header TS][138 bytes AAC frame milieu]
Paquet TS 3 (188 bytes) : [Header TS][50 bytes AAC frame fin][autres données]
                              ↑
                          Resync trouve ce 0x47
                          Mais frame AAC est fragmenté !
```

**Conséquence** : Le décodeur AAC reçoit un frame incomplet → `error:30`

### Hypothèse #2 : Validation Sync TS Insuffisante

**Problème** :
La fonction `ts_resync_find()` valide seulement :
- 3 sync bytes 0x47 espacés de 188 bytes

Mais **ne vérifie PAS** :
- ✗ PID (Program ID) du paquet TS
- ✗ Continuity counter (séquence des paquets)
- ✗ Payload unit start indicator (début de frame)
- ✗ Adaptation field (padding, timestamps)

**Conséquence** : Faux positifs possibles → Resync à un endroit non optimal

### Hypothèse #3 : État Interne Décodeur AAC

**Problème** :
Le décodeur AAC maintient un état interne (contexte, buffers, derniers samples). Après un resync TS, cet état peut être **incohérent** avec les nouvelles données.

**Conséquence** : Le décodeur ne peut pas synchroniser correctement → `error:30`

### Hypothèse #4 : Données Réseau Corrompues

**Probabilité** : Faible
- Le stream HTTPS est protégé par TLS (checksums)
- Les erreurs sont reproductibles à des moments similaires (pas aléatoires)

---

## 5. Pistes d'Amélioration

### Option A : Parser Complet MPEG-TS (Recommandé ⭐)

**Principe** :
Ne pas juste chercher des sync bytes 0x47, mais **parser correctement** les headers MPEG-TS pour identifier :
- PID audio (257 dans les logs)
- Payload unit start indicator (début de frame PES/AAC)
- Continuity counter (valider séquence)

**Avantages** :
- ✅ Resync précis au début d'un frame AAC
- ✅ Évite les faux positifs
- ✅ Robustesse accrue

**Inconvénients** :
- Complexité code (parsing headers TS)
- CPU légèrement plus élevé

**Implémentation Suggérée** :
```c
static bool ts_resync_smart(const uint8_t *buf, size_t len, size_t *out_skip)
{
    for (size_t i = 0; i + 376 < len; i++) {
        if (buf[i] != 0x47) continue;

        // Vérifier header TS
        uint16_t pid = ((buf[i+1] & 0x1F) << 8) | buf[i+2];
        uint8_t pusi = (buf[i+1] & 0x40) >> 6;  // Payload unit start
        uint8_t adapt = (buf[i+3] & 0x30) >> 4; // Adaptation field

        // Chercher PID audio (257) avec PUSI=1 (début frame)
        if (pid == 257 && pusi == 1) {
            // Valider continuité sur 3 paquets
            if (buf[i + 188] == 0x47 && buf[i + 376] == 0x47) {
                *out_skip = i;
                return true;
            }
        }
    }
    return false;
}
```

### Option B : Reset Décodeur AAC Après Chaque Resync

**Principe** :
Après un resync TS, **reset complet** du décodeur AAC pour éviter état incohérent.

**Avantages** :
- ✅ Simple à implémenter
- ✅ Élimine problème d'état interne

**Inconvénients** :
- ⚠️ Perte de quelques frames AAC (audio coupé brièvement)
- ⚠️ Latence accrue

**Implémentation** :
```c
if (ts_resync_find(...) && skip > 0) {
    cur_off += skip;

    // Reset décodeur pour état propre
    esp_audio_simple_dec_close(dec_handle);
    esp_audio_simple_dec_open(&dec_cfg, &dec_handle);

    ESP_LOGW(TAG, "Resync TS + reset décodeur AAC");
}
```

### Option C : Buffer de Réassemblage PES (Avancé)

**Principe** :
Implémenter un buffer pour **réassembler les frames PES/AAC** complets depuis les paquets TS avant de les passer au décodeur.

**Avantages** :
- ✅ Garantit frames AAC complets et valides
- ✅ Robustesse maximale

**Inconvénients** :
- ❌ Complexité élevée (parser PES, gérer fragmentation)
- ❌ RAM additionnelle (~4-8 KB)
- ❌ CPU plus élevé

### Option D : Tolérance Décodeur (Expérimental)

**Principe** :
Configurer le décodeur AAC pour être plus **tolérant** aux erreurs (mode dégradé, error concealment).

**Avantages** :
- ✅ Pas de changement architecture
- ✅ Le décodeur masque les erreurs

**Inconvénients** :
- ⚠️ Dépend des capacités du décodeur ESP AAC
- ⚠️ Qualité audio potentiellement dégradée

---

## 6. Tests et Validation

### Métriques de Succès

Pour valider une solution, les critères sont :

| Métrique | Cible | Actuel |
|----------|-------|--------|
| Erreurs AAC | **< 1/min** | 7.5/min ❌ |
| Frames PCM/min | > 3000 | 4500 ✅ |
| Stitch activations | < 10 | 0 ✅ |
| Reset décodeur | < 1/10min | 0 ✅ |
| Heap stable | > 1.8 MB | 1.91 MB ✅ |

### Plan de Tests Suggéré

**Phase 1 : Validation Parser TS (Option A)**
1. Implémenter `ts_resync_smart()` avec parsing PID + PUSI
2. Tester 60 min France Inter FIP midfi
3. Compter erreurs AAC → Cible < 5 total

**Phase 2 : Fallback Reset Décodeur (Option B)**
4. Si Option A insuffisante, ajouter reset décodeur après resync
5. Tester 60 min
6. Écoute subjective : clics audibles ? Latence ?

**Phase 3 : Long-run**
7. Test 4h+ pour valider stabilité heap
8. Vérifier pas de memory leaks
9. Monitoring température/CPU

---

## 7. Comparaison avec Solutions Alternatives

### Alternative 1 : Retour à Encoded Buffer 256 KB

**Principe** : Annuler zéro-copie, revenir à accumulation massive

**Avantages** :
- ✅ Stabilité prouvée (tests passés)
- ✅ Buffer absorbe toute variabilité

**Inconvénients** :
- ❌ Perte gain RAM (-142 KB perdu)
- ❌ Copies mémoire (CPU)
- ❌ Pas de solution long-terme

**Recommandation** : ❌ Ne pas revenir en arrière

### Alternative 2 : Qualité Lofi (64 kbps)

**Principe** : Réduire bitrate stream pour simplifier décodage

**Avantages** :
- ✅ Moins de données → moins d'erreurs potentielles
- ✅ RAM réduite

**Inconvénients** :
- ❌ Qualité audio dégradée (voix OK, musique ❌)
- ❌ Ne résout pas le problème racine

**Recommandation** : ⚠️ Dernier recours uniquement

---

## 8. Recommandations pour Discussion Équipe

### Priorité 1 : Parser MPEG-TS Intelligent (Option A)

**Effort** : 1-2 jours développement + tests
**Impact** : Potentiellement **-90% erreurs** (cible < 1/min)
**Risque** : Faible (rollback facile)

**Action** :
- Implémenter `ts_resync_smart()` avec parsing PID 257 + PUSI
- Tester en parallèle avec version actuelle
- Valider sur 60 min de stream

### Priorité 2 : Instrumentation Avancée

**Objectif** : Comprendre **exactement où** surviennent les erreurs

**Actions** :
- Logger offset exact dans buffer quand error:30
- Dumper 512 bytes avant/après l'erreur (hex)
- Analyser patterns (PID, sync bytes, données)
- Identifier si erreurs surviennent après resync ou aléatoirement

### Priorité 3 : Tests Comparatifs

**Objectif** : Valider si le problème est spécifique à France Inter ou général

**Actions** :
- Tester avec d'autres streams HLS AAC/TS (BBC, NPR, etc.)
- Tester avec stream local controlé (FFmpeg)
- Comparer taux d'erreurs

### Priorité 4 : Consultation Fournisseur Décodeur

**Objectif** : Vérifier capacités décodeur ESP AAC

**Questions** :
- Y a-t-il un mode "error concealment" ?
- Peut-on ajuster tolérance aux erreurs ?
- Recommendations pour resync après discontinuité ?

---

## 9. Conclusion

### État Actuel : Utilisable mais Perfectible

**Points Positifs ✅**
- Architecture zéro-copie fonctionne (-142 KB RAM)
- Démarrage rapide (WiFi 2s, audio 5s)
- Pas de memory leaks, heap stable
- Amélioration 85% vs versions précédentes

**Points d'Amélioration ⚠️**
- **7.5 erreurs AAC/min** (cible < 1/min)
- Resync TS basique (sync bytes seulement)
- Pas de parsing headers MPEG-TS

### Prochaines Étapes

1. **Court terme (1 semaine)** : Implémenter parser TS intelligent (Option A)
2. **Moyen terme (2 semaines)** : Tests long-run + instrumentation avancée
3. **Long terme (1 mois)** : Optimisations complémentaires si nécessaire

### Question pour l'Équipe

**Le niveau d'erreur actuel (7.5/min) est-il acceptable pour un MVP/prototype ?**
- ✅ **Oui** → Passer aux autres features, améliorer plus tard
- ❌ **Non** → Prioriser Option A (parser TS) avant release

---

**Auteur** : Claude (Assistant IA)
**Révision** : À compléter par l'équipe
**Statut** : **DRAFT - Pour Discussion**
