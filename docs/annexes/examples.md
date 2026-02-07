> **STATUT : INFORMATIF**
>
> Ce document :
> - n’introduit aucune règle normative
> - ne remplace aucune règle du standard
> - ne peut jamais contredire `docs/standard.md`
>
> Toute règle opposable est définie exclusivement dans `docs/standard.md`.

# 📦 Exemples (`basic_app`) — Guide d’implémentation

## Annexe au standard `iobewi-idf-components`

> **Statut : INFORMATIF**  
> Ce document **n’introduit aucune règle normative**.  
> Il décrit **comment structurer et écrire** les exemples requis par
> `docs/standard.md` (sections **STD-STR-002**, **STD-API-***).

---

## 1. Rôle des exemples (`basic_app`)

Les exemples servent à :

* démontrer l’usage **correct** de l’API publique
* valider que le composant est **intégrable tel quel**
* fournir un **point d’entrée minimal** pour l’utilisateur
* garantir une **compilation fonctionnelle** dans la CI

👉 Un exemple **n’est pas** :

* une application finale
* un banc de test
* un lieu pour ajouter de la logique métier
* une “documentation bis” du composant

---

## 2. Périmètre d’un `basic_app`

Un `basic_app` :

* utilise **uniquement l’API publique**
* ne dépend **que** des composants déclarés (résolution ESP-IDF standard)
* ne modifie **jamais** le composant
* est volontairement **simple, lisible, reproductible**

👉 Si l’exemple devient complexe, il est trop gros : le contenu est idéalement migré vers
un composant `lib_*` / `app_*` ou vers des tests unitaires.

---

## 3. Structure canonique d’un exemple

Structure recommandée :

```

examples/<component_id>/basic_app/
├── CMakeLists.txt
├── sdkconfig.defaults
├── README.md
└── main/
├── CMakeLists.txt
└── main.c

```

Rappels pratiques :

* `<component_id>` = **identique** au dossier du composant
* le standard exige au minimum `basic_app/`.  
* des variantes (`advanced_app/`, `stress_app/`, etc.) peuvent exister si elles apportent une valeur claire, mais il est recommandé de conserver `basic_app/` comme exemple de référence simple et stable.

---

## 4. Convention de nommage et identité (pratique)

* Le dossier exemple reprend **strictement** le `component_id` :

```

components/iobewi_driver_vl53l0x/
examples/iobewi_driver_vl53l0x/basic_app/

```

* L’exemple inclut via l’alias API `<component_api>` :

```c
#include "drv_vl53l0x/drv_vl53l0x.h"
```

---

## 5. `main.c` — Bonnes pratiques

### Ce que `main.c` fait généralement (selon le standard)

* inclure l’API publique :

  ```c
  #include "<component_api>/<component_api>.h"
  ```

* initialiser une configuration (si exposée) via `*_config_init()`

* créer une instance (`*_new`) si le composant a un état

* exécuter 1 à 3 appels représentatifs (happy path + 1 cas d’erreur simple)

* produire quelques logs **courts** et **observables**

### Ce que `main.c` évite généralement (selon le standard)

* accéder à des structures internes
* utiliser des fonctions non documentées / non publiques
* implémenter une logique métier (mapping, filtrage, état applicatif…)
* contourner une faiblesse de l’API (ex: ré-essais, sleeps magiques, patch runtime)
* dupliquer le composant (copier/coller une partie de l’implémentation)

---

## 6. Exemple type (conceptuel)

```c
#include "drv_example/drv_example.h"
#include "esp_log.h"

static const char *TAG = "basic_app";

void app_main(void)
{
    drv_example_config_t config;
    drv_example_config_init(&config);

    drv_example_t *handle = NULL;
    esp_err_t err = drv_example_new(&config, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "drv_example_new failed: %s", esp_err_to_name(err));
        return;
    }

    err = drv_example_read(handle);
    ESP_LOGI(TAG, "read: %s", esp_err_to_name(err));

    drv_example_del(handle);
}
```

