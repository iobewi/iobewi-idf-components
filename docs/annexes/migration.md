> **STATUT : INFORMATIF**
>
> Ce document :
> - n’introduit aucune règle normative
> - ne remplace aucune règle du standard
> - ne peut jamais contredire `docs/standard.md`
>
> Toute règle opposable est définie exclusivement dans `docs/standard.md`.

# 🔄 Migration & Mise en Conformité

## Annexe au standard `iobewi-idf-components`

> **Statut : INFORMATIF**
> Ce document **n’introduit aucune règle normative**.
> Il explique **comment rendre conforme** un composant existant au regard de
> `docs/standard.md`.

---

## 1. Objectif de la migration

La migration vise à :

* aligner un composant existant sur le **standard normatif**
* éliminer les ambiguïtés structurelles
* séparer correctement les responsabilités
* rendre le composant **auditable et CI-compatible**

👉 Une migration ne devrait **jamais** modifier le standard.

---

## 2. Démarche recommandée (vue d’ensemble)

Ordre conseillé :

1. identifier l’identité canonique du composant
2. vérifier la catégorie (`driver / library / middleware / application`)
3. corriger la structure de fichiers
4. corriger l’API publique
5. nettoyer les dépendances
6. ajouter / adapter les tests unitaires
7. valider via CI

---

## 3. Étape 1 — Identifier l’identité canonique

Rappel (voir `STD-TAX-000`) :

```
component_id == component_dir
```

Actions pratiques :

* renommer le dossier si nécessaire
* vérifier la cohérence avec l’API exposée
* vérifier que le `<name>` est identique partout

Exemple :

```
AVANT : components/vl53l0x_driver/
APRÈS : components/iobewi_driver_vl53l0x/
```

---

## 4. Étape 2 — Valider la catégorie du composant

Questions simples :

* accède-t-il directement au hardware ?
  → `driver`
* contient-il de la logique réutilisable ?
  → `library`
* inclut-il `rcl` / `rclc` ?
  → `middleware`
* orchestre-t-il plusieurs briques ?
  → `application`

👉 **Un seul choix possible.**
Si plusieurs réponses sont “oui”, le composant est **mal découpé**.

---

## 5. Étape 3 — Corriger la structure des fichiers

Structure cible (rappel) :

```
components/<component_id>/
├── include/<component_api>/
│   ├── <component_api>_types.h
│   └── <component_api>.h
└── src/
    └── <component_api>.c
```

Actions typiques :

* supprimer les headers publics hors namespace
* déplacer les types dans `_types.h`
* fusionner les `.c` si nécessaire (sauf justification claire)

---

## 6. Étape 4 — Nettoyer l’API publique

Checklist opérationnelle :

* toutes les fonctions publiques sont préfixées `<component_api>_`
* toutes retournent `esp_err_t`
* aucune structure interne exposée
* présence de `*_new()` / `*_del()` si état

Exemple de renommage :

```
scan_init()        → app_scan_ultra_new()
scan_engine_step() → app_scan_ultra_step()
```

---

## 7. Étape 5 — Corriger les dépendances

À partir du dossier :

```
components/iobewi_libs_xxx/
```

Vérifier :

* `REQUIRES` → uniquement `iobewi_driver_*`
* aucune dépendance vers `mw_*` ou `app_*`
* aucune inclusion implicite

Exemple de correction :

```
❌ REQUIRES mw_uros_core
✅ REQUIRES drv_a02yyuw
```

---

## 8. Étape 6 — Extraire ce qui n’est pas censé être là

Cas fréquent :

> un `drv_*` contient :
>
> * filtrage
> * provider
> * logique multi-capteurs

👉 Action recommandée :

* extraire vers un nouveau `lib_*`
* réduire le `drv_*` au pilotage pur

Exemple réel :

```
AVANT :
iobewi_driver_vl53l0x/
  tof_provider.c
  tof_config.c

APRÈS :
iobewi_driver_vl53l0x/        # pilotage pur
iobewi_libs_vl53l0x_provider/ # logique réutilisable
```

---

## 9. Étape 7 — Ajouter ou adapter les tests unitaires

Rappel :

* tests existants → adapter
* aucun test → écrire les tests critiques d’abord :

  * API
  * erreurs
  * mémoire

👉 Voir `docs/annexes/testing.md`.

---

## 10. Étape 8 — Vérification finale

Avant de considérer la migration terminée :

* [ ] nom du dossier conforme
* [ ] API alignée avec le nom canonique
* [ ] dépendances propres
* [ ] tests unitaires présents
* [ ] `basic_app` compilable
* [ ] CI verte

---

## 11. Cas fréquents et solutions

### “Je dois casser l’API existante”

* acceptable **uniquement** dans une branche dédiée
* documenter clairement la migration
* ne jamais casser silencieusement

### “Mon composant mélange trop de choses”

* découper
* créer plusieurs composants conformes
* laisser l’orchestration à `app_*`

---

## 12. Résumé opérationnel

Migrer un composant, c’est :

1. **renommer**
2. **isoler**
3. **simplifier**
4. **tester**
5. **valider**

Pas réinventer.

---

## 🔗 Références

* `docs/standard.md`

  * `STD-TAX-*`
  * `STD-STR-*`
  * `STD-API-*`
  * `STD-MEM-*`
* `docs/annexes/testing.md`
* `docs/annexes/examples.md`

---

## 📌 Conclusion

Une migration réussie :

* ne modifie pas le standard
* clarifie les responsabilités
* réduit la dette technique
* rend le composant **durable**
