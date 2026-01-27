# 📁 Guide de Structure des Répertoires - iobewi-idf-components

**Version 1.0** | Référence pour comprendre l'organisation du projet

Ce document explique l'organisation des répertoires et la convention de nommage du framework `iobewi-idf-components`.

---

## 🎯 Objectifs de l'Organisation

1. **Clarté** : Séparation nette entre composants et exemples
2. **Identité** : Préfixe `iobewi_` pour identification immédiate
3. **Standards ESP-IDF** : Organisation proche des conventions officielles
4. **Compatibilité** : API stable malgré réorganisation
5. **Scalabilité** : Structure qui supporte la croissance du projet

---

## 📂 Structure Racine du Projet

```
iobewi-idf-components/
├── components/          # 🔧 Code source des composants
│   ├── iobewi_driver_*/
│   ├── iobewi_libs_*/
│   ├── iobewi_mw_*/
│   └── iobewi_apps_*/
├── examples/            # 💡 Applications d'exemple
│   ├── iobewi_driver_*/
│   ├── iobewi_libs_*/
│   ├── iobewi_mw_*/
│   └── iobewi_apps_*/
├── docs/               # 📚 Documentation
│   ├── architecture/
│   ├── audits/
│   ├── guides/
│   ├── reports/
│   └── summary/
├── tools/              # 🛠️ Scripts et outils
│   └── scripts/
└── test/               # 🧪 Tests (structure future)
```

---

## 🏷️ Convention de Nommage : Double Nomenclature

Le framework utilise une **double nomenclature** pour maximiser la clarté tout en préservant la compatibilité API :

### Niveau 1 : Noms de Répertoires (Branding)

Les **répertoires** utilisent le préfixe `iobewi_` complet pour :
- Renforcer l'identité de marque
- Éviter les conflits avec d'autres bibliothèques
- Rendre l'origine explicite au premier coup d'œil

**Format** : `iobewi_<catégorie>_<nom>`

| Catégorie | Format Répertoire | Exemples |
|-----------|-------------------|----------|
| Drivers | `iobewi_driver_*` | `iobewi_driver_vl53l0x`, `iobewi_driver_a02yyuw` |
| Libraries | `iobewi_libs_*` | `iobewi_libs_status_led`, `iobewi_libs_a02_provider` |
| Middleware | `iobewi_mw_*` | `iobewi_mw_uros_core`, `iobewi_mw_scan_builder` |
| Applications | `iobewi_apps_*` | `iobewi_apps_scan_tof`, `iobewi_apps_scan_ultra` |

### Niveau 2 : Chemins Include et API (Technique)

Les **chemins include** et **noms de fonctions/types** utilisent la forme courte pour :
- Respecter les conventions ESP-IDF
- Maintenir une API concise
- Assurer la compatibilité avec le CDC

**Format Include** : `<catégorie courte>_<nom>/<catégorie courte>_<nom>.h`

**Format API** : `<catégorie courte>_<nom>_<action>()`

| Catégorie | Préfixe API | Chemin Include | Fonctions |
|-----------|-------------|----------------|-----------|
| Drivers | `drv_*` | `drv_vl53l0x/drv_vl53l0x.h` | `drv_vl53l0x_init()` |
| Libraries | `lib_*` | `lib_status_led/lib_status_led.h` | `lib_status_led_set()` |
| Middleware | `mw_*` | `mw_uros_core/mw_uros_core.h` | `mw_uros_core_start()` |
| Applications | `app_*` | `app_scan_tof/app_scan_tof.h` | `app_scan_tof_init()` |

---

## 📋 Table de Mapping Complète

