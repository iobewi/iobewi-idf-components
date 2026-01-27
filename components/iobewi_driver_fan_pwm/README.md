# drv_fan_pwm – Driver PWM ventilateur

Driver matériel pur pour pilotage de ventilateurs via PWM matériel LEDC.

## Caractéristiques

* Pilotage PWM matériel via LEDC
* Support ventilateurs PWM standards (ex. 4 fils PC)
* API générique (pas de sémantique applicative)
* Support multi-canaux (plusieurs ventilateurs)
* Contrôle duty-cycle en pourcentage (0–100 %)
* Enable / disable par canal (duty mémorisé)
* Conforme CDC iobewi-idf-components (catégorie `drv_*`)

---

## API Publique

### Types

```c
typedef struct drv_fan_pwm_s drv_fan_pwm_t;
```

```c
typedef struct {
    int gpio_num;               // GPIO PWM
    ledc_channel_t channel;     // Canal LEDC
    uint32_t hpoint;            // HPOINT (généralement 0)
} drv_fan_pwm_channel_cfg_t;
```

```c
typedef struct {
    ledc_mode_t speed_mode;                 // LEDC_LOW_SPEED_MODE recommandé
    ledc_timer_t timer;                     // Timer LEDC
    ledc_timer_bit_t duty_resolution;       // Résolution duty (ex: 10 bits)
    uint32_t freq_hz;                       // Fréquence PWM (ex: 25 kHz)
    const drv_fan_pwm_channel_cfg_t *channels;
    size_t channel_count;
} drv_fan_pwm_config_t;
```

```c
typedef struct {
    uint32_t duty_raw;   // Duty LEDC en ticks
    bool enabled;        // Etat du canal
} drv_fan_pwm_channel_state_t;
```

---

### Fonctions

#### Lifecycle

```c
esp_err_t drv_fan_pwm_new(const drv_fan_pwm_config_t *cfg,
                          drv_fan_pwm_t **out);

esp_err_t drv_fan_pwm_del(drv_fan_pwm_t *handle);
```

---

#### Contrôle

```c
esp_err_t drv_fan_pwm_set_duty_pct(drv_fan_pwm_t *handle,
                                  size_t index,
                                  float duty_pct);

esp_err_t drv_fan_pwm_enable(drv_fan_pwm_t *handle,
                             size_t index,
                             bool enable);

esp_err_t drv_fan_pwm_get_state(drv_fan_pwm_t *handle,
                                size_t index,
                                drv_fan_pwm_channel_state_t *out);
```

**Important :**

* Le duty est exprimé en **pourcentage (0–100 %)**.
* Les valeurs hors plage sont clampées.
* Si un canal est désactivé (`enable=false`), le duty est forcé à 0 mais **mémorisé**.
* À la réactivation, le duty précédent est automatiquement restauré.
* L’API est **thread-safe** (mutex interne).

---

## Exemple d’utilisation

Voir `examples/basic_app/main/main.c`

```c
#include "drv_fan_pwm/drv_fan_pwm.h"

void app_main(void)
{
    drv_fan_pwm_config_t cfg;
    drv_fan_pwm_config_init(&cfg);

    const drv_fan_pwm_channel_cfg_t channels[] = {
        {
            .gpio_num = 4,
            .channel = LEDC_CHANNEL_0,
            .hpoint = 0,
        }
    };

    cfg.channels = channels;
    cfg.channel_count = 1;
    cfg.freq_hz = 25000;
    cfg.duty_resolution = LEDC_TIMER_10_BIT;
    cfg.timer = LEDC_TIMER_0;
    cfg.speed_mode = LEDC_LOW_SPEED_MODE;

    drv_fan_pwm_t *fan = NULL;
    if (drv_fan_pwm_new(&cfg, &fan) != ESP_OK) {
        return;
    }

    drv_fan_pwm_set_duty_pct(fan, 0, 50.0f);
    vTaskDelay(pdMS_TO_TICKS(2000));

    drv_fan_pwm_enable(fan, 0, false);
    vTaskDelay(pdMS_TO_TICKS(2000));

    drv_fan_pwm_enable(fan, 0, true);
}
```

---

## Dépendances

* `esp_driver_ledc`
* `freertos`

---

## Configuration matérielle

Le driver utilise le périphérique **LEDC** pour générer un signal PWM matériel.

**Pins supportées :**
Toute GPIO valide en sortie PWM (selon contraintes ESP32-S3).

**Fréquence typique :**

* 25 kHz recommandé pour ventilateurs PWM 4 fils PC

**Résolution :**

* Typiquement 8 à 12 bits selon compromis fréquence / précision

---

## Limites

* Pas de gestion du tachymètre (RPM)
* Pas de gestion de l’alimentation du ventilateur
* Un timer LEDC est partagé par tous les canaux de l’instance
* Pas de ramping automatique (à gérer en couche supérieure)

---

## Conformité CDC

* ✅ Catégorie `drv_*` (driver pur)
* ✅ Pas de logique métier
* ✅ API `esp_err_t` exclusive
* ✅ Handle opaque
* ✅ Thread-safe (mutex interne)
* ✅ Structure CDC : `include/drv_fan_pwm/`, `src/`, `examples/`
* ✅ Allocation documentée et maîtrisée (`calloc`)

---

## License

MIT