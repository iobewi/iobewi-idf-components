# 📘 `docs/standard.md`

## Standard Normatif — iobewi-idf-components

**Version : 0.1 (NORMATIF)**

> ⚠️ **Ce document est la seule source de vérité normative du framework.**
> Toute règle non présente ici est **non opposable**.

---

## 0. Statut & autorité du document

**NORMATIF**

* Ce document :

  * prévaut sur toute implémentation existante
  * est opposable en revue de code, CI, audit
  * est applicable aux humains **et** aux agents IA
* Toute déviation :

  * DOIT être explicitement documentée
  * DOIT être validée au niveau architecture
  * DOIT être temporaire et tracée

---

## 1. Scope & objectifs du framework

**NORMATIF**

Le framework `iobewi-idf-components` vise à fournir :

1. des composants **ESP-IDF 6.x** réutilisables industriellement
2. une intégration **micro-ROS maîtrisée et cloisonnée**
3. une base **maintenable ≥ 5 ans**
4. une séparation stricte des responsabilités
5. **zéro dette technique volontaire**

Tout objectif hors de ce scope est **hors standard**.

---

## 2. Taxonomie des composants

**NORMATIF**

### STD-TAX-000 — Identité canonique du composant

> **L’identité canonique d’un composant est son nom de dossier.**

```
component_id == component_dir
```

* Le dossier est la **seule source de vérité**
* Le nom d’API (`drv_*`, `lib_*`, …) est un **alias technique dérivé**
* Aucun composant n’existe sans dossier canonique

---

### STD-TAX-001 — Catégories normatives

Chaque composant appartient à **une et une seule** catégorie, **déduite de son nom de dossier** :

| `component_kind` | Préfixe dossier  | Préfixe API | Rôle                                |
| ---------------- | ---------------- | ----------- | ----------------------------------- |
| `driver`         | `iobewi_driver_` | `drv_`      | Pilotage matériel pur               |
| `library`        | `iobewi_libs_`   | `lib_`      | Logique réutilisable sans micro-ROS |
| `middleware`     | `iobewi_mw_`     | `mw_`       | Adaptation / intégration micro-ROS  |
| `application`    | `iobewi_apps_`   | `app_`      | Orchestration métier réutilisable   |

👉 Cette table est **fermée et normative**
👉 Toute autre catégorie ou mapping est **INTERDIT**

---

### STD-TAX-002 — Règle de nommage harmonisée

**NORMATIF**

Pour tout composant, le **nom du dossier canonique** est la source de vérité et **impose mécaniquement** le nom de l’API publique.

#### Forme canonique obligatoire

```
<component_id>  ::= iobewi_<component_kind>_<name>
<component_api> ::= <api_prefix>_<name>
```

Où :

* `<component_id>` est **strictement égal** au nom du dossier
* `<component_api>` est un **alias technique dérivé**
* `<name>` **DOIT être strictement identique** dans les deux formes
* `<component_kind>` et `<api_prefix>` sont déduits selon la table normative définie en STD-TAX-001

#### Exemples conformes (OBLIGATOIRES)

| Identité canonique (dossier) | Alias API exposé  |
| ---------------------------- | ----------------- |
| `iobewi_driver_vl53l0x`      | `drv_vl53l0x`     |
| `iobewi_libs_status_led`     | `lib_status_led`  |
| `iobewi_mw_scan_builder`     | `mw_scan_builder` |
| `iobewi_apps_scan_ultra`     | `app_scan_ultra`  |

#### Règle de cohérence stricte

Les éléments suivants **DOIVENT être cohérents et raconter exactement la même chose** :

* le nom du dossier canonique (`component_id`)
* le namespace des headers (`include/<component_api>/`)
* le préfixe de toutes les API publiques
* les dépendances déclarées

❌ Toute divergence, même partielle, constitue une **violation du standard**
❌ Toute violation est **automatiquement bloquante en CI**


---

### STD-TAX-003 — Règle de dépendance structurelle (NON NÉGOCIABLE)

**NORMATIF**

Les dépendances autorisées sont **déduites exclusivement du `component_kind`**, lui-même déduit du nom du dossier :