| Nom Répertoire | Nom API | Chemin Include |
|----------------|---------|----------------|
| `iobewi_driver_vl53l0x` | `drv_vl53l0x` | `drv_vl53l0x/drv_vl53l0x.h` |
| `iobewi_driver_a02yyuw` | `drv_a02yyuw` | `drv_a02yyuw/drv_a02yyuw.h` |
| `iobewi_driver_led_rgb` | `drv_led_rgb` | `drv_led_rgb/drv_led_rgb.h` |
| `iobewi_driver_ntc_adc` | `drv_ntc_adc` | `drv_ntc_adc/drv_ntc_adc.h` |
| `iobewi_driver_fan_pwm` | `drv_fan_pwm` | `drv_fan_pwm/drv_fan_pwm.h` |
| `iobewi_driver_fan_tach` | `drv_fan_tach` | `drv_fan_tach/drv_fan_tach.h` |
| `iobewi_libs_status_led` | `lib_status_led` | `lib_status_led/lib_status_led.h` |
| `iobewi_libs_a02_provider` | `lib_a02_provider` | `lib_a02_provider/lib_a02_provider.h` |
| `iobewi_libs_vl53l0x_provider` | `lib_vl53l0x_provider` | `lib_vl53l0x_provider/lib_vl53l0x_provider.h` |
| `iobewi_mw_uros_core` | `mw_uros_core` | `mw_uros_core/mw_uros_core.h` |
| `iobewi_mw_uros_transport_usb` | `mw_uros_transport_usb` | `mw_uros_transport_usb/mw_uros_transport_usb.h` |
| `iobewi_mw_scan_builder` | `mw_scan_builder` | `mw_scan_builder/mw_scan_builder.h` |
| `iobewi_apps_scan_tof` | `app_scan_tof` | `app_scan_tof/app_scan_tof.h` |
| `iobewi_apps_scan_ultra` | `app_scan_ultra` | `app_scan_ultra/app_scan_ultra.h` |

---

## 🔍 Exemple Détaillé : `iobewi_driver_vl53l0x`

### Structure du Répertoire

```
components/iobewi_driver_vl53l0x/     ← Nom de répertoire avec préfixe iobewi_
├── CMakeLists.txt
├── idf_component.yml
├── README.md
├── include/
│   └── drv_vl53l0x/                  ← Namespace API (forme courte)
│       ├── drv_vl53l0x_types.h       ← Header types
│       └── drv_vl53l0x.h             ← Header API
├── src/
│   └── vl53l0x_driver.c
└── st_api/
    └── ...
```

### Utilisation dans le Code

```c
// ✅ Include utilise la forme courte
#include "drv_vl53l0x/drv_vl53l0x.h"

// ✅ Types utilisent le préfixe court
drv_vl53l0x_dev_t dev;
drv_vl53l0x_config_t config;

// ✅ Fonctions utilisent le préfixe court
esp_err_t ret = drv_vl53l0x_init(&dev, &config);
uint16_t distance_mm = drv_vl53l0x_read(&dev);
```

### CMakeLists.txt d'un Exemple

```cmake
cmake_minimum_required(VERSION 3.16)

# Pointer vers le dossier components/ qui contient iobewi_driver_vl53l0x/
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(vl53l0x_basic_example)
```

**Note** : ESP-IDF détecte automatiquement `iobewi_driver_vl53l0x` dans `components/` et expose les includes via le namespace `drv_vl53l0x/`.

---

## 🆕 Créer un Nouveau Composant

### Étape 1 : Choisir la Catégorie

Déterminez la catégorie selon le [CDC](cdc.md) :
- `iobewi_driver_*` : Accès matériel direct (I2C, SPI, GPIO, UART)
- `iobewi_libs_*` : Logique réutilisable sans micro-ROS
- `iobewi_mw_*` : Intégration micro-ROS
- `iobewi_apps_*` : Orchestration métier

### Étape 2 : Créer la Structure

