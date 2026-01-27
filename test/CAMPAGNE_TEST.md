# Campagne de test – iobewi-idf-components

Cette campagne formalise l’exécution des tests présents dans `test/` en suivant
les recommandations du README (tests autonomes, comportements attendus stricts,
justification des changements). Elle est conçue pour une exécution **CI** et
**banc matériel**.

## Objectifs

- Détecter les régressions fonctionnelles, temporelles et mémoire.
- Garantir la reproductibilité (projets ESP‑IDF autonomes, versions figées).
- Documenter clairement les attentes et les écarts.

## Périmètre couvert

- **Non‑régression** : cas figés qui ne doivent plus casser.
- **Intégration** : combinaison de plusieurs composants.
- **Performance** : mesures de latence/heap/CPU.
- **Matériel** : bancs et cartes utilisées.

## Extension à l’ensemble des composants

La campagne s’applique **à chaque composant** du dépôt. Pour assurer une
couverture explicite, utiliser le tableau suivant comme référentiel et compléter
les tests manquants au fil des évolutions.

| Composant | Type de test attendu | Tests existants | À compléter |
| --- | --- | --- | --- |
| `iobewi_apps_scan_tof` | intégration + performance | aucun | tests à créer |
| `iobewi_apps_scan_ultra` | intégration + performance | aucun | tests à créer |
| `iobewi_driver_a02yyuw` | non‑régression | `test/non_regression/a02yyuw_basic` | étendre si besoin |
| `iobewi_driver_fan_pwm` | non‑régression | aucun | tests à créer |
| `iobewi_driver_fan_tach` | non‑régression | aucun | tests à créer |
| `iobewi_driver_led_rgb` | non‑régression | aucun | tests à créer |
| `iobewi_driver_ntc_adc` | non‑régression | aucun | tests à créer |
| `iobewi_driver_vl53l0x` | non‑régression | aucun | tests à créer |
| `iobewi_libs_a02_provider` | intégration | aucun | tests à créer |
| `iobewi_libs_status_led` | non‑régression | aucun | tests à créer |
| `iobewi_libs_vl53l0x_provider` | intégration | aucun | tests à créer |
| `iobewi_mw_scan_builder` | intégration | aucun | tests à créer |
| `iobewi_mw_uros_core` | intégration + performance | aucun | tests à créer |
| `iobewi_mw_uros_transport_usb` | intégration | aucun | tests à créer |

## Pré‑requis

- ESP‑IDF installé et configuré (`IDF_PATH` valide).
- Outils de build ESP‑IDF disponibles (`idf.py` dans le PATH).
- Accès aux cartes matérielles décrites dans `test/hw/`.
- Numéro de série de la carte (pour le flash) et port série configuré.

## Stratégie d’exécution

### 1) Validation statique (rapide)

- Vérifier que chaque test est un **projet ESP‑IDF autonome**.
- Vérifier que chaque test décrit un comportement **attendu** (dans le code ou
  dans un README local si disponible).

### 2) Non‑régression (obligatoire)

Chaque test de `test/non_regression/` est exécuté **individuellement**, dans un
répertoire propre, afin d’éviter les effets de bord.

#### Exemple : `a02yyuw_basic`

1. Se placer dans `test/non_regression/a02yyuw_basic`.
2. Lancer la compilation et le flash sur le banc matériel.
3. Capturer les logs série et vérifier les attentes documentées dans le test.

Commandes typiques (adaptées au port série) :

```bash
cd test/non_regression/a02yyuw_basic
idf.py set-target <target>
idf.py build
idf.py -p <port> flash monitor
```

**Critères d’acceptation**

- Le binaire compile sans erreur.
- Les logs d’exécution correspondent strictement au comportement attendu.
- Toute divergence déclenche une analyse et une justification si changement
  de test nécessaire.

### 3) Intégration

- Exécuter chaque test d’intégration en isolant les configurations et en
  conservant les journaux.
- Vérifier la cohérence entre composants (interfaces, data‑flow, gestion
  d’erreurs).

### 4) Performance

- Mesurer latence, heap et CPU en conditions définies.
- Comparer aux seuils précédemment validés.
- Reporter toute régression avec métriques (delta, cible, conditions).

## Gestion des résultats

- Conserver les **logs** (build + exécution) et l’**environnement** (ESP‑IDF,
  toolchain, cible).
- Documenter les résultats par test : OK/KO, anomalies, actions correctives.
- Toute modification qui implique un changement de test doit être **justifiée
  et tracée** (issue, ticket, ou note de version).

## Reporting minimal attendu (template)

- **Test** : nom du test
- **Cible** : type de carte/SoC
- **Commande** : script/commande exécutée
- **Résultat** : OK/KO
- **Logs** : lien ou fichier attaché
- **Observations** : écarts, performance, mémoire
- **Justification** : si le test a été modifié

## Matériel

- Voir `test/hw/` pour les bancs disponibles et leurs caractéristiques.
