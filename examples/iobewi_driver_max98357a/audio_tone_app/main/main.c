#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "drv_max98357a/drv_max98357a.h"

static const char *TAG = "audio_tone_app";

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * @brief Génère un signal sinusoïdal (tone) dans un buffer PCM 16-bit
 *
 * @param buffer Buffer de sortie (int16_t)
 * @param sample_count Nombre d'échantillons à générer
 * @param sample_rate Taux d'échantillonnage (Hz)
 * @param frequency Fréquence du tone (Hz)
 * @param amplitude Amplitude (0.0 à 1.0), 1.0 = pleine échelle
 */
static void generate_sine_wave(int16_t *buffer, size_t sample_count,
                                uint32_t sample_rate, uint32_t frequency,
                                float amplitude)
{
    for (size_t i = 0; i < sample_count; i++) {
        float t = (float)i / (float)sample_rate;
        float sample = amplitude * sinf(2.0f * M_PI * frequency * t);
        buffer[i] = (int16_t)(sample * 16000.0f); // Scale to 16-bit range
    }
}

/**
 * @brief Joue un tone à une fréquence donnée pendant une durée spécifiée
 */
static esp_err_t play_tone(drv_max98357a_t *amp, uint32_t sample_rate,
                           uint32_t frequency, float amplitude,
                           uint32_t duration_ms)
{
    const size_t buffer_samples = 512;
    int16_t *buffer = malloc(buffer_samples * sizeof(int16_t));
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        return ESP_ERR_NO_MEM;
    }

    // Générer une période de la sinusoïde
    generate_sine_wave(buffer, buffer_samples, sample_rate, frequency, amplitude);

    // Calculer le nombre d'itérations pour la durée souhaitée
    uint32_t total_samples = (sample_rate * duration_ms) / 1000;
    uint32_t iterations = total_samples / buffer_samples;

    ESP_LOGI(TAG, "Playing %lu Hz tone for %lu ms (amplitude: %.2f)",
             frequency, duration_ms, amplitude);

    size_t bytes_written;
    esp_err_t ret = ESP_OK;

    for (uint32_t i = 0; i < iterations; i++) {
        ret = drv_max98357a_write(amp, buffer,
                                  buffer_samples * sizeof(int16_t),
                                  &bytes_written, 1000);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write audio data: %s", esp_err_to_name(ret));
            break;
        }
    }

    free(buffer);
    return ret;
}

/**
 * @brief Exemple avancé : génération de tons audio avec le MAX98357A
 *
 * Démontre:
 * - Génération de signaux sinusoïdaux
 * - Lecture de différentes fréquences (notes de musique)
 * - Contrôle de l'amplitude (volume)
 * - Séquence musicale simple
 */
void app_main(void)
{
    ESP_LOGI(TAG, "MAX98357A audio tone example");
    ESP_LOGI(TAG, "This example plays various audio tones");

    // Configuration du MAX98357A
    // Note: Adapter les GPIO selon votre matériel
    drv_max98357a_config_t cfg = {
        .bclk_gpio = GPIO_NUM_14,      // I2S BCLK
        .ws_gpio = GPIO_NUM_15,        // I2S LRCLK/WS
        .dout_gpio = GPIO_NUM_16,      // I2S DOUT (vers DIN du MAX98357A)
        .sd_mode_gpio = GPIO_NUM_17,   // Shutdown control
        .sample_rate = 16000,          // 16 kHz
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .slot_mode = I2S_SLOT_MODE_MONO,
        .gain = DRV_MAX98357A_GAIN_9DB,
        .dma_buf_count = 6,
        .dma_buf_len = 512,
    };

    ESP_LOGI(TAG, "Creating MAX98357A driver...");
    drv_max98357a_t *amp = NULL;
    esp_err_t ret = drv_max98357a_new(&cfg, &amp);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create driver: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Driver created successfully (sample rate: %lu Hz)", cfg.sample_rate);

    // Jouer différentes fréquences (notes de musique)
    // Do (261 Hz), Ré (294 Hz), Mi (330 Hz), Fa (349 Hz), Sol (392 Hz), La (440 Hz), Si (494 Hz)
    struct {
        uint32_t freq;
        const char *name;
    } notes[] = {
        {261, "Do (C4)"},
        {294, "Ré (D4)"},
        {330, "Mi (E4)"},
        {349, "Fa (F4)"},
        {392, "Sol (G4)"},
        {440, "La (A4)"},
        {494, "Si (B4)"},
        {523, "Do (C5)"},
    };

    const float amplitude = 0.5f; // 50% de volume pour éviter la saturation
    const uint32_t note_duration = 500; // 500 ms par note

    ESP_LOGI(TAG, "Playing musical scale (amplitude: %.2f)...", amplitude);

    // Jouer la gamme montante
    for (int i = 0; i < 8; i++) {
        ESP_LOGI(TAG, "Note %d/8: %s (%lu Hz)", i + 1, notes[i].name, notes[i].freq);
        ret = play_tone(amp, cfg.sample_rate, notes[i].freq, amplitude, note_duration);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to play tone");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Courte pause entre les notes
    }

    ESP_LOGI(TAG, "Scale finished");

    // Jouer un signal de test à 1 kHz (référence audio classique)
    ESP_LOGI(TAG, "Playing 1 kHz test tone for 2 seconds...");
    play_tone(amp, cfg.sample_rate, 1000, 0.3f, 2000);

    // Démonstration de différentes amplitudes
    ESP_LOGI(TAG, "Testing different amplitudes (440 Hz)...");
    float amplitudes[] = {0.1f, 0.3f, 0.5f, 0.7f, 0.9f};
    for (int i = 0; i < 5; i++) {
        ESP_LOGI(TAG, "Amplitude: %.2f", amplitudes[i]);
        play_tone(amp, cfg.sample_rate, 440, amplitudes[i], 500);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    // Jouer une petite mélodie : Do-Mi-Sol-Do (accord parfait majeur)
    ESP_LOGI(TAG, "Playing simple melody (C major arpeggio)...");
    int melody[] = {0, 2, 4, 7}; // Index dans le tableau notes
    for (int i = 0; i < 4; i++) {
        play_tone(amp, cfg.sample_rate, notes[melody[i]].freq, 0.5f, 600);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGI(TAG, "Audio playback finished");

    // Nettoyage
    ESP_LOGI(TAG, "Cleaning up...");
    drv_max98357a_del(amp);

    ESP_LOGI(TAG, "Example finished");
}
