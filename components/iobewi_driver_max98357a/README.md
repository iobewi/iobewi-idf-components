# drv_max98357a - Driver MAX98357A

Driver matériel pur pour l'amplificateur audio numérique I2S MAX98357A.

## Caractéristiques

- Interface I2S standard (BCLK, LRCLK/WS, DIN)
- Support mono/stéréo
- Taux d'échantillonnage configurables (16kHz - 96kHz)
- Bits par échantillon: 16, 24, 32 bits
- Contrôle SD_MODE (shutdown) optionnel
- Gestion DMA pour performances optimales
- API générique (pas de sémantique applicative)
- Conforme CDC iobewi-idf-components (catégorie `drv_*`)

## Spécifications MAX98357A

Le MAX98357A est un amplificateur audio numérique de classe D avec interface I2S:
- Sortie: 3.2W @ 4Ω, 5V, 10% THD
- SNR: 92dB (A-weighted)
- THD+N: 0.015% (1kHz, 1W, 8Ω)
- Gain: Configurable 3dB à 15dB (9dB par défaut)
- Alimentation: 2.5V à 5.5V
- Shutdown: Mode basse consommation via SD_MODE

## API Publique

### Types

```c
typedef enum {
    DRV_MAX98357A_GAIN_3DB = 0,
    DRV_MAX98357A_GAIN_6DB,
    DRV_MAX98357A_GAIN_9DB,    // Défaut
    DRV_MAX98357A_GAIN_12DB,
    DRV_MAX98357A_GAIN_15DB,
} drv_max98357a_gain_t;

typedef struct {
    gpio_num_t bclk_gpio;           // I2S bit clock
    gpio_num_t ws_gpio;             // I2S word select (LRCLK)
    gpio_num_t dout_gpio;           // I2S data out
    gpio_num_t sd_mode_gpio;        // Shutdown control (optionnel)
    uint32_t sample_rate;           // Ex: 16000, 44100, 48000 Hz
    i2s_data_bit_width_t bits_per_sample;
    i2s_slot_mode_t slot_mode;      // Mono/Stéréo
    drv_max98357a_gain_t gain;
    uint32_t dma_buf_count;
    uint32_t dma_buf_len;
} drv_max98357a_config_t;
```

### Fonctions

#### Lifecycle

```c
esp_err_t drv_max98357a_new(const drv_max98357a_config_t *cfg, drv_max98357a_t **out);
esp_err_t drv_max98357a_del(drv_max98357a_t *handle);
```

#### Écriture audio

```c
esp_err_t drv_max98357a_write(drv_max98357a_t *handle, const void *data,
                               size_t size, size_t *bytes_written,
                               uint32_t timeout_ms);
```

#### Contrôle

```c
esp_err_t drv_max98357a_enable(drv_max98357a_t *handle);
esp_err_t drv_max98357a_disable(drv_max98357a_t *handle);
esp_err_t drv_max98357a_get_i2s_handle(drv_max98357a_t *handle,
                                        i2s_chan_handle_t *i2s_handle);
```

## Exemple d'utilisation

```c
#include "drv_max98357a/drv_max98357a.h"
#include <math.h>

// Génération d'un signal sinusoïdal de test (440 Hz)
void generate_sine_wave(int16_t *buffer, size_t sample_count,
                        uint32_t sample_rate, uint32_t frequency) {
    for (size_t i = 0; i < sample_count; i++) {
        float t = (float)i / sample_rate;
        buffer[i] = (int16_t)(sin(2.0f * M_PI * frequency * t) * 16000.0f);
    }
}

void app_main(void) {
    // Configuration du driver
    drv_max98357a_config_t cfg = {
        .bclk_gpio = GPIO_NUM_14,      // I2S BCLK
        .ws_gpio = GPIO_NUM_15,        // I2S LRCLK
        .dout_gpio = GPIO_NUM_16,      // I2S DOUT (vers DIN du MAX98357A)
        .sd_mode_gpio = GPIO_NUM_17,   // Shutdown control
        .sample_rate = 16000,          // 16 kHz
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .slot_mode = I2S_SLOT_MODE_MONO,
        .gain = DRV_MAX98357A_GAIN_9DB,
        .dma_buf_count = 6,
        .dma_buf_len = 512,
    };

    // Création du driver
    drv_max98357a_t *amp = NULL;
    esp_err_t ret = drv_max98357a_new(&cfg, &amp);
    if (ret != ESP_OK) {
        ESP_LOGE("app", "Failed to create MAX98357A driver");
        return;
    }

    // Activer l'amplificateur
    drv_max98357a_enable(amp);

    // Buffer audio (440 Hz tone)
    const size_t sample_count = 1024;
    int16_t *audio_buffer = malloc(sample_count * sizeof(int16_t));
    generate_sine_wave(audio_buffer, sample_count, cfg.sample_rate, 440);

    // Lecture audio en boucle
    size_t bytes_written;
    for (int i = 0; i < 10; i++) {
        ret = drv_max98357a_write(amp, audio_buffer,
                                  sample_count * sizeof(int16_t),
                                  &bytes_written, 1000);
        if (ret != ESP_OK) {
            ESP_LOGE("app", "Failed to write audio data");
        }
    }

    // Nettoyage
    free(audio_buffer);
    drv_max98357a_disable(amp);
    drv_max98357a_del(amp);
}
```

