# 📐 Architecture applicative – Intégration A02YYUW (ultrason) + micro-ROS

## 1. Objet

Ce document décrit l'architecture applicative cible pour intégrer les capteurs ultrasoniques **A02YYUW** (4 capteurs à 90°) dans l'écosystème **iobewi-idf-components** avec **micro-ROS**, en respectant strictement le **CDC** du framework.

## 2. Contexte & objectifs

- **Contexte** : ajout d'un driver `drv_a02yyuw` pour capteurs ultrasoniques A02YYUW (SEN0311).
- **Objectif principal** : produire un `sensor_msgs/LaserScan` micro-ROS, en réutilisant au maximum les briques existantes (`mw_scan_builder`, `mw_uros_core`) et sans violer la taxonomie CDC.
- **Contraintes clés** :
  - respecter la séparation **drv/lib/mw/app**
  - aucune dépendance micro-ROS dans les drivers
  - aucune logique applicative dans les middlewares

## 3. Règles CDC à respecter

Règles structurantes (extraites du CDC) :

- **Taxonomie** : `drv_*` (hardware), `lib_*` (utils), `mw_*` (micro-ROS), `app_*` (métier).
- **Dépendances autorisées** : `drv_* → lib_* → mw_* → app_*`.
- **Interdictions** : dépendances inverses, logique métier dans `mw_*`, micro-ROS dans `drv_*`.

## 4. Architecture cible (vue composant)

```
┌───────────────────────────────────────────────────────────────┐
│                         mw_uros_core                          │
│            (gestion session + publication micro-ROS)          │
└───────────────────────────┬───────────────────────────────────┘
                            │ callbacks app_* (init/step/fini)
                            ▼
┌───────────────────────────────────────────────────────────────┐
│                       app_scan_ultra                          │
│  - orchestre le scan                                          │
│  - récupère snapshot A02                                      │
│  - remplit LaserScan via mw_scan_builder                      │
└───────────────┬─────────────────────────────┬─────────────────┘
                │                             │
                ▼                             ▼
┌─────────────────────────┐     ┌───────────────────────────────┐
│   lib_a02_provider      │     │        mw_scan_builder        │
│ - convertit A02 → sample│     │ - construit sensor_msgs/       │
│ - médiane + filtrage    │     │   LaserScan                   │
└───────────────┬─────────┘     └───────────────────────────────┘
                │
                ▼
┌─────────────────────────┐
│       drv_a02yyuw       │
│ - UART + GPIO           │
│ - multi-capteurs        │
└─────────────────────────┘
```

### Notes d'implémentation
- **`drv_a02yyuw`** reste un driver pur (UART + GPIO) sans micro-ROS.
- **`lib_a02_provider`** transforme les lectures A02 en échantillons compatibles avec le builder (structure de type `ultrasonic_sample_t`).
- **`mw_scan_builder`** est réutilisé tel quel pour fabriquer le `LaserScan`.
- **`app_scan_ultra`** orchestre la logique métier et fournit les callbacks à `mw_uros_core`.

## 5. Flux de données

1. `app_scan_ultra` déclenche un cycle de scan.
2. `lib_a02_provider` :
   - sélectionne chaque capteur A02 (power gating),
   - jette la première mesure,
   - lit 3 mesures, applique une médiane,
   - convertit en mètres et remplit un sample.
3. `app_scan_ultra` mappe chaque capteur dans un **bin angulaire** du LaserScan.
4. `mw_scan_builder` construit le message `sensor_msgs/LaserScan`.
5. `mw_uros_core` publie le message `LaserScan`.

## 6. Mapping angulaire (4 capteurs à 90°)

Hypothèse : capteurs espacés de 90° sur le robot (0°, 90°, 180°, 270°).

### Option recommandée : bins = 36 (résolution 10°)

| Capteur | Angle | Bin | Distance entre bins |
|---------|-------|-----|---------------------|
| 0       | 0°    | 0   | 10° par bin         |
| 1       | 90°   | 9   | 10° par bin         |
| 2       | 180°  | 18  | 10° par bin         |
| 3       | 270°  | 27  | 10° par bin         |

