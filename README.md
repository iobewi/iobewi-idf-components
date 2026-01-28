# 📦 iobewi-idf-components

Framework de composants **ESP-IDF 6.x** réutilisables avec intégration micro-ROS.

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.x-blue)](https://github.com/espressif/esp-idf)
[![micro-ROS](https://img.shields.io/badge/micro--ROS-compatible-green)](https://micro.ros.org/)
[![License](https://img.shields.io/badge/license-MIT-brightgreen)](LICENSE)

---

## ✅ Source normative

Le document **`docs/standard.md`** est la **seule source de vérité normative** du framework. Toute règle non présente dans ce document est **non opposable**.

Pour une vue d’ensemble de la documentation, voir `docs/README.md`.

---

## 🎯 Objectifs

Le framework **iobewi-idf-components** vise à fournir :

1. des composants **ESP-IDF 6.x** réutilisables industriellement
2. une intégration **micro-ROS maîtrisée et cloisonnée**
3. une base **maintenable ≥ 5 ans**
4. une **séparation stricte** des responsabilités
5. **zéro dette technique volontaire**

---

## 📁 Structure du dépôt

```
iobewi-idf-components/
├── components/   # Composants ESP-IDF
├── docs/         # Documentation (standard + annexes)
├── examples/     # Exemples par composant
├── test/         # Tests (framework et composants)
└── tools/        # Outils et scripts
```

---

## 📚 Taxonomie & nommage (normatif)

### Identité canonique

> **L’identité canonique d’un composant est son nom de dossier.**

```
component_id == component_dir
```

### Catégories normatives

| `component_kind` | Préfixe dossier       | Préfixe API | Rôle                                |
| ---------------- | ---------------------- | ----------- | ----------------------------------- |
| `driver`         | `iobewi_driver_`       | `drv_`      | Pilotage matériel pur               |
| `library`        | `iobewi_libs_`         | `lib_`      | Logique réutilisable sans micro-ROS |
| `middleware`     | `iobewi_mw_`           | `mw_`       | Adaptation / intégration micro-ROS  |
| `application`    | `iobewi_apps_`         | `app_`      | Orchestration métier réutilisable   |

### Règle de nommage harmonisée

```
<component_id>  ::= iobewi_<component_kind>_<name>
<component_api> ::= <api_prefix>_<name>
```

Les éléments suivants **DOIVENT** rester strictement cohérents :

- nom du dossier canonique (`component_id`)
- namespace des headers (`include/<component_api>/`)
- préfixe des API publiques
- dépendances déclarées

---

## 🔗 Dépendances autorisées (normatif)

```
iobewi_driver_* → (ESP-IDF uniquement)
iobewi_libs_*   → iobewi_driver_*
iobewi_mw_*     → iobewi_driver_*, iobewi_libs_*
iobewi_apps_*   → iobewi_driver_*, iobewi_libs_*, iobewi_mw_*
```

Toute dépendance hors de ce graphe est **interdite**.

---

## 🧱 Structure canonique d’un composant (normatif)

```
components/<component_id>/
├── CMakeLists.txt
├── idf_component.yml
├── README.md
├── include/<component_api>/
│   ├── <component_api>_types.h
│   └── <component_api>.h
└── src/
    └── <component_api>.c
```

Chaque composant **DOIT** fournir un exemple minimal :

```
examples/<component_id>/basic_app/
```

---

## 📦 Composants disponibles (arborescence actuelle)

### Drivers (`iobewi_driver_*` → `drv_*`)

| Component ID | API publique |
| --- | --- |
| `iobewi_driver_a02yyuw` | `drv_a02yyuw` |
| `iobewi_driver_fan_pwm` | `drv_fan_pwm` |
| `iobewi_driver_fan_tach` | `drv_fan_tach` |
| `iobewi_driver_led_rgb` | `drv_led_rgb` |
| `iobewi_driver_ntc_adc` | `drv_ntc_adc` |
| `iobewi_driver_vl53l0x` | `drv_vl53l0x` |

### Bibliothèques (`iobewi_libs_*` → `lib_*`)

| Component ID | API publique |
| --- | --- |
| `iobewi_libs_a02_provider` | `lib_a02_provider` |
| `iobewi_libs_status_led` | `lib_status_led` |
| `iobewi_libs_vl53l0x_provider` | `lib_vl53l0x_provider` |

### Middleware (`iobewi_mw_*` → `mw_*`)

| Component ID | API publique |
| --- | --- |
| `iobewi_mw_scan_builder` | `mw_scan_builder` |
| `iobewi_mw_uros_core` | `mw_uros_core` |
| `iobewi_mw_uros_transport_usb` | `mw_uros_transport_usb` |

### Applications (`iobewi_apps_*` → `app_*`)

| Component ID | API publique |
| --- | --- |
| `iobewi_apps_scan_tof` | `app_scan_tof` |
| `iobewi_apps_scan_ultra` | `app_scan_ultra` |

---

## 🚀 Démarrage rapide

### Prérequis

- **ESP-IDF 6.x** ([Installation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/))
- **micro-ROS pour ESP-IDF** ([micro_ros_espidf_component](https://github.com/micro-ROS/micro_ros_espidf_component))

### Installation

```bash
cd ~/esp
git clone https://github.com/votre-org/iobewi-idf-components.git
```

### Consommer les composants

```cmake
# CMakeLists.txt (racine du projet)
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_SOURCE_DIR}/../iobewi-idf-components/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mon_projet)
```

```cmake
# main/CMakeLists.txt
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        iobewi_mw_uros_core
        iobewi_libs_status_led
)
```

### Inclure une API publique

```c
#include "mw_uros_core/mw_uros_core.h"
#include "lib_status_led/lib_status_led.h"
```

---

## 📖 Documentation

- **Standard (NORMATIF)** : `docs/standard.md`
- **Documentation indexée** : `docs/README.md`
- **Annexes (informatif)** : `docs/annexes/`

---

## 🤝 Contribution

- Lire **`docs/standard.md`** avant toute contribution.
- Toute déviation **DOIT** être documentée, validée au niveau architecture et tracée.
- Aucun compromis sur la conformité : taxonomie, structure, API, tests et exemples.

---

## 📄 Licence

Ce projet est sous licence **MIT** - voir le fichier [LICENSE](LICENSE) pour plus de détails.

---

**Construit pour ESP-IDF et micro-ROS.**
