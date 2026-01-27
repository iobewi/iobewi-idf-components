# 📘 Cahier des Charges Global  
## Framework **iobewi-idf-components**  
### ESP-IDF / micro-ROS

---

## 1. Objet du document

Ce document définit les **règles techniques, architecturales et organisationnelles obligatoires** applicables à l’ensemble du framework **iobewi-idf-components**.

Il constitue un **contrat technique opposable** à tous les contributeurs (humains ou agents).

---

## 2. Objectifs du framework

Le framework **iobewi-idf-components** vise à :

1. Fournir des composants ESP-IDF **réutilisables industriellement**
2. Intégrer **micro-ROS** de manière propre et maîtrisée
3. Garantir une **maintenabilité long terme (≥ 5 ans)**
4. Assurer une **traçabilité claire des responsabilités**
5. Éliminer toute **dette technique volontaire**

---

## 3. Périmètre technique

### 3.1 Plateforme

- **ESP-IDF** : version 6.x
- **Langage principal** : C
- **RTOS** : FreeRTOS (ESP-IDF)
- **ROS** : micro-ROS (rcl / rclc)
- **Build system** : CMake ESP-IDF natif

Tout autre framework est interdit sans validation explicite.

### 3.2 Organisation du Projet

Le framework utilise une structure organisée avec séparation des composants et exemples :

```
iobewi-idf-components/
├── components/              # Code source des composants
├── examples/                # Applications d'exemple
├── docs/                    # Documentation
└── tools/                   # Scripts et outils
```

### 3.3 Convention de Nommage - Double Nomenclature

Le framework applique une **double nomenclature** pour allier identité de marque et concision technique :

**Niveau répertoire** (Branding) :
- Format : `iobewi_<catégorie>_<nom>`
- Exemples : `iobewi_driver_vl53l0x`, `iobewi_libs_status_led`, `iobewi_mw_uros_core`

**Niveau API** (Technique) :
- Chemins include : `<catégorie courte>_<nom>/<catégorie courte>_<nom>.h`
- Fonctions : `<catégorie courte>_<nom>_<action>()`
- Types : `<catégorie courte>_<nom>_<type>_t`

**Mapping catégories** :
- `iobewi_driver_*` ↔ `drv_*`
- `iobewi_libs_*` ↔ `lib_*`
- `iobewi_mw_*` ↔ `mw_*`
- `iobewi_apps_*` ↔ `app_*`

**Exemple concret** :
```
Répertoire : components/iobewi_driver_vl53l0x/
Include    : #include "drv_vl53l0x/drv_vl53l0x.h"
API        : drv_vl53l0x_init(&dev)
Types      : drv_vl53l0x_dev_t
```

**Rationale** :
- Répertoires `iobewi_*` : identité de marque, évite conflits, origine claire
- API `drv_*/lib_*/mw_*/app_*` : concision, conformité ESP-IDF, taxonomie explicite

Voir [Guide de Structure des Répertoires](directory_structure.md) pour les détails complets.

---

## 4. Taxonomie des composants (OBLIGATOIRE)

Chaque composant appartient à **une seule catégorie**.

### 4.1 `drv_*` — Drivers

- Pilotage matériel pur
- Accès direct aux bus (I2C, SPI, GPIO, UART…)
- Aucune dépendance micro-ROS
- Aucune logique métier
- Configurable et générique

### 4.2 `lib_*` — Bibliothèques utilitaires

- Code réutilisable générique sans micro-ROS
- Abstractions fonctionnelles (mapping état→couleur, conversion, helpers)
- Peut dépendre de `drv_*`
- Aucun accès matériel direct (délégué aux `drv_*`)
- Aucune logique applicative
- Aucune dépendance micro-ROS
- Exemples : `lib_status_led`, `lib_config_parser`, `lib_state_machine`

### 4.3 `mw_*` — Middleware / Adaptateurs micro-ROS

- **Intégration micro-ROS obligatoire**
- Builders de messages ROS
- Helpers rcl / rclc
- Wrappers QoS, time sync, allocators
- Peut dépendre de `drv_*` et `lib_*`
- Aucun accès matériel direct
- Aucune logique applicative

### 4.4 `app_*` — Composants métier

- Logique fonctionnelle réutilisable
- Orchestration de `drv_*`, `lib_*` et `mw_*`
- Aucun paramètre hardcodé
- Configuration uniquement via API ou structures

---

### 4.5 Règle de dépendance (NON NÉGOCIABLE)

```

drv_*  →  lib_*  →  mw_*  →  app_*

```

**Règles strictes :**
- `drv_*` : ne dépend de rien (sauf ESP-IDF)
- `lib_*` : peut dépendre de `drv_*` uniquement
- `mw_*` : peut dépendre de `drv_*` et `lib_*`
- `app_*` : peut dépendre de `drv_*`, `lib_*` et `mw_*`

Toute dépendance inverse est strictement interdite.

---

## 5. Structure des composants

### 5.1 Arborescence obligatoire

**Note** : `<component_dir>` = nom de répertoire (ex: `iobewi_driver_vl53l0x`)
**Note** : `<component_name>` = nom API court (ex: `drv_vl53l0x`)