**Configuration LaserScan** :
```c
angle_min = 0.0          // 0°
angle_max = 2*PI         // 360°
angle_increment = 0.1745 // 10° en radians (PI/18)
ranges[36]               // 36 bins, bins non observés = NAN
```

**Avantages** :
- ✅ Compatible avec les conventions LaserScan ROS (résolution 10°)
- ✅ Bonne visualisation dans RViz
- ✅ Bins non observés marqués comme `NAN` (acceptable)

**Alternative** : bins = 4 (minimaliste, 1 bin par capteur) si la bande passante est limitée.

## 7. API & responsabilités

### 7.1 `drv_a02yyuw`

**Responsabilité** : lecture UART + gestion GPIO (EN + mode)

**API publique** :
```c
esp_err_t drv_a02yyuw_new(const drv_a02yyuw_config_t *config, drv_a02yyuw_t **out);
esp_err_t drv_a02yyuw_select(drv_a02yyuw_t *handle, int sensor_id, drv_a02yyuw_mode_t mode);
esp_err_t drv_a02yyuw_read(drv_a02yyuw_t *handle, uint16_t *distance_mm, uint32_t timeout_ms);
esp_err_t drv_a02yyuw_del(drv_a02yyuw_t *handle);
```

**Contraintes** :
- ❌ Aucune dépendance micro-ROS
- ❌ Aucune logique métier (médiane, filtrage)

### 7.2 `lib_a02_provider`

**Responsabilité** : convertir lectures A02 → échantillons ultrason (mètres + validité)

**API publique** :
```c
typedef struct {
    float distance_m;     // Distance en mètres
    bool valid;           // Mesure valide
    uint32_t timestamp;   // Timestamp optionnel (µs depuis boot)
} ultrasonic_sample_t;

typedef struct {
    drv_a02yyuw_t *driver;       // Handle du driver A02
    uint8_t sensor_count;         // Nombre de capteurs (4)
    uint8_t median_filter_size;   // Taille du filtre médian (3 par défaut)
    float range_min_m;            // Distance min valide (0.3m)
    float range_max_m;            // Distance max valide (4.5m)
    drv_a02yyuw_mode_t mode;      // Mode de lecture (REALTIME ou PROCESSED)
} lib_a02_provider_config_t;

typedef struct lib_a02_provider_s lib_a02_provider_t;

// Initialiser la configuration avec valeurs par défaut
esp_err_t lib_a02_provider_config_init(lib_a02_provider_config_t *config);

// Créer une instance du provider
esp_err_t lib_a02_provider_new(const lib_a02_provider_config_t *config,
                                lib_a02_provider_t **out);

// Lire un snapshot de tous les capteurs
// samples[sensor_count] doit être alloué par l'appelant
esp_err_t lib_a02_provider_read_snapshot(lib_a02_provider_t *provider,
                                          ultrasonic_sample_t *samples,
                                          size_t count);

// Détruire l'instance
esp_err_t lib_a02_provider_del(lib_a02_provider_t *provider);
```

**Logique incluse** :
- ✅ Rejet première trame après power-up
- ✅ Filtre médian sur N mesures (3 par défaut)
- ✅ Conversion mm → mètres
- ✅ Validation range min/max
- ✅ Gestion timeout/erreurs

**Contraintes** :
- ❌ Aucune dépendance micro-ROS
- ❌ Aucune connaissance du mapping angulaire (rôle de `app_*`)

### 7.3 `mw_scan_builder`

**Responsabilité** : construction `sensor_msgs/LaserScan` à partir d'échantillons

**API existante** (à vérifier/adapter si nécessaire) :
```c
// Vérifier si mw_scan_builder accepte un type générique ou seulement tof_sample_t
// Si besoin, créer un adaptateur dans app_scan_ultra
```

**Contraintes** :
- ❌ Aucune logique métier
- ✅ Peut dépendre de `drv_*` et `lib_*` (mais pas ici)

### 7.4 `app_scan_ultra`

**Responsabilité** : orchestration scan + callbacks micro-ROS

