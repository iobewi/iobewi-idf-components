## lib_vl53l0x_provider

**Provider multi-capteurs VL53L0X avec lecture parallèle et snapshot atomique**

### Description

Le composant `lib_vl53l0x_provider` fournit une abstraction de haut niveau pour gérer plusieurs capteurs VL53L0X en parallèle. Chaque capteur est lu dans une tâche FreeRTOS dédiée, et les mesures sont accessibles via un snapshot atomique (lock-free).

**Caractéristiques** :
- ✅ Multi-capteurs (1-8 capteurs supportés)
- ✅ Lecture parallèle (une tâche FreeRTOS par capteur)
- ✅ Snapshot atomique lock-free (double-check de séquence)
- ✅ Retry automatique avec backoff exponentiel
- ✅ Configuration dynamique (pas de Kconfig)
- ✅ Conforme au CDC iobewi-idf-components

### Dépendances

- **`drv_vl53l0x`** : Driver VL53L0X (accès matériel I2C)
- **`freertos`** : Tasks, sempahores, spinlocks
- **`esp_timer`** : Timestamps

### API Publique

#### Types

```c
typedef struct lib_vl53l0x_provider_s lib_vl53l0x_provider_t;

typedef struct {
    bool valid;         // true si mesure valide
    uint8_t status;     // 0 = OK, sinon code d'erreur
    float range_m;      // Distance en mètres (NAN si invalide)
    uint32_t seq;       // Numéro de séquence
} lib_vl53l0x_sample_t;

typedef struct {
    gpio_num_t sda_gpio;
    gpio_num_t scl_gpio;
    uint32_t i2c_freq_hz;
    uint32_t timing_budget_us;
    uint32_t gpio_ready_timeout_ms;
} lib_vl53l0x_bus_config_t;

typedef struct {
    gpio_num_t xshut_gpio;
    gpio_num_t int_gpio;
    uint8_t addr_7b;
    uint8_t bin_idx;
} lib_vl53l0x_hw_config_t;

typedef struct {
    const lib_vl53l0x_bus_config_t *bus_config;
    const lib_vl53l0x_hw_config_t *hw_configs;
    uint8_t sensor_count;
} lib_vl53l0x_provider_config_t;
```

#### Fonctions

```c
// Initialiser config avec valeurs par défaut
esp_err_t lib_vl53l0x_provider_config_init(lib_vl53l0x_provider_config_t *config);

// Créer provider
esp_err_t lib_vl53l0x_provider_new(const lib_vl53l0x_provider_config_t *config,
                                     lib_vl53l0x_provider_t **out);

// Lire snapshot atomique
esp_err_t lib_vl53l0x_provider_read_snapshot(lib_vl53l0x_provider_t *handle,
                                               lib_vl53l0x_sample_t *samples);

// Détruire provider
esp_err_t lib_vl53l0x_provider_del(lib_vl53l0x_provider_t *handle);
```

### Exemple d'Utilisation

```c
#include "lib_vl53l0x_provider/lib_vl53l0x_provider.h"

void app_main(void)
{
    // Configuration bus I2C
    lib_vl53l0x_bus_config_t bus_config = {
        .sda_gpio = GPIO_NUM_21,
        .scl_gpio = GPIO_NUM_22,
        .i2c_freq_hz = 400000,
        .timing_budget_us = 30000,
        .gpio_ready_timeout_ms = 1000,
    };

    // Configuration capteurs (exemple 4 capteurs)
    lib_vl53l0x_hw_config_t hw_configs[4] = {
        {.xshut_gpio = GPIO_NUM_25, .int_gpio = GPIO_NUM_26, .addr_7b = 0x30, .bin_idx = 0},
        {.xshut_gpio = GPIO_NUM_27, .int_gpio = GPIO_NUM_14, .addr_7b = 0x31, .bin_idx = 9},
        {.xshut_gpio = GPIO_NUM_12, .int_gpio = GPIO_NUM_13, .addr_7b = 0x32, .bin_idx = 18},
        {.xshut_gpio = GPIO_NUM_15, .int_gpio = GPIO_NUM_2, .addr_7b = 0x33, .bin_idx = 27},
    };

    // Configuration provider
    lib_vl53l0x_provider_config_t config;
    lib_vl53l0x_provider_config_init(&config);
    config.bus_config = &bus_config;
    config.hw_configs = hw_configs;
    config.sensor_count = 4;

    // Créer provider
    lib_vl53l0x_provider_t *provider = NULL;
    esp_err_t ret = lib_vl53l0x_provider_new(&config, &provider);
    if (ret != ESP_OK) {
        ESP_LOGE("APP", "Failed to create provider: %s", esp_err_to_name(ret));
        return;
    }

    // Lire snapshot
    lib_vl53l0x_sample_t samples[4];
    while (1) {
        ret = lib_vl53l0x_provider_read_snapshot(provider, samples);
        if (ret == ESP_OK) {
            for (int i = 0; i < 4; i++) {
                if (samples[i].valid) {
                    ESP_LOGI("APP", "Sensor[%d]: %.2f m", i, samples[i].range_m);
                } else {
                    ESP_LOGI("APP", "Sensor[%d]: INVALID (status=%d)", i, samples[i].status);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    // Détruire (jamais atteint ici)
    lib_vl53l0x_provider_del(provider);
}
```

