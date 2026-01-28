> **STATUT : INFORMATIF**
>
> Ce document :
> - n’introduit aucune règle normative
> - ne remplace aucune règle du standard
> - ne peut jamais contredire `docs/standard.md`
>
> Toute règle opposable est définie exclusivement dans `docs/standard.md`.

# 🤖 Outils IA & Prompts Projet — iobewi-idf-components

## Annexe au standard `iobewi-idf-components`

> **Statut : INFORMATIF**
>
> Ce document **n’introduit aucune règle normative**.
> Il décrit **comment utiliser des assistants IA** (Claude, Codex, GitHub Copilot, etc.)
> **sans jamais affaiblir** les exigences définies dans `docs/standard.md`.

---

## 1. Principe fondamental

Les assistants IA sont considérés comme :

- des **outils d’assistance**
- des **contributeurs non autonomes**
- des **agents soumis au standard**

👉 **Ils ne définissent jamais les règles.**  
👉 **Ils sont censés s’y conformer.**

La **seule source de vérité normative** est :

- `docs/standard.md`
- validée par `tools/scripts/check_conformity.sh`

---

## 2. IA supportées (agnostique fournisseur)

Le framework est conçu pour fonctionner avec :

- Claude Code
- Codex
- GitHub Copilot (Chat / Inline)
- tout autre LLM intégré à un IDE, CI ou pipeline

Aucune dépendance spécifique à un fournisseur n’est introduite.

---

## 3. Rôle des prompts projet

Les **prompts projet** servent à :

- cadrer le contexte de travail
- rappeler les contraintes non négociables
- éviter les dérives classiques des LLM (invention, shortcuts, mélange des responsabilités)

Ils sont :

- **informatifs**
- **versionnés**
- **lisibles par un humain**
- **réutilisables sur plusieurs IA**

👉 Un prompt **n’exécute rien**  
👉 Un prompt **n’outrepasse jamais le standard**

---

## 4. Organisation recommandée

```

docs/ai/
├── README.md                 # Vue d’ensemble IA
├── prompt_project.md         # Prompt racine (recommandé pour standardiser les agents)
├── prompt_component.md       # Travail sur un composant
├── prompt_audit.md           # Audit / refactor
└── prompt_tests.md           # Tests unitaires

```

Ces fichiers peuvent être :

- copiés dans `.claude/instructions.md`
- collés dans Codex / Copilot Chat
- injectés comme *system prompt* dans une CI IA
- utilisés comme base d’onboarding

---

## 5. Prompt racine — `prompt_project.md`

### Objectif

Aligner **toute IA** sur le cadre global du framework.

### Contenu recommandé

```md
Tu interviens sur le framework iobewi-idf-components.

Ce projet est régi par un standard technique opposable.
Pour produire du code **conforme au standard** (`docs/standard.md`), il est attendu que :

- docs/standard.md (NORMATIF)
- la taxonomie drv_* → lib_* → mw_* → app_*
- les contrats d’API (esp_err_t, handle opaque, *_new / *_del)
- la structure canonique des composants
- les tests unitaires obligatoires

Le script `tools/scripts/check_conformity.sh` est un **outil de validation** (CI/local) qui applique des contrôles dérivés du standard.
En cas de divergence, la référence normative reste `docs/standard.md`.

Interdictions :
- inventer une structure
- contourner une règle
- mélanger responsabilités
- exposer des structs internes
- créer de la dette technique volontaire
````

---

## 6. Prompt composant — `prompt_component.md`

### Quand l’utiliser

* création d’un composant
* modification d’un composant existant

### Responsabilités imposées à l’IA

* identifier la **catégorie exacte** (drv/lib/mw/app)
* respecter le graphe de dépendances
* produire une API conforme et minimale
* fournir un `basic_app` fonctionnel

### Rappel clé

```md
Un composant = un dossier canonique.
Le nom du dossier impose le nom de l’API.
Toute divergence est une violation bloquante.
```

---

## 7. Prompt audit / refactor — `prompt_audit.md`

### Objectif

Permettre à une IA de :

* détecter des non-conformités
* proposer une trajectoire de correction
* sans refactor sauvage

### Méthode attendue

* constats factuels
* références explicites aux règles `STD-*`
* priorisation P0 / P1 / P2
* plan d’action progressif

👉 **Une IA ne “nettoie” jamais un composant sans justification.**

---

## 8. Prompt tests unitaires — `prompt_tests.md`

### Cadre strict

Les tests produits par une IA sont attendus pour :

* être unitaires (pas d’intégration)
* utiliser des mocks déterministes
* couvrir les contrats d’API
* valider la robustesse mémoire

### Interdictions explicites

* hardware réel
* timing réel
* tests basés sur les logs
* dépendance à un agent micro-ROS

---

## 9. Intégration avec les outils existants

### `.claude/instructions.md`

Peut contenir :

* `prompt_project.md`
* ou une concaténation des prompts utiles

### Git / CI

Les prompts **n’exemptent jamais** :

* l’exécution de `check_conformity.sh`
* les revues de code
* les tests unitaires

---

## 10. Hiérarchie d’autorité (rappel)

```
docs/standard.md          ← autorité normative
tools/scripts/*.sh        ← validation automatique
docs/annexes/*.md        ← guides informatifs
docs/ai/*.md             ← cadrage IA
prompts IA               ← assistance uniquement
```

👉 Si un prompt contredit le standard : **le prompt a tort**.

---

## 11. Position officielle du projet

* Les IA sont **acceptées**
* Les IA sont **encadrées**
* Les IA sont **auditables**
* Les IA ne remplacent **ni l’architecture**, ni le standard

---

## 📌 Conclusion

L’intégration IA dans `iobewi-idf-components` est :

* **volontaire**
* **maîtrisée**
* **multi-outil**
* **sans compromis sur la qualité**

Les prompts alignent le contexte.
Les scripts valident la réalité.
