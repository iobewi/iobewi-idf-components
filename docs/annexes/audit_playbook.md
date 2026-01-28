> **STATUT : INFORMATIF**
>
> Ce document :
> - n’introduit aucune règle normative
> - ne remplace aucune règle du standard
> - ne peut jamais contredire `docs/standard.md`
>
> Toute règle opposable est définie exclusivement dans `docs/standard.md`.

# 🧪 Audit Playbook — iobewi-idf-components

## Procédure d’audit de conformité

## Annexe informative au standard `docs/standard.md`

> **STATUT : INFORMATIF — PROCÉDURAL**
>
> Ce document décrit **COMMENT AUDITER** un composant vis-à-vis du standard.
>
> * Il **n’introduit aucune règle normative**
> * Il **n’interprète jamais** le standard
> * Il **n’explique pas comment corriger**
>
> 👉 Toute règle opposable est définie **exclusivement** dans `docs/standard.md`.

---

## 1. Objectif du playbook

Ce playbook définit une **procédure d’audit stricte, reproductible et déterministe** permettant de :

* auditer un composant existant
* qualifier sa conformité au standard
* identifier des **violations factuelles**
* produire un **verdict exploitable** (humain / CI / IA)

👉 Ce document est un **outil d’exécution**, pas un guide de conception.

---

## 2. Quand utiliser ce playbook

Ce playbook est utilisé :

* lors d’un audit initial de dépôt
* avant une release
* dans une pipeline CI
* en revue de code formelle
* par un agent IA d’analyse statique

Il n’est pas destiné à être utilisé pour :

* développer un composant
* décider d’une architecture
* proposer une solution technique

---

## 3. Principe fondamental

> **Un audit vérifie des faits observables.
> Il ne juge jamais l’intention.**

Sont **irrecevables** :

* “ça fonctionne”
* “c’est temporaire”
* “ce sera refactoré plus tard”
* “c’est généré par une IA”

Un audit se base **uniquement** sur :

* les noms
* les fichiers présents
* les dépendances déclarées
* les signatures publiques
* les tests existants

---

## 4. Ordre d’audit (STRICT)

Un audit suit **toujours** l’ordre ci-dessous :

1. Identification canonique
2. Taxonomie & dépendances
3. Structure de fichiers
4. API publique
5. Gestion mémoire
6. Exemples
7. Tests unitaires
8. Verdict final

⚠️

* L’ordre ne devrait pas être modifié
* Aucune étape n’est censée être sautée

---

## 5. Étape 1 — Identification canonique

### Objectif

Vérifier que le composant **existe formellement** selon le standard.

### Vérifications

* [ ] Le composant possède un dossier sous `components/`
* [ ] Le nom du dossier commence par `iobewi_`
* [ ] Le composant correspond à **une seule catégorie normative**

### Données à extraire

* `component_id`
* catégorie normative déduite
* ambiguïtés éventuelles

### Verdict

* ✅ OK
* ❌ Violation `STD-TAX-000` / `STD-TAX-001`

---

## 6. Étape 2 — Taxonomie & dépendances

### Objectif

Vérifier que le composant respecte **strictement** son rôle normatif.

### Vérifications

* [ ] Dépendances compatibles avec la catégorie
* [ ] Aucun include listé comme interdit par le standard (ex: `rcl` dans `drv_*`)
* [ ] Aucun accès matériel hors `drv_*`

### Outils autorisés

```bash
grep rcl
grep i2c_master
grep gpio
```

Inspection de :

* `CMakeLists.txt`
* includes publics

### Verdict

* ✅ OK
* ❌ Violation `STD-TAX-003`

---

## 7. Étape 3 — Structure de fichiers

### Objectif

Valider la **structure canonique minimale**.

### Vérifications

* [ ] `CMakeLists.txt`
* [ ] `idf_component.yml`
* [ ] `README.md`
* [ ] `include/<component_api>/`
* [ ] `<component_api>_types.h`
* [ ] `<component_api>.h`
* [ ] `src/<component_api>.c`

### Red flags immédiats

* headers hors namespace
* fichiers `*_engine.h`
* types et API mélangés
* plusieurs `.c` sans justification documentée

### Verdict

* ✅ OK
* ❌ Violation `STD-STR-001` / `STD-STR-003`

---

## 8. Étape 4 — API publique

### Objectif

Vérifier les **contrats observables** de l’API.

### Vérifications

* [ ] Toutes les fonctions sont préfixées `<component_api>_`
* [ ] Toutes retournent `esp_err_t`
* [ ] Validation systématique des arguments
* [ ] `*_new()` / `*_del()` présents si état

### Inspection typique

```bash
grep -R "esp_err_t" include/<component_api>/
```

### Verdict

* ✅ OK
* ❌ Violation `STD-API-001` → `STD-API-007`

---

## 9. Étape 5 — Gestion mémoire

### Objectif

Détecter toute **non-conformité mémoire observable**.

### Vérifications

* [ ] Allocations explicites
* [ ] Libérations associées
* [ ] Aucun ownership ambigu
* [ ] Comportement sain en cas d’erreur

### Verdict

* ✅ OK
* ❌ Violation `STD-MEM-001` / `STD-MEM-002`

---

## 10. Étape 6 — Exemples (`basic_app`)

### Objectif

Vérifier l’utilisabilité minimale du composant.

### Vérifications

* [ ] `examples/<component_id>/basic_app/` existe
* [ ] Compile sans modification
* [ ] Utilise uniquement l’API publique
* [ ] Ne contient aucune logique métier

### Verdict

* ✅ OK
* ❌ Violation `STD-STR-002`

---

## 11. Étape 7 — Tests unitaires

### Objectif

Vérifier la **testabilité réelle** du composant.

### Vérifications

* [ ] Tests unitaires présents
* [ ] Aucun hardware réel requis
* [ ] Contrats API testés
* [ ] Cas d’erreur couverts

### Verdict

* ✅ OK
* ❌ Violation `STD-TST-001` / `STD-TST-002`

---

## 12. Étape 8 — Verdict final

### Règle absolue

> **Une seule violation normative ⇒ composant NON conforme**

Il n’existe :

* aucun score
* aucun “presque conforme”
* aucune interprétation

### États possibles

| État           | Signification            |
| -------------- | ------------------------ |
| ✅ Conforme     | Aucune violation         |
| ❌ Non conforme | ≥ 1 violation            |
| ⚠️ Dérogation  | Documentée hors standard |

---

## 13. Artefact d’audit (recommandé)

Chaque audit devrait produire un artefact factuel :

```md
Audit — <component_id>

Date :
Auditeur :
Version du standard :

Violations détectées :
- STD-XXX-YYY : constat factuel

Verdict :
- Conforme / Non conforme
```

---

## 14. Usage par agents IA

Les agents IA :

* DOIVENT suivre l’ordre strict
* NE DOIVENT PAS interpréter
* NE DOIVENT PAS proposer de solution
* NE DOIVENT produire que des constats observables

---

## 📌 Conclusion

Ce playbook :

* rend les audits **objectifs et déterministes**
* permet l’automatisation CI
* protège le standard de toute dérive interprétative

Il est un **outil d’audit**, pas un guide de conception.

👉 **Autorité unique** : `docs/standard.md`

---

**Version** : 1.1
**Dernière mise à jour** : 2026-01-27
