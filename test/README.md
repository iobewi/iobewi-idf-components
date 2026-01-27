# Tests – non régression et intégration

Ce dossier contient les tests de non-régression, d’intégration
et de performance des composants **iobewi-idf-components**.

Contrairement aux `examples/`, ces tests ont pour objectif de
détecter toute régression fonctionnelle, temporelle ou mémoire.

## Types de tests

- `integration/` : plusieurs composants intégrés ensemble
- `non_regression/` : cas figés qui ne doivent plus casser
- `performance/` : mesures (latence, heap, CPU)
- `hw/` : documentation des bancs et cartes utilisées

## Règles

- chaque test est un projet ESP-IDF autonome
- les comportements attendus sont strictement définis
- toute modification nécessitant un changement de test
  doit être justifiée et tracée

Ces tests sont destinés à être exécutés en CI
ou sur banc matériel.
