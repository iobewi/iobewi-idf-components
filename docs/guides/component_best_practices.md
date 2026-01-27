# 📘 Guide des Bonnes Pratiques - Composants iobewi-idf-components

**Version 1.0** | Référence pour la création et l'harmonisation des composants

Ce document définit les **bonnes pratiques obligatoires** pour tous les composants du framework `iobewi-idf-components`. Il sert de référence pour :
- ✅ Créer de nouveaux composants conformes
- ✅ Auditer les composants existants
- ✅ Harmoniser la "grammaire" des composants

---

## 🎯 Objectifs

1. **Cohérence architecturale** : Tous les composants partagent la même structure
2. **Maintenabilité** : Code industriel avec ≥ 5 ans de durée de vie
3. **Conformité CDC** : Respect strict du Cahier des Charges
4. **Zéro dette technique** : Qualité sans compromis

---

## 📚 Table des Matières

1. [Structure Canonique d'un Composant](#structure-canonique)
2. [Taxonomie et Séparation des Responsabilités](#taxonomie)
3. [Pattern Handle Opaque](#pattern-handle-opaque)
4. [Conventions de Nommage](#conventions-nommage)
5. [Organisation des Headers](#organisation-headers)
6. [Configuration CMakeLists.txt](#configuration-cmake)
7. [Documentation et Exemples](#documentation-exemples)
8. [Checklist de Conformité](#checklist-conformite)
9. [Exemples Conformes vs Non-Conformes](#exemples)
10. [Plan d'Harmonisation](#plan-harmonisation)

---

<a name="structure-canonique"></a>
## 1. 📁 Structure Canonique d'un Composant

### Structure Obligatoire

```
<component_name>/
├── CMakeLists.txt              # ✅ OBLIGATOIRE
├── idf_component.yml           # ✅ OBLIGATOIRE
├── README.md                   # ✅ OBLIGATOIRE
├── Kconfig                     # ⚠️ OPTIONNEL (si configuration nécessaire)
├── include/
│   └── <component_name>/       # ✅ OBLIGATOIRE : namespace isolé
│       ├── <component_name>.h          # ✅ OBLIGATOIRE : API publique
│       └── <component_name>_types.h    # ✅ OBLIGATOIRE : Types publics
├── src/
│   └── <component_name>.c      # ✅ OBLIGATOIRE : Implémentation
└── examples/
    └── basic_app/              # ✅ OBLIGATOIRE : Exemple fonctionnel
        ├── CMakeLists.txt
        ├── sdkconfig.defaults
        ├── README.md           # ⚠️ RECOMMANDÉ
        └── main/
            ├── CMakeLists.txt
            ├── main.c
            └── Kconfig.projbuild  # ⚠️ OPTIONNEL
```

### Règles Strictes

#### ✅ Headers Publics

- **Namespace isolé** : `include/<component_name>/`
- **Fichier types** : `<component_name>_types.h` TOUJOURS présent
- **Fichier API** : `<component_name>.h` contient les fonctions publiques

**Rationale** : Évite les collisions de noms et clarifie l'API publique

#### ✅ Implémentation

- **Un seul fichier source** : `src/<component_name>.c` (sauf cas complexe justifié)
- **Structs privées** : Définies dans le `.c`, jamais exposées dans les headers publics

**Rationale** : Simplifie la maintenance et force l'encapsulation

#### ✅ Exemple Fonctionnel

- **OBLIGATOIRE** : `examples/basic_app/` avec exemple compilable
- **README.md** : Documentation d'utilisation de l'exemple
- **sdkconfig.defaults** : Configuration pré-remplie

**Rationale** : Garantit que le composant est utilisable et testé

---

<a name="taxonomie"></a>
## 2. 🏷️ Taxonomie et Séparation des Responsabilités

### Règle de Dépendance (Non Négociable)

```
drv_*  →  lib_*  →  mw_*  →  app_*
```

### 🔧 `drv_*` — Drivers Matériels

**Responsabilité** : Pilotage matériel pur (I2C, SPI, GPIO, UART…)

**Autorisé** :
- ✅ Accès direct au matériel via ESP-IDF drivers
- ✅ Configuration hardware (GPIO, baudrate, timing)
- ✅ Gestion d'état matériel (power, mode)

**Interdit** :
- ❌ **Dépendance micro-ROS** : Aucun include de rcl/rclc
- ❌ **Logique métier** : Pas de filtrage, pas de traitement de données
- ❌ **Abstractions de haut niveau** : Pas de "provider", "manager", "snapshot"

**Exemple conforme** : `drv_a02yyuw`
- Gère UART, GPIO EN, GPIO Mode
- Retourne distance brute en millimètres
- Pas de filtrage, pas de conversion

**Exemple NON conforme** : `drv_vl53l0x` (voir section 9)
- ❌ Contient `tof_provider.h` (abstraction lib-like)
- ❌ Contient `tof_config.h` (configuration lib-like)
- ❌ Contient `tof_snapshot.h` (utilities lib-like)

### 📚 `lib_*` — Bibliothèques Utilitaires

**Responsabilité** : Code réutilisable générique sans micro-ROS

**Autorisé** :
- ✅ Abstractions fonctionnelles (providers, managers, utilities)
- ✅ Dépendances sur `drv_*`
- ✅ Traitement de données (filtrage, conversion)
- ✅ Logique algorithmique réutilisable

**Interdit** :
- ❌ **Accès matériel direct** : Utiliser `drv_*` comme interface
- ❌ **Dépendance micro-ROS** : Aucun include de rcl/rclc

**Exemple conforme** : `lib_a02_provider`
- Utilise `drv_a02yyuw` pour accéder aux capteurs
- Implémente filtrage médian
- Retourne échantillons avec métadonnées

**Ce qui devrait être dans `lib_*` (extrait de `drv_vl53l0x`)** :
- `tof_provider` : Abstraction multi-capteurs → devrait être `lib_vl53l0x_provider`
- `tof_config` : Configuration de haut niveau → devrait être dans `lib_*`
- `tof_snapshot` : Utilitaires snapshot → devrait être dans `lib_*`

### 🔌 `mw_*` — Middleware micro-ROS

**Responsabilité** : Intégration micro-ROS (builders, helpers rcl/rclc)

**Autorisé** :
- ✅ **Intégration micro-ROS obligatoire** : rcl/rclc includes
- ✅ Dépendances sur `drv_*` et `lib_*`
- ✅ Builders de messages ROS
- ✅ Helpers rcl/rclc génériques

**Interdit** :
- ❌ **Accès matériel direct** : Utiliser `drv_*`
- ❌ **Logique applicative** : Pas d'orchestration métier

**Exemple conforme** : `mw_scan_builder`
- Builder générique pour `sensor_msgs/LaserScan`
- Pas de connaissance des capteurs spécifiques

**Exemple conforme** : `mw_uros_core`
- Infrastructure micro-ROS générique
- Gestion connexion, pub/sub, LED status

### 🚀 `app_*` — Composants Métier

**Responsabilité** : Logique fonctionnelle réutilisable

**Autorisé** :
- ✅ Orchestration de `drv_*`, `lib_*` et `mw_*`
- ✅ Configuration via API (pas de hardcode)
- ✅ Logique métier de haut niveau

**Interdit** :
- ❌ **Paramètres hardcodés** : Tout doit être configurable

**Exemple conforme** : `app_scan_ultra`
- Orchestre `lib_a02_provider` + `mw_uros_core`
- Configuration flexible (bins, angles, mapping)
- Remplit directement `sensor_msgs/LaserScan`

**Exemple NON conforme** : `app_scan_tof` (voir section 9)
- ❌ Manque `app_scan_tof_types.h`
- ❌ Seul `scan_engine.h` présent

---

<a name="pattern-handle-opaque"></a>
## 3. 🔒 Pattern Handle Opaque

### Définition

Le **handle opaque** est un pattern obligatoire pour tous les composants avec état.

### Implémentation Obligatoire

#### Dans `<component>_types.h` :

```c
/**
 * @brief Handle opaque pour le composant <component>
 */
typedef struct <component>_s <component>_t;
```

#### Dans `<component>.h` :

```c
/**
 * @brief Crée une nouvelle instance du composant
 *
 * @param[in]  config  Configuration (non NULL)
 * @param[out] out     Pointeur vers le handle (non NULL)
 * @return ESP_OK en cas de succès
 * @return ESP_ERR_INVALID_ARG si config ou out est NULL
 * @return ESP_ERR_NO_MEM si allocation échoue
 */
esp_err_t <component>_new(const <component>_config_t *config, <component>_t **out);

/**
 * @brief Détruit l'instance du composant
 *
 * @param[in] handle  Handle à détruire (non NULL)
 * @return ESP_OK en cas de succès
 * @return ESP_ERR_INVALID_ARG si handle est NULL
 */
esp_err_t <component>_del(<component>_t *handle);
```

#### Dans `<component>.c` :

```c
/**
 * @brief Structure privée du composant (JAMAIS dans le .h)
 */
struct <component>_s {
    // Champs privés
    int example_field;
    void *internal_data;
};

esp_err_t <component>_new(const <component>_config_t *config, <component>_t **out)
{
    if (config == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    <component>_t *handle = calloc(1, sizeof(<component>_t));
    if (handle == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // Initialisation...

    *out = handle;
    return ESP_OK;
}

esp_err_t <component>_del(<component>_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Nettoyage des ressources...

    free(handle);
    return ESP_OK;
}
```

### Avantages

1. **Encapsulation** : Struct privée = zéro dépendance sur l'implémentation
2. **Évolutivité** : Changement de struct sans casser l'ABI
3. **Sécurité** : Impossible d'accéder directement aux champs

---

<a name="conventions-nommage"></a>
## 4. 📝 Conventions de Nommage

### Préfixes Obligatoires

Tous les symboles publics doivent porter le préfixe du composant :

| Élément | Format | Exemple |
|---------|--------|---------|
| **Fonctions** | `<component>_action()` | `drv_a02yyuw_read()` |
| **Types** | `<component>_type_t` | `lib_a02_provider_config_t` |
| **Enums** | `<COMPONENT>_ENUM_VALUE` | `DRV_A02YYUW_MODE_PROCESSED` |
| **Defines** | `<COMPONENT>_CONSTANT` | `DRV_A02YYUW_MAX_SENSORS` |
| **Handles** | `<component>_t` | `app_scan_ultra_t` |

### Conventions de Casse

- **snake_case** : Fonctions, variables, types
- **UPPER_SNAKE_CASE** : Enums, defines, constantes
- **PascalCase** : ❌ INTERDIT (réservé aux types ROS)

### Exemples Conformes

```c
// ✅ Bon : préfixe clair, casse cohérente
esp_err_t drv_a02yyuw_config_init(drv_a02yyuw_config_t *config);
esp_err_t lib_a02_provider_read_snapshot(lib_a02_provider_t *handle, ultrasonic_sample_t *samples, size_t count);
bool app_scan_ultra_step(void *ctx, void *ros_msg);

// ✅ Bon : enums préfixées
typedef enum {
    DRV_A02YYUW_MODE_REALTIME = 0,
    DRV_A02YYUW_MODE_PROCESSED = 1,
} drv_a02yyuw_mode_t;
```

### Exemples NON Conformes

```c
// ❌ Mauvais : pas de préfixe de composant
esp_err_t read_sensor(sensor_t *handle);

// ❌ Mauvais : nom générique sans namespace
typedef struct {
    int value;
} config_t;  // Collision possible !

// ❌ Mauvais : PascalCase pour code ESP-IDF
esp_err_t TofProviderRead(TofProvider *handle);
```

---

<a name="organisation-headers"></a>
## 5. 📄 Organisation des Headers

### Règle des Deux Headers

**OBLIGATOIRE** : Tout composant doit avoir **exactement 2 headers publics** :

1. **`<component>_types.h`** : Types, structs, enums
2. **`<component>.h`** : API fonctionnelle

### Contenu de `<component>_types.h`

```c
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Dépendances types externes (si nécessaire)
#include "drv_xxx/drv_xxx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handle opaque du composant
 */
typedef struct <component>_s <component>_t;

/**
 * @brief Énumérations publiques
 */
typedef enum {
    <COMPONENT>_VALUE_A = 0,
    <COMPONENT>_VALUE_B = 1,
} <component>_enum_t;

/**
 * @brief Structures de configuration publiques
 */
typedef struct {
    int field1;
    bool field2;
} <component>_config_t;

/**
 * @brief Structures de données publiques
 */
typedef struct {
    float value;
    bool valid;
} <component>_data_t;

#ifdef __cplusplus
}
#endif
```

### Contenu de `<component>.h`

```c
#pragma once

#include "<component>/<component>_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise une structure de configuration avec valeurs par défaut
 *
 * @param[out] config  Structure à initialiser (non NULL)
 * @return ESP_OK en cas de succès
 * @return ESP_ERR_INVALID_ARG si config est NULL
 */
esp_err_t <component>_config_init(<component>_config_t *config);

/**
 * @brief Crée une nouvelle instance
 */
esp_err_t <component>_new(const <component>_config_t *config, <component>_t **out);

/**
 * @brief Fonctions opérationnelles
 */
esp_err_t <component>_operation(<component>_t *handle, ...);

/**
 * @brief Détruit l'instance
 */
esp_err_t <component>_del(<component>_t *handle);

#ifdef __cplusplus
}
#endif
```

### Pourquoi 2 Headers ?

1. **Séparation des responsabilités** :
   - Types = contrat de données
   - API = contrat d'opérations

2. **Réduction des dépendances** :
   - Un composant peut inclure seulement `*_types.h` si pas besoin de l'API

3. **Clarté** :
   - Lecture facilitée : types d'abord, puis fonctions

### ❌ Anti-Pattern : Header Unique

```c
// ❌ INTERDIT : Tout dans un seul header
// scan_engine.h (app_scan_tof)
#pragma once

// Types + API mélangés
typedef struct { ... } scan_config_t;  // Devrait être dans app_scan_tof_types.h
esp_err_t scan_init(void);             // Devrait être dans app_scan_tof.h
```

---

<a name="configuration-cmake"></a>
## 6. ⚙️ Configuration CMakeLists.txt

### CMakeLists.txt Racine du Composant

```cmake
idf_component_register(
    SRCS "src/<component>.c"
    INCLUDE_DIRS "include"
    REQUIRES <public_dependencies>
    PRIV_REQUIRES <private_dependencies>
)
```

#### Règles de Dépendances

| Type | Utilisation | Visibilité |
|------|-------------|-----------|
| **REQUIRES** | Dépendances publiques (dans headers .h) | Propagées aux utilisateurs |
| **PRIV_REQUIRES** | Dépendances privées (seulement dans .c) | Cachées aux utilisateurs |

#### Exemples

**Driver (`drv_a02yyuw`)** :
```cmake
idf_component_register(
    SRCS "src/drv_a02yyuw.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_uart esp_driver_gpio
    # Pas de PRIV_REQUIRES car pas d'esp_timer dans .c
)
```

**Library (`lib_a02_provider`)** :
```cmake
idf_component_register(
    SRCS "src/lib_a02_provider.c"
    INCLUDE_DIRS "include"
    REQUIRES drv_a02yyuw           # Public : utilisé dans lib_a02_provider_types.h
    PRIV_REQUIRES esp_timer        # Privé : utilisé seulement dans .c
)
```

**Application (`app_scan_ultra`)** :
```cmake
idf_component_register(
    SRCS "src/app_scan_ultra.c"
    INCLUDE_DIRS "include"
    REQUIRES lib_a02_provider      # Public : utilisé dans app_scan_ultra_types.h
    PRIV_REQUIRES esp_timer        # Privé : utilisé seulement dans .c
)
```

### CMakeLists.txt Example (basic_app)

```cmake
cmake_minimum_required(VERSION 3.16)

# ✅ BON : Charger uniquement le composant nécessaire
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../..")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(<example_name>)
```

### ❌ Anti-Pattern : Charger Tous les Composants

```cmake
# ❌ MAUVAIS : Charge TOUT le framework (y compris micro-ROS)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../..")
```

**Conséquence** : Force l'exemple à dépendre de micro-ROS même si pas nécessaire

---

<a name="documentation-exemples"></a>
## 7. 📚 Documentation et Exemples

### README.md du Composant

Structure obligatoire :

```markdown
# <component_name>

Description en 1-2 phrases.

## Description

Paragraphe détaillé.

## Caractéristiques

- Liste des features

## Dépendances

- `drv_xxx` : Raison
- `lib_yyy` : Raison

## API

### Types Publics

```c
typedef struct { ... } component_config_t;
```

### Fonctions Publiques

```c
esp_err_t component_new(...);
```

## Exemple d'Utilisation

```c
// Code minimal fonctionnel
```

## Configuration

Description de Kconfig (si applicable)

## Exemple Complet

Voir `examples/basic_app/`

## Limitations

- Liste des limitations connues
```

### README.md de l'Exemple

Structure obligatoire :

```markdown
# Exemple basic_app - <component_name>

Description de l'exemple.

## Prérequis

### Matériel

- Liste du hardware

### Logiciels

- Liste des dépendances

## Compilation

```bash
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

## Configuration

Paramètres Kconfig disponibles.

## Résultat Attendu

Logs attendus.

## Dépannage

Solutions aux problèmes courants.
```

---

<a name="checklist-conformite"></a>
## 8. ✅ Checklist de Conformité

Utilisez cette checklist pour auditer un composant existant ou valider un nouveau composant.

### Structure de Fichiers

- [ ] `CMakeLists.txt` présent à la racine
- [ ] `idf_component.yml` présent
- [ ] `README.md` présent et complet
- [ ] `include/<component>/` namespace isolé
- [ ] `include/<component>/<component>_types.h` existe
- [ ] `include/<component>/<component>.h` existe
- [ ] `src/<component>.c` existe
- [ ] `examples/basic_app/` existe et compile

### Pattern Handle Opaque

- [ ] Typedef opaque dans `_types.h` : `typedef struct <component>_s <component>_t;`
- [ ] Struct privée définie dans `.c` uniquement
- [ ] Fonction `<component>_new()` présente
- [ ] Fonction `<component>_del()` présente
- [ ] Fonction `<component>_config_init()` présente (si config complexe)

### Conventions de Nommage

- [ ] Toutes les fonctions publiques préfixées `<component>_`
- [ ] Tous les types préfixés `<component>_`
- [ ] Enums en `UPPER_SNAKE_CASE`
- [ ] snake_case pour fonctions et types
- [ ] Pas de PascalCase (sauf types ROS)

### Séparation des Responsabilités

**Pour `drv_*` :**
- [ ] Pas d'include micro-ROS (rcl/rclc)
- [ ] Pas de logique métier (filtrage, conversion)
- [ ] Pas d'abstractions type "provider" (déplacer vers `lib_*`)

**Pour `lib_*` :**
- [ ] Pas d'accès matériel direct (utilise `drv_*`)
- [ ] Pas d'include micro-ROS

**Pour `mw_*` :**
- [ ] Inclut micro-ROS (rcl/rclc)
- [ ] Pas d'accès matériel direct
- [ ] Pas de logique applicative

**Pour `app_*` :**
- [ ] Pas de paramètres hardcodés
- [ ] Configuration via API

### CMakeLists.txt

- [ ] `REQUIRES` contient dépendances publiques uniquement
- [ ] `PRIV_REQUIRES` contient dépendances privées
- [ ] Example `EXTRA_COMPONENT_DIRS` pointe vers `../..` (pas `../../..`)

### Documentation

- [ ] README.md complet avec API documentation
- [ ] Example README.md avec instructions de build
- [ ] Commentaires Doxygen sur toutes les fonctions publiques

---

<a name="exemples"></a>
## 9. 🔍 Exemples Conformes vs Non-Conformes

### ✅ Composants Conformes (Référence)

#### `drv_a02yyuw` — Driver Conforme

```
drv_a02yyuw/
├── include/drv_a02yyuw/
│   ├── drv_a02yyuw_types.h    ✅ Types séparés
│   └── drv_a02yyuw.h          ✅ API séparée
└── src/drv_a02yyuw.c          ✅ Implémentation unique
```

**Points forts** :
- ✅ Handle opaque strict
- ✅ Pas de logique métier (juste pilotage UART/GPIO)
- ✅ Nommage cohérent : `drv_a02yyuw_*`
- ✅ Pas de dépendance micro-ROS

#### `lib_a02_provider` — Library Conforme

```
lib_a02_provider/
├── include/lib_a02_provider/
│   ├── lib_a02_provider_types.h    ✅ Types séparés
│   └── lib_a02_provider.h          ✅ API séparée
└── src/lib_a02_provider.c          ✅ Implémentation unique
```

**Points forts** :
- ✅ Abstraction provider correctement placée dans `lib_*`
- ✅ Filtrage médian (logique métier) dans `lib_*` (pas `drv_*`)
- ✅ Utilise `drv_a02yyuw` sans accès matériel direct
- ✅ Pas de dépendance micro-ROS

#### `app_scan_ultra` — Application Conforme

```
app_scan_ultra/
├── include/app_scan_ultra/
│   ├── app_scan_ultra_types.h    ✅ Types séparés
│   └── app_scan_ultra.h          ✅ API séparée
└── src/app_scan_ultra.c          ✅ Implémentation unique
```

**Points forts** :
- ✅ Orchestration propre : `lib_a02_provider` + `mw_uros_core`
- ✅ Configuration flexible (bins, angles, mapping)
- ✅ Pas de hardcode

---

### ❌ Composants Non-Conformes (Nécessitent Refactorisation)

#### `drv_vl53l0x` — Driver Non-Conforme

```
drv_vl53l0x/
├── include/drv_vl53l0x/
│   ├── drv_vl53l0x.h        ✅ OK
│   ├── tof_config.h         ❌ Abstraction lib-like dans driver
│   ├── tof_provider.h       ❌ Provider dans driver
│   ├── tof_snapshot.h       ❌ Utilitaires dans driver
│   └── vl53l0x_api/         ⚠️ Vendor code (toléré)
└── src/
    ├── drv_vl53l0x.c
    ├── tof_config.c         ❌ Implémentation lib-like
    ├── tof_provider.c       ❌ Implémentation lib-like
    └── tof_snapshot.c       ❌ Implémentation lib-like
```

**Problèmes identifiés** :

1. **Violation de la taxonomie** :
   - `tof_provider` = Abstraction multi-capteurs → devrait être `lib_vl53l0x_provider`
   - `tof_config` = Configuration de haut niveau → devrait être dans `lib_*`
   - `tof_snapshot` = Utilitaires → devrait être dans `lib_*`

2. **Driver pollué** :
   - Le driver contient de la logique métier
   - Responsabilités mélangées

**Refactorisation nécessaire** :

```
AVANT (non-conforme)           APRÈS (conforme)
────────────────────           ────────────────
drv_vl53l0x/                   drv_vl53l0x/
├── tof_provider.h       ──→   ├── drv_vl53l0x_types.h  ✅
├── tof_config.h               └── drv_vl53l0x.h        ✅
├── tof_snapshot.h
└── drv_vl53l0x.h              lib_vl53l0x_provider/    ✅ NOUVEAU
                               ├── lib_vl53l0x_provider_types.h
                               └── lib_vl53l0x_provider.h
                                   (contient tof_provider, tof_config, tof_snapshot)
```

#### `app_scan_tof` — Application Non-Conforme

```
app_scan_tof/
├── include/app_scan_tof/
│   └── scan_engine.h        ❌ Pas de types header
└── src/scan_engine.c
```

**Problèmes identifiés** :

1. **Structure incomplète** :
   - Manque `app_scan_tof_types.h`
   - Nom générique `scan_engine` (pas de préfixe `app_scan_tof_`)

2. **Violation des conventions** :
   - Types et API probablement mélangés dans `scan_engine.h`

**Refactorisation nécessaire** :

```
AVANT (non-conforme)           APRÈS (conforme)
────────────────────           ────────────────
app_scan_tof/                  app_scan_tof/
├── include/app_scan_tof/      ├── include/app_scan_tof/
│   └── scan_engine.h    ──→   │   ├── app_scan_tof_types.h  ✅
└── src/scan_engine.c          │   └── app_scan_tof.h        ✅
                               └── src/app_scan_tof.c         ✅
```

**Renommage nécessaire** :
- `scan_engine_*` → `app_scan_tof_*`

---

<a name="plan-harmonisation"></a>
## 10. 🔄 Plan d'Harmonisation

### Priorités

| Priorité | Composant | Action | Effort |
|----------|-----------|--------|--------|
| **P0** | `drv_vl53l0x` | Extraction `lib_vl53l0x_provider` | Moyen |
| **P1** | `app_scan_tof` | Ajout `app_scan_tof_types.h` + renommage | Faible |
| **P2** | Tous | Audit complet conformité checklist | Faible |

### Phase 1 : Extraction `lib_vl53l0x_provider`

**Objectif** : Nettoyer `drv_vl53l0x` en extrayant les abstractions lib-like

**Étapes** :

1. **Créer `lib_vl53l0x_provider`** :
   ```
   lib_vl53l0x_provider/
   ├── include/lib_vl53l0x_provider/
   │   ├── lib_vl53l0x_provider_types.h
   │   └── lib_vl53l0x_provider.h
   └── src/lib_vl53l0x_provider.c
   ```

2. **Déplacer le code** :
   - `drv_vl53l0x/tof_provider.{h,c}` → `lib_vl53l0x_provider/`
   - `drv_vl53l0x/tof_config.{h,c}` → `lib_vl53l0x_provider/`
   - `drv_vl53l0x/tof_snapshot.{h,c}` → `lib_vl53l0x_provider/`

3. **Renommer les fonctions** :
   - `tof_provider_*` → `lib_vl53l0x_provider_*`
   - `tof_config_*` → `lib_vl53l0x_provider_config_*`
   - `tof_snapshot_*` → `lib_vl53l0x_provider_snapshot_*`

4. **Nettoyer `drv_vl53l0x`** :
   ```
   drv_vl53l0x/
   ├── include/drv_vl53l0x/
   │   ├── drv_vl53l0x_types.h      ✅ Créer
   │   ├── drv_vl53l0x.h            ✅ Garder
   │   └── vl53l0x_api/             ✅ Garder (vendor)
   └── src/drv_vl53l0x.c            ✅ Garder
   ```

5. **Mettre à jour `app_scan_tof`** :
   ```c
   // AVANT
   #include "drv_vl53l0x/tof_provider.h"

   // APRÈS
   #include "lib_vl53l0x_provider/lib_vl53l0x_provider.h"
   ```

### Phase 2 : Harmonisation `app_scan_tof`

**Objectif** : Conformer la structure de `app_scan_tof`

**Étapes** :

1. **Créer `app_scan_tof_types.h`** :
   ```c
   #pragma once

   typedef struct app_scan_tof_s app_scan_tof_t;

   typedef struct {
       // Configuration
   } app_scan_tof_config_t;
   ```

2. **Renommer `scan_engine.{h,c}` → `app_scan_tof.{h,c}`**

3. **Renommer les fonctions** :
   - `scan_engine_init()` → `app_scan_tof_init()`
   - `scan_engine_step()` → `app_scan_tof_step()`
   - etc.

4. **Séparer types et API** :
   - Types → `app_scan_tof_types.h`
   - API → `app_scan_tof.h`

### Phase 3 : Audit Complet

**Objectif** : Vérifier tous les composants avec la checklist

**Composants à auditer** :
- `drv_led_rgb`
- `lib_status_led`
- `mw_scan_builder`
- `mw_uros_core`
- `mw_uros_transport_usb`

**Pour chaque composant** :
1. Exécuter la checklist de conformité (section 8)
2. Documenter les déviations
3. Planifier corrections si nécessaire

---

## 📌 Résumé Exécutif

### Règles d'Or

1. **2 Headers Obligatoires** : `*_types.h` + `*.h`
2. **Handle Opaque** : Struct privée dans `.c`
3. **Préfixe Partout** : `<component>_*` sur tous les symboles publics
4. **Taxonomie Stricte** : `drv_*` (hardware) → `lib_*` (logic) → `mw_*` (micro-ROS) → `app_*` (orchestration)
5. **Pas de Pollution** : Drivers sans logique métier, libs sans matériel, mw sans app logic

### Composants Références (100% Conformes)

- ✅ `drv_a02yyuw`
- ✅ `lib_a02_provider`
- ✅ `app_scan_ultra`

**Utilisez ces composants comme templates pour nouveaux composants**

### Actions Prioritaires

1. **Immédiat** : Extraire `lib_vl53l0x_provider` de `drv_vl53l0x`
2. **Court terme** : Harmoniser `app_scan_tof`
3. **Moyen terme** : Auditer tous les composants

---

## 📞 Support

Pour toute question sur ce guide :
- 📚 Lire le [CDC](cdc.md)
- 🔍 Examiner les composants conformes (drv_a02yyuw, lib_a02_provider, app_scan_ultra)
- 💬 Ouvrir une discussion sur le dépôt

---

**Version 1.0** | Dernière mise à jour : 2026-01-26
