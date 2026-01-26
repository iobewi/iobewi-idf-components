# lib_a02_provider

Provider pour capteurs ultrasoniques **A02YYUW** avec filtrage médian et validation de range

## 📋 Description

Composant bibliothèque qui transforme les lectures brutes du driver `drv_a02yyuw` en échantillons ultrasoniques calibrés et filtrés. Ce composant abstrait la logique de traitement des mesures (médiane, validation) pour simplifier l'intégration dans les applications.

## 🏗️ Taxonomie

**Catégorie** : `lib_*` (Bibliothèque utilitaire)

**Dépendances** :
- `drv_a02yyuw` (driver A02YYUW)
- ESP-IDF standard

**Règles** :
- ✅ Peut dépendre de `drv_*`
- ❌ Aucune dépendance micro-ROS
- ❌ Aucun accès matériel direct
- ❌ Aucune logique métier applicative

## 🔧 Fonctionnalités

- **Acquisition séquentielle** : gère le power gating des capteurs
- **Rejet première mesure** : jette la première trame après power-up (configurable)
- **Filtre médian** : applique une médiane sur N mesures (3 par défaut)
- **Conversion d'unités** : mm → mètres
- **Validation de range** : vérifie que les mesures sont dans [range_min, range_max]
- **Gestion d'erreurs** : marque les samples invalides en cas de timeout ou hors range
- **Logging** : ESP-IDF standard avec TAG `lib_a02_provider`

## 🚀 API

### Types principaux

```c
typedef struct {
    float distance_m;     // Distance en mètres
    bool valid;           // true si la mesure est valide
    uint32_t timestamp;   // Timestamp µs depuis boot
} ultrasonic_sample_t;

typedef struct {
    drv_a02yyuw_t *driver;       // Handle du driver (requis)
    uint8_t sensor_count;         // Nombre de capteurs (4 typique)
    uint8_t median_filter_size;   // Taille du filtre (3 par défaut, impair)
    float range_min_m;            // Distance min valide (0.3m)
    float range_max_m;            // Distance max valide (4.5m)
    drv_a02yyuw_mode_t mode;      // REALTIME ou PROCESSED
    uint32_t read_timeout_ms;     // Timeout lecture (500ms)
    bool discard_first_sample;    // Jeter 1ère mesure (true)
} lib_a02_provider_config_t;
```

### Fonctions

```c
// Initialiser config avec valeurs par défaut
esp_err_t lib_a02_provider_config_init(lib_a02_provider_config_t *config);

// Créer une instance du provider
esp_err_t lib_a02_provider_new(const lib_a02_provider_config_t *config,
                                lib_a02_provider_t **out_handle);

// Lire un snapshot de tous les capteurs
esp_err_t lib_a02_provider_read_snapshot(lib_a02_provider_t *handle,
                                          ultrasonic_sample_t *samples,
                                          size_t count);

// Détruire l'instance
esp_err_t lib_a02_provider_del(lib_a02_provider_t *handle);
```

## 📝 Exemple d'utilisation

```c
#include "drv_a02yyuw/drv_a02yyuw.h"
#include "lib_a02_provider/lib_a02_provider.h"

static const int EN_PINS[] = {14, 10, 7, 4};

void app_main(void)
{
    // 1. Créer le driver A02YYUW
    drv_a02yyuw_config_t drv_config;
    drv_a02yyuw_config_init(&drv_config);
    drv_config.uart_num = UART_NUM_1;
    drv_config.uart_rx_gpio = 18;
    drv_config.gpio_mode = 5;
    drv_config.mode_active_high = true;
    drv_config.gpio_en_list = EN_PINS;
    drv_config.sensor_count = 4;

    drv_a02yyuw_t *driver = NULL;
    ESP_ERROR_CHECK(drv_a02yyuw_new(&drv_config, &driver));

    // 2. Créer le provider
    lib_a02_provider_config_t prov_config;
    lib_a02_provider_config_init(&prov_config);
    prov_config.driver = driver;
    prov_config.sensor_count = 4;
    prov_config.median_filter_size = 3;
    prov_config.range_min_m = 0.3f;
    prov_config.range_max_m = 4.5f;

    lib_a02_provider_t *provider = NULL;
    ESP_ERROR_CHECK(lib_a02_provider_new(&prov_config, &provider));

    // 3. Lire les capteurs en boucle
    ultrasonic_sample_t samples[4];

    while (1) {
        esp_err_t ret = lib_a02_provider_read_snapshot(provider, samples, 4);

        if (ret == ESP_OK) {
            for (int i = 0; i < 4; i++) {
                if (samples[i].valid) {
                    printf("Sensor[%d]: %.2f m\n", i, samples[i].distance_m);
                } else {
                    printf("Sensor[%d]: INVALID\n", i);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }

    // 4. Nettoyage
    lib_a02_provider_del(provider);
    drv_a02yyuw_del(driver);
}
```

