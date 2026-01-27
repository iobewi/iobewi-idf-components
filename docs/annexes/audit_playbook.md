# 🧪 Audit Playbook — iobewi-idf-components

## Annexe informative au standard `docs/standard.md`

> **STATUT : INFORMATIF**
>
> Ce document décrit **comment auditer** un composant vis-à-vis du standard.
> Il **n’introduit aucune règle normative**.
>
> Toute règle opposable est définie **exclusivement** dans `docs/standard.md`.

---

## 1. Objectif du playbook

Ce playbook fournit une **méthodologie reproductible** pour :

- auditer un composant existant
- qualifier sa conformité au standard
- identifier précisément les violations
- produire un diagnostic exploitable (humain / CI / IA)

👉 Il ne remplace **jamais** le standard  
👉 Il ne l’interprète **jamais**

---

## 2. Quand utiliser ce playbook

Ce playbook est utilisé :

- lors d’un audit initial d’un dépôt
- avant une release
- lors d’une intégration CI
- en revue de code
- par un agent IA chargé d’analyse statique

Il **n’est pas** un guide de développement.

---

## 3. Principe fondamental

> **Un audit vérifie des faits observables.  
> Il ne juge jamais l’intention.**

Conséquences :

- “ça marche” ❌ non recevable
- “c’est temporaire” ❌ non recevable
- “l’IA l’a généré” ❌ non recevable

Seuls comptent :
- les noms
- les fichiers
- les dépendances
- les signatures
- les tests

---

## 4. Vue d’ensemble d’un audit

Un audit suit **toujours** cet ordre :

1. Identification canonique
2. Taxonomie & dépendances
3. Structure de fichiers
4. API publique
5. Gestion mémoire
6. Exemples
7. Tests unitaires
8. Verdict

👉 Ne jamais changer l’ordre  
👉 Ne jamais “sauter” une étape

---

## 5. Étape 1 — Identification canonique

### Objectif
Vérifier que le composant **existe formellement** selon le standard.

### Vérifications

- [ ] Le composant a un dossier sous `components/`
- [ ] Le nom du dossier commence par `iobewi_`
- [ ] Le dossier correspond à **une seule catégorie normative**

### Questions à se poser

- Quel est le `component_id` ?
- Quelle catégorie est déduite automatiquement ?
- Existe-t-il une ambiguïté de nommage ?

### Verdict possible

- ✅ OK
- ❌ Violation STD-TAX-000 / STD-TAX-001

---

## 6. Étape 2 — Taxonomie & dépendances

### Objectif
Vérifier que le composant respecte **strictement** son rôle.

### Vérifications

- [ ] Les dépendances sont compatibles avec la catégorie
- [ ] Aucun include interdit (`rcl` dans drv_*, etc.)
- [ ] Aucun appel matériel hors `drv_*`

### Outils utiles

- `grep rcl`
- `grep i2c_master`
- inspection de `CMakeLists.txt`

### Violations typiques

- driver avec logique métier
- lib avec accès GPIO
- mw avec orchestration applicative

### Verdict possible

- ✅ OK
- ❌ Violation STD-TAX-003

---

## 7. Étape 3 — Structure de fichiers

### Objectif
Valider la **structure canonique minimale**.

### Vérifications

- [ ] `CMakeLists.txt`
- [ ] `idf_component.yml`
- [ ] `README.md`
- [ ] `include/<component_api>/`
- [ ] `<component_api>_types.h`
- [ ] `<component_api>.h`
- [ ] `src/<component_api>.c`

### Red flags immédiats

- headers hors namespace
- fichiers `*_engine.h`
- mélange types / API
- multiple `.c` sans justification claire

### Verdict possible

- ✅ OK
- ❌ Violation STD-STR-001 / STD-STR-003

---

## 8. Étape 4 — API publique

### Objectif
Vérifier que l’API respecte les **contrats observables**.

### Vérifications

- [ ] Toutes les fonctions sont préfixées `<component_api>_`
- [ ] Toutes retournent `esp_err_t`
- [ ] Validation systématique des arguments
- [ ] `*_new()` et `*_del()` présents si état

### Inspection typique

```bash
grep -R "esp_err_t" include/<component_api>/
````

### Red flags

* fonctions sans préfixe
* types génériques (`config_t`, `context_t`)
* structs publiques non opaques

### Verdict possible

* ✅ OK
* ❌ Violation STD-API-001 à STD-API-007

---

## 9. Étape 5 — Gestion mémoire

### Objectif

Détecter toute dette mémoire ou ambiguïté d’ownership.

### Vérifications

* [ ] allocations explicites
* [ ] libérations associées
* [ ] pas d’allocation cachée
* [ ] comportement sain en cas d’erreur

### Red flags

* `malloc` sans `free`
* allocation dans une fonction “read”
* absence de `*_del()`

### Verdict possible

* ✅ OK
* ❌ Violation STD-MEM-001 / STD-MEM-002

---

## 10. Étape 6 — Exemples (`basic_app`)

### Objectif

Valider l’utilisabilité réelle du composant.

### Vérifications

* [ ] `examples/<component_id>/basic_app/` existe
* [ ] compile sans modification
* [ ] utilise uniquement l’API publique
* [ ] aucune logique métier

### Red flags

* accès à des headers internes
* contournement d’API
* exemple trop complexe

### Verdict possible

* ✅ OK
* ❌ Violation STD-STR-002

---

## 11. Étape 7 — Tests unitaires

### Objectif

Vérifier la **testabilité réelle** du composant.

### Vérifications

* [ ] présence de tests unitaires
* [ ] tests sans hardware réel
* [ ] contrats API testés
* [ ] erreurs couvertes

### Red flags

* tests absents
* tests “visuels”
* tests dépendants du matériel

### Verdict possible

* ✅ OK
* ❌ Violation STD-TST-001 / STD-TST-002

---

## 12. Étape 8 — Verdict final

### Règle absolue

> **Une seule violation normative ⇒ composant NON conforme**

Il n’existe **aucun score**, **aucun “presque conforme”** au niveau normatif.

### États possibles

| État                     | Signification                           |
| ------------------------ | --------------------------------------- |
| ✅ Conforme               | Aucune violation détectée               |
| ❌ Non conforme           | ≥ 1 règle violée                        |
| ⚠️ Dérogation documentée | Acceptée temporairement (hors standard) |

---

## 13. Traçabilité d’audit (recommandé)

Pour chaque audit, produire un artefact minimal :

```md
## Audit — <component_id>

Date :
Auditeur :
Version standard :

Violations détectées :
- STD-XXX-YYY : description factuelle

Décision :
- Conforme / Non conforme

Actions requises :
- …
```

---

## 14. Usage par agents IA

Les agents IA utilisant ce playbook :

* DOIVENT suivre l’ordre strict
* NE DOIVENT PAS interpréter
* NE DOIVENT PAS proposer de règle nouvelle
* NE DOIVENT produire que des constats factuels

---

## 📌 Conclusion

Ce playbook :

* rend les audits **objectifs**
* élimine les débats subjectifs
* permet l’automatisation progressive
* protège le standard de toute dérive

Il est un **outil**, pas une loi.

La loi reste : `docs/standard.md`.

---

**Version** : 1.0
**Dernière mise à jour** : 2026-01-27