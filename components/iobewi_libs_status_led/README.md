# lib_status_led - Bibliothèque Status LED

Bibliothèque pour mapper des états applicatifs vers des couleurs LED RGB.

## Rôle

Ce composant fait le pont entre la logique applicative (états abstraits) et le matériel (driver LED RGB).

**Responsabilité:**
- Définir une sémantique d'états applicatifs (WAITING, CONNECTED, ERROR, etc.)
- Mapper chaque état vers une couleur RGB spécifique
- Utiliser `drv_led_rgb` pour l'accès matériel

**Ce que ce composant NE FAIT PAS:**
- Accès GPIO direct (délégué à `drv_led_rgb`)
- Logique applicative (délégué aux composants `app_*`)

## États supportés

| État | Couleur | RGB | Sémantique |
|------|---------|-----|------------|
| `LIB_STATUS_LED_OFF` | Éteint | (0,0,0) | LED désactivée |
| `LIB_STATUS_LED_WAITING` | Bleu | (0,0,255) | Attente connexion/initialisation |
| `LIB_STATUS_LED_CONNECTED` | Vert | (0,255,0) | Connecté et opérationnel |
| `LIB_STATUS_LED_ERROR` | Rouge | (255,0,0) | Erreur critique |

## API Publique

### Types

```c
typedef enum {
    LIB_STATUS_LED_OFF = 0,
    LIB_STATUS_LED_WAITING,
    LIB_STATUS_LED_CONNECTED,
    LIB_STATUS_LED_ERROR
} lib_status_led_state_t;

typedef struct {
    gpio_num_t gpio;        // GPIO de la LED
    uint8_t brightness;     // Luminosité (0-255)
} lib_status_led_config_t;
```

### Fonctions

```c
esp_err_t lib_status_led_new(const lib_status_led_config_t *cfg, lib_status_led_t **out);
esp_err_t lib_status_led_set_state(lib_status_led_t *handle, lib_status_led_state_t state);
esp_err_t lib_status_led_del(lib_status_led_t *handle);
```

## Exemple d'utilisation

Voir `examples/basic_app/main/main.c`

```c
#include "lib_status_led/lib_status_led.h"

void app_main(void) {
    // Configuration
    lib_status_led_config_t cfg = {
        .gpio = GPIO_NUM_38,
        .brightness = 100,
    };

    // Création
    lib_status_led_t *status = NULL;
    esp_err_t ret = lib_status_led_new(&cfg, &status);
    if (ret != ESP_OK) return;

    // Cycle de vie typique d'une application
    lib_status_led_set_state(status, LIB_STATUS_LED_WAITING);     // Bleu
    vTaskDelay(pdMS_TO_TICKS(2000));

    lib_status_led_set_state(status, LIB_STATUS_LED_CONNECTED);   // Vert
    vTaskDelay(pdMS_TO_TICKS(5000));

    lib_status_led_set_state(status, LIB_STATUS_LED_ERROR);       // Rouge
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Nettoyage
    lib_status_led_del(status);
}
```

## Dépendances

- `drv_led_rgb` (driver matériel)

## Architecture

```
app_* (logique métier)
  └─→ lib_status_led (mapping états)
        └─→ drv_led_rgb (accès GPIO/RMT)
```

## Conformité CDC

- ✅ Catégorie `mw_*` (middleware)
- ✅ Pas d'accès matériel direct
- ✅ Pas de logique applicative
- ✅ Dépend uniquement de `drv_*`
- ✅ API `esp_err_t` exclusive
- ✅ Handle opaque
- ✅ Structure CDC: `include/lib_status_led/`, `src/`, `examples/`

## Extension possible

Pour ajouter de nouveaux états:

1. Modifier `lib_status_led_types.h`:
```c
typedef enum {
    LIB_STATUS_LED_OFF = 0,
    LIB_STATUS_LED_WAITING,
    LIB_STATUS_LED_CONNECTED,
    LIB_STATUS_LED_ERROR,
    LIB_STATUS_LED_WARNING,    // Nouveau: orange
} lib_status_led_state_t;
```

2. Modifier `lib_status_led.c`:
```c
static const drv_led_rgb_color_t state_colors[] = {
    // ...
    [LIB_STATUS_LED_WARNING] = {.r = 255, .g = 165, .b = 0},  // Orange
};
```

## License

MIT
