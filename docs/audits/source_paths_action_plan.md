# Plan d'action — Mise à jour des chemins de sources

## Contexte
Un contrôle des chemins de sources utilisés dans les exemples a été demandé pour vérifier que les références pointent bien vers `components/` conformément au guide de structure des répertoires. Le point critique concerne la variable `EXTRA_COMPONENT_DIRS` dans les `CMakeLists.txt` des exemples.

## Constats
- Plusieurs exemples utilisaient encore des chemins relatifs historiques (`../..`) ou des références directes à des composants spécifiques (`../../../drv_*`, `../../../lib_*`, `../../../mw_*`).
- Ces chemins ne reflètent plus la structure standardisée où tous les composants sont centralisés sous `components/`.

## Actions réalisées
- Standardisation de `EXTRA_COMPONENT_DIRS` dans **tous** les exemples pour pointer vers `"${CMAKE_CURRENT_LIST_DIR}/../../components"`.
- Suppression des références directes à des composants isolés (drivers/libs/middleware) au profit du dossier racine `components/`.

## Plan d'action complémentaire
1. **Validation automatique**
   - Ajouter un contrôle simple (script ou CI) qui vérifie que tous les `examples/**/CMakeLists.txt` utilisent `../../components`.
2. **Revue documentaire**
   - Vérifier que les guides et README d'exemples mentionnent uniquement `components/` (pas de composants isolés).
3. **Gouvernance**
   - Intégrer une checklist de conformité dans les PR pour éviter la régression des chemins.

## Checklist de suivi
- [ ] Script de vérification ajouté ou référencé dans `tools/scripts/`.
- [ ] Docs d’exemples alignées avec `../../components`.
- [ ] Revue PR mise à jour pour valider les chemins d’exemples.
