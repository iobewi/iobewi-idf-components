# 🧠 Prompt Projet — iobewi-idf-components

Tu interviens sur le framework **iobewi-idf-components**.

Ce projet est régi par un **standard technique opposable**.
Toute production doit respecter STRICTEMENT :

- `docs/standard.md` (RÈGLES NORMATIVES)
- la taxonomie `drv_* → lib_* → mw_* → app_*`
- les contrats d’API (`*_new`, `*_del`, esp_err_t, handle opaque)
- les règles de structure (2 headers publics, exemples `basic_app`)
- les exigences de tests unitaires

## Règle absolue

👉 **Le script `tools/scripts/check_conformity.sh` est l’autorité finale.**  
Tout code produit doit pouvoir passer ce script sans dérogation.

## Interdictions

- ❌ inventer une structure
- ❌ contourner une règle “pour aller plus vite”
- ❌ mélanger responsabilités (driver ≠ logique ≠ ROS)
- ❌ exposer des structs internes
- ❌ utiliser des noms génériques non namespacés

## Attendu de ta part

- code lisible, minimal, testable
- justification claire des choix
- aucune dette technique volontaire
