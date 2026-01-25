# drv_led_rgb - Driver LED RGB

Driver matériel pur pour LED RGB (WS2812, SK6812, APA106).

## Caractéristiques

- Pilotage direct GPIO via RMT
- Support WS2812/WS2812B, SK6812, APA106
- API générique (pas de sémantique applicative)
- Gestion multi-LED (strip)
- Contrôle couleur RGB (0-255) et luminosité
- Conforme CDC iobewi-idf-components (catégorie `drv_*`)

## API Publique

### Types

```c
typedef struct {
    uint8_t r;  // Rouge (0-255)
    uint8_t g;  // Vert (0-255)
    uint8_t b;  // Bleu (0-255)
} drv_led_rgb_color_t;

typedef struct {
    gpio_num_t gpio;                 // GPIO pour data LED
    uint8_t max_leds;                // Nombre de LEDs (1 pour status, N pour strip)
    drv_led_rgb_type_t led_type;    // Type de LED (WS2812, SK6812, APA106)
    uint8_t default_brightness;      // Luminosité par défaut (0-255, 0=max)
} drv_led_rgb_config_t;
```

### Fonctions

#### Lifecycle

```c
esp_err_t drv_led_rgb_new(const drv_led_rgb_config_t *cfg, drv_led_rgb_t **out);
esp_err_t drv_led_rgb_del(drv_led_rgb_t *handle);
```

#### Contrôle

```c
esp_err_t drv_led_rgb_set_color(drv_led_rgb_t *handle, uint8_t led_idx,
                                  const drv_led_rgb_color_t *color);
esp_err_t drv_led_rgb_set_brightness(drv_led_rgb_t *handle, uint8_t brightness);
esp_err_t drv_led_rgb_clear(drv_led_rgb_t *handle);
esp_err_t drv_led_rgb_refresh(drv_led_rgb_t *handle);
```

**Important:**
- Les changements de couleur sont bufferisés. Appeler `drv_led_rgb_refresh()` pour appliquer les modifications au matériel.
- La luminosité (0-255, 0=off, 255=max) est appliquée en scalant les valeurs RGB lors de `set_color()`.
- Après `set_brightness()`, il faut rappeler `set_color()` pour appliquer la nouvelle luminosité.

## Exemple d'utilisation

Voir `examples/basic_app/main/main.c`

```c
#include "drv_led_rgb/drv_led_rgb.h"

void app_main(void) {
    // Configuration
    drv_led_rgb_config_t cfg = {
        .gpio = GPIO_NUM_38,
        .max_leds = 1,
        .led_type = DRV_LED_RGB_TYPE_WS2812,
        .default_brightness = 50,
    };

    // Création driver
    drv_led_rgb_t *led = NULL;
    esp_err_t ret = drv_led_rgb_new(&cfg, &led);
    if (ret != ESP_OK) {
        return;
    }

    // Couleur rouge
    drv_led_rgb_color_t red = {.r = 255, .g = 0, .b = 0};
    drv_led_rgb_set_color(led, 0, &red);
    drv_led_rgb_refresh(led);

    vTaskDelay(pdMS_TO_TICKS(2000));

    // Nettoyage
    drv_led_rgb_clear(led);
    drv_led_rgb_refresh(led);
    drv_led_rgb_del(led);
}
```

## Dépendances

- `espressif/led_strip` (version 3.0.2+)
- `driver` (ESP-IDF GPIO/RMT)

## Configuration matérielle

Le driver utilise le périphérique RMT (Remote Control) pour générer les signaux de timing précis requis par les LED WS2812.

**Pins supportées:** Toute GPIO de sortie disponible (voir documentation ESP32)

**Timing:** 10 MHz de résolution (suffisant pour WS2812: 1.25 µs / 0.4 µs)

## Limites

- Pas de support DMA dans cette version (led_strip RMT sans DMA)
- Maximum ~64 LEDs recommandé (limite mémoire RMT)
- Pas de gestion couleur HSV (conversion à faire en couche supérieure)

## Conformité CDC

- ✅ Catégorie `drv_*` (driver pur)
- ✅ Pas de dépendance micro-ROS
- ✅ Pas de logique métier
- ✅ API `esp_err_t` exclusive
- ✅ Handle opaque
- ✅ Structure CDC: `include/drv_led_rgb/`, `src/`, `examples/`
- ✅ Allocation documentée (context interne via calloc)

## License

MIT
