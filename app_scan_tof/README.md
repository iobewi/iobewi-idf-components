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
│  │ scan_engine_create()   │     │
│  │ scan_engine_start()    │     │
│  │ scan_engine_step()     │     │
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

### Scan Engine

```c
typedef struct {
    int max_scans_per_cycle;
    int scan_timeout_ms;
} scan_engine_config_t;

typedef struct scan_engine scan_engine_t;

esp_err_t scan_engine_create(const scan_engine_config_t *cfg,
                               scan_engine_t **out);

esp_err_t scan_engine_start(scan_engine_t *engine);

esp_err_t scan_engine_step(scan_engine_t *engine,
                             tof_snapshot_t *snapshot_out);

void scan_engine_destroy(scan_engine_t *engine);
```

## Utilisation avec mw_uros_core

Voir `/workspaces/uros_vl53l0x/main/uros_app_scan.c` pour l'implémentation complète.

```c
// Callbacks pour mw_uros_core
bool scan_app_init(void *app_context) {
    // Créer scan_engine
    // Créer scan_builder
    return true;
}

bool scan_app_step(void *app_context, void *ros_message) {
    // scan_engine_step() → récupère snapshot ToF
    // scan_builder_fill() → remplit LaserScan
    return true;
}

void scan_app_fini(void *app_context) {
    // Détruit scan_engine et scan_builder
}
```

## Configuration

Le scan engine est configuré via:
- Nombre max de scans par cycle
- Timeout de scan (ms)
- Nombre de bins angulaires (LaserScan)
- Limites de portée min/max

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