**Configuration** :
```c
typedef struct {
    // Provider config
    lib_a02_provider_config_t provider_config;

    // Scan builder config
    uint8_t bins;                    // Nombre de bins LaserScan (36 recommandé)
    float angle_min;                 // 0.0 (0°)
    float angle_max;                 // 2*PI (360°)
    float range_min;                 // 0.3m
    float range_max;                 // 4.5m

    // Mapping capteurs → bins
    uint8_t sensor_bin_mapping[4];   // Ex: {0, 9, 18, 27} pour 36 bins

    // micro-ROS
    const char *frame_id;            // "base_link"
} app_scan_ultra_config_t;
```

**Callbacks micro-ROS** :
```c
bool app_scan_ultra_init(void *ctx);
bool app_scan_ultra_step(void *ctx, void *ros_msg);
void app_scan_ultra_fini(void *ctx);
```

**Logique** :
- ✅ Orchestration : `lib_a02_provider` + `mw_scan_builder`
- ✅ Mapping capteurs → bins angulaires
- ✅ Remplissage bins non observés avec `NAN`

### 7.5 Gestion d'erreurs

| Composant | Erreur | Comportement |
|-----------|--------|--------------|
| `drv_a02yyuw` | Timeout UART | Retourne `ESP_ERR_TIMEOUT` |
| `lib_a02_provider` | Timeout capteur | Marque `sample.valid = false` |
| `lib_a02_provider` | Hors range | Marque `sample.valid = false` |
| `app_scan_ultra` | Sample invalide | Remplit bin avec `NAN` |
| `app_scan_ultra` | Tous capteurs en erreur | Log `LOGW`, publie quand même (bins à NAN) |

**Logging** :
- `ESP_LOGE` : erreurs critiques (init failed, allocation failed)
- `ESP_LOGW` : timeouts, mesures hors range
- `ESP_LOGI` : événements normaux (scan complete)
- `ESP_LOGD` : debug (mesures brutes)

## 8. Considérations de performances

### Timing de scan séquentiel (UART 9600 bps)

| Opération | Durée |
|-----------|-------|
| Lecture 1 trame A02 | ~4ms (4 octets @ 9600 bps) |
| Power-down | 10ms |
| Mode settle | 2ms |
| Power-up | 20ms |
| **Total switching** | **32ms** |
| Lecture 3 mesures + médiane | ~12ms |
| **Total par capteur** | **~44ms** |
| **Total scan 4 capteurs** | **~176ms** |

