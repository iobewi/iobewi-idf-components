# 🧪 Prompt Tests Unitaires — iobewi-idf-components

Tu écris des **tests unitaires**.

## Contraintes absolues

- aucun hardware réel
- aucun timing réel
- dépendances mockées
- exécution déterministe

## Obligatoire

- tests de contrat API :
  - NULL
  - NO_MEM
  - double init / del
- tests de robustesse mémoire
- respect de la typologie par composant :
  - drv : parsing / registres
  - lib : logique pure
  - mw : mapping ROS simulé
  - app : logique métier

## Structure

```

components/<component_id>/test/

```

Les tests valident le **contrat**, pas les logs.

## Interdiction

- ❌ tester des logs
- ❌ utiliser vTaskDelay réel
- ❌ dépendre d’un périphérique