```
components/<component_dir>/          # Ex: components/iobewi_driver_vl53l0x/
├── CMakeLists.txt
├── idf_component.yml
├── README.md
├── Kconfig                          # (optionnel)
├── include/
│   └── <component_name>/            # Ex: drv_vl53l0x/
│       ├── <component_name>.h       # Ex: drv_vl53l0x.h
│       └── <component_name>_types.h # Ex: drv_vl53l0x_types.h
└── src/
    └── <component_name>.c           # Ex: vl53l0x_driver.c

examples/<component_dir>/            # Ex: examples/iobewi_driver_vl53l0x/
└── basic_app/
    ├── CMakeLists.txt               # Pointe vers ../../components
    ├── sdkconfig.defaults
    ├── README.md
    └── main/
        ├── CMakeLists.txt
        └── main.c                   # #include "drv_vl53l0x/drv_vl53l0x.h"
```

**Exemple concret** : Composant `drv_vl53l0x`
```
components/iobewi_driver_vl53l0x/    # Répertoire avec préfixe iobewi_
├── include/
│   └── drv_vl53l0x/                 # Namespace API (forme courte)
│       ├── drv_vl53l0x.h
│       └── drv_vl53l0x_types.h
└── src/
    └── vl53l0x_driver.c

examples/iobewi_driver_vl53l0x/
└── basic_app/
    └── main/
        └── main.c                   # #include "drv_vl53l0x/drv_vl53l0x.h"
```

### 5.2 Règles strictes

- Un composant = un dossier dans `components/`
- Nom de répertoire : format `iobewi_<catégorie>_<nom>`
- Namespace include : format `<catégorie courte>_<nom>/`
- Un seul `CMakeLists.txt` par composant
- `examples/<component_dir>/basic_app` obligatoire
- Exemples pointent vers `../../components` via `EXTRA_COMPONENT_DIRS`
- Aucun code métier hors de `app_*`
- Aucun accès matériel hors de `drv_*`

---

## 6. API publique

### 6.1 Règles générales

- Langage : C
- API exposée uniquement via :
```

include/<component_name>/

````
- Aucun include plat
- Aucune variable globale exposée
- Utilisation de handles opaques recommandée

### 6.2 Signatures obligatoires

- Toutes les fonctions publiques retournent `esp_err_t`
- Toute allocation dynamique doit être documentée

Exemple canonique :

```c
typedef struct drv_xxx drv_xxx_t;

esp_err_t drv_xxx_new(const drv_xxx_config_t *cfg, drv_xxx_t **out);
esp_err_t drv_xxx_del(drv_xxx_t *handle);
````

---

## 7. Gestion mémoire

* Ownership clair et documenté
* Toute allocation doit avoir une fonction de libération publique
* Aucune fuite mémoire tolérée
* Aucune allocation cachée non documentée

---

## 8. Configuration (Kconfig)

### 8.1 Principes

* Le composant doit fonctionner sans Kconfig si possible
* Kconfig réservé à :

  * fonctionnalités optionnelles
  * niveaux de log

### 8.2 Conventions

* Tous les symbols doivent être préfixés :

  ```
  CONFIG_IOBEWI_<COMPONENT_NAME>_*
  ```
* Aucun GPIO / pin imposé dans un driver générique

---

## 9. Logging & erreurs

### 9.1 Logs

* `ESP_LOGx` uniquement
* TAG = nom du composant
* Niveau par défaut : INFO ou inférieur
* Aucun log verbeux en fonctionnement nominal

### 9.2 Erreurs

* Utilisation exclusive des codes ESP-IDF standards :

  * `ESP_ERR_INVALID_ARG`
  * `ESP_ERR_TIMEOUT`
  * `ESP_ERR_NO_MEM`
  * etc.

---

## 10. CMake & build

* Toutes les dépendances doivent être explicitement déclarées
* Utilisation stricte de :

  * `REQUIRES`
  * `PRIV_REQUIRES`
* Aucun include implicite
* Aucun hack de build toléré

---

## 11. micro-ROS (`mw_*` uniquement)

* Aucune logique applicative
* Fournir :

  * helpers d’initialisation
  * builders de messages
  * wrappers QoS, time sync, allocators
* Compatible rclc
* Aucun transport hardcodé

---

## 12. Exemples & livrables

Chaque composant doit fournir :

* Code compilable ESP-IDF 6.x
* `examples/basic_app` fonctionnel
* README documentant :

  * le rôle du composant
  * l’API publique
  * les dépendances
* Aucun TODO critique
* Aucune dette technique volontaire

---

## 13. Qualité & refus des shortcuts

* Toute solution non maintenable doit être refusée
* Toute ambiguïté de design doit être signalée
* La simplicité prime sur l’optimisation prématurée
* Le framework doit rester lisible par un tiers expert

---

## 14. Autorité du document

Ce document :

* prévaut sur toute implémentation existante
* sert de référence en revue de code
* est opposable aux agents automatiques
* définit le niveau minimal acceptable

Toute déviation doit être explicitement justifiée et validée au niveau architecture.

---

## 15. Objectif final

Construire un framework **ESP-IDF / micro-ROS** :

* modulaire
* testable
* maintenable sur le long terme
* aligné avec les bonnes pratiques Espressif
* exploitable industriellement sans refonte

---