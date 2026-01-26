## app_scan_ultra

Application micro-ROS publiant des **sensor_msgs/LaserScan** à partir de capteurs ultrasoniques **A02YYUW**

## 📋 Description

Composant applicatif qui orchestre `lib_a02_provider` pour publier des messages `sensor_msgs/LaserScan` sur micro-ROS. Ce composant implémente le pattern de callbacks requis par `mw_uros_core` pour une intégration transparente avec ROS 2.

## 🏗️ Taxonomie

**Catégorie** : `app_*` (Application métier)

**Dépendances** :
- `lib_a02_provider` (provider ultrasonique)
- `micro_ros_espidf_component` (micro-ROS)
- `mw_uros_core` (core micro-ROS)

**Règles** :
- ✅ Peut dépendre de `drv_*`, `lib_*` et `mw_*`
- ✅ Orchestre la logique métier
- ✅ Configuration via API (pas de hardcode)

## 🔧 Fonctionnalités

- **Acquisition** : Utilise `lib_a02_provider` pour lire les capteurs
- **Mapping angulaire** : Mappe les capteurs sur des bins du LaserScan
- **Remplissage LaserScan** : Crée le message ROS avec header, paramètres et ranges
- **Gestion NAN** : Bins non observés marqués comme NAN
- **Callbacks micro-ROS** : Implémente `app_init`, `app_step`, `app_fini`
- **Logging** : ESP-IDF standard avec TAG `app_scan_ultra`

## 🚀 API

### Types principaux

```c
typedef struct {
    // Configuration du provider
    lib_a02_provider_config_t provider_config;

    // Configuration du LaserScan
    uint8_t bins;                    // Nombre de bins (36 recommandé)
    float angle_min;                 // 0.0 (0°)
    float angle_max;                 // 2*PI (360°)
    float range_min;                 // 0.3m
    float range_max;                 // 4.5m

    // Mapping capteurs → bins
    uint8_t sensor_bin_mapping[4];  // Ex: {0, 9, 18, 27}

    // Frame ID
    const char *frame_id;            // "base_link"
} app_scan_ultra_config_t;
```

### Fonctions

```c
// Initialiser config avec valeurs par défaut
esp_err_t app_scan_ultra_config_init(app_scan_ultra_config_t *config);

// Créer une instance
esp_err_t app_scan_ultra_new(const app_scan_ultra_config_t *config,
                              app_scan_ultra_t **out_handle);

// Callbacks micro-ROS
bool app_scan_ultra_init(void *ctx);
bool app_scan_ultra_step(void *ctx, void *ros_msg);
void app_scan_ultra_fini(void *ctx);

// Détruire l'instance
esp_err_t app_scan_ultra_del(app_scan_ultra_t *handle);
```

## 📝 Exemple d'utilisation

```c
#include "drv_a02yyuw/drv_a02yyuw.h"
#include "app_scan_ultra/app_scan_ultra.h"
#include "mw_uros_core/mw_uros_core.h"

static const int EN_PINS[] = {14, 10, 7, 4};

void app_main(void)
{
    // 1. Créer le driver A02YYUW
    drv_a02yyuw_config_t drv_config;
    drv_a02yyuw_config_init(&drv_config);
    drv_config.uart_num = UART_NUM_1;
    drv_config.uart_rx_gpio = 18;
    drv_config.gpio_mode = 5;
    drv_config.gpio_en_list = EN_PINS;
    drv_config.sensor_count = 4;

    drv_a02yyuw_t *driver = NULL;
    ESP_ERROR_CHECK(drv_a02yyuw_new(&drv_config, &driver));

    // 2. Configurer l'application
    app_scan_ultra_config_t app_config;
    app_scan_ultra_config_init(&app_config);

    // Configurer le provider
    app_config.provider_config.driver = driver;
    app_config.provider_config.sensor_count = 4;

    // LaserScan : 36 bins, 10° résolution
    app_config.bins = 36;
    app_config.angle_min = 0.0f;
    app_config.angle_max = 2.0f * M_PI;

    // Mapping : 0°, 90°, 180°, 270°
    app_config.sensor_bin_mapping[0] = 0;   // 0°
    app_config.sensor_bin_mapping[1] = 9;   // 90°
    app_config.sensor_bin_mapping[2] = 18;  // 180°
    app_config.sensor_bin_mapping[3] = 27;  // 270°

    app_config.frame_id = "base_link";

    // 3. Créer l'application
    app_scan_ultra_t *app = NULL;
    ESP_ERROR_CHECK(app_scan_ultra_new(&app_config, &app));

    // 4. Configurer micro-ROS
    uros_core_config_t uros_config = {
        .node_name = "esp32_ultrasonic",
        .topic_name = "/scan",
        .domain_id = 0,
        .timer_period_ms = 200,  // 5 Hz
        .qos_history = RMW_QOS_POLICY_HISTORY_KEEP_LAST,
        .qos_depth = 10,
        .qos_reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT,
        .qos_durability = RMW_QOS_POLICY_DURABILITY_VOLATILE,
        .stack_size = 16000,
        .task_priority = 5,
        .core_affinity = 1,
        .status_led_gpio = 38,
        .status_led_brightness = 100,
        .on_metrics = NULL,
    };

    // 5. Configurer l'interface applicative
    uros_app_interface_t app_interface = {
        .type_support = ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, LaserScan),
        .message_size = sizeof(sensor_msgs__msg__LaserScan),
        .app_init = app_scan_ultra_init,
        .app_step = app_scan_ultra_step,
        .app_fini = app_scan_ultra_fini,
        .app_context = app,
        .app_context_size = sizeof(app_scan_ultra_t),
    };

    // 6. Créer et démarrer le core micro-ROS
    uros_core_context_t *core = NULL;
    esp_err_t ret = uros_core_create(&uros_config, &app_interface, &core);
    if (ret == ESP_OK) {
        uros_core_start(core);
    }
}
```

