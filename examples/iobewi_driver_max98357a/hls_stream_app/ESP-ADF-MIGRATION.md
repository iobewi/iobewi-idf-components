# Migration ESP-ADF pour ESP-IDF 6.x

> **Statut**: ⏸️ En attente - ESP-ADF v3 master pas encore totalement compatible ESP-IDF 6.1
> **Date**: 2026-02-08
> **ESP-IDF**: 6.1.0
> **ESP-ADF**: v3 master (branche master, commit d108801d)

## Contexte

Tentative de migration de l'application HLS Stream vers ESP-ADF pour éviter de "réinventer la roue". ESP-ADF v3 master est annoncé compatible ESP-IDF 6.x mais nécessite de nombreux patchs.

## Architecture cible CDC-compliant

### Composants créés
- **`iobewi_libs_esp_adf_wrapper`**: Wrapper CDC pour ESP-ADF (lib_*)
  - Encapsule audio_pipeline, http_stream, i2s_stream, aac_decoder
  - API générique pour ampli I2S (pas spécifique MAX98357A)
  - Contrôle GPIO gain ampli (amp_ctrl_gpio)

- **`iobewi_apps_hls_player`**: Orchestration HLS player (app_*)
  - Délègue streaming/décodage au wrapper ESP-ADF
  - Business logic : configuration, démarrage, arrêt

### Types génériques
```c
typedef enum {
    I2S_AMP_GAIN_3DB = 0,   // pin contrôle = LOW
    I2S_AMP_GAIN_9DB = 2,   // pin contrôle = HIGH
    // autres gains non implémentés
} i2s_amp_gain_t;

typedef struct {
    const char *stream_url;
    int amp_ctrl_gpio;      // GPIO contrôle gain (-1 = non utilisé)
    i2s_amp_gain_t gain;    // Gain matériel
    float volume;           // Volume logiciel 0.0-1.0
    // ... autres params I2S
} lib_esp_adf_wrapper_config_t;
```

## Installation ESP-ADF

```bash
cd /workspaces
git clone --recursive https://github.com/espressif/esp-adf.git
cd esp-adf
git checkout master  # Important ! Pas release/v2.x
```

**Configuration CMakeLists.txt:**
```cmake
set(ADF_PATH "/workspaces/esp-adf")
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../../components/iobewi_libs_esp_adf_wrapper"
    "${CMAKE_CURRENT_LIST_DIR}/../../../components/iobewi_apps_hls_player"
    "${ADF_PATH}/components"
)
```

## Patchs appliqués

### 1. Migration JSON (ESP-IDF 6.x)

**Problème**: Composant `json` n'existe plus, remplacé par `espressif/cjson`

**Solution:**
```yaml
# main/idf_component.yml
dependencies:
  espressif/cjson:
    version: "^1.7.19"
```

**Patchs composants ESP-ADF:**
```bash
# esp-sr/CMakeLists.txt ligne 16
json → espressif__cjson

# clouds/CMakeLists.txt ligne 18
json → espressif__cjson

# esp_coze/CMakeLists.txt ligne 7
json → espressif__cjson
```

### 2. Compatibilité FreeRTOS

**Problème**: Types FreeRTOS legacy (xSemaphoreHandle, etc.)

**Solution sdkconfig.defaults:**
```ini
CONFIG_FREERTOS_ENABLE_BACKWARD_COMPATIBILITY=y
```

### 3. Warnings dépréciation

**Problème**: APIs legacy I2C/Touch traitées comme erreurs

**Solution sdkconfig.defaults:**
```ini
CONFIG_I2C_SUPPRESS_DEPRECATE_WARN=y
CONFIG_TOUCH_SUPPRESS_DEPRECATE_WARN=y
```

### 4. Dépendances drivers ESP-IDF 6.x

**Problème**: esp_peripherals manque dépendances explicites