Points clés :

* API utilisée telle quelle
* aucun hack
* aucun accès interne
* lisible en 30 secondes

---

## 7. Logging dans les exemples (pratique)

Recommandations :

* définir `TAG` local à l’exemple (`"basic_app"`) ou `<component_api>_example`
* logguer les `esp_err_t` via `esp_err_to_name(err)`
* limiter les logs à l’essentiel : init, action, résultat, cleanup

Exemple :

```c
static const char *TAG = "basic_app";
ESP_LOGI(TAG, "init ok");
ESP_LOGE(TAG, "op failed: %s", esp_err_to_name(err));
```

---

## 8. Gestion d’erreurs (pratique)

Un `basic_app` peut montrer un pattern d’erreur simple, mais :

* pas de boucles de retry sophistiquées
* pas de backoff, pas de watchdog logique
* pas de “mode dégradé”

Pattern recommandé :

```c
if (err != ESP_OK) {
    ESP_LOGE(TAG, "failed: %s", esp_err_to_name(err));
    goto cleanup;
}
```

---

## 9. `sdkconfig.defaults`

Objectifs :

* rendre l’exemple **immédiatement compilable**
* éviter toute interaction manuelle obligatoire
* documenter les paramètres importants

Bonnes pratiques :

* activer uniquement ce qui est nécessaire
* pas de tuning performance
* pas de paramètres spécifiques carte finale
* éviter les valeurs “produit” (pins figées, allocations énormes, logs trop verbeux)

Recommandation opérationnelle :

* si l’exemple a besoin de pins/ports : préférer `main/Kconfig.projbuild` pour exposer
  des réglages, et garder `sdkconfig.defaults` minimal (logs/console/paramètres génériques).

---

## 10. `CMakeLists.txt` de l’exemple

### `examples/<component_id>/basic_app/CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(basic_app)
```

### `main/CMakeLists.txt`

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
)
```

👉 L'exemple évite de référencer un chemin de composant particulier.
👉 Il est recommandé de ne pas ajouter de `REQUIRES` manuellement ici : ESP-IDF résout via
`EXTRA_COMPONENT_DIRS` + `idf_component.yml` / `REQUIRES` des composants.

### ⚠️ Piège : `EXTRA_COMPONENT_DIRS` trop large

**Problème fréquent** : Utiliser un chemin trop large dans `EXTRA_COMPONENT_DIRS` :

```cmake
# ❌ ÉVITER : inclut TOUS les composants du projet
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components"
)
```

**Conséquences** :

* CMake charge **tous** les composants du répertoire parent, même ceux non utilisés
* Si un composant n'est pas compatible avec la cible (ex: `esp32s2`), la compilation échoue
* Erreur typique : `Component "xxx" is not compatible with target "esp32s2"`
* Ralentit la configuration CMake inutilement

**Solution recommandée** : Inclure uniquement le composant nécessaire :

```cmake
# ✅ PRÉFÉRER : inclut uniquement le composant requis
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../../components/iobewi_driver_xxx"
)
```

**En cas d'erreur** :

```bash
# Nettoyer et reconfigurer
rm -rf build
idf.py set-target esp32s3  # ou votre cible
```

**Rationale** : Un exemple `basic_app` dépend généralement d'**un seul** composant.
Inclure tout le répertoire parent crée des dépendances implicites non souhaitées.

---

## 11. README.md de l’exemple

Contenu recommandé :

```md
# basic_app — <component_id>

## Description
Démonstration minimale de l’utilisation du composant `<component_api>`.

## Prérequis
- ESP-IDF 6.x
- Carte supportée

## Compilation
idf.py set-target esp32s3
idf.py build
idf.py flash monitor

## Configuration
- menuconfig : (si Kconfig.projbuild présent)
- pins / options : voir "Component config" → "<component_id> basic_app"

## Résultat attendu
- Logs init OK
- 1 action principale
- cleanup OK
```

