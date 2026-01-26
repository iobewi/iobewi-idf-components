# drv_a02yyuw

Driver ESP-IDF pour capteur de distance ultrasonique **A02YYUW (SEN0311)**

## 📋 Description

Driver matériel pour piloter plusieurs capteurs A02YYUW sur un UART partagé. Ce composant permet de gérer N capteurs avec :
- **UART partagé** : Un seul port UART pour tous les capteurs (RX uniquement côté ESP)
- **Sélection par alimentation** : Chaque capteur dispose d'un GPIO EN dédié (STMPS2141STR)
- **Contrôle de mode** : GPIO commun pour sélectionner le mode temps réel ou filtré
- **Gestion automatique** : Power cycling, stabilisation et flush UART

## 🏗️ Taxonomie

**Catégorie** : `drv_*` (Driver matériel)

**Dépendances** :
- ESP-IDF `driver` (UART, GPIO)
- ESP-IDF `freertos`

**Règles** :
- ✅ Accès matériel direct (UART, GPIO)
- ❌ Aucune dépendance micro-ROS
- ❌ Aucune logique métier

## 📡 Protocole UART

- **Baudrate** : 9600 bps
- **Format** : 8N1 (8 bits, no parity, 1 stop bit)
- **Trame** : `[0xFF, DATA_H, DATA_L, SUM]`
  - `SUM = (0xFF + DATA_H + DATA_L) & 0xFF`
  - `distance_mm = (DATA_H << 8) | DATA_L`

## 🔌 Câblage

### Capteur A02YYUW

| Pin capteur | Signal | Connexion ESP32 |
|-------------|--------|-----------------|
| Pin 1 (VCC) | Alimentation | Sortie STMPS2141STR (commutée via GPIO EN) |
| Pin 2 (GND) | Masse | GND |
| Pin 3 (RX)  | Mode | GPIO mode (ex: GPIO5) |
| Pin 4 (TX)  | Données UART | GPIO RX UART (ex: GPIO18) |

### Configuration multi-capteurs

```
ESP32 UART RX (GPIO18) ──┬──> Capteur 0 TX
                         ├──> Capteur 1 TX
                         ├──> Capteur 2 TX
                         └──> Capteur N TX

ESP32 Mode GPIO (GPIO5) ──┬──> Capteur 0 RX
                          ├──> Capteur 1 RX
                          ├──> Capteur 2 RX
                          └──> Capteur N RX

ESP32 GPIO EN[0] ─────> STMPS2141[0] ─> VCC Capteur 0
ESP32 GPIO EN[1] ─────> STMPS2141[1] ─> VCC Capteur 1
ESP32 GPIO EN[2] ─────> STMPS2141[2] ─> VCC Capteur 2
ESP32 GPIO EN[N] ─────> STMPS2141[N] ─> VCC Capteur N
```

## 🚀 API

### Types principaux

```c
typedef struct drv_a02yyuw_s drv_a02yyuw_t;  // Handle opaque

typedef enum {
    DRV_A02YYUW_MODE_REALTIME = 0,   // Mode temps réel (RX = LOW)
    DRV_A02YYUW_MODE_PROCESSED = 1,  // Mode filtré (RX = HIGH/float)
} drv_a02yyuw_mode_t;

typedef struct {
    uart_port_t uart_num;        // Port UART (ex: UART_NUM_1)
    int uart_rx_gpio;            // GPIO RX UART (connecté au TX du capteur)
    int uart_tx_gpio;            // GPIO TX UART (typiquement UART_PIN_NO_CHANGE)
    int baudrate;                // 9600 par défaut
    int rx_buffer_size;          // 256 par défaut

    int gpio_mode;               // GPIO contrôle mode (-1 si non utilisé)
    bool mode_active_high;       // true = HIGH pour mode processed

    const int *gpio_en_list;     // Liste des GPIO EN (un par capteur)
    int sensor_count;            // Nombre de capteurs

    uint32_t t_mode_settle_ms;   // Délai stabilisation mode (2ms défaut)
    uint32_t t_power_up_ms;      // Délai après power-up (20ms défaut)
    uint32_t t_power_down_ms;    // Délai après power-down (10ms défaut)
} drv_a02yyuw_config_t;
```

### Fonctions

```c
// Initialiser la configuration avec valeurs par défaut
esp_err_t drv_a02yyuw_config_init(drv_a02yyuw_config_t *config);

// Créer une instance du driver
esp_err_t drv_a02yyuw_new(const drv_a02yyuw_config_t *config, drv_a02yyuw_t **out_handle);

// Sélectionner un capteur et configurer son mode
esp_err_t drv_a02yyuw_select(drv_a02yyuw_t *handle, int sensor_id, drv_a02yyuw_mode_t mode);

// Lire une mesure de distance (en millimètres)
esp_err_t drv_a02yyuw_read(drv_a02yyuw_t *handle, uint16_t *distance_mm, uint32_t timeout_ms);

// Détruire l'instance du driver
esp_err_t drv_a02yyuw_del(drv_a02yyuw_t *handle);
```

