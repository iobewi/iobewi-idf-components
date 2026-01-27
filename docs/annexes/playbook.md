# 🧰 Playbook — Audit & Bonnes pratiques

## iobewi-idf-components

> **Statut : INFORMATIF**
> Ce document **n’introduit aucune règle normative**.
>
> Il explique **comment appliquer et auditer** le standard :
>
> * `docs/standard.md`
>
> Références principales :
>
> * `STD-TAX-*` (taxonomie)
> * `STD-STR-*` (structure)
> * `STD-API-*` (API & contrats)
> * `STD-BLD-*` (CMake / build)
> * `STD-TST-*` (tests)

---

## 0) Avant de commencer un audit

Avant tout audit (humain ou IA) :

* identifier le `component_id` attendu
* identifier la **catégorie cible** (`driver | library | middleware | application`)
* lister les dépendances réelles (CMake + includes)
* compiler l’exemple `basic_app` si présent

> ⚠️ Toute règle opposable est **exclusivement** dans `docs/standard.md`.

---

## 1) Checklist de conformité (opérationnelle)

### 1.1 Arborescence

* [ ] `components/<component_id>/CMakeLists.txt`
* [ ] `components/<component_id>/idf_component.yml`
* [ ] `components/<component_id>/README.md`
* [ ] `components/<component_id>/include/<component_api>/<component_api>_types.h`
* [ ] `components/<component_id>/include/<component_api>/<component_api>.h`
* [ ] `components/<component_id>/src/<component_api>.c`
* [ ] `examples/<component_id>/basic_app/` existe et compile

---

### 1.2 Taxonomie / dépendances

* [ ] `component_id` respecte `iobewi_<kind>_<name>`
* [ ] `<component_api>` respecte `<prefix>_<name>`
* [ ] dépendances conformes au graphe `driver → libs → mw → apps`
* [ ] aucun include micro-ROS hors `mw_*`

---

### 1.3 API

* [ ] fonctions publiques retournent `esp_err_t`
* [ ] validation systématique des arguments (`NULL`, tailles, enums)
* [ ] `new/del` présents si composant avec état
* [ ] handle opaque (struct interne non exposée)
* [ ] symboles publics strictement préfixés `<component_api>_`

---

### 1.4 Robustesse

* [ ] aucun état partiellement modifié en cas d’erreur
* [ ] pas de fuite mémoire (init/del répétés)
* [ ] échec d’allocation traité proprement

---

### 1.5 Tests

* [ ] tests unitaires présents
* [ ] aucun hardware réel requis
* [ ] contrats API testés :

  * `NULL`
  * `ESP_ERR_NO_MEM`
  * double init
  * double del
  * erreurs internes

---

## 2) Guide de diagnostic rapide (symptômes → causes)

### 2.1 “Driver pollué”

**Symptômes**

* `provider`, `manager`, `snapshot`, filtrage, mapping dans `drv_*`

**Cause**

* mélange responsabilités `drv_*` / `lib_*`

**Action typique**

* extraire en `lib_*` tout ce qui n’est pas du pilotage matériel pur

---

### 2.2 “Middleware qui touche au hardware”

**Symptômes**

* `mw_*` accède directement à GPIO / I2C / UART

**Action typique**

* introduire ou utiliser un `drv_*` dédié
* faire dépendre le `mw_*` de ce driver

---

### 2.3 “Application sans types.h”

**Symptômes**

* header unique
* types et API mélangés
* pas de `<component_api>_types.h`

**Action typique**

* split en `*_types.h` + `*.h`
* renommer tous les symboles en `<component_api>_*`

---

## 3) Patterns pratiques (rappels utiles)

### 3.1 Structure de composant

Référence : `STD-STR-001`, `STD-STR-002`

* le composant vit dans `components/<component_id>/`
* l’exemple minimal vit dans `examples/<component_id>/basic_app/`

---

### 3.2 CMake — patterns utiles

Référence : `STD-BLD-001`

```cmake
idf_component_register(
    SRCS "src/<component_api>.c"
    INCLUDE_DIRS "include"
    REQUIRES <deps_publiques>
    PRIV_REQUIRES <deps_privees>
)
```

Rappels :

* `REQUIRES` → dépendances visibles depuis les headers publics
* `PRIV_REQUIRES` → dépendances confinées aux `.c`

#### Exemple `basic_app`

```cmake
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(basic_app)
```

---

### 3.3 Configuration d’exemple (menuconfig-first)

Référence : `docs/annexes/examples.md`

Recommandations :

* exposer GPIO / ports / modes via `main/Kconfig.projbuild`
* garder `sdkconfig.defaults` optionnel (preset)

---

## 4) Anti-patterns fréquents (retours terrain)

* driver contenant filtrage / provider / snapshot
* `scan_engine.h` sans namespace `app_*`
* types et API mélangés dans un seul header
* includes non namespacés
* dépendances implicites (absentes de `REQUIRES/PRIV_REQUIRES`)
* Kconfig utilisé pour du métier au lieu de runtime API

---

## 5) Exemples d’audit concrets

### 5.1 `drv_vl53l0x` — driver pollué

**Problème**

* présence de `tof_provider.*`, `tof_config.*`, `tof_snapshot.*`

**Action**

* créer `iobewi_libs_vl53l0x_provider`
* déplacer les abstractions lib-like
* conserver dans le driver uniquement :

  * accès bas niveau
  * API hardware pure

---

### 5.2 `app_scan_tof` — structure incomplète

**Problème**

* header unique
* pas de `app_scan_tof_types.h`
* symboles non préfixés

**Action**

* renommer en `app_scan_tof.{h,c}`
* ajouter `app_scan_tof_types.h`
* renommer tous les symboles

---

## 6) Grille de priorité (audit)

* **P0** — violation taxonomie / dépendances (bloquant CI)
* **P1** — violations API / encapsulation
* **P2** — lisibilité, factoring, améliorations internes

---

## 7) Format de rapport d’audit (recommandé)

```
Composant : <component_id>
Catégorie attendue : driver | library | middleware | application

Constats factuels
- ...
- ...

Non-conformités (référence standard)
- STD-TAX-003 : ...
- STD-API-005 : ...

Plan d’action
1. ...
2. ...

Impacts
- breaking API : oui / non
- impact dépendances : oui / non
```

---

## 8) Plan d’harmonisation type

Approche efficace :

1. corriger nom dossier + API
2. corriger structure headers
3. isoler responsabilités (`drv / lib / mw / app`)
4. nettoyer dépendances CMake
5. ajouter tests unitaires
6. valider via `basic_app`

---

## ✅ Conclusion

Ce playbook permet :

* d’auditer un composant **sans lire tout le code**
* d’identifier rapidement les violations structurelles
* de proposer un refactor aligné standard
* d’être utilisé par **humains et agents IA**

📌 **Autorité unique** : `docs/standard.md`
📎 **Ce document** : guide d’exécution et d’audit