# 🧰 Playbook — Diagnostic & Remédiation

## iobewi-idf-components

## Annexe informative au standard `docs/standard.md`

> **STATUT : INFORMATIF — REMÉDIATION**
>
> Ce document explique **comment diagnostiquer et corriger** des non-conformités après audit.
>
> * Il **n’introduit aucune règle normative**
> * Il **ne remplace pas** la procédure d’audit
> * Il **ne prononce aucun verdict**
>
> 📌 Autorité normative : `docs/standard.md`
> 🧪 Procédure d’audit & verdict : `docs/annexes/audit_playbook.md`

---

## 0) Usage prévu

Ce document est utilisé :

* **après** un audit ayant identifié des violations (`STD-*`)
* pour définir un **plan de correction** (refactor, extraction, renommage)
* pour guider une **revue de correction**
* par un agent IA chargé de proposer un plan d’harmonisation

Ce document **n’est pas** :

* une checklist de conformité
* une procédure d’audit formelle
* une source de règles

---

## 1) Workflow recommandé (audit → correction → re-audit)

1. Exécuter l’audit avec `docs/annexes/audit_playbook.md`
2. Lister les violations factuelles (`STD-XXX-YYY`)
3. Utiliser ce guide pour :

   * identifier la cause typique
   * choisir une stratégie de refactor
   * estimer l’impact (API / dépendances / exemples)
4. Implémenter les corrections
5. Rejouer l’audit (et les builds d’exemples / tests)

> Objectif : passer d’une liste `STD-*` à un plan d’action concret.

---

## 2) Diagnostic rapide (symptômes → causes → actions typiques)

### 2.1 Driver “pollué” (`drv_*` contient du métier)

**Symptômes (observables)**

* présence de `provider`, `manager`, `snapshot`, filtrage, conversion “haut niveau”
* types ou API non strictement hardware

**Cause typique**

* mélange de responsabilités `drv_*` / `lib_*`

**Action typique**

* extraire en `lib_*` tout ce qui n’est pas du pilotage matériel pur
* conserver dans `drv_*` uniquement :

  * accès bus (I2C/SPI/UART/GPIO)
  * init/config hardware
  * lecture brute / état matériel

**Impact à évaluer**

* dépendances : `app_*` / `mw_*` devront dépendre de `lib_*` au lieu de `drv_*`
* API : possible besoin de maintenir une API de compat temporaire

---

### 2.2 `lib_*` qui touche au hardware

**Symptômes**

* appels directs I2C/GPIO/UART dans un `lib_*`
* `REQUIRES esp_driver_*` sans passer par un `drv_*`

**Cause typique**

* absence de driver dédié ou “shortcut” historique

**Action typique**

* introduire un `drv_*` minimal
* faire dépendre `lib_*` de ce driver
* déplacer toute I/O hardware vers le `drv_*`

---

### 2.3 `mw_*` qui touche au hardware

**Symptômes**

* `mw_*` accède à GPIO/I2C/UART
* transport hardcodé
* dépendance directe à un périphérique

**Cause typique**

* manque d’abstraction driver
* middleware trop spécifique

**Action typique**

* introduire / utiliser un `drv_*` dédié
* rendre le `mw_*` générique : mapping ROS + orchestration micro-ROS uniquement
* isoler la config transport dans un module spécialisé `mw_*` (si nécessaire)

---

### 2.4 `app_*` structure incomplète ou noms génériques

**Symptômes**

* header unique (types + API mélangés)
* pas de `<component_api>_types.h`
* fichiers `scan_engine.*`, `manager.*`, etc. sans namespace

**Cause typique**

* composant créé hors standard ou migration incomplète

**Action typique**

* split strict : `*_types.h` + `*.h`
* renommer fichiers et symboles → `<component_api>_*`
* restaurer le handle opaque si état

---

### 2.5 Dépendances CMake incorrectes (REQUIRES / PRIV_REQUIRES)

**Symptômes**

* un utilisateur du composant doit ajouter “à la main” des dépendances
* headers publics incluent des headers de dépendance non déclarée en `REQUIRES`

**Cause typique**

* confusion entre dépendance publique et privée

**Action typique**

* si un header public inclut une dépendance → `REQUIRES`
* si utilisé uniquement dans `.c` → `PRIV_REQUIRES`

---

## 3) Patterns de remédiation (recettes courtes)

### 3.1 Extraction “propre” (`drv_*` → `lib_*`)

**Quand** : driver pollué (provider/config/snapshot dans `drv_*`)
**But** : restaurer `drv_*` minimal + créer `lib_*` pour le reste

**Recette**

1. Créer `components/iobewi_libs_<name>/` + structure standard
2. Déplacer les abstractions “haut niveau” dans `lib_*`
3. Renommer les symboles en `lib_<name>_*`
4. Mettre à jour `REQUIRES` / `PRIV_REQUIRES`
5. Mettre à jour les includes dans `app_*` / `mw_*`

---

### 3.2 Réparation “2 headers” (types/API)

**Quand** : un seul header, ou types mélangés
**Recette**

1. Créer `<component_api>_types.h` (types publics + handle opaque)
2. Créer `<component_api>.h` (API publique)
3. Déplacer la struct interne dans `.c`
4. Ajuster `#include "<component_api>/<component_api>_types.h"`

---

### 3.3 Renommage namespace (composant → symbole)

**Quand** : symboles génériques, collisions probables
**Recette**

1. Renommer fichiers : `scan_engine.c` → `<component_api>.c`
2. Renommer toutes fonctions/types/defines → `<component_api>_*`
3. Vérifier les includes externes (exemples, apps)

---

## 4) Anti-patterns fréquents (à corriger systématiquement)

* driver contenant filtrage / provider / snapshot
* types et API dans un seul header
* headers hors namespace `include/<component_api>/`
* symboles publics non préfixés
* dépendances implicites (CMake incomplet)
* Kconfig utilisé pour du métier au lieu d’une config runtime
* tests qui requièrent du hardware réel

---

## 5) Exemples de remédiation (cas réels)

### 5.1 `drv_vl53l0x` — extraction en `lib_vl53l0x_provider`

**Symptôme**

* fichiers `tof_provider.*`, `tof_config.*`, `tof_snapshot.*` dans `drv_*`

**Remédiation**

* créer `iobewi_libs_vl53l0x_provider`
* déplacer ces abstractions dans `lib_*`
* conserver dans `drv_vl53l0x` uniquement l’accès bas niveau

---

### 5.2 `app_scan_tof` — restauration structure standard

**Symptôme**

* header unique, pas de `*_types.h`, symboles génériques

**Remédiation**

* `scan_engine.{h,c}` → `app_scan_tof.{h,c}`
* ajouter `app_scan_tof_types.h`
* renommer les symboles en `app_scan_tof_*`

---

## 6) Grille de priorité (pour planifier un refactor)

> Cette grille aide à prioriser les corrections.
> Elle ne remplace pas le verdict de conformité (donné par l’audit).

* **P0** — violations taxonomie / dépendances (risque structurel majeur)
* **P1** — violations API / encapsulation / contrats
* **P2** — lisibilité / factoring / améliorations internes

---

## 7) Format de plan d’action (recommandé)

```text
Composant : <component_id>
Violations (depuis l’audit) :
- STD-XXX-YYY : constat

Plan d’action :
1) Action
   - fichiers impactés :
   - impact API : oui/non
   - impact dépendances : oui/non
2) Action
   ...

Validation :
- build basic_app : OK/KO
- tests unitaires : OK/KO
- re-audit : OK/KO