### Architecture

#### Snapshot Lock-Free

Le mécanisme de snapshot utilise un double-check de séquence :

1. Lire `seq[i]` (ACQUIRE)
2. Si impair → writer en cours, attendre
3. Lire `sample[i]`
4. Lire `seq[i]` (ACQUIRE)
5. Si `seq` inchangé et pair → valide
6. Sinon → retry

**Avantages** :
- ✅ Pas de mutex (zéro contention)
- ✅ Thread-safe
- ✅ Temps de lecture déterministe

#### Tâches Capteurs

Chaque capteur a une tâche FreeRTOS :

```
sensor_task:
  while (!stop_requested):
    - Attendre GPIO data-ready (IRQ)
    - Prendre mutex I2C
    - Lire VL53L0X_GetRangingMeasurementData
    - Relâcher mutex
    - Mettre à jour sample avec update_one (lock-free)
    - Backoff exponentiel si erreur
    - vTaskDelay(2ms + backoff)
```

**Avantages** :
- ✅ Lecture parallèle de tous les capteurs
- ✅ Retry automatique
- ✅ Partage du bus I2C via mutex

### Performance

| Métrique | Valeur |
|----------|--------|
| **Fréquence mesure** | ~30 Hz par capteur |
| **Latence snapshot** | < 1 ms (lock-free) |
| **Taille RAM** | ~500 bytes + 4KB stack par capteur |
| **CPU overhead** | < 5% (4 capteurs) |

### Configuration

Aucune configuration Kconfig nécessaire. Tout est passé dynamiquement via `lib_vl53l0x_provider_config_t`.

### Limitations

- Maximum 255 capteurs (limitation uint8_t)
- Bus I2C partagé (séquentiel, mutex)
- GPIO INT unique par capteur requis

### Migration depuis tof_provider

**Avant** (`tof_provider`) :
```c
tof_provider_init();  // Config statique via Kconfig
tof_sample_t samples[TOF_COUNT];
tof_provider_snapshot(samples);
```

**Après** (`lib_vl53l0x_provider`) :
```c
lib_vl53l0x_provider_config_t config = { /* dynamic config */ };
lib_vl53l0x_provider_t *provider = NULL;
lib_vl53l0x_provider_new(&config, &provider);
lib_vl53l0x_sample_t samples[SENSOR_COUNT];
lib_vl53l0x_provider_read_snapshot(provider, samples);
lib_vl53l0x_provider_del(provider);
```

### Conformité CDC

✅ **Conforme au CDC iobewi-idf-components v2.0**

- ✅ Headers : `lib_vl53l0x_provider_types.h` + `lib_vl53l0x_provider.h`
- ✅ Nommage : Préfixe `lib_vl53l0x_provider_*`
- ✅ Handle opaque : `typedef struct lib_vl53l0x_provider_s lib_vl53l0x_provider_t;`
- ✅ Taxonomie : `lib_*` (logique métier, pas d'accès matériel direct)
- ✅ Dépendances : `drv_vl53l0x` (driver pur)
- ✅ Pas de micro-ROS

### Voir Aussi

- [drv_vl53l0x](../drv_vl53l0x/README.md) : Driver VL53L0X (accès matériel)
- [app_scan_tof](../app_scan_tof/README.md) : Application scan ToF 360°
- [docs/component_best_practices.md](../docs/component_best_practices.md) : Référence CDC
