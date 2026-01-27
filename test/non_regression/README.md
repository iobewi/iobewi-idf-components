# Tests de non-régression

Ce dossier regroupe les tests de non-régression du dépôt.
Chaque test doit être un projet ESP-IDF autonome avec des attentes
précises et vérifiables.

## Règles

- Un test = un dossier autonome (build, flash, monitor).
- Les critères d'acceptation doivent être explicitement décrits dans
  un README local ou dans les logs attendus.
- Toute évolution d'un test doit être justifiée et tracée.

## Références

- Campagne de test : `test/CAMPAGNE_TEST.md`
- Reporting : `test/reporting/REPORT_TEMPLATE.md`
