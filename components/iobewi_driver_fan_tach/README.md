# drv_fan_tach – Driver tachymètre ventilateur

Driver matériel pur pour lecture de signal tachymètre de ventilateur (2 fils / 3 fils / 4 fils) basé sur le périphérique **PCNT** de l’ESP32.

Le driver fournit des **mesures brutes (pulses / période)** sans aucune interprétation applicative (RPM, seuils, alarmes).

---

## Caractéristiques

* Lecture signal tachymètre via GPIO
* Comptage matériel via PCNT (faible charge CPU)
* Mesure brute `pulse_count / period_ms`
* Fenêtre de mesure déterministe
* Support multi-canaux (un PCNT par entrée)
* Compatible ESP-IDF 5.x / 6.x
* Conforme CDC iobewi-idf-components (catégorie `drv_*`)

---

## API Publique

### Types

```c
typedef struct {
    int gpio_num;                     // GPIO d'entrée tachy
    int16_t counter_high_limit;       // Limite haute PCNT
    int16_t counter_low_limit;        // Limite basse PCNT
    bool pullup_enable;               // Pull-up interne GPIO
} drv_fan_tach_channel_cfg_t;

typedef struct {
    const drv_fan_tach_channel_cfg_t *channels;
    size_t channel_count;
} drv_fan_tach_config_t;

typedef struct {
    int32_t pulse_count;              // Nombre de pulses comptées
    uint32_t period_ms;               // Fenêtre de mesure
    bool valid;                       // Mesure valide
} drv_fan_tach_sample_t;
```

---

### Fonctions

#### Lifecycle

```c
esp_err_t drv_fan_tach_new(
    const drv_fan_tach_config_t *cfg,
    drv_fan_tach_t **out
);

esp_err_t drv_fan_tach_del(drv_fan_tach_t *handle);
```

#### Contrôle

```c
esp_err_t drv_fan_tach_start(drv_fan_tach_t *handle, size_t index);
esp_err_t drv_fan_tach_stop(drv_fan_tach_t *handle, size_t index);
```

#### Lecture

```c
esp_err_t drv_fan_tach_read(
    drv_fan_tach_t *handle,
    size_t index,
    uint32_t period_ms,
    drv_fan_tach_sample_t *out
);
```

**Important :**

* `drv_fan_tach_read()` effectue une mesure **synchrone** :

  * reset compteur
  * comptage pendant `period_ms`
  * arrêt + lecture
* Aucune conversion en RPM n’est faite (volontaire).
* Le calcul RPM doit être fait dans la couche supérieure si nécessaire.

---

## Exemple d’utilisation

Voir `examples/basic_app/main/main.c`

```c
#include "drv_fan_tach/drv_fan_tach.h"

void app_main(void)
{
    static const drv_fan_tach_channel_cfg_t channels[] = {
        {
            .gpio_num = 4,
            .counter_high_limit = 30000,
            .counter_low_limit = -30000,
            .pullup_enable = true,
        },
    };

    drv_fan_tach_config_t cfg = {
        .channels = channels,
        .channel_count = 1,
    };

    drv_fan_tach_t *tach = NULL;
    if (drv_fan_tach_new(&cfg, &tach) != ESP_OK) {
        return;
    }

    while (1) {
        drv_fan_tach_sample_t sample;
        drv_fan_tach_read(tach, 0, 1000, &sample);

        if (sample.valid) {
            ESP_LOGI("TACH", "pulses=%ld period=%lu ms",
                     (long)sample.pulse_count,
                     (unsigned long)sample.period_ms);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

---

## Dépendances

* `esp_driver_pcnt` (PCNT – Pulse Counter)
* `driver` (GPIO, FreeRTOS, logging)

---

## Configuration matérielle

Le signal tachymètre est généralement :

* **open-collector / open-drain**
* nécessite une **résistance de pull-up** (interne ou externe)

**GPIO :**

* Toute GPIO d’entrée valide
* Pull-up interne activable via config

**PCNT :**

* Comptage sur **front montant**
* Accumulation activée (gestion overflow matériel)

---

## Limites

* Pas de calcul RPM
* Pas de filtrage logiciel avancé
* Pas de détection d’arrêt / défaut
* Une unit PCNT par canal (choix robuste, non mutualisé)

---

## Conformité CDC

* ✅ Catégorie `drv_*` (driver pur)
* ✅ Pas de logique métier
* ✅ API `esp_err_t` exclusive
* ✅ Handle opaque
* ✅ Compatible ESP-IDF 5.x / 6.x
* ✅ Structure CDC :

  * `include/drv_fan_tach/`
  * `src/`
  * `examples/`
* ✅ Allocation documentée (context interne via `calloc`)

---

## License

MIT