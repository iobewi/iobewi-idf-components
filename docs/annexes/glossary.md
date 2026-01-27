
> **STATUT : INFORMATIF**
>
> Ce document :
> - n’introduit aucune règle normative
> - ne remplace aucune règle du standard
> - ne peut jamais contredire `docs/standard.md`
>
> Toute règle opposable est définie exclusivement dans `docs/standard.md`.

# 📚  Glossaire — iobewi-idf-components

> Document **informatif**
> Objectif : définir un vocabulaire **non ambigu**, partagé par les humains **et** les agents IA.

---

## Composant

**Définition**
Unité logicielle autonome, versionnable et réutilisable, identifiée par un **dossier canonique unique** sous `components/`.

**Propriétés** :

* possède une identité canonique
* expose une API publique stable
* respecte une catégorie normative (`driver`, `library`, `middleware`, `application`)
* est testable indépendamment

📌 Un composant **n’existe pas** sans dossier canonique.

---

## Identité canonique

**Définition**
Nom du dossier :

```
components/<component_id>/
```

**Statut** :
👉 **Source de vérité unique**

**Règles associées** :

* détermine la catégorie du composant
* détermine le nom de l’API publique
* détermine les dépendances autorisées

📌 Toute divergence entre identité canonique et API est une **violation du standard**.

---

## Alias API

**Définition**
Nom technique dérivé de l’identité canonique, utilisé pour :

* le namespace des headers
* le préfixe des symboles publics

**Exemples** :

| Identité canonique       | Alias API         |
| ------------------------ | ----------------- |
| `iobewi_driver_vl53l0x`  | `drv_vl53l0x`     |
| `iobewi_libs_status_led` | `lib_status_led`  |
| `iobewi_mw_scan_builder` | `mw_scan_builder` |
| `iobewi_apps_scan_ultra` | `app_scan_ultra`  |

📌 L’alias API **n’est jamais une source de vérité**, seulement une projection.

---

## Catégorie de composant (`component_kind`)

**Définition**
Classe fonctionnelle **normative** déduite **exclusivement** du nom du dossier.

**Catégories autorisées** :

* `driver`
* `library`
* `middleware`
* `application`

📌 Toute autre catégorie est **interdite**.

---

## Driver (`drv_*`)

**Définition**
Composant dont la responsabilité est le **pilotage matériel pur**.

**Caractéristiques** :

* accès direct au hardware (GPIO, I2C, SPI, UART…)
* aucune logique métier
* aucune abstraction fonctionnelle
* aucune dépendance micro-ROS

📌 Un driver **ne décide rien** et **ne transforme rien**.

---

## Driver pollué

**Définition**
Driver (`drv_*`) contenant des responsabilités qui **n’appartiennent pas au pilotage matériel**.

**Exemples de pollution** :

* filtrage de données
* agrégation multi-capteurs
* logique de configuration métier
* abstraction type *provider*, *manager*, *snapshot*

📌 Un driver pollué est **structurellement non conforme**.

---

## Library (`lib_*`)

**Définition**
Composant de logique réutilisable **sans micro-ROS**.

**Responsabilités typiques** :

* filtrage
* conversion
* agrégation
* logique algorithmique
* providers, managers, helpers

📌 Une library **ne touche jamais le hardware directement**.

---

## Middleware (`mw_*`)

**Définition**
Composant d’intégration micro-ROS.

**Responsabilités** :

* interaction avec `rcl`, `rclc`
* builders de messages ROS
* gestion QoS, time sync, allocateurs
* adaptation entre monde embarqué et ROS

📌 Un middleware :

* ❌ n’accède pas au hardware
* ❌ ne contient pas de logique métier

---

## Application (`app_*`)

**Définition**
Composant d’orchestration métier réutilisable.

**Responsabilités** :

* coordination de `drv_*`, `lib_*`, `mw_*`
* logique fonctionnelle de haut niveau
* configuration runtime

📌 Une application **ne hardcode rien**.

---

## Logique métier

**Définition**
Toute logique qui :

* interprète des données
* prend une décision fonctionnelle
* applique un filtrage ou une politique
* orchestre plusieurs composants

**Exemples** :

* filtrage médian
* mapping capteurs → bins
* règles de validité
* temporisation fonctionnelle

📌 La logique métier est **strictement interdite** dans `drv_*`.

---

## Handle opaque

**Définition**
Type public représentant une instance interne, sans exposer sa structure.

```c
typedef struct <component>_s <component>_t;
```

**Objectifs** :

* encapsulation stricte
* stabilité ABI
* prévention des dépendances illégales

📌 Toute structure interne exposée dans un header public est une **violation grave**.

---

## API publique

**Définition**
Ensemble des symboles exposés via :

```
include/<component_api>/
```

**Inclut** :

* fonctions
* types
* enums
* defines

📌 Tout symbole public **doit être namespacé**.

---

## Namespace API

**Définition**
Répertoire `include/<component_api>/`.

**Rôle** :

* isoler les symboles
* éviter les collisions
* refléter l’identité canonique

📌 Aucun header public hors de ce namespace n’est autorisé.

---

## Exemple (`basic_app`)

**Définition**
Application minimale démontrant l’usage **public** de l’API d’un composant.

**Contraintes** :

* utilise uniquement l’API publique
* compile sans modification du composant
* ne contient aucune logique interne du composant

📌 L’exemple est une **preuve d’utilisabilité**.

---

## Contrat d’API

**Définition**
Ensemble des comportements garantis par l’API.

**Inclut** :

* gestion des erreurs
* cycle de vie
* invariants mémoire
* comportement en cas d’entrées invalides

📌 Un contrat non testé est **non respecté**.

---

## Conformité

**Définition**
État d’un composant respectant **toutes** les règles normatives du standard.

📌 La conformité est :

* binaire (oui / non)
* vérifiable automatiquement
* opposable en CI

---

## Non-conformité

**Définition**
Violation explicite ou implicite du standard.

**Conséquences** :

* blocage CI
* refus de merge
* exclusion release

📌 La non-conformité **ne se négocie pas**, elle se corrige.

---

## Exception

**Définition**
Dérogation **temporaire et explicitement documentée** à une règle normative.

**Contraintes** :

* justifiée
* tracée
* validée
* bornée dans le temps

📌 Une exception non documentée est **invalide**.

---

## Agent IA

**Définition**
Tout système automatisé générant ou modifiant du code (ChatGPT, Claude, Copilot, Codex, etc.).

📌 Un agent IA :

* est soumis au même standard qu’un humain
* ne peut invoquer aucune règle externe
* ne peut jamais justifier une violation

---

## Source de vérité

**Définition**
Document **normatif** unique faisant autorité.

Dans ce framework :

```
docs/standard.md
```

📌 Tout autre document est **informatif uniquement**.