```
iobewi_driver_* → (ESP-IDF uniquement)
iobewi_libs_*   → iobewi_driver_*
iobewi_mw_*     → iobewi_driver_*, iobewi_libs_*
iobewi_apps_*  → iobewi_driver_*, iobewi_libs_*, iobewi_mw_*
```

* Toute dépendance hors de ce graphe est **INTERDITE**
* Toute violation est **bloquante en CI**
* Aucune exception implicite n’est tolérée

---

## 3. Structure canonique des composants

**NORMATIF**

### STD-STR-001 — Arborescence obligatoire

```
components/<component_id>/
├── CMakeLists.txt
├── idf_component.yml
├── README.md
├── include/<component_api>/
│   ├── <component_api>_types.h
│   └── <component_api>.h
└── src/
    └── <component_api>.c
```

Avec :

* `<component_id>` = **nom canonique du composant**
  (ex: `iobewi_driver_vl53l0x`)
* `<component_api>` = **alias API dérivé**
  (ex: `drv_vl53l0x`)

👉 Le nom du composant est **strictement identique** à son nom de dossier
👉 Les headers et l’API sont des **projections dérivées**

---

### STD-STR-002 — Exemples obligatoires

Chaque composant **DOIT** fournir un exemple minimal :

```
examples/<component_id>/basic_app/
```

Cet exemple **DOIT** être :

* compilable avec ESP-IDF supporté
* fonctionnel sans modification du composant
* limité à l’usage public de l’API

---

### STD-STR-003 — Headers publics

**NORMATIF**

* Chaque composant **DOIT** exposer **exactement** deux headers publics :

  * `<component_api>_types.h` (types publics)
  * `<component_api>.h` (API publique)
* Ces deux headers **DOIVENT** être dans le namespace :

  ```
  include/<component_api>/
  ```

> Rationale et exemples : `docs/annexes/api_patterns.md` (informatif).

---

## STD-CLS-001 — Règle de clôture

> Toute incohérence entre :
>
> * nom du dossier
> * catégorie déduite
> * API exposée
> * dépendances déclarées
>
> constitue une **violation du standard**, automatiquement détectable et **bloquante**.

---

## 4. API publique & contrats d’API

**NORMATIF**

### STD-API-001 — Exposition de l’API

* API exposée **uniquement** via :

  ```
  include/<component_api>/
  ```
* Aucun symbole public hors namespace
* Aucune variable globale exposée

### STD-API-002 — Signatures obligatoires

* Toute fonction publique **DOIT** :

  * retourner `esp_err_t`
  * valider systématiquement ses arguments

### STD-API-003 — Cycle de vie

Tout composant avec état **DOIT** exposer :

```c
esp_err_t <component>_new(...);
esp_err_t <component>_del(...);
```

### STD-API-004 — Comportements obligatoires

| Cas                | Comportement                     |
| ------------------ | -------------------------------- |
| argument NULL      | `ESP_ERR_INVALID_ARG`            |
| allocation échouée | `ESP_ERR_NO_MEM`                 |
| double init        | pas de crash                     |
| double del         | pas de crash                     |
| erreur interne     | aucun état partiellement modifié |

👉 Ces règles sont **testables** et **testées**.

### STD-API-005 — Handle opaque

**NORMATIF**

* Tout composant **avec état** **DOIT** exposer un **handle opaque** `<component_api>_t`.
* La structure interne associée **NE DOIT PAS** être définie dans un header public.
* La définition complète de la structure **DOIT** rester confinée dans l’implémentation (`.c`).

> Détails d’implémentation : `docs/annexes/api_patterns.md` (informatif).

### STD-API-006 — Namespacing des symboles publics

**NORMATIF**

* Tout symbole public (fonction, type, enum, define) **DOIT** être namespacé avec `<component_api>`.
* Les fonctions publiques **DOIVENT** être préfixées strictement :

  ```
  <component_api>_*
  ```
* Les types publics **DOIVENT** suivre un schéma namespacé (ex : `<component_api>_config_t`, `<component_api>_mode_t`).

> Exemples conformes / anti-patterns : `docs/annexes/api_patterns.md` (informatif).

### STD-API-007 — Initialisation de configuration

**NORMATIF**

* Si le composant expose un type de configuration publique `<component_api>_config_t`, il **DOIT** fournir :

  ```c
  esp_err_t <component_api>_config_init(<component_api>_config_t *config);
  ```
