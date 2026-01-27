# drv_ntc_adc - Driver ADC brut pour NTC

Driver matériel pur pour lire des canaux ADC (NTC typiquement) en **valeurs brutes**.

## Caractéristiques

- Lecture ADC **oneshot** via composant `esp_adc`
- Support multi-canaux (N canaux configurés)
- API générique (pas de sémantique applicative)
- Retour **RAW uniquement** (pas de conversion Ohms/°C)
- Conforme CDC iobewi-idf-components (catégorie `drv_*`)

## API Publique

### Types

```c
typedef struct drv_ntc_adc_s drv_ntc_adc_t;

typedef struct {
    adc_channel_t channel;   // Canal ADC
    adc_atten_t   atten;     // Atténuation (ex: ADC_ATTEN_DB_12)
} drv_ntc_adc_channel_cfg_t;

typedef struct {
    adc_unit_t unit;                               // ADC_UNIT_1 ou ADC_UNIT_2
    adc_bitwidth_t bitwidth;                       // Résolution (ADC_BITWIDTH_DEFAULT conseillé)
    const drv_ntc_adc_channel_cfg_t *channels;     // Tableau de canaux
    size_t channel_count;                          // Nombre de canaux
} drv_ntc_adc_config_t;

typedef struct {
    uint32_t raw;      // Valeur ADC brute
    bool valid;        // true si lecture OK
} drv_ntc_adc_sample_t;
````

### Fonctions

#### Lifecycle

```c
esp_err_t drv_ntc_adc_config_init(drv_ntc_adc_config_t *config);
esp_err_t drv_ntc_adc_new(const drv_ntc_adc_config_t *config, drv_ntc_adc_t **out);
esp_err_t drv_ntc_adc_del(drv_ntc_adc_t *handle);
```

#### Lecture

```c
esp_err_t drv_ntc_adc_read(drv_ntc_adc_t *handle, size_t index, drv_ntc_adc_sample_t *out);
```

**Important :**

* `index` référence l’entrée dans `channels[]` (0..channel_count-1)
* Le driver ne fait **aucun filtrage** ni calibration : c’est à faire au-dessus si nécessaire.

## Exemple d'utilisation

Voir `examples/basic_app/main/main.c`

```c
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "drv_ntc_adc/drv_ntc_adc.h"

static const char *TAG = "basic_app";

void app_main(void)
{
    static const drv_ntc_adc_channel_cfg_t channels[] = {
        { .channel = ADC_CHANNEL_8, .atten = ADC_ATTEN_DB_12 },
        { .channel = ADC_CHANNEL_6, .atten = ADC_ATTEN_DB_12 },
    };

    drv_ntc_adc_config_t cfg;
    ESP_ERROR_CHECK(drv_ntc_adc_config_init(&cfg));
    cfg.unit = ADC_UNIT_1;
    cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    cfg.channels = channels;
    cfg.channel_count = sizeof(channels) / sizeof(channels[0]);

    drv_ntc_adc_t *adc = NULL;
    ESP_ERROR_CHECK(drv_ntc_adc_new(&cfg, &adc));

    while (1) {
        for (size_t i = 0; i < cfg.channel_count; i++) {
            drv_ntc_adc_sample_t s = {0};
            esp_err_t ret = drv_ntc_adc_read(adc, i, &s);
            if (ret == ESP_OK && s.valid) {
                ESP_LOGI(TAG, "idx=%u raw=%" PRIu32, (unsigned)i, s.raw);
            } else {
                ESP_LOGW(TAG, "idx=%u read err: %s", (unsigned)i, esp_err_to_name(ret));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

## Dépendances

* `esp_adc` (API oneshot)
* `esp_common`, `log` (via IDF)

## Configuration matérielle

* **ADC Unit** : préférer `ADC_UNIT_1` (ADC2 peut être contraint selon le SoC / Wi-Fi selon cas)
* **Atténuation** :

  * `ADC_ATTEN_DB_12` recommandé si la tension peut monter proche de 3.3V
* **Résolution** :

  * `ADC_BITWIDTH_DEFAULT` recommandé (laisse l’IDF choisir la résolution optimale)

## Limites

* Lecture brute : pas de conversion, pas de filtrage, pas de calibration
* Mapping GPIO↔ADC channel dépend de la cible (ESP32-S3). Vérifier le schéma et la doc SoC.
* ADC peut être non-linéaire : si besoin de précision, utiliser `adc_cali_*` dans une couche supérieure.

## Conformité CDC

* ✅ Catégorie `drv_*` (driver pur)
* ✅ Pas de logique métier (pas de °C / Ohms)
* ✅ API `esp_err_t` exclusive
* ✅ Handle opaque
* ✅ Structure CDC: `include/`, `src/`, `examples/`
* ✅ Allocation documentée (context interne via calloc)

## License

MIT