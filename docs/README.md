# Documentation – iobewi-idf-components

Ce dossier regroupe la documentation technique, process et projet associée au framework **iobewi-idf-components** (ESP-IDF 6.x).

> ⚠️ **Source normative unique**
>
> Le document `docs/standard.md` est la **seule source de vérité normative**.
> Tout autre document est **informatif** (non opposable) et ne peut pas contredire le standard.

---

## Entrées rapides

- **Standard (NORMATIF)** : `standard.md`
- **Annexes (INFORMATIF)** : `annexes/`
- **Campagnes & audits** : `audits/`
- **Rapports & suivi** : `reports/` / `summary/`

---

## Structure

### `standard.md` (NORMATIF)
- Règles opposables : taxonomie, structure, API, dépendances, micro-ROS, tests, mémoire, IA.
- Toute évolution du framework **commence** par une modification de ce fichier.

### `annexes/` (INFORMATIF)
Guides d’implémentation et aide à l’application du standard (non opposables) :

- `api_patterns.md` : patterns d’implémentation (handle opaque, 2 headers, signatures, config_init, etc.)
- `examples.md` : guide des `basic_app` (structure, contenu minimal, anti-bloat)
- `testing.md` : typologie et stratégies de tests unitaires (contrats API, mémoire, mocks, etc.)
- `playbook.md` : audits, diagnostics, refactors (procédures reproductibles)
- `ai_tools.md` : intégration agents IA (prompts projet, Claude/Codex/Copilot, bonnes pratiques d’usage)
- `migration.md` : migrations, transitions, changements cassants, compatibilité

### `architecture/`
- Vues système, choix d’architecture, conventions transverses.
- Intégration micro-ROS à l’échelle du framework (hors détails normatifs).

### `guides/`
- Guides techniques et conventions d’équipe (format, workflow, outillage).
- Ne doit pas contenir de règles “obligatoires” non recopiées dans `standard.md`.

### `audits/`
- Audits de conformité, campagnes qualité, plans de remise aux normes.
- Les audits doivent référencer explicitement les IDs du standard (ex: `STD-STR-003`).

### `reports/`
- Rapports de sprint, livrables projet, décisions datées (ADR-like si besoin).

### `summary/`
- Synthèses et état global (conformité, composants référents, dette/écarts ouverts).

---

## Règle de maintenance

- **Normatif** → `docs/standard.md` uniquement.
- **Explicatif / how-to** → `docs/annexes/`.
- **Historique / suivi** → `audits/`, `reports/`, `summary/`.

Chaque sous-dossier contient un `README.md` décrivant son périmètre et ses conventions.