**Patch esp_peripherals/CMakeLists.txt:**
```cmake
# Ajouter après ligne 77
if (IDF_VERSION_MAJOR GREATER_EQUAL 6)
list(APPEND COMPONENT_REQUIRES esp_driver_ledc esp_driver_i2c)
endif()
```

### 5. Erreurs compilation stricte

**audio_sonic.c ligne 134, 139:**
```c
// Avant
calloc(sizeof(short), BUF_SIZE)
// Après
calloc(BUF_SIZE, sizeof(short))
```

**audio_forge.c ligne 988, 1013:**
```c
// Avant
abs((float_value) * 100)
// Après
fabsf((float_value) * 100)
```

**led_indicator.h ligne 28:**
```c
// Ajouter après #include "display_service.h"
#include "hal/gpio_types.h"
```

## Problèmes restants (non résolus)

1. **Multiples erreurs compilation** dans composants ESP-ADF non critiques
2. **audio_stream/audio_recorder**: Dépendances esp-sr désactivées (fonctionnalités AEC/TTS perdues)
3. **display_service**: Erreurs gpio_num_t dans plusieurs fichiers
4. **audio_hal**: Erreurs drivers legacy dans zl38063
5. Probablement **10+ patchs supplémentaires** nécessaires

## Recommandation future

### Quand migrer ?

✅ **Migrer quand:**
- ESP-ADF publie version officielle "ESP-IDF 6.x compatible"
- Release notes mentionnent explicitement ESP-IDF 6.1+
- Exemples ESP-ADF compilent sans patch sur ESP-IDF 6.1

❌ **Ne PAS migrer tant que:**
- ESP-ADF master nécessite patchs multiples
- Fonctionnalités critiques (http_stream, i2s_stream, aac_decoder) non testées

### Checklist migration

1. [ ] Vérifier compatibilité officielle ESP-ADF
2. [ ] Tester compilation exemples ESP-ADF sur ESP-IDF 6.1
3. [ ] Appliquer patchs JSON (voir section Patchs appliqués)
4. [ ] Configurer FreeRTOS backward compatibility
5. [ ] Tester wrapper `iobewi_libs_esp_adf_wrapper`
6. [ ] Valider streaming HLS + décodage AAC
7. [ ] Tester contrôle gain ampli I2S
8. [ ] Mesurer performance vs implémentation actuelle

## Références

- **Guide migration ESP-IDF 6.0**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/migration-guides/release-6.x/6.0/protocols.html
- **ESP-ADF repo**: https://github.com/espressif/esp-adf
- **Composant cJSON**: https://components.espressif.com/components/espressif/cjson
- **Session travail**: `/root/.claude/projects/-workspaces-iobewi-idf-components-examples-iobewi-driver-max98357a-hls-stream-app/017ce896-5782-41b5-845f-8194ce71ac20.jsonl`

## Fichiers de référence

Architecture CDC créée (à réutiliser lors migration):
```
components/
├── iobewi_libs_esp_adf_wrapper/
│   ├── include/iobewi_libs_esp_adf_wrapper/
│   │   ├── iobewi_libs_esp_adf_wrapper.h
│   │   └── iobewi_libs_esp_adf_wrapper_types.h
│   ├── src/iobewi_libs_esp_adf_wrapper.c
│   └── CMakeLists.txt
│
└── iobewi_apps_hls_player/
    ├── include/iobewi_apps_hls_player/
    │   ├── iobewi_apps_hls_player.h
    │   └── iobewi_apps_hls_player_types.h
    ├── src/iobewi_apps_hls_player.c
    └── CMakeLists.txt
```

**Note**: Ces composants existent déjà mais utilisent actuellement une implémentation custom sans ESP-ADF. Lors de la migration, remplacer l'implémentation interne du wrapper par des appels ESP-ADF.

---

**Conclusion**: ESP-ADF v3 master (2026-02-08) nécessite trop de patchs pour ESP-IDF 6.1. Conserver implémentation actuelle fonctionnelle et surveiller releases ESP-ADF officielles compatibles ESP-IDF 6.x.