```bash
# Exemple : Créer un nouveau driver pour un capteur BME280
cd components/

mkdir -p iobewi_driver_bme280/include/drv_bme280
mkdir -p iobewi_driver_bme280/src

# Créer les headers
touch iobewi_driver_bme280/include/drv_bme280/drv_bme280_types.h
touch iobewi_driver_bme280/include/drv_bme280/drv_bme280.h

# Créer l'implémentation
touch iobewi_driver_bme280/src/drv_bme280.c

# Créer les fichiers de configuration
touch iobewi_driver_bme280/CMakeLists.txt
touch iobewi_driver_bme280/idf_component.yml
touch iobewi_driver_bme280/README.md
```

### Étape 3 : Respecter les Conventions

**CMakeLists.txt** :
```cmake
idf_component_register(
    SRCS "src/drv_bme280.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c
)
```

**drv_bme280_types.h** :
```c
#pragma once

typedef struct drv_bme280_s drv_bme280_t;

typedef struct {
    // Configuration
} drv_bme280_config_t;
```

**drv_bme280.h** :
```c
#pragma once

#include "drv_bme280/drv_bme280_types.h"

esp_err_t drv_bme280_init(const drv_bme280_config_t *config, drv_bme280_t **out);
esp_err_t drv_bme280_read(drv_bme280_t *handle, float *temp, float *humidity);
esp_err_t drv_bme280_del(drv_bme280_t *handle);
```

### Étape 4 : Créer l'Exemple

```bash
cd examples/

mkdir -p iobewi_driver_bme280/basic_app/main

# Structure complète de l'exemple
iobewi_driver_bme280/
└── basic_app/
    ├── CMakeLists.txt          # Pointe vers ../../components
    ├── sdkconfig.defaults
    ├── README.md
    └── main/
        ├── CMakeLists.txt
        └── main.c
```

**CMakeLists.txt de l'exemple** :
```cmake
cmake_minimum_required(VERSION 3.16)

# Pointer vers components/ pour accéder à iobewi_driver_bme280
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(bme280_basic_app)
```

**main/main.c** :
```c
#include "drv_bme280/drv_bme280.h"  // ← Include forme courte

void app_main(void)
{
    drv_bme280_config_t config;
    drv_bme280_t *sensor;

    drv_bme280_init(&config, &sensor);

    float temp, hum;
    drv_bme280_read(sensor, &temp, &hum);

    drv_bme280_del(sensor);
}
```

---

## 🔄 Impact sur les Projets Existants

### Migration des Projets Utilisateurs

Si vous avez des projets existants utilisant l'ancienne structure :

**Avant (ancienne structure)** :
```cmake
# CMakeLists.txt
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../drv_vl53l0x")
```

**Après (nouvelle structure)** :
```cmake
# CMakeLists.txt
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")
```

**Aucune modification du code** :
```c
// Le code reste identique ✅
#include "drv_vl53l0x/drv_vl53l0x.h"

drv_vl53l0x_dev_t dev;
drv_vl53l0x_init(&dev, &config);
```

### Compatibilité API

✅ **L'API publique est 100% rétrocompatible** :
- Les chemins include restent identiques (`drv_*/`, `lib_*/`, `mw_*/`, `app_*/`)
- Les noms de fonctions restent identiques (`drv_*_*`, `lib_*_*`, etc.)
- Les noms de types restent identiques (`drv_*_t`, `lib_*_config_t`, etc.)

❌ **Seuls les chemins de répertoires changent** :
- `drv_vl53l0x/` → `components/iobewi_driver_vl53l0x/`
- `lib_status_led/` → `components/iobewi_libs_status_led/`

---

## 📊 Rationale de la Double Nomenclature

### Pourquoi Deux Noms Différents ?

#### Niveau Répertoire : `iobewi_*` (Branding)

**Avantages** :
- ✅ Identité de marque forte
- ✅ Évite confusion avec bibliothèques tierces
- ✅ Autodocumentant (origine claire)
- ✅ Prévient les conflits de noms

**Exemples** :
```bash
# Clair que c'est du code iobewi
components/iobewi_driver_vl53l0x/
components/iobewi_mw_uros_core/

# vs ancien (ambiguïté possible)
drv_vl53l0x/  # De qui est ce driver ?
mw_uros_core/ # Framework de qui ?
```

