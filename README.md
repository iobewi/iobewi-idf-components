# 📦 iobewi-idf-components

Framework de composants ESP-IDF réutilisables avec intégration micro-ROS.

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.x-blue)](https://github.com/espressif/esp-idf)
[![micro-ROS](https://img.shields.io/badge/micro--ROS-compatible-green)](https://micro.ros.org/)
[![License](https://img.shields.io/badge/license-MIT-brightgreen)](LICENSE)

---

## 🎯 Objectifs

Le framework **iobewi-idf-components** fournit :

- ✅ **Composants ESP-IDF réutilisables** : drivers, bibliothèques, middleware micro-ROS
- ✅ **Architecture propre** : taxonomie stricte et règles de dépendance claires
- ✅ **Intégration micro-ROS** : helpers et adaptateurs pour ROS 2
- ✅ **Maintenabilité long terme** : code industriel avec ≥ 5 ans de durée de vie
- ✅ **Traçabilité** : conformité au cahier des charges (CDC)
- ✅ **Zéro dette technique** : qualité sans compromis

---

## 📁 Structure du Projet

Le framework utilise une **double nomenclature** pour combiner clarté et concision :

```
iobewi-idf-components/
├── components/              # Code source des composants
│   ├── iobewi_driver_*/     # Drivers matériels (noms répertoires)
│   ├── iobewi_libs_*/       # Bibliothèques utilitaires
│   ├── iobewi_mw_*/         # Middleware micro-ROS
│   └── iobewi_apps_*/       # Applications métier
├── examples/                # Applications d'exemple
│   ├── iobewi_driver_*/
│   ├── iobewi_libs_*/
│   ├── iobewi_mw_*/
│   └── iobewi_apps_*/
├── docs/                    # Documentation
└── tools/                   # Scripts et outils
```

### Convention de Nommage

- **Répertoires** : `iobewi_<catégorie>_<nom>` (ex: `iobewi_driver_vl53l0x`)
- **API** : `<catégorie courte>_<nom>_*()` (ex: `drv_vl53l0x_init()`)
- **Include** : `#include "<catégorie courte>_<nom>/<catégorie courte>_<nom>.h"` (ex: `#include "drv_vl53l0x/drv_vl53l0x.h"`)

**Exemple concret** :
```
Répertoire : components/iobewi_driver_vl53l0x/
Include    : #include "drv_vl53l0x/drv_vl53l0x.h"
API        : drv_vl53l0x_init(&dev)
```

Pour plus de détails, consultez le [Guide de Structure des Répertoires](docs/guides/directory_structure.md).

---

## 📚 Taxonomie des Composants

Chaque composant appartient à **une seule catégorie** :

### 🔧 `drv_*` — Drivers Matériels

Pilotage matériel pur (I2C, SPI, GPIO, UART…)

**Exemples :**
- `drv_led_rgb` : Driver LED RGB WS2812
- `drv_vl53l0x` : Driver capteur ToF VL53L0X

**Règles :**
- ✅ Accès matériel direct
- ❌ Aucune dépendance micro-ROS
- ❌ Aucune logique métier

### 📚 `lib_*` — Bibliothèques Utilitaires

Code réutilisable générique sans micro-ROS

**Exemples :**
- `lib_status_led` : Mapping états applicatifs → couleurs LED

**Règles :**
- ✅ Abstractions fonctionnelles
- ✅ Peut dépendre de `drv_*`
- ❌ Aucun accès matériel direct
- ❌ Aucune dépendance micro-ROS

### 🔌 `mw_*` — Middleware micro-ROS

Intégration micro-ROS (builders, helpers rcl/rclc)

**Exemples :**
- `mw_scan_builder` : Builder de messages LaserScan
- `mw_uros_core` : Infrastructure micro-ROS générique
- `mw_uros_transport_usb` : Transport USB-CDC pour micro-ROS

**Règles :**
- ✅ **Intégration micro-ROS obligatoire**
- ✅ Peut dépendre de `drv_*` et `lib_*`
- ❌ Aucun accès matériel direct
- ❌ Aucune logique applicative

### 🚀 `app_*` — Composants Métier

Logique fonctionnelle réutilisable

**Exemples :**
- `app_scan_tof` : Application de scan ToF avec publication ROS

**Règles :**
- ✅ Orchestration de `drv_*`, `lib_*` et `mw_*`
- ✅ Configuration via API (pas de hardcode)
- ❌ Aucun paramètre hardcodé

---

## 🔗 Règle de Dépendance (Non Négociable)

```
drv_*  →  lib_*  →  mw_*  →  app_*
```

**Exemples valides :**
- ✅ `lib_status_led` → `drv_led_rgb`
- ✅ `mw_uros_core` → `lib_status_led`
- ✅ `app_scan_tof` → `mw_scan_builder` → `drv_vl53l0x`

**Exemples invalides :**
- ❌ `drv_*` → `mw_*` (dépendance inverse)
- ❌ `lib_*` → `mw_*` (saute la hiérarchie)

---

## 📦 Composants Disponibles

### Drivers (`drv_*`)

| Composant | Description | Bus/Interface |
|-----------|-------------|---------------|
| **drv_a02yyuw** | Driver capteur ultrasonique A02YYUW (SEN0311) | UART (9600 bps) |
| **drv_led_rgb** | Driver LED RGB WS2812/SK6812 | GPIO (RMT) |
| **drv_vl53l0x** | Driver capteur ToF VL53L0X | I2C |

### Bibliothèques (`lib_*`)

| Composant | Description | Dépendances |
|-----------|-------------|-------------|
| **lib_a02_provider** | Provider capteurs ultrasoniques A02YYUW avec filtrage médian | drv_a02yyuw |
| **lib_status_led** | Mapping états applicatifs → couleurs LED | drv_led_rgb |

### Middleware micro-ROS (`mw_*`)

| Composant | Description | Type de message |
|-----------|-------------|-----------------|
| **mw_scan_builder** | Builder de messages LaserScan | sensor_msgs/LaserScan |
| **mw_uros_core** | Infrastructure micro-ROS générique | Agnostique |
| **mw_uros_transport_usb** | Transport USB-CDC pour micro-ROS | N/A |

### Applications (`app_*`)

| Composant | Description | Publishes |
|-----------|-------------|-----------|
| **app_scan_tof** | Application scan ToF 360° | sensor_msgs/LaserScan |
| **app_scan_ultra** | Application scan ultrasonique A02YYUW | sensor_msgs/LaserScan |

---

## 🚀 Démarrage Rapide

### Prérequis

- **ESP-IDF 6.x** ([Installation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/))
- **micro-ROS pour ESP-IDF** ([micro_ros_espidf_component](https://github.com/micro-ROS/micro_ros_espidf_component))
- **ESP32-S2** ou **ESP32-S3** (pour composants USB)

### Installation

1. Cloner le dépôt dans votre projet ESP-IDF :

```bash
cd ~/esp
git clone https://github.com/votre-org/iobewi-idf-components.git
```

2. Ajouter les composants à votre projet :

```cmake
# CMakeLists.txt (racine du projet)
cmake_minimum_required(VERSION 3.16)

# Pointer vers le dossier components/ qui contient tous les composants
set(EXTRA_COMPONENT_DIRS "${CMAKE_SOURCE_DIR}/../iobewi-idf-components/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mon_projet)
```

3. Déclarer les dépendances dans votre composant :

```cmake
# main/CMakeLists.txt
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        mw_uros_core
        lib_status_led
)
```

### Exemple : Application micro-ROS simple

```c
#include "mw_uros_core/mw_uros_core.h"
#include <std_msgs/msg/string.h>

// Contexte applicatif
typedef struct {
    uint32_t counter;
} app_context_t;

// Callback d'initialisation
bool app_init(void *ctx) {
    app_context_t *app = (app_context_t *)ctx;
    app->counter = 0;
    return true;
}

// Callback de step : remplir le message ROS
bool app_step(void *ctx, void *ros_msg) {
    app_context_t *app = (app_context_t *)ctx;
    std_msgs__msg__String *msg = (std_msgs__msg__String *)ros_msg;

    static char buffer[64];
    snprintf(buffer, sizeof(buffer), "Hello from ESP32! Counter: %u", app->counter++);

    msg->data.data = buffer;
    msg->data.size = strlen(buffer);
    msg->data.capacity = msg->data.size + 1;

    return true;
}

// Callback de nettoyage
void app_fini(void *ctx) {
    // Nettoyage si nécessaire
}

void app_main(void) {
    // Configuration micro-ROS
    uros_core_config_t config = {
        .node_name = "esp32_node",
        .topic_name = "/hello",
        .domain_id = 0,
        .timer_period_ms = 1000,  // Publier à 1 Hz
        .qos_history = RMW_QOS_POLICY_HISTORY_KEEP_LAST,
        .qos_depth = 10,
        .qos_reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT,
        .qos_durability = RMW_QOS_POLICY_DURABILITY_VOLATILE,
        .stack_size = 16000,
        .task_priority = 5,
        .core_affinity = 1,
        .status_led_gpio = 38,           // LED status sur GPIO 38
        .status_led_brightness = 100,
        .on_metrics = NULL,
    };

    // Contexte applicatif
    static app_context_t app_ctx;

    // Interface applicative
    uros_app_interface_t app = {
        .type_support = ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, String),
        .message_size = sizeof(std_msgs__msg__String),
        .app_init = app_init,
        .app_step = app_step,
        .app_fini = app_fini,
        .app_context = &app_ctx,
        .app_context_size = sizeof(app_context_t),
    };

    // Création et démarrage
    uros_core_context_t *core = NULL;
    esp_err_t ret = uros_core_create(&config, &app, &core);
    if (ret == ESP_OK) {
        uros_core_start(core);
    }
}
```

### Compiler et flasher

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Lancer l'agent micro-ROS

```bash
# Sur votre PC hôte
docker run -it --rm --device=/dev/ttyUSB0 \
    microros/micro-ros-agent:humble \
    serial --dev /dev/ttyUSB0 -b 115200
```

---

## 📖 Documentation

### Cahier des Charges

Le framework suit strictement le **[Cahier des Charges (CDC)](docs/cdc.md)** qui définit :

- ✅ Taxonomie des composants
- ✅ Règles de dépendance
- ✅ Structure obligatoire des composants
- ✅ Conventions API (retour `esp_err_t`)
- ✅ Gestion mémoire
- ✅ Logging et erreurs
- ✅ Qualité et maintenabilité

### Documentation des Composants

Chaque composant contient un `README.md` détaillé :

- **API publique** : types, fonctions, exemples
- **Dépendances** : composants requis
- **Exemple fonctionnel** : `examples/basic_app/`

### Exemples

Tous les composants incluent un exemple fonctionnel dans `examples/basic_app/` :

```bash
cd drv_led_rgb/examples/basic_app
idf.py build flash monitor
```

---

## 🏗️ Structure d'un Composant

Chaque composant suit la structure obligatoire définie dans le CDC :

```
<component_name>/
├── CMakeLists.txt              # Build configuration
├── idf_component.yml           # Component manifest
├── README.md                   # Documentation
├── Kconfig                     # Configuration (optionnel)
├── include/
│   └── <component_name>/
│       ├── <component_name>.h       # API publique
│       └── <component_name>_types.h # Types publics
├── src/
│   └── <component_name>.c      # Implémentation
└── examples/
    └── basic_app/
        ├── CMakeLists.txt
        ├── sdkconfig.defaults
        └── main/
            ├── CMakeLists.txt
            └── main.c
```

---

## 🔧 Conventions API

Toutes les fonctions publiques suivent les conventions ESP-IDF :

### Signatures Obligatoires

```c
// Création d'instance (pattern handle opaque)
esp_err_t component_new(const component_config_t *cfg, component_t **out);

// Opérations
esp_err_t component_operation(component_t *handle, ...);

// Destruction
esp_err_t component_del(component_t *handle);
```

### Codes d'Erreur

Utilisation exclusive des codes ESP-IDF standards :
- `ESP_OK` : Succès
- `ESP_ERR_INVALID_ARG` : Paramètres invalides
- `ESP_ERR_NO_MEM` : Allocation mémoire échouée
- `ESP_ERR_TIMEOUT` : Timeout
- `ESP_FAIL` : Échec générique

### Gestion Mémoire

- **Ownership clair** : documenté dans les headers
- **Allocation/libération** : toute allocation a une fonction `_del()`
- **Zéro fuite** : validation obligatoire

---

## 🧪 Tests et Qualité

### Validation Build

Chaque composant doit compiler sans warnings :

```bash
cd <component>/examples/basic_app
idf.py build
```

### Validation Runtime

Les exemples doivent s'exécuter sans erreur :

```bash
idf.py flash monitor
```

### Revue de Code

Avant intégration, vérifier :
- ✅ Conformité au CDC
- ✅ Pas de dette technique
- ✅ Documentation complète
- ✅ Exemples fonctionnels
- ✅ Zéro warnings de compilation

---

## 🤝 Contribution

### Ajouter un Nouveau Composant

1. **Choisir la catégorie** : `drv_*`, `lib_*`, `mw_*` ou `app_*`
2. **Créer la structure** selon le CDC (section 5)
3. **Implémenter l'API** avec signatures `esp_err_t`
4. **Ajouter un exemple** dans `examples/basic_app/`
5. **Documenter** dans `README.md`
6. **Valider** la conformité au CDC

### Règles de Contribution

- 📖 **Lire le [CDC](docs/cdc.md)** avant toute contribution
- ✅ Toute déviation doit être **explicitement justifiée**
- 🚫 Aucune dette technique volontaire
- 📝 Documentation obligatoire
- 🧪 Exemples fonctionnels obligatoires

---

## 📄 Licence

Ce projet est sous licence **MIT** - voir le fichier [LICENSE](LICENSE) pour plus de détails.

---

## 🙏 Remerciements

- **Espressif Systems** pour ESP-IDF
- **micro-ROS** pour l'intégration ROS 2 embarqué
- **Communauté ESP32** pour le support

---

## 📞 Support

- 📚 **Documentation** : Voir `docs/cdc.md` et README de chaque composant
- 🐛 **Issues** : Ouvrir une issue sur le dépôt
- 💬 **Discussions** : Section Discussions du dépôt

---

**Construit avec ❤️ pour ESP-IDF et micro-ROS**
