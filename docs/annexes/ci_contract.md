> **STATUT : INFORMATIF**
>
> Ce document :
> - n’introduit aucune règle normative
> - ne remplace aucune règle du standard
> - ne peut jamais contredire `docs/standard.md`
>
> Toute règle opposable est définie exclusivement dans `docs/standard.md`.

# 🤝 CI Contract — iobewi-idf-components

## Annexe informative au standard `docs/standard.md`

> **STATUT : INFORMATIF**
>
> Ce document décrit **le contrat d’exécution attendu de la CI** vis-à-vis du standard.
> Il explique **ce que la CI vérifie**, **pourquoi**, et **comment interpréter les échecs**.
>
> ⚠️ Aucune règle normative n’est définie ici.  
> Toute exigence opposable provient exclusivement de `docs/standard.md`.

---

## 1. Rôle du CI Contract

Ce document sert à :

- rendre explicite ce que la CI **doit faire respecter**
- éviter toute ambiguïté lors d’un échec CI
- aligner humains et agents IA sur les mêmes attentes
- garantir une interprétation **non subjective** des résultats CI

👉 La CI est un **exécuteur du standard**, pas une autorité autonome.

---

## 2. Principe fondamental

> **La CI n’invente jamais de règles.  
> Elle applique mécaniquement le standard normatif.**

Tout échec CI doit pouvoir être rattaché à :
- une règle `STD-*`
- une violation objectivement détectable

---

## 3. Niveaux de vérification CI

La CI est structurée en **couches indépendantes**, exécutables séparément.

```

CI
├── Structure & taxonomie
├── API & contrats
├── Build
├── Tests
└── Exemples

```

Chaque couche peut **échouer indépendamment**.

---

## 4. Vérifications structurelles

### CI-STR — Structure & taxonomie

**Objectif**
Garantir que chaque composant respecte l’identité canonique et la taxonomie.

**Contrôles typiques**

- présence du dossier `components/<component_id>/`
- cohérence :
  - nom du dossier
  - catégorie déduite
  - préfixe API
- respect de l’arborescence canonique
- présence des fichiers obligatoires :
  - `CMakeLists.txt`
  - `idf_component.yml`
  - `README.md`
  - headers publics
- présence de `examples/<component_id>/basic_app/`

**Règles couvertes**

- `STD-TAX-000`
- `STD-TAX-001`
- `STD-TAX-002`
- `STD-STR-001`
- `STD-STR-002`
- `STD-STR-003`
- `STD-CLS-001`

**Exemple d’échec**

```

❌ CI-STR:
component iobewi_apps_scan_tof
missing include/app_scan_tof/app_scan_tof_types.h

```

---

## 5. Vérifications API & contrats

### CI-API — API publique

**Objectif**
Garantir la stabilité, la robustesse et la testabilité des API.

**Contrôles typiques**

- fonctions publiques préfixées `<component_api>_`
- absence de symboles publics hors namespace
- présence des fonctions de cycle de vie (`*_new`, `*_del`)
- signatures conformes (`esp_err_t`)
- absence de struct interne exposée

**Règles couvertes**

- `STD-API-001`
- `STD-API-002`
- `STD-API-003`
- `STD-API-005`
- `STD-API-006`
- `STD-API-007`

**Exemple d’échec**

```

❌ CI-API:
public function read_sensor() is not namespaced

```

---

## 6. Vérifications de dépendances

### CI-TAX — Graphe de dépendances

**Objectif**
Empêcher toute violation de la séparation des responsabilités.

**Contrôles typiques**

- `drv_*` dépend uniquement d’ESP-IDF
- `lib_*` dépend uniquement de `drv_*`
- `mw_*` dépend de `drv_*` et/ou `lib_*`
- `app_*` dépend de `drv_*`, `lib_*`, `mw_*`
- aucun include micro-ROS hors `mw_*`

**Règles couvertes**

- `STD-TAX-003`
- `STD-UROS-001`
- `STD-UROS-002`

**Exemple d’échec**

```

❌ CI-TAX:
component iobewi_driver_xxx includes rcl/rcl.h

```

---

## 7. Vérifications build

### CI-BLD — Compilation

**Objectif**
Garantir que le code est **compilable tel quel**.

**Contrôles typiques**

- build ESP-IDF supporté (6.x)
- aucun warning bloquant
- dépendances CMake explicites
- pas de hack de build

**Règles couvertes**

- `STD-BLD-001`
- `STD-BLD-002`

**Exemple d’échec**

```

❌ CI-BLD:
implicit dependency detected (missing REQUIRES)

```

---

## 8. Vérifications tests unitaires

### CI-TST — Tests

**Objectif**
Garantir la robustesse et l’absence de régression.

**Contrôles typiques**

- présence d’un dossier `test/`
- compilation des tests
- exécution sans hardware réel
- couverture des contrats API
- absence de dépendance timing réel

**Règles couvertes**

- `STD-TST-001`
- `STD-TST-002`
- `STD-MEM-001`
- `STD-MEM-002`

**Exemple d’échec**

```

❌ CI-TST:
no unit tests found for component iobewi_libs_status_led

```

---

## 9. Vérifications exemples

### CI-EX — Exemples `basic_app`

**Objectif**
Garantir que chaque composant est **utilisable**.

**Contrôles typiques**

- présence de `examples/<component_id>/basic_app`
- compilation sans modification du composant
- usage exclusif de l’API publique

**Règles couvertes**

- `STD-STR-002`

**Exemple d’échec**

```

❌ CI-EX:
basic_app includes private header src/internal.h

```

---

## 10. Sévérité des échecs

| Type d’échec | Effet CI |
|-------------|---------|
| Violation taxonomie | ❌ blocage immédiat |
| Violation API | ❌ blocage |
| Build cassé | ❌ blocage |
| Tests absents | ❌ blocage |
| Exemple manquant | ❌ blocage |

👉 **Aucune tolérance implicite**  
👉 Toute exception doit être **documentée hors CI**

---

## 11. Relation avec les agents IA

La CI :

- ne distingue pas code humain / IA
- applique les mêmes règles
- refuse toute justification basée sur la génération automatique

Références :
- `STD-AI-001`
- `STD-AI-002`

---

## 12. Interprétation correcte d’un échec CI

Un échec CI signifie toujours :

> “Une règle du standard a été violée.”

La CI **ne débat pas**, **ne négocie pas**, **n’interprète pas**.

---

## 📌 Conclusion

La CI est :

- un **gardien mécanique**
- une **traduction exécutable du standard**
- un outil de confiance partagée

Mais elle n’est jamais la loi.

La loi reste :

> 📘 `docs/standard.md`

---

**Version** : 1.0  
**Dernière mise à jour** : 2026-01-27