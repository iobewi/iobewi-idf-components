# mw_scan_builder - Middleware LaserScan Builder

Middleware pour construire des messages `sensor_msgs/LaserScan` à partir de mesures ToF.

## Rôle

Ce composant fait le pont entre les données brutes du capteur ToF et le format ROS 2 LaserScan.

**Responsabilités:**
- Initialiser un message LaserScan (paramètres angulaires, limites de portée)
- Mapper les mesures ToF vers les bins angulaires corrects
- Gérer les buffers ranges et frame_id
- Filtrer les mesures invalides (→ NAN)

**Ce que ce composant NE FAIT PAS:**
- Accès matériel direct au capteur (délégué à `drv_vl53l0x`)
- Orchestration du scan (délégué à `app_*`)
- Publication ROS (délégué à `mw_uros_core`)

## Architecture

```
┌──────────────────────────┐
│  drv_vl53l0x (hardware)  │
│  tof_sample_t[]          │
└────────────┬─────────────┘
             │
             ▼
┌──────────────────────────┐
│   mw_scan_builder        │
│  ┌────────────────────┐  │
│  │ scan_builder_init()│  │
│  │ scan_builder_fill()│  │
│  └────────────────────┘  │
└────────────┬─────────────┘
             │
             ▼
     sensor_msgs/LaserScan
```

## API Publique

### Types

```c
typedef struct {
    float angle_min;        // Angle minimal (radians)
    float angle_inc;        // Incrément angulaire (radians)
    int bins;               // Nombre de bins

    float range_min;        // Portée minimale (m)
    float range_max;        // Portée maximale (m)

    float scan_time;        // Temps total du scan (s)
    float time_increment;   // Temps entre mesures (s)

    const char *frame_id;   // Frame ROS
} scan_config_t;

typedef struct {
    float *ranges_buffer;
    size_t ranges_capacity;
    char *frame_id_buffer;
    size_t frame_id_capacity;
    bool owns_ranges_buffer;
    bool owns_frame_id_buffer;
} scan_builder_storage_t;
```

### Fonctions

```c
bool scan_builder_init(sensor_msgs__msg__LaserScan *msg,
                       const scan_config_t *cfg,
                       scan_builder_storage_t *storage);

void scan_builder_deinit(sensor_msgs__msg__LaserScan *msg,
                          scan_builder_storage_t *storage);

void scan_builder_fill(sensor_msgs__msg__LaserScan *msg,
                       const scan_config_t *cfg,
                       const tof_sample_t samples[TOF_COUNT],
                       const tof_hw_config_t *hw_cfg);
```

## Exemple d'utilisation

```c
#include "mw_scan_builder/mw_scan_builder.h"
#include <sensor_msgs/msg/laser_scan.h>

void app_main(void) {
    // Configuration scan
    scan_config_t scan_cfg = {
        .angle_min = -M_PI,
        .angle_inc = 2 * M_PI / 84,
        .bins = 84,
        .range_min = 0.03f,
        .range_max = 2.0f,
        .scan_time = 0.1f,
        .time_increment = 0.1f / 84,
        .frame_id = "base_link",
    };

    // Buffers statiques
    static float ranges_buffer[84];
    static char frame_id_buffer[32];

    scan_builder_storage_t storage = {
        .ranges_buffer = ranges_buffer,
        .ranges_capacity = 84,
        .frame_id_buffer = frame_id_buffer,
        .frame_id_capacity = 32,
        .owns_ranges_buffer = false,
        .owns_frame_id_buffer = false,
    };

    // Message LaserScan
    sensor_msgs__msg__LaserScan scan_msg;

    // Initialiser
    if (!scan_builder_init(&scan_msg, &scan_cfg, &storage)) {
        ESP_LOGE(TAG, "Failed to init scan builder");
        return;
    }

    // Récupérer échantillons ToF
    tof_sample_t samples[TOF_COUNT];
    const tof_hw_config_t *hw_cfg = tof_config_get();

    // Remplir le message
    scan_builder_fill(&scan_msg, &scan_cfg, samples, hw_cfg);

    // Publier avec mw_uros_core...

    // Cleanup
    scan_builder_deinit(&scan_msg, &storage);
}
```

## Gestion mémoire

Le builder supporte 2 modes:

**1. Buffers fournis par l'appelant (recommandé):**
```c
static float ranges_buffer[84];
static char frame_id_buffer[32];

scan_builder_storage_t storage = {
    .ranges_buffer = ranges_buffer,
    .ranges_capacity = 84,
    .frame_id_buffer = frame_id_buffer,
    .frame_id_capacity = 32,
    .owns_ranges_buffer = false,  // Appelant garde ownership
    .owns_frame_id_buffer = false,
};
```

**2. Allocation dynamique (si CONFIG_MICRO_ROS_SCAN_BUILDER_ALLOC_MALLOC activé):**
```c
scan_builder_storage_t storage = {
    .ranges_buffer = NULL,  // scan_builder_init() allouera
    .ranges_capacity = 0,
    .frame_id_buffer = NULL,
    .frame_id_capacity = 0,
};

scan_builder_init(&msg, &cfg, &storage);
// Builder a alloué et positionné owns_* = true

scan_builder_deinit(&msg, &storage);
// Builder libère automatiquement
```

## Mapping ToF → LaserScan

Chaque capteur ToF est mappé vers un **bin fixe** via `hw_cfg[sensor_index].bin_idx`:

```
ToF Index   Angle    Bin
─────────────────────────
    0       -180°     0
    1       -175°     1
    ...
   42          0°    42
    ...
   83       +175°    83
```

Les bins sans capteur restent à `NAN` (zone non observée).

## Dépendances

- `drv_vl53l0x` (types tof_sample_t, tof_hw_config_t)
- `micro_ros_espidf_component` (sensor_msgs)

## Conformité CDC

- ✅ Catégorie `mw_*` (middleware)
- ✅ Pas d'accès matériel direct
- ✅ Pas de logique applicative
- ✅ Construction de message ROS générique
- ✅ Structure CDC: `include/mw_scan_builder/`, `src/`

## License

MIT
