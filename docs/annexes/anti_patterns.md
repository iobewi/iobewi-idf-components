> **STATUT : INFORMATIF**
>
> Ce document :
> - n’introduit aucune règle normative
> - ne remplace aucune règle du standard
> - ne peut jamais contredire `docs/standard.md`
>
> Toute règle opposable est définie exclusivement dans `docs/standard.md`.

# 🚫 Anti-patterns — iobewi-idf-components

## Annexe informative au standard `docs/standard.md`

> **STATUT : INFORMATIF**
>
> Ce document liste des **anti-patterns récurrents observés en audit**.
> Il explique **pourquoi** ils sont problématiques et **comment les corriger**.
>
> ⚠️ Aucune règle n’est créée ici.  
> Toute non-conformité est toujours rattachée à une règle **déjà définie** dans `docs/standard.md`.

---

## 1. Rôle de ce document

Ce document sert à :

- accélérer les audits (reconnaissance rapide des erreurs)
- éviter la réintroduction de dettes techniques connues
- aider les contributeurs (humains ou IA) à **ne pas répéter** les mêmes erreurs
- fournir un vocabulaire commun en revue de code

👉 Ce document **n’est pas normatif**  
👉 Il **n’ajoute aucune exigence**

---

## 2. Principe fondamental

> **Un anti-pattern n’est pas “mal” parce qu’il est moche.  
> Il est problématique parce qu’il viole une règle normative existante.**

Chaque anti-pattern est donc toujours lié à :
- une **règle violée**
- une **conséquence observable**
- une **trajectoire de correction claire**

---

## 3. Anti-patterns de taxonomie (les plus critiques)

### AP-TAX-001 — Driver pollué par de la logique métier

**Symptômes observables**

- fichiers nommés :
  - `*_provider.*`
  - `*_manager.*`
  - `*_snapshot.*`
- filtrage, mapping, agrégation dans un `drv_*`
- structures “riches” retournées par un driver

**Pourquoi c’est un problème**

- viole la séparation des responsabilités
- empêche la réutilisabilité
- rend le driver impossible à tester isolément

**Règle violée**

- `STD-TAX-003`

**Correction attendue**

- extraire la logique en `lib_*`
- laisser au driver :
  - accès matériel
  - registres
  - trames brutes

---

### AP-TAX-002 — `mw_*` qui touche au hardware

**Symptômes observables**

- `gpio_*`, `i2c_*`, `uart_*` dans un middleware
- initialisation de périphériques ESP-IDF dans `mw_*`

**Pourquoi c’est un problème**

- mélange infrastructure ROS et hardware
- rend le middleware non portable
- complique les tests unitaires

**Règles violées**

- `STD-TAX-003`
- `STD-UROS-001`

**Correction attendue**

- déplacer l’accès matériel dans un `drv_*`
- injecter le driver dans le `mw_*`

---

### AP-TAX-003 — Application qui “recrée” un middleware

**Symptômes observables**

- logique micro-ROS dans `app_*`
- appels directs à `rcl/rclc` hors `mw_*`

**Pourquoi c’est un problème**

- duplication
- couplage fort
- impossibilité de mutualiser

**Règle violée**

- `STD-TAX-003`
- `STD-UROS-001`

---

## 4. Anti-patterns de structure

### AP-STR-001 — Header unique fourre-tout

**Symptômes observables**

- un seul header public
- types + API mélangés
- noms génériques (`config_t`, `state_t`)

**Pourquoi c’est un problème**

- API illisible
- dépendances implicites
- évolution risquée

**Règles violées**

- `STD-STR-003`
- `STD-API-006`

**Correction attendue**

- séparer :
  - `<component>_types.h`
  - `<component>.h`

---

### AP-STR-002 — Headers publics hors namespace

**Symptômes observables**

- `include/foo.h`
- headers directement sous `include/`

**Pourquoi c’est un problème**

- collisions de noms
- include ambigu
- API non isolée

**Règle violée**

- `STD-STR-003`

---

### AP-STR-003 — Multiples `.c` sans justification claire

**Symptômes observables**

- `*_engine.c`
- `*_core.c`
- `*_utils.c` dans un même composant

**Pourquoi c’est un problème**

- responsabilités floues
- composant trop large
- souvent signe d’un mauvais découpage

**Règle généralement associée**

- violation indirecte de `STD-TAX-003`

**Correction attendue**

- scinder en plusieurs composants
- ou justifier explicitement (cas rares)

---

## 5. Anti-patterns d’API

### AP-API-001 — Fonctions non préfixées

**Symptômes observables**

```c
esp_err_t init(void);
````

**Pourquoi c’est un problème**

* collision de symboles
* API ambiguë

**Règle violée**

* `STD-API-006`

---

### AP-API-002 — Struct publique exposée

**Symptômes observables**

```c
typedef struct {
    int state;
    int fd;
} drv_xxx_t;
```

**Pourquoi c’est un problème**

* casse l’encapsulation
* ABI figée
* impossible à faire évoluer

**Règle violée**

* `STD-API-005`

---

### AP-API-003 — Fonction publique sans validation d’arguments

**Symptômes observables**

* aucun check `NULL`
* crash possible

**Pourquoi c’est un problème**

* API fragile
* comportements indéterminés

**Règle violée**

* `STD-API-002`

---

## 6. Anti-patterns mémoire

### AP-MEM-001 — Allocation cachée

**Symptômes observables**

* `malloc` dans une fonction `read()`
* pas de fonction de libération associée

**Pourquoi c’est un problème**

* fuites mémoire
* ownership flou

**Règles violées**

* `STD-MEM-001`
* `STD-MEM-002`

---

### AP-MEM-002 — Absence de `*_del()`

**Symptômes observables**

* `*_new()` sans `*_del()`

**Pourquoi c’est un problème**

* impossibilité de libérer proprement
* non-testable

**Règle violée**

* `STD-API-003`

---

## 7. Anti-patterns exemples & tests

### AP-EX-001 — Exemple qui contourne l’API

**Symptômes observables**

* include de headers internes
* accès à des champs privés

**Pourquoi c’est un problème**

* masque les défauts d’API
* trompe l’utilisateur

**Règle violée**

* `STD-STR-002`

---

### AP-TST-001 — “Tests” dépendants du hardware

**Symptômes observables**

* capteur requis
* délais réels
* GPIO réels

**Pourquoi c’est un problème**

* non déterministe
* inutilisable en CI

**Règles violées**

* `STD-TST-001`
* `STD-TST-002`

---

## 8. Anti-patterns IA spécifiques

### AP-AI-001 — Code généré sans alignement standard

**Symptômes observables**

* patterns “génériques”
* structures non opaques
* noms non préfixés

**Pourquoi c’est un problème**

* l’IA n’est pas une autorité normative
* dette technique immédiate

**Règles violées**

* `STD-AI-001`
* `STD-AI-002`

---

## 9. Utilisation par agents IA

Un agent IA utilisant ce document :

* DOIT s’en servir comme **catalogue d’odeurs**
* NE DOIT PAS en déduire de nouvelles règles
* DOIT toujours rattacher un anti-pattern à une règle du standard

---

## 📌 Conclusion

Les anti-patterns :

* accélèrent la détection des violations
* évitent les débats subjectifs
* renforcent la cohérence du framework

Mais ils **ne remplacent jamais** le standard.

La seule loi reste :

> 📘 `docs/standard.md`

---

**Version** : 1.0
**Dernière mise à jour** : 2026-01-27