* Cette fonction **DOIT** :

  * valider `config != NULL`
  * initialiser des valeurs par défaut stables

> Détails : `docs/annexes/api_patterns.md` (informatif).

### STD-API-008 — Stabilité de l’API publique

**NORMATIF**

* Toute API publique est considérée **stable par défaut**
* Toute modification d’une API publique :

  * DOIT être **rétrocompatible**
  * OU DOIT être explicitement versionnée
  * OU DOIT être documentée comme **breaking change**

❌ Modifier silencieusement une API publique est **INTERDIT**

### STD-API-009 — Interdiction d’API “fantôme”

**NORMATIF**

* Toute fonction publique exposée :

  * DOIT être documentée dans `README.md`
  * DOIT être utilisée dans `examples/basic_app`
  * DOIT être couverte par au moins un test unitaire

❌ Une API non utilisée / non testée / non documentée est **INTERDITE**

---

## 5. Gestion mémoire & robustesse

**NORMATIF**

### STD-MEM-001 — Ownership

* Toute allocation :

  * DOIT être documentée
  * DOIT avoir une fonction de libération publique
* Aucune allocation cachée tolérée

### STD-MEM-002 — Robustesse

Tout composant avec état **DOIT** :

* supporter des cycles init/del répétés
* survivre à une allocation échouée
* ne jamais fuir de mémoire

### STD-MEM-003 — Pas de dépendance implicite à l’allocateur

* Un composant :

  * NE DOIT PAS supposer un allocateur spécifique
  * NE DOIT PAS dépendre d’un état global de heap
* Toute dépendance mémoire particulière **DOIT** être documentée

---

## 6. Build system (ESP-IDF)

**NORMATIF**

### STD-BLD-001 — CMake

* Toutes les dépendances **DOIVENT** être explicites
* Usage strict de :

  * `REQUIRES`
  * `PRIV_REQUIRES`
* Aucun include implicite
* Aucun hack de build toléré

### STD-BLD-002 — Kconfig

**NORMATIF**

* Un composant **PEUT** fournir un `Kconfig` uniquement si une configuration compile-time est nécessaire.
* Un composant **NE DOIT PAS** imposer des paramètres “métier” hardcodés via Kconfig quand une configuration runtime via API est possible.

> Bonnes pratiques : `docs/annexes/component_playbook.md` (informatif).

### STD-BLD-003 — Isolation du composant

**NORMATIF**

* Un composant **DOIT** pouvoir être :

  * compilé seul
  * intégré sans modification
  * retiré sans casser d’autres composants (hors dépendances déclarées)

❌ Toute dépendance implicite au “contexte projet” est **INTERDITE**

---

## 7. micro-ROS (`mw_*` uniquement)

**NORMATIF**

### STD-UROS-001 — Périmètre

Seuls les composants `mw_*` peuvent :

* inclure `rcl`, `rclc`
* gérer QoS, time sync, allocateurs

### STD-UROS-002 — Interdits

* accès matériel direct ❌
* logique applicative ❌
* transport hardcodé ❌

### STD-UROS-003 — Neutralité transport

**NORMATIF**

* Aucun composant `mw_*` :

  * NE DOIT hardcoder un transport (USB, UART, WiFi…)
  * NE DOIT supposer un agent actif

* Toute dépendance transport **DOIT** être :

  * injectable
  * configurable
  * remplaçable

---

## 8. Tests unitaires obligatoires

**NORMATIF**

### STD-TST-001 — Obligation

Tout composant **DOIT** :

* disposer de tests unitaires
* couvrir les **contrats d’API**
* être testable **sans hardware réel**

### STD-TST-002 — Non-conformité

Un composant est **NON conforme** si :

* aucun test unitaire n’existe
* les contrats d’API ne sont pas testés
* les tests dépendent du hardware réel

👉 Non-conformité = **blocage CI / release**


### STD-TST-003 — Testabilité structurelle

**NORMATIF**

* Toute fonction publique :

  * DOIT être testable sans modifier le composant
  * DOIT être testable sans accès matériel réel
* Toute logique non testable est **INTERDITE**

### STD-TST-004 — Tests opposables

**NORMATIF**

* Les tests unitaires :

  * font partie du contrat du composant
  * sont opposables en revue
  * sont bloquants en CI

