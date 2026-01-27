# app_scan_tof - Application ToF Scanner

Application d'orchestration pour scanner ToF multi-capteurs avec publication LaserScan.

## Rôle

Ce composant orchestre le scan ToF et coordonne les différents modules (driver, builder, core).

**Responsabilités:**
- Orchestrer le cycle de scan (démarrage, step, arrêt)
- Coordonner drv_vl53l0x et mw_scan_builder
- Gérer le timing et la synchronisation
- Implémenter les callbacks pour mw_uros_core

**Ce que ce composant NE FAIT PAS:**
- Accès matériel direct (délégué à `drv_vl53l0x`)
- Construction du message LaserScan (délégué à `mw_scan_builder`)
- Publication ROS (délégué à `mw_uros_core`)

## Architecture

```
┌─────────────────────────────────┐
│      mw_uros_core               │
│   app_init(), app_step()        │
└───────────┬─────────────────────┘
            │
            ▼
┌─────────────────────────────────┐
│     app_scan_tof                │
│  ┌────────────────────────┐     │
│  │ app_scan_tof_new()     │     │
│  │ app_scan_tof_step()    │     │
│  │ app_scan_tof_del()     │     │
│  └────────────────────────┘     │
└──┬────────────────────┬─────────┘
   │                    │
   ▼                    ▼
┌──────────┐      ┌──────────────┐
│drv_vl53l0x│      │mw_scan_builder│
│ (ToF HW)  │      │ (LaserScan)   │
└───────────┘      └───────────────┘
```

## API Publique

### Types

```c
typedef struct app_scan_tof_s app_scan_tof_t;

typedef struct {
    lib_vl53l0x_provider_config_t provider_config;
    mw_scan_builder_config_t scan_config;
    int64_t (*time_provider)(void);
} app_scan_tof_config_t;
```

### Fonctions

```c
esp_err_t app_scan_tof_config_init(app_scan_tof_config_t *config);

esp_err_t app_scan_tof_new(const app_scan_tof_config_t *config,
                            app_scan_tof_t **out);

esp_err_t app_scan_tof_step(app_scan_tof_t *handle,
                             sensor_msgs__msg__LaserScan *out_msg);

esp_err_t app_scan_tof_set_time_provider(app_scan_tof_t *handle,
                                          int64_t (*time_provider_ns)(void));

esp_err_t app_scan_tof_del(app_scan_tof_t *handle);
```

## Utilisation avec mw_uros_core

Voir `/workspaces/uros_vl53l0x/main/uros_app_scan.c` pour l'implémentation complète.

```c
// Callbacks pour mw_uros_core
bool scan_app_init(void *app_context) {
    // Créer app_scan_tof
    app_scan_tof_config_t config;
    app_scan_tof_config_init(&config);
    // ... configurer ...
    app_scan_tof_new(&config, (app_scan_tof_t**)app_context);
    return true;
}

bool scan_app_step(void *app_context, void *ros_message) {
    // app_scan_tof_step() → lit capteurs + remplit LaserScan
    app_scan_tof_t *handle = *(app_scan_tof_t**)app_context;
    sensor_msgs__msg__LaserScan *msg = (sensor_msgs__msg__LaserScan*)ros_message;
    return app_scan_tof_step(handle, msg) == ESP_OK;
}

void scan_app_fini(void *app_context) {
    // Détruit app_scan_tof
    app_scan_tof_t *handle = *(app_scan_tof_t**)app_context;
    app_scan_tof_del(handle);
}
```

## Configuration

L'application est configurée via `app_scan_tof_config_t`:
- **provider_config** : Configuration des capteurs VL53L0X (I2C, pins, mapping)
- **scan_config** : Configuration du LaserScan (bins, angles, portées)
- **time_provider** : Provider de temps pour synchronisation ROS (optionnel)

## Dépendances

- `drv_vl53l0x` (accès capteurs ToF)
- `mw_scan_builder` (construction LaserScan)
- `freertos` (RTOS)
- `esp_timer` (timing)

## Conformité CDC

- ✅ Catégorie `app_*` (application)
- ✅ Orchestre drv + mw
- ✅ Pas d'accès matériel direct
- ✅ Logique métier scan ToF
- ✅ Structure CDC: `include/app_scan_tof/`, `src/`

## License

MIT