## ⚙️ Configuration

### Valeurs par défaut

| Paramètre | Valeur | Description |
|-----------|--------|-------------|
| `bins` | 36 | Résolution 10° (360°/36) |
| `angle_min` | 0.0 | 0° |
| `angle_max` | 6.283185 | 360° (2π radians) |
| `range_min` | 0.3 | Distance min (m) |
| `range_max` | 4.5 | Distance max (m) |
| `sensor_bin_mapping` | {0, 9, 18, 27} | 0°, 90°, 180°, 270° |
| `frame_id` | "base_link" | Frame TF ROS |

### Mapping angulaire

Pour 4 capteurs espacés de 90° et 36 bins (résolution 10°) :

| Capteur | Position | Angle | Bin |
|---------|----------|-------|-----|
| 0 | Avant | 0° | 0 |
| 1 | Droite | 90° | 9 |
| 2 | Arrière | 180° | 18 |
| 3 | Gauche | 270° | 27 |

Les bins entre les capteurs sont remplis avec `NAN` (non observés).

### Options de résolution

| Bins | Résolution | Angle increment | Utilisation |
|------|------------|-----------------|-------------|
| **36** | 10° | 0.1745 rad | **Recommandé** (standard ROS) |
| 72 | 5° | 0.0873 rad | Haute résolution |
| 4 | 90° | 1.5708 rad | Minimaliste (bande passante limitée) |

## 📖 Message ROS publié

```
sensor_msgs/LaserScan
  header:
    stamp: <timestamp>
    frame_id: "base_link"
  angle_min: 0.0
  angle_max: 6.283185 (2π)
  angle_increment: 0.174533 (10° en rad)
  time_increment: 0.044 (~44ms/capteur)
  scan_time: 0.176 (~176ms total)
  range_min: 0.3
  range_max: 4.5
  ranges[36]: [d0, NAN, NAN, ..., d1, NAN, ..., d2, NAN, ..., d3, NAN, ...]
  intensities[]: [] (vide)
```

## 🔧 Performances

### Fréquence de publication

- **Temps d'acquisition** : ~176ms (4 capteurs × 44ms)
- **Fréquence max** : ~5.7 Hz
- **Fréquence recommandée** : 5 Hz (200ms période)

### Utilisation mémoire

- **Stack micro-ROS** : 16 KB (configurable)
- **Message LaserScan** : ~200 bytes (36 bins × 4 bytes/float + header)
- **Context app** : <1 KB

## 🐛 Dépannage

| Problème | Cause | Solution |
|----------|-------|----------|
| Tous bins à NAN | Tous capteurs invalides | Vérifier lib_a02_provider |
| Message non publié | Agent micro-ROS absent | Lancer agent : `ros2 run micro_ros_agent micro_ros_agent serial --dev /dev/ttyUSB0` |
| Bins incorrects | Mauvais mapping | Vérifier `sensor_bin_mapping` |
| Fréquence basse | timer_period trop court | Augmenter à ≥ 200ms |

## 🧪 Visualisation RViz

### Lancer l'agent micro-ROS

```bash
ros2 run micro_ros_agent micro_ros_agent serial --dev /dev/ttyUSB0 -b 115200
```

### Visualiser dans RViz

```bash
ros2 run rviz2 rviz2
```

Configuration RViz :
1. **Fixed Frame** : `base_link`
2. **Add** → **LaserScan**
3. **Topic** : `/scan`
4. **Size** : 0.05
5. **Color** : By range (arc-en-ciel)

## 📄 Licence

Ce composant fait partie du framework **iobewi-idf-components** sous licence MIT.

## 🔗 Voir aussi

- **lib_a02_provider** : Provider ultrasonique avec filtrage
- **drv_a02yyuw** : Driver matériel A02YYUW
- **mw_uros_core** : Core micro-ROS générique
- **docs/architecture_a02yyuw_microros.md** : Document d'architecture complet