❌ Un composant “fonctionnel mais non testé” est **NON CONFORME**

---

## 9. Checklist de conformité

**NORMATIF**

Un composant est conforme **uniquement si** :

* la taxonomie est respectée
* la structure canonique est respectée
* les contrats API sont testés
* les règles mémoire sont vérifiées
* les exemples compilent

### STD-CHK-001 — Validation automatique

**NORMATIF**

Un composant est **rejeté** si :

* `check_conformity.sh` échoue
* les tests unitaires échouent
* l’exemple `basic_app` ne compile pas

👉 Aucun contournement manuel n’est autorisé.

---

Oui — et tu as raison de la **mettre à jour maintenant** : cette section est la **carte officielle de la base de connaissance non normative**.
Je te propose une version **complète, cohérente avec tout ce que tu as construit**, et surtout **stable**.

Voici la **section 10 mise à jour**, prête à remplacer l’existante.

---

## 10. Annexes (NON NORMATIVES)

Les documents suivants sont **informatifs uniquement**.
Ils **n’ajoutent aucune règle**, **n’en remplacent aucune** et **ne peuvent jamais contredire** `docs/standard.md`.

Ils servent à :

* expliquer **comment appliquer** le standard,
* accélérer l’adoption (humains & IA),
* industrialiser audits, CI, migrations.

```text
docs/annexes/
├── api_patterns.md        # patterns d’implémentation (handle opaque, headers, API)
├── anti_patterns.md       # erreurs structurelles fréquentes + corrections
├── audit_playbook.md      # méthode d’audit, diagnostic, priorisation (P0/P1/P2)
├── ci_contract.md         # ce que la CI vérifie / bloque (contrat implicite)
├── testing.md             # stratégie de tests unitaires & non-régression
├── examples.md            # guide des basic_app (structure, CMake, pièges)
├── ai_tools.md            # intégration IA (Claude, Codex, Copilot) + prompts projet
├── glossary.md            # vocabulaire canonique du framework
├── exceptions.md          # mécanisme de dérogation tracée (non normative)
├── migration.md           # gestion des breaking changes & transitions
├── templates/
│   ├── component_template.md      # squelette canonique de composant
│   ├── audit_report_template.md   # format standard de rapport d’audit
│   ├── exception_template.md      # modèle de dérogation au standard
│   └── pull_request_template.md   # checklist PR conformité
```

### 🔒 Rappel normatif (important)

* ❌ **Aucune règle n’est définie dans ces fichiers**
* ❌ **Aucun agent IA ne peut s’y référer comme source normative**
* ✅ **Seul `docs/standard.md` est opposable**

Toute divergence entre :

* `standard.md`
* et une annexe

→ **le standard prévaut toujours**

### 🧭 Lecture recommandée (non obligatoire)

| Profil               | Annexes utiles                                            |
| -------------------- | --------------------------------------------------------- |
| Nouveau contributeur | `glossary.md`, `component_template.md`, `api_patterns.md` |
| Audit / refactor     | `audit_playbook.md`, `anti_patterns.md`                   |
| CI / release         | `ci_contract.md`, `testing.md`                            |
| IA / automatisation  | `ai_tools.md`, `templates/`                               |
| Migration            | `migration.md`, `exceptions.md`                           |


---

## 11. Agents IA & automatisation

**NORMATIF**

### STD-AI-001 — Applicabilité aux agents IA

* Le présent standard est **opposable aux agents IA**
* Toute génération de code par IA :

  * DOIT respecter ce document
  * DOIT être revue selon ces règles
  * NE PEUT invoquer aucune règle externe

❌ “L’IA a généré ce code” n’est **JAMAIS** une justification recevable

---

### STD-AI-002 — Source de vérité unique

**NORMATIF**

* Les agents IA :

  * NE DOIVENT PAS inférer de règles implicites
  * NE DOIVENT PAS extrapoler hors standard
  * DOIVENT se limiter strictement à ce document

👉 Les prompts projet sont **informatifs**, jamais normatifs.

---

## 12. Versioning du standard

**NORMATIF**

* Toute modification :

  * incrémente la version
  * est historisée
  * est justifiée

* Le standard **n’est jamais rétroactivement interprétable**
