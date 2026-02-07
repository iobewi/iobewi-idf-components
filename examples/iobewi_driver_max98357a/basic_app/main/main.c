#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "drv_max98357a/drv_max98357a.h"

static const char *TAG = "basic_app";

/**
 * @brief Génère un buffer de silence (zéros)
 */
static void generate_silence(int16_t *buffer, size_t sample_count)
{
    memset(buffer, 0, sample_count * sizeof(int16_t));
}

/**
 * @brief Exemple basique d'utilisation du driver MAX98357A
 *
 * Démontre:
 * - Initialisation du driver avec I2S
 * - Contrôle enable/disable
 * - Écriture de données audio (silence)
 * - Nettoyage
 */
void app_main(void)
{
    ESP_LOGI(TAG, "MAX98357A basic example");
    ESP_LOGI(TAG, "This example demonstrates basic driver functionality");

    // Configuration du MAX98357A
    // Note: Adapter les GPIO selon votre matériel
    drv_max98357a_config_t cfg = {
        .bclk_gpio = GPIO_NUM_5,      // I2S BCLK
        .ws_gpio = GPIO_NUM_6,        // I2S LRCLK/WS
        .dout_gpio = GPIO_NUM_7,      // I2S DOUT (vers DIN du MAX98357A)
        .sd_mode_gpio = GPIO_NUM_8,   // Shutdown control
        .sample_rate = 16000,          // 16 kHz
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .slot_mode = I2S_SLOT_MODE_MONO,
        .gain = DRV_MAX98357A_GAIN_9DB,
        .dma_buf_count = 6,
        .dma_buf_len = 512,
    };

    ESP_LOGI(TAG, "Configuration:");
    ESP_LOGI(TAG, "  BCLK:      GPIO %d", cfg.bclk_gpio);
    ESP_LOGI(TAG, "  WS/LRCLK:  GPIO %d", cfg.ws_gpio);
    ESP_LOGI(TAG, "  DOUT:      GPIO %d", cfg.dout_gpio);
    ESP_LOGI(TAG, "  SD_MODE:   GPIO %d", cfg.sd_mode_gpio);
    ESP_LOGI(TAG, "  Sample rate: %lu Hz", cfg.sample_rate);
    ESP_LOGI(TAG, "  Bits: %d", cfg.bits_per_sample);

    // Création du driver
    drv_max98357a_t *amp = NULL;
    esp_err_t ret = drv_max98357a_new(&cfg, &amp);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create MAX98357A driver: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "MAX98357A driver created successfully");

    // Le driver est enabled par défaut
    ESP_LOGI(TAG, "Amplifier is now enabled");

    // Préparer un buffer de silence
    const size_t sample_count = 512;
    int16_t *audio_buffer = malloc(sample_count * sizeof(int16_t));
    if (audio_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        drv_max98357a_del(amp);
        return;
    }

    generate_silence(audio_buffer, sample_count);

    // Écrire du silence pendant 2 secondes
    ESP_LOGI(TAG, "Playing silence for 2 seconds...");
    size_t bytes_written;
    const int iterations = (cfg.sample_rate * 2) / sample_count; // 2 secondes

    for (int i = 0; i < iterations; i++) {
        ret = drv_max98357a_write(amp, audio_buffer,
                                  sample_count * sizeof(int16_t),
                                  &bytes_written, 1000);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write audio data: %s", esp_err_to_name(ret));
            break;
        }
    }

    ESP_LOGI(TAG, "Silence playback finished");

    // Démonstration du contrôle enable/disable
    ESP_LOGI(TAG, "Testing SD_MODE control...");

    ESP_LOGI(TAG, "Disabling amplifier (low power mode)");
    ret = drv_max98357a_disable(amp);
    if (ret == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "Re-enabling amplifier");
        ret = drv_max98357a_enable(amp);
        if (ret == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Nettoyage
    ESP_LOGI(TAG, "Cleaning up...");
    free(audio_buffer);
    drv_max98357a_del(amp);

    ESP_LOGI(TAG, "Example finished");
    ESP_LOGI(TAG, "Note: For actual audio, see audio_tone_app example");
}