## Configuration matérielle

### Connexions typiques (ESP32-S3)

| MAX98357A | ESP32-S3 | Description |
|-----------|----------|-------------|
| VIN       | 5V/3V3   | Alimentation (2.5V-5.5V) |
| GND       | GND      | Masse |
| DIN       | GPIO_16  | I2S Data In (DOUT ESP32) |
| BCLK      | GPIO_14  | I2S Bit Clock |
| LRCLK     | GPIO_15  | I2S Word Select |
| SD_MODE   | GPIO_17  | Shutdown (HIGH=on, LOW=off) |
| GAIN      | N/C      | Laissé flottant pour gain 9dB |

### Configuration du gain

Le gain est configuré selon l'état de la pin GAIN pendant le power-up:
- **Flottant**: 9dB (configuration la plus courante)
- **GND**: 15dB
- **VDD**: 3dB
- **Contrôle dynamique**: Nécessite circuit externe pour varier durant LRCLK

**Note:** Ce driver utilise le gain par défaut (9dB). Pour un contrôle dynamique du gain, un circuit externe est nécessaire.

## Taux d'échantillonnage supportés

Le MAX98357A supporte les taux standards:
- 8 kHz, 16 kHz, 32 kHz
- 11.025 kHz, 22.05 kHz, 44.1 kHz
- 48 kHz, 96 kHz

**Recommandation:** 16 kHz pour applications vocales, 44.1 kHz ou 48 kHz pour audio musical.

## Formats audio

Le driver supporte:
- **Mono**: Un seul canal audio
- **Stéréo**: Deux canaux (left/right)

Pour le MAX98357A (mono), utiliser `I2S_SLOT_MODE_MONO` et connecter uniquement le canal gauche ou droite.

## Buffers DMA

Les buffers DMA affectent la latence et la stabilité:
- `dma_buf_count`: Nombre de buffers (recommandé: 4-8)
- `dma_buf_len`: Taille en échantillons par buffer (recommandé: 256-1024)

**Latence = dma_buf_count × dma_buf_len / sample_rate**

Exemple: 6 buffers × 512 échantillons / 16000 Hz = 192 ms

## Dépendances

- ESP-IDF 5.0+ (driver I2S v2)
- `driver` (ESP-IDF I2S, GPIO)

## Limites connues

- Pas de contrôle dynamique du gain (nécessite circuit externe)
- Pas de support I2S master clock (MCLK) - non nécessaire pour MAX98357A
- Format I2S uniquement (pas de support PCM/DSP)

## Dépannage

### Pas de son

1. Vérifier que SD_MODE est à HIGH (si utilisé)
2. Vérifier les connexions I2S (BCLK, LRCLK, DIN)
3. Vérifier l'alimentation du MAX98357A (2.5V-5.5V)
4. Vérifier que le format audio correspond (sample_rate, bits_per_sample)

### Son distordu

1. Vérifier le gain (9dB par défaut, réduire si saturation)
2. Réduire le volume du signal PCM (normaliser entre -32768 et +32767 pour 16-bit)
3. Augmenter le voltage d'alimentation (5V recommandé pour puissance max)

### Coupures audio (underruns)

1. Augmenter `dma_buf_count` (ex: 8 au lieu de 6)
2. Augmenter `dma_buf_len` (ex: 1024 au lieu de 512)
3. Réduire la charge CPU dans la tâche d'écriture audio

## Conformité CDC

- ✅ Catégorie `drv_*` (driver pur)
- ✅ Pas de dépendance micro-ROS
- ✅ Pas de logique métier
- ✅ API `esp_err_t` exclusive
- ✅ Handle opaque
- ✅ Structure CDC: `include/drv_max98357a/`, `src/`
- ✅ Allocation documentée (contexte interne via calloc)

## License

MIT
