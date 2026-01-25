# mw_uros_core - Middleware micro-ROS Core

Middleware générique pour intégrer micro-ROS dans des applications ESP-IDF.

## Rôle

Ce composant fournit l'infrastructure micro-ROS réutilisable pour n'importe quel type d'application ROS 2.

**Responsabilités:**
- Gestion du lifecycle RCL (node, publisher, executor, timer)
- 2 tâches FreeRTOS (main_task pour spin + pub_task pour publication)
- Supervision de l'agent (ping, reconnexion automatique)
- Synchronisation temporelle avec l'agent
- Gestion LED status via `lib_status_led`
- Métriques runtime (publish_failures, agent_missed_pings)

**Ce que ce composant NE FAIT PAS:**
- Logique métier applicative (délégué aux `app_*`)
- Accès matériel direct (délégué aux `drv_*`)
- Construction de messages spécifiques (délégué aux `mw_*` builders)

## Architecture

Le core utilise un système de **callbacks applicatifs** pour rester générique:

```
┌─────────────────────────────────────┐
│  Application (app_scan_tof, etc.)   │
│  ┌─────────────────────────────┐    │
│  │ app_init()                  │    │
│  │ app_step() → remplit msg    │    │
│  │ app_fini()                  │    │
│  └─────────────────────────────┘    │
└──────────────┬──────────────────────┘
               │
               ▼
┌─────────────────────────────────────┐
│         mw_uros_core                │
│  ┌─────────────────────────────┐    │
│  │ main_task: RCL spin         │    │
│  │ pub_task: app_step + publish│    │
│  │ agent supervision           │    │
│  │ time sync, metrics          │    │
│  └─────────────────────────────┘    │
└──────────────┬──────────────────────┘
               │
               ▼
       micro-ROS agent
```

## API Publique

### Types

```c
typedef struct {
    const rosidl_message_type_support_t *type_support;
    size_t message_size;
    bool (*app_init)(void *app_context);
    bool (*app_step)(void *app_context, void *ros_message);
    void (*app_fini)(void *app_context);
    void *app_context;
    size_t app_context_size;
} uros_app_interface_t;

typedef struct {
    // ROS config
    const char *node_name;
    const char *topic_name;
    uint32_t domain_id;
    uint32_t timer_period_ms;

    // QoS config
    rmw_qos_history_policy_t qos_history;
    size_t qos_depth;
    rmw_qos_reliability_policy_t qos_reliability;
    rmw_qos_durability_policy_t qos_durability;

    // Task config
    uint32_t stack_size;
    uint32_t task_priority;
    int32_t core_affinity;

    // Status LED (optional)
    int32_t status_led_gpio;      // -1 = disabled
    uint8_t status_led_brightness;

    // Metrics hook (optional)
    void (*on_metrics)(uint32_t publish_failures, uint32_t app_step_failures);
} uros_core_config_t;
```

### Fonctions

#### Lifecycle

```c
uros_core_context_t *uros_core_create(const uros_core_config_t *config,
                                      const uros_app_interface_t *app);
void uros_core_destroy(uros_core_context_t *ctx);
bool uros_core_start(uros_core_context_t *ctx);
```

#### Utilitaires

```c
void uros_core_sync_time(void);
void uros_core_log_rcl_failure(const char *tag, const char *label, rcl_ret_t rc);
void uros_core_configure_entity_timeout(void);
```

## Exemple d'utilisation

Voir `examples/basic_app/main/main.c` pour un exemple complet.

### Exemple minimal (publisher String)

```c
#include "mw_uros_core/mw_uros_core.h"
#include <std_msgs/msg/string.h>

// Contexte applicatif
typedef struct {
    uint32_t counter;
} my_app_context_t;

// Callback init
bool my_app_init(void *ctx) {
    my_app_context_t *app = (my_app_context_t *)ctx;
    app->counter = 0;
    return true;
}

// Callback step: remplit le message
bool my_app_step(void *ctx, void *ros_msg) {
    my_app_context_t *app = (my_app_context_t *)ctx;
    std_msgs__msg__String *msg = (std_msgs__msg__String *)ros_msg;

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "Hello %u", app->counter++);
    msg->data.data = buffer;
    msg->data.size = strlen(buffer);
    msg->data.capacity = msg->data.size + 1;

    return true;
}

// Callback cleanup
void my_app_fini(void *ctx) {
    // Rien à faire
}

void app_main(void) {
    // Configuration core
    uros_core_config_t config = {
        .node_name = "my_node",
        .topic_name = "/hello",
        .domain_id = 0,
        .timer_period_ms = 1000,
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

    // Contexte applicatif
    static my_app_context_t app_ctx;

    // Interface applicative
    uros_app_interface_t app = {
        .type_support = ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, String),
        .message_size = sizeof(std_msgs__msg__String),
        .app_init = my_app_init,
        .app_step = my_app_step,
        .app_fini = my_app_fini,
        .app_context = &app_ctx,
        .app_context_size = sizeof(my_app_context_t),
    };

    // Création et démarrage
    uros_core_context_t *core = uros_core_create(&config, &app);
    if (core != NULL) {
        uros_core_start(core);
    }
}
```

## Dépendances

- `micro_ros_espidf_component` (micro-ROS pour ESP-IDF)
- `lib_status_led` (middleware LED status)
- `freertos` (RTOS)
- `esp_timer` (timer ESP-IDF)

## Fonctionnalités avancées

### Supervision agent

- Ping automatique toutes les 1s
- Après 3 pings manqués: reconnexion automatique
- LED passe en WAITING pendant reconnexion

### Gestion erreurs

- Backoff exponentiel en cas de rafale d'erreurs de publication
- Détection de congestion
- Métriques exposées via hook `on_metrics()`

### Thread-safety

- Mutex RCL protège `rcl_publish()` et `rclc_executor_spin_some()`
- Architecture 2 tasks évite les dead-locks

## Conformité CDC

- ✅ Catégorie `mw_*` (middleware)
- ✅ Pas d'accès matériel direct
- ✅ Pas de logique applicative hardcodée
- ✅ Dépend uniquement de micro-ROS et `lib_status_led`
- ✅ API générique réutilisable
- ✅ Structure CDC: `include/mw_uros_core/`, `src/`, `examples/`

## Extensions futures

Pour ajouter d'autres types de messages, créer un nouveau composant `app_*`:

```c
// app_imu.c
bool uros_app_imu_start(void) {
    uros_app_interface_t app = {
        .type_support = ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, Imu),
        .message_size = sizeof(sensor_msgs__msg__Imu),
        .app_init = imu_app_init,
        .app_step = imu_app_step,
        .app_fini = imu_app_fini,
        // ...
    };
    return uros_core_start(uros_core_create(&config, &app));
}
```

## License

MIT
