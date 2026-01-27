# 🧪 Guide des Tests Unitaires – Typologie par Composant

Ce document décrit les **types de tests unitaires attendus** pour chaque catégorie de composant du framework `iobewi-idf-components`. Il sert de référence pour cadrer les cas de test, les niveaux d’isolation et les critères d’acceptation.

---

## 🎯 Objectifs

- **Fiabiliser l’API publique** de chaque composant.
- **Isoler les responsabilités** par couche (`drv_`, `lib_`, `mw_`, `app_`).
- **Garantir des comportements déterministes** (entrées → sorties attendues).
- **Documenter l’intention** via des tests lisibles et reproductibles.

---

## ✅ Règles générales

- Les tests unitaires doivent **cibler une fonction ou une responsabilité précise**.
- Les dépendances matérielles doivent être **mockées ou simulées**.
- Chaque test doit définir explicitement :
  - **Préconditions** (état initial, configuration)
  - **Entrées**
  - **Sorties attendues** (valeurs, erreurs, états)
- Le test **ne dépend pas du timing** ou de l’environnement réel.

---

## 📌 Typologie par composant

### 1) `drv_*` — Drivers matériels

**Objectif** : Valider le découpage bas niveau, la gestion d’état, et les conversions minimales.

**Types de tests unitaires attendus :**

✅ **Tests de parsing/protocoles**
- Trames valides vs invalides
- Checksums, header, format
- Conversions bit/byte

✅ **Tests d’état interne**
- Initialisation correcte
- Transitions d’état (enable/disable, mode)
- Gestion des erreurs I/O simulées

✅ **Tests de paramètres extrêmes**
- Limites de registre
- Valeurs out-of-range
- Valeurs par défaut

**Ce qui doit être simulé :**
- I2C / SPI / UART
- GPIO

**À éviter :**
- Tests avec vrai matériel (ceci relève des tests d’intégration)

---

### 2) `lib_*` — Bibliothèques utilitaires

**Objectif** : Valider la logique algorithmique réutilisable et le traitement de données.

**Types de tests unitaires attendus :**

✅ **Tests de transformation**
- Filtrage (moyenne, médian, etc.)
- Normalisation / conversion
- Enrichissement de structure

✅ **Tests de flux de données**
- Pipeline simple : entrée → sortie
- Gestion des cas limites (buffer vide, taille max)

✅ **Tests d’erreurs et retours**
- Erreurs propagées depuis `drv_*`
- Valeurs de retour (esp_err_t, codes custom)

✅ **Tests de compatibilité API**
- Vérifier que les types exposés sont stables

---

### 3) `mw_*` — Middleware micro-ROS

**Objectif** : Valider la liaison ROS, la sérialisation, et la boucle de service.

**Types de tests unitaires attendus :**

✅ **Tests de mapping ROS**
- Conversion interne → message ROS
- Champs obligatoires présents
- Timestamp / frame_id corrects

✅ **Tests de publication simulée**
- Publication déclenchée avec données valides
- Gestion d’échec de publication

✅ **Tests de configuration**
- Paramètres ROS (topic, QoS)
- Valeurs par défaut

**Ce qui doit être mocké :**
- rcl / rclc / executor
- Transport

---

### 4) `app_*` — Applications finales

**Objectif** : Valider la cohérence de l’usage des composants et la logique métier embarquée.

**Types de tests unitaires attendus :**

✅ **Tests de logique métier**
- Orchestration des modules
- Conditions d’activation
- Séquences d’états

✅ **Tests de configuration**
- Lecture de paramètres
- Réactions attendues aux flags

✅ **Tests de fail-safe**
- Gestion d’erreurs critiques
- Timeout ou absence de données

**À limiter :**
- Accès direct au hardware (priorité aux mocks)

---

## 📋 Checklist rapide

| Type de composant | Tests critiques | Mocks obligatoires |
|-------------------|----------------|---------------------|
| `drv_*` | parsing, checksum, init | I2C/SPI/UART/GPIO |
| `lib_*` | filtrage, erreurs, limites | drivers (`drv_*`) |
| `mw_*` | mapping ROS, publish | rcl/rclc/executor |
| `app_*` | logique métier, config | services externes |

---

## ✅ Résultat attendu

Un composant est considéré **conforme** si :
- les tests couvrent les cas nominaux + cas limites
- les erreurs sont vérifiées explicitement
- la logique est testée sans dépendre du matériel réel

---

## 🔗 Références internes

- [Bonnes pratiques des composants](component_best_practices.md)
- [Structure du répertoire](directory_structure.md)
- [Tests non-régression & intégration](../../test/README.md)