**Fréquence de publication** : ~**5.7 Hz max** (limité par l'UART séquentiel)

### Optimisations possibles

1. **Réduire délais power cycling** : tester stabilité avec délais plus courts
2. **Mode REALTIME** : si le mode PROCESSED n'est pas nécessaire
3. **Parallélisation partielle** : exécuter d'autres tâches pendant les delays
4. **UART plus rapide** : vérifier si A02YYUW supporte 19200 bps (non standard)

### Budget CPU

- Scan + publication : <10% CPU @ 240 MHz (estimation)
- Compatible avec autres tâches temps réel (ToF, IMU, etc.)

## 9. Compatibilité avec `mw_scan_builder`

### Option 1 : Adaptateur dans `app_scan_ultra`

Si `mw_scan_builder` accepte uniquement `tof_sample_t`, créer un adaptateur :

```c
// Dans app_scan_ultra
tof_sample_t tof_samples[4];
for (int i = 0; i < 4; i++) {
    tof_samples[i].distance_mm = ultrasonic_samples[i].distance_m * 1000.0f;
    tof_samples[i].valid = ultrasonic_samples[i].valid;
}
```

### Option 2 : Généraliser `mw_scan_builder`

Modifier `mw_scan_builder` pour accepter un type générique `scan_sample_t` :

```c
typedef struct {
    float distance_m;
    bool valid;
} scan_sample_t;
```

**Recommandation** : **Option 1** (adaptateur) pour éviter de modifier `mw_scan_builder` existant.

## 10. Points ouverts – Décisions finales

| Point ouvert | Décision |
|--------------|----------|
| **Frame ROS** | `base_link` (convention ROS standard) |
| **Range min/max** | min=0.3m, max=4.5m (datasheet A02YYUW) |
| **Fréquence cible** | ~5Hz (limité par UART 9600 bps séquentiel) |
| **Bins LaserScan** | 36 bins (résolution 10°) avec bins non observés = `NAN` |
| **Mode A02YYUW** | `DRV_A02YYUW_MODE_PROCESSED` (filtré par capteur) |
| **Filtre médiane** | 3 mesures (bon compromis vitesse/précision) |

## 11. Plan d'implémentation

### Phase 1 : `lib_a02_provider` (Priorité 1)

**Livrables** :
1. Structure du composant :
   ```
   lib_a02_provider/
   ├── CMakeLists.txt
   ├── idf_component.yml
   ├── README.md
   ├── include/lib_a02_provider/
   │   ├── lib_a02_provider.h
   │   └── lib_a02_provider_types.h
   ├── src/
   │   └── lib_a02_provider.c
   └── examples/
       └── basic_app/
   ```

2. Implémentation :
   - API `config_init`, `new`, `read_snapshot`, `del`
   - Logique médiane (fonction réutilisable)
   - Validation range min/max
   - Gestion timeout/erreurs

3. Tests :
   - Exemple standalone (sans micro-ROS)
   - Validation sur 4 capteurs
   - Mesure de performance (timing)

**Durée estimée** : 1-2 jours

### Phase 2 : `app_scan_ultra` (Priorité 2)

**Livrables** :
1. Structure du composant :
   ```
   app_scan_ultra/
   ├── CMakeLists.txt
   ├── idf_component.yml
   ├── README.md
   ├── include/app_scan_ultra/
   │   ├── app_scan_ultra.h
   │   └── app_scan_ultra_types.h
   ├── src/
   │   └── app_scan_ultra.c
   └── examples/
       └── basic_app/
   ```

2. Implémentation :
   - Callbacks `app_init`, `app_step`, `app_fini`
   - Intégration `lib_a02_provider` + `mw_scan_builder`
   - Mapping capteurs → bins angulaires
   - Remplissage bins avec `NAN` si non observés
   - Configuration via struct (pas de hardcode)

3. Tests :
   - Compilation avec `mw_uros_core`
   - Validation sur matériel (4 capteurs)
   - Visualisation RViz (topic `/scan`)

**Durée estimée** : 2-3 jours

### Phase 3 : Documentation & validation (Priorité 3)

**Livrables** :
1. README détaillés pour `lib_a02_provider` et `app_scan_ultra`
2. Schémas de câblage (4 capteurs + STMPS2141STR)
3. Guide d'intégration ROS 2
4. Mesures de performance réelles
5. Vidéo/captures RViz

**Durée estimée** : 1 jour

## 12. Diagramme de séquence (runtime)

```
app_scan_ultra          lib_a02_provider         drv_a02yyuw
      |                        |                       |
      |-- read_snapshot() ---->|                       |
      |                        |-- select(0) --------->|
      |                        |                       |-- GPIO EN[0]=1
      |                        |                       |-- delay 20ms
      |                        |<-- ESP_OK ------------|
      |                        |-- read() ------------>|
      |                        |<-- throwaway ---------|
      |                        |-- read() ------------>|
      |                        |<-- 1250mm ------------|
      |                        |-- read() ------------>|
      |                        |<-- 1248mm ------------|
      |                        |-- read() ------------>|
      |                        |<-- 1252mm ------------|
      |                        |   [médiane: 1250mm]   |
      |                        |                       |
      |                        |-- select(1) --------->|
      |                        |   ... (idem) ...      |
      |                        |                       |
      |<-- samples[4] ---------|                       |
      |                        |                       |
      |-- mw_scan_builder() -->|                       |
      |<-- LaserScan msg ------|                       |
      |                        |                       |
      |-- mw_uros_core ------->| (publication)         |
```

## 13. Checklist de conformité CDC

- [x] `drv_a02yyuw` sans dépendance micro-ROS
- [x] `lib_a02_provider` sans dépendance micro-ROS
- [x] Aucune logique métier dans `mw_*`
- [x] Hiérarchie de dépendances respectée
- [x] Pattern handle opaque (`_new`/`_del`)
- [x] API `esp_err_t` standard
- [x] Configuration via struct (pas de hardcode)
- [x] Documentation complète
- [x] Exemples fonctionnels

---

✅ **Cette architecture est validée et prête pour l'implémentation.**

**Prochaine étape** : Démarrer Phase 1 (`lib_a02_provider`)
