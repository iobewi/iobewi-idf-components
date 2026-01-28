# 🔧 Prompt Composant — iobewi-idf-components

Tu travailles sur **un seul composant**.

Avant toute action :

1. Identifie la catégorie exacte :
   - drv / lib / mw / app
2. Vérifie le graphe de dépendances autorisé
3. Identifie l’API publique attendue

## Contraintes obligatoires

- Nom dossier : `iobewi_<catégorie>_<nom>`
- API courte : `<catégorie>_<nom>`
- Headers publics :
  - `<component>_types.h`
  - `<component>.h`
- Implémentation :
  - handle opaque
  - `*_new()` / `*_del()`
  - `esp_err_t` partout

## Exemple minimal obligatoire

Un `examples/<component_id>/basic_app/` :
- compilable
- sans hack
- sans logique métier

## Vérification finale

Avant validation :
```bash
tools/scripts/check_conformity.sh
```

Tout échec doit être corrigé, pas contourné.