## ⚙️ Configuration

### Valeurs par défaut

| Paramètre | Valeur par défaut | Description |
|-----------|-------------------|-------------|
| `sensor_count` | 4 | Nombre de capteurs |
| `median_filter_size` | 3 | Taille du filtre médian (impair) |
| `range_min_m` | 0.3 | Distance minimale valide (m) |
| `range_max_m` | 4.5 | Distance maximale valide (m) |
| `mode` | `DRV_A02YYUW_MODE_PROCESSED` | Mode de lecture |
| `read_timeout_ms` | 500 | Timeout par lecture (ms) |
| `discard_first_sample` | `true` | Jeter 1ère mesure après power-up |

### Filtre médian

Le filtre médian élimine les mesures aberrantes. Recommandations :
- **3 mesures** : bon compromis vitesse/précision (~12ms)
- **5 mesures** : plus robuste mais plus lent (~20ms)
- **7 mesures** : maximum recommandé (~28ms)

⚠️ La taille doit être **impaire** et ≥ 1.

### Validation de range

Les mesures hors de [range_min_m, range_max_m] sont marquées comme **invalides**.

**Plages typiques A02YYUW** :
- Documentée : 0.3m - 4.5m
- Pratique : 0.4m - 4.0m (pour éviter les bords instables)

## 🔧 Gestion mémoire

- **Allocation** : `lib_a02_provider_new()` alloue le handle + buffer médian
- **Ownership** : Le provider ne possède PAS le driver (doit être géré séparément)
- **Libération** : `lib_a02_provider_del()` libère toute la mémoire

## 📖 Notes techniques

### Performances

Pour 4 capteurs avec filtre médian = 3 :
- **Power cycling** : ~32ms par capteur
- **Lecture 3 mesures** : ~12ms par capteur
- **Total par snapshot** : ~176ms (4 capteurs)
- **Fréquence max** : ~5.7 Hz

### Gestion d'erreurs

| Erreur | Comportement |
|--------|--------------|
| Timeout lecture | `sample.valid = false`, log `LOGW` |
| Hors range | `sample.valid = false`, log `LOGW` |
| Tous capteurs échouent | Retourne `ESP_FAIL`, log `LOGE` |
| Au moins 1 valide | Retourne `ESP_OK` |

### Timestamp

Le champ `timestamp` est rempli avec `esp_timer_get_time()` (microsecondes depuis boot). Utile pour la synchronisation avec d'autres capteurs ou pour le timestamping ROS.

## 🧪 Exemples

### basic_app

Exemple simple avec 4 capteurs et affichage console.

```bash
cd examples/basic_app
idf.py build flash monitor
```

## 🐛 Dépannage

| Problème | Cause possible | Solution |
|----------|----------------|----------|
| Tous samples invalides | Driver non initialisé | Vérifier `drv_a02yyuw_new()` |
| Timeouts fréquents | GPIO EN incorrects | Vérifier câblage STMPS2141STR |
| Mesures erratiques | Filtre médian = 1 | Augmenter à 3 ou 5 |
| Samples toujours hors range | range_min/max incorrects | Ajuster selon datasheet (0.3-4.5m) |

## 📄 Licence

Ce composant fait partie du framework **iobewi-idf-components** sous licence MIT.

## 🔗 Voir aussi

- **drv_a02yyuw** : Driver matériel A02YYUW
- **app_scan_ultra** : Application micro-ROS utilisant ce provider
- **mw_scan_builder** : Builder LaserScan pour ROS
