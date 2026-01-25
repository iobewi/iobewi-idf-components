# mw_uros_transport_usb - Middleware micro-ROS USB-CDC Transport

Middleware pour transport micro-ROS via USB-CDC (Communication Device Class).

## Rôle

Ce composant fournit l'implémentation du transport USB-CDC pour micro-ROS sur ESP32-S2/S3.

**Responsabilités:**
- Implémentation de l'interface `uxrCustomTransport` pour micro-ROS
- Gestion de la communication USB-CDC via TinyUSB
- Logging ESP-IDF via USB-CDC (optionnel)
- Initialisation TinyUSB

**Ce que ce composant NE FAIT PAS:**
- Accès USB matériel direct (délégué à TinyUSB/ESP-IDF)
- Logique applicative ROS (délégué à `mw_uros_core`)

## Architecture

```
┌─────────────────────────────────┐
│     mw_uros_core                │
│  (micro-ROS application)        │
└───────────┬─────────────────────┘
            │
            ▼
┌─────────────────────────────────┐
│  mw_uros_transport_usb          │
│  ┌───────────────────────────┐  │
│  │ uxrCustomTransport        │  │
│  │  - open/close/read/write  │  │
│  └────────────┬──────────────┘  │
│               │                 │
│  ┌────────────▼──────────────┐  │
│  │ TinyUSB CDC-ACM           │  │
│  └───────────────────────────┘  │
└────────────┬────────────────────┘
             │
             ▼
      Hardware USB (ESP32-S3)
```

## API Publique

### Transport micro-ROS

```c
bool esp_usbcdc_open(struct uxrCustomTransport* transport);
bool esp_usbcdc_close(struct uxrCustomTransport* transport);
size_t esp_usbcdc_write(struct uxrCustomTransport* transport,
                        const uint8_t* buf, size_t len, uint8_t* err);
size_t esp_usbcdc_read(struct uxrCustomTransport* transport,
                       uint8_t* buf, size_t len, int timeout, uint8_t* err);
```

### Logging USB-CDC

```c
esp_err_t esp_usbcdc_logging_init(void);
```

### Initialisation TinyUSB

```c
esp_err_t esp_usbcdc_tinyusb_init_once(const tinyusb_config_t *tinyusb_config);
```

## Utilisation

### Avec micro-ROS

Le transport USB-CDC est utilisé automatiquement si configuré dans micro-ROS :

```c
#include "mw_uros_transport_usb/esp_usbcdc_transport.h"
#include <rmw_microros/rmw_microros.h>

void app_main(void) {
    // Le transport est configuré automatiquement par micro-ROS
    // si CONFIG_MICRO_ROS_TRANSPORT_USB_CDC=y

    // Initialisation micro-ROS core
    // ...
}
```

### Logging via USB-CDC

Pour activer le logging ESP-IDF via USB-CDC :

```c
#include "mw_uros_transport_usb/esp_usbcdc_logging.h"

void app_main(void) {
    if (esp_usbcdc_logging_init() == ESP_OK) {
        ESP_LOGI("MAIN", "USB-CDC logging enabled");
    }
}
```

## Configuration

### Kconfig

```
CONFIG_MICRO_ROS_TRANSPORT_USB_CDC=y    # Transport USB-CDC pour micro-ROS
CONFIG_TINYUSB_CDC_ENABLED=y             # TinyUSB CDC-ACM
CONFIG_TINYUSB_CDC_COUNT=2               # 2 ports CDC (1 pour micro-ROS, 1 pour logs)
```

### Ports USB-CDC

- **Port 0 (TINYUSB_CDC_ACM_0)** : Transport micro-ROS
- **Port 1 (TINYUSB_CDC_ACM_1)** : Logging ESP-IDF (optionnel)

## Cibles supportées

- ✅ ESP32-S2
- ✅ ESP32-S3
- ❌ ESP32 (pas de USB natif)
- ❌ ESP32-C3 (pas de USB CDC natif)

## Dépendances

- `esp_tinyusb` (Stack USB ESP-IDF)
- `micro_ros_espidf_component` (micro-ROS pour ESP-IDF)

## Configuration micro-ROS Agent

Pour se connecter depuis un PC :

```bash
# Linux/Mac
ros2 run micro_ros_agent micro_ros_agent serial --dev /dev/ttyACM0

# Windows
ros2 run micro_ros_agent micro_ros_agent serial --dev COM3
```

**Note:** Le port série apparaît automatiquement quand l'ESP32-S3 est connecté via USB.

## Conformité CDC

- ✅ Catégorie `mw_*` (middleware)
- ✅ Pas d'accès matériel direct
- ✅ Implémente interface micro-ROS
- ✅ Dépend de TinyUSB (driver ESP-IDF officiel)
- ✅ Structure CDC: `include/mw_uros_transport_usb/`, `src/`

## Performance

- **Débit**: ~1 Mbps (USB Full Speed)
- **Latence**: ~1-2 ms
- **Fiabilité**: Reconnexion automatique si câble déconnecté

## Debugging

Si le transport USB ne fonctionne pas :

1. Vérifier que `CONFIG_TINYUSB_CDC_COUNT >= 1`
2. Vérifier que le câble USB supporte les données (pas juste alimentation)
3. Vérifier les permissions série sur Linux (`sudo usermod -a -G dialout $USER`)
4. Vérifier les logs TinyUSB avec `CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y`

## License

MIT