## 📝 Exemple d'utilisation

### Capteur unique

```c
#include "drv_a02yyuw/drv_a02yyuw.h"

static const int EN_PINS[] = {14}; // GPIO EN du STMPS2141STR

void app_main(void)
{
    // Configuration
    drv_a02yyuw_config_t config;
    drv_a02yyuw_config_init(&config);

    config.uart_num = UART_NUM_1;
    config.uart_rx_gpio = 18;
    config.gpio_mode = 5;
    config.mode_active_high = true;
    config.gpio_en_list = EN_PINS;
    config.sensor_count = 1;

    // Création du driver
    drv_a02yyuw_t *sensor = NULL;
    ESP_ERROR_CHECK(drv_a02yyuw_new(&config, &sensor));

    while (1) {
        // Sélectionner le capteur 0 en mode filtré
        ESP_ERROR_CHECK(drv_a02yyuw_select(sensor, 0, DRV_A02YYUW_MODE_PROCESSED));

        // Lire la distance
        uint16_t distance_mm = 0;
        esp_err_t ret = drv_a02yyuw_read(sensor, &distance_mm, 500);
        if (ret == ESP_OK) {
            printf("Distance: %u mm\n", distance_mm);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Nettoyage
    drv_a02yyuw_del(sensor);
}
```

### Multi-capteurs

Voir `examples/multi_app/` pour un exemple complet avec 4 capteurs.

## 🧪 Exemples

### basic_app

Exemple simple avec un seul capteur et filtre médian sur 3 mesures.

```bash
cd examples/basic_app
idf.py menuconfig  # Configurer les GPIO
idf.py build flash monitor
```

### multi_app

Exemple avec 4 capteurs en scan séquentiel.

```bash
cd examples/multi_app
idf.py build flash monitor
```

## ⚙️ Configuration

Les exemples utilisent `Kconfig` pour configurer les GPIO :

```
CONFIG_A02_BASIC_EN_GPIO=14          # GPIO EN du STMPS2141STR
CONFIG_A02_BASIC_UART_RX_GPIO=18     # GPIO RX UART
CONFIG_A02_BASIC_MODE_GPIO=5         # GPIO contrôle mode
```

## 🔧 Gestion mémoire

- **Allocation** : `drv_a02yyuw_new()` alloue dynamiquement le handle et la liste des GPIO EN
- **Ownership** : Le driver possède la mémoire allouée
- **Libération** : `drv_a02yyuw_del()` libère toute la mémoire

## 📖 Notes techniques

### Première mesure après sélection

La première mesure après `drv_a02yyuw_select()` peut être invalide. Il est recommandé de :
1. Jeter la première trame
2. Lire N mesures (N ≥ 3)
3. Appliquer un filtre médian

### Timings recommandés

- `t_power_down_ms` : 10ms minimum (temps de décharge du capteur)
- `t_mode_settle_ms` : 2ms minimum (stabilisation du mode)
- `t_power_up_ms` : 20ms minimum (boot du capteur)

### Mode temps réel vs filtré

- **Mode temps réel** (`DRV_A02YYUW_MODE_REALTIME`) : Mesures brutes, rafraîchissement rapide
- **Mode filtré** (`DRV_A02YYUW_MODE_PROCESSED`) : Mesures filtrées par le capteur, plus stable

## 🐛 Dépannage

| Problème | Cause possible | Solution |
|----------|----------------|----------|
| Aucune trame reçue | Mauvais GPIO RX | Vérifier le câblage TX capteur → RX ESP |
| Checksum invalide | Bruit, alimentation instable | Ajouter condensateur 100µF sur VCC capteur |
| Timeout systématique | Capteur non alimenté | Vérifier GPIO EN et STMPS2141STR |
| Valeurs erratiques | Interférences UART | Vérifier que les TX des capteurs éteints sont en haute impédance |

## 📄 Licence

Ce composant fait partie du framework **iobewi-idf-components** sous licence MIT.

## 🔗 Ressources

- **Datasheet A02YYUW** : [DFRobot SEN0311](https://wiki.dfrobot.com/A02YYUW_Waterproof_Ultrasonic_Sensor_SKU_SEN0311)
- **STMPS2141STR** : [STMicroelectronics](https://www.st.com/en/power-management/stmps2141.html)
- **ESP-IDF UART** : [Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/uart.html)