#### Niveau API : `drv_*` / `lib_*` / `mw_*` / `app_*` (Technique)

**Avantages** :
- ✅ Concision du code
- ✅ Conformité CDC stricte
- ✅ Convention ESP-IDF standard
- ✅ Taxonomie explicite dans le nom

**Exemples** :
```c
// ✅ Concis et clair
drv_vl53l0x_init(&dev);
lib_status_led_set(LED_STATUS_OK);
mw_scan_builder_add_range(&builder, 1.5);

// ❌ Verbeux et redondant
iobewi_driver_vl53l0x_init(&dev);
iobewi_libs_status_led_set(LED_STATUS_OK);
iobewi_mw_scan_builder_add_range(&builder, 1.5);
```

### Comparaison avec Autres Frameworks

| Framework | Répertoires | API | Stratégie |
|-----------|-------------|-----|-----------|
| **iobewi** | `iobewi_driver_*` | `drv_*_*()` | Double nomenclature |
| ESP-IDF | `esp_driver_i2c` | `i2c_master_*()` | Nom complet répertoire, API courte |
| Linux Kernel | `drivers/i2c/` | `i2c_*()` | Chemin complet, API courte |
| Zephyr | `drivers/sensor/` | `sensor_*()` | Chemin complet, API courte |

Notre approche combine le meilleur des deux mondes :
- Répertoires explicites (comme ESP-IDF)
- API concise et taxonomique (comme Zephyr/Linux)

---

## 🛠️ Outils et Scripts

### Script de Conformité

Le script `tools/scripts/check_conformity.sh` comprend la double nomenclature :

```bash
# Exécuter depuis la racine du projet
./tools/scripts/check_conformity.sh

# Le script :
# 1. Détecte components/iobewi_*
# 2. Mappe iobewi_driver_vl53l0x → drv_vl53l0x
# 3. Vérifie conformité API avec drv_vl53l0x_*
```

### Variables ESP-IDF

ESP-IDF expose automatiquement les bons chemins :

```cmake
# Dans votre projet
set(EXTRA_COMPONENT_DIRS "/path/to/iobewi-idf-components/components")

# ESP-IDF détecte :
# - components/iobewi_driver_vl53l0x/
#
# Et expose automatiquement :
# - drv_vl53l0x/drv_vl53l0x.h
#
# Grâce à INCLUDE_DIRS "include" dans CMakeLists.txt
```

---

## ✅ Checklist Conformité Structure

Utilisez cette checklist pour valider un nouveau composant :

- [ ] Répertoire dans `components/` avec préfixe `iobewi_<catégorie>_<nom>`
- [ ] Namespace include utilise forme courte : `<catégorie courte>_<nom>/`
- [ ] Headers suivent convention : `<catégorie courte>_<nom>_types.h` + `<catégorie courte>_<nom>.h`
- [ ] Fonctions préfixées forme courte : `<catégorie courte>_<nom>_<action>()`
- [ ] Types préfixés forme courte : `<catégorie courte>_<nom>_<type>_t`
- [ ] CMakeLists.txt déclare `INCLUDE_DIRS "include"`
- [ ] Exemple dans `examples/iobewi_<catégorie>_<nom>/`
- [ ] CMakeLists.txt exemple pointe vers `../../components`
- [ ] README.md utilise les bons noms (répertoire ET API)

---

## 📚 Références

- [CDC](cdc.md) : Cahier des Charges complet
- [Component Best Practices](component_best_practices.md) : Guide détaillé des bonnes pratiques
- [README Principal](../../README.md) : Vue d'ensemble du framework

---

## 🔄 Historique des Versions

| Version | Date | Changements |
|---------|------|-------------|
| 1.0 | 2026-01-27 | Création initiale - Documentation de la double nomenclature |

---

**Version 1.0** | Dernière mise à jour : 2026-01-27