👉 Éviter la duplication de la documentation du composant : l’exemple gagne à rester court.

---

## 12. `Kconfig.projbuild` (optionnel mais utile)

Quand l’utiliser :

* quand l’exemple a besoin de configurer des GPIO/UART/I2C, sans figer des valeurs dans le code
* quand l’exemple vise à rester portable (devboards différentes)

Squelette recommandé :

```kconfig
menu "<component_id> basic_app"

config <COMPONENT_API_UPPER>_BASIC_APP_I2C_SCL_GPIO
    int "I2C SCL GPIO"
    default 10

config <COMPONENT_API_UPPER>_BASIC_APP_I2C_SDA_GPIO
    int "I2C SDA GPIO"
    default 11

endmenu
```

Puis côté code :

```c
int scl = CONFIG_<COMPONENT_API_UPPER>_BASIC_APP_I2C_SCL_GPIO;
```

Notes :

* réserver `Kconfig.projbuild` aux paramètres “wiring” / intégration
* éviter les paramètres “métier” (sinon ce n’est plus un basic_app)

---

## 13. Cas particuliers par type de composant

### 13.1 Composants `drv_*`

Objectif :

* montrer une séquence init → action → cleanup
* rendre visibles les erreurs “classiques” (invalid arg, timeout, not found)

Recommandations :

* si le hardware est absent : l’exemple peut **échouer proprement** en loggant l’erreur.
* éviter toute simulation “fake hardware” dans `basic_app` (c’est le job des tests unitaires)

### 13.2 Composants `lib_*`

Objectif :

* illustrer une transformation / calcul / mapping
* données simples, déterministes

Recommandation :

* utiliser des entrées constantes dans `main.c`
* logguer la sortie attendue (sans “assert” — les asserts sont pour les tests)

### 13.3 Composants `mw_*`

Objectif :

* démontrer une intégration micro-ROS **minimale**
* éviter les dépendances externes fragiles (agent obligatoire, réseau…)

Recommandations pratiques :

* si possible : démontrer le mapping / préparation de message sans exiger un agent réel
* si un agent est nécessaire : documenter clairement le prérequis dans le README de l’exemple

### 13.4 Composants `app_*`

Objectif :

* montrer l’orchestration minimale (init + 1 cycle)
* aucun comportement “produit final”

Recommandations :

* pas de state-machine complète dans `basic_app`
* pas de logique de sécurité produit (fail-safe complexe, retries…)

---

## 14. Erreurs fréquentes

* exemple trop complexe ("mini-produit")
* logique métier intégrée
* duplication de code du composant
* dépendance implicite non documentée
* exemple qui masque une faiblesse de l'API
* exemple qui impose une carte spécifique sans le dire
* exemple qui dépend de timing réel (delays arbitraires) pour "fonctionner"
* **`EXTRA_COMPONENT_DIRS` incluant tous les composants** au lieu d'uniquement celui requis (voir section 10)

---

## 15. Checklist rapide (opérationnelle)

Avant de valider un `basic_app` :

* [ ] compile sans modification
* [ ] utilise uniquement l’API publique
* [ ] ne modifie pas le composant
* [ ] reste lisible et minimal
* [ ] reflète fidèlement l’usage attendu
* [ ] README indique prérequis et résultat attendu
* [ ] pas de hacks (sleeps magiques, retries, contournements)

---

## 🔗 Références

* `docs/standard.md`

  * `STD-STR-002`
  * `STD-API-*`
* `docs/annexes/api_patterns.md`
* `docs/annexes/testing.md`
* `docs/annexes/microros.md`
* `docs/annexes/audit_playbook.md`
* `docs/annexes/audit_remediation.md`

---

## 📌 Conclusion

Un bon `basic_app` est :

* **minimal**
* **honnête**
* **didactique**
* **jetable**

Il montre **comment utiliser** un composant —
pas comment contourner ses limites.
