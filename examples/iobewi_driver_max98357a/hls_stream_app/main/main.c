#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_heap_caps.h"

#include "drv_max98357a/drv_max98357a.h"
#include "wifi_helper.h"
#include "stream_player.h"
#include "decoder/impl/esp_aac_dec.h"
#include "simple_dec/impl/esp_ts_dec.h"

static const char *TAG = "main";

// Configuration depuis menuconfig
#define WIFI_SSID           CONFIG_WIFI_SSID
#define WIFI_PASSWORD       CONFIG_WIFI_PASSWORD
#define WIFI_MAX_RETRY      CONFIG_WIFI_MAXIMUM_RETRY
#define HLS_STREAM_URL      CONFIG_HLS_STREAM_URL
#define STREAM_BUFFER_SIZE  (CONFIG_STREAM_BUFFER_SIZE * 1024)

#define I2S_BCLK_PIN        CONFIG_I2S_BCLK_PIN
#define I2S_WS_PIN          CONFIG_I2S_WS_PIN
#define I2S_DOUT_PIN        CONFIG_I2S_DOUT_PIN
#define I2S_SD_MODE_PIN     CONFIG_I2S_SD_MODE_PIN
#define AUDIO_SAMPLE_RATE   CONFIG_AUDIO_SAMPLE_RATE

static drv_max98357a_t *s_driver = NULL;

/**
 * @brief Initialise la mémoire NVS
 */
static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Effacement de la partition NVS");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

/**
 * @brief Initialise le driver MAX98357A
 */
static esp_err_t init_audio_driver(void)
{
    drv_max98357a_config_t driver_config = {
        .bclk_gpio = I2S_BCLK_PIN,
        .ws_gpio = I2S_WS_PIN,
        .dout_gpio = I2S_DOUT_PIN,
        .sd_mode_gpio = (I2S_SD_MODE_PIN >= 0) ? I2S_SD_MODE_PIN : GPIO_NUM_NC,
        .sample_rate = AUDIO_SAMPLE_RATE,
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .slot_mode = I2S_SLOT_MODE_STEREO,
        .gain = DRV_MAX98357A_GAIN_3DB,  // Réduit de 9dB à 3dB pour volume plus bas
        .dma_buf_count = 8,      // Augmenté de 6 à 8 buffers
        .dma_buf_len = 512,      // Réduit à 512 (max 1023 mais 512 est optimal)
    };

    ESP_LOGI(TAG, "Initialisation du driver MAX98357A");
    ESP_LOGI(TAG, "  Sample rate: %d Hz", AUDIO_SAMPLE_RATE);
    ESP_LOGI(TAG, "  BCLK: GPIO%d, WS: GPIO%d, DOUT: GPIO%d",
             I2S_BCLK_PIN, I2S_WS_PIN, I2S_DOUT_PIN);
    if (I2S_SD_MODE_PIN >= 0) {
        ESP_LOGI(TAG, "  SD_MODE: GPIO%d", I2S_SD_MODE_PIN);
    }

    esp_err_t ret = drv_max98357a_new(&driver_config, &s_driver);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Échec de création du driver: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = drv_max98357a_enable(s_driver);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Échec d'activation du driver: %s", esp_err_to_name(ret));
        drv_max98357a_del(s_driver);
        s_driver = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "Driver MAX98357A initialisé avec succès");
    return ESP_OK;
}

/**
 * @brief Affiche les informations système périodiquement
 */
static void monitor_task(void *pvParameters)
{
    while (1) {
        // Afficher l'état de la mémoire
        size_t free_heap = esp_get_free_heap_size();
        size_t min_free_heap = esp_get_minimum_free_heap_size();

        ESP_LOGI(TAG, "=== État du système ===");
        ESP_LOGI(TAG, "Heap libre: %u bytes (min: %u bytes)", free_heap, min_free_heap);

        // Statistiques du player
        size_t bytes_downloaded = 0;
        int buffer_fill = 0;
        stream_player_get_stats(&bytes_downloaded, &buffer_fill);
        ESP_LOGI(TAG, "Stream: %u KB téléchargés, buffer: %d%%",
                 bytes_downloaded / 1024, buffer_fill);

        // WiFi
        ESP_LOGI(TAG, "WiFi: %s", wifi_helper_is_connected() ? "connecté" : "déconnecté");

        vTaskDelay(pdMS_TO_TICKS(10000)); // Toutes les 10 secondes
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "  MAX98357A HLS Stream Player");
    ESP_LOGI(TAG, "===========================================");

    // 1. Initialiser NVS
    ESP_LOGI(TAG, "Initialisation NVS...");
    ESP_ERROR_CHECK(init_nvs());

    // 2. Initialiser et connecter WiFi
    ESP_LOGI(TAG, "Initialisation WiFi...");
    ESP_ERROR_CHECK(wifi_helper_init());

    ESP_LOGI(TAG, "Connexion au WiFi SSID: %s", WIFI_SSID);
    esp_err_t ret = wifi_helper_connect(WIFI_SSID, WIFI_PASSWORD, WIFI_MAX_RETRY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Échec de connexion WiFi");
        return;
    }

    ESP_LOGI(TAG, "WiFi connecté avec succès");

    // 3. Enregistrer les décodeurs AAC et TS
    ESP_LOGI(TAG, "Enregistrement du décodeur AAC...");
    esp_audio_err_t audio_ret = esp_aac_dec_register();
    if (audio_ret != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "Échec d'enregistrement du décodeur AAC: %d", audio_ret);
        return;
    }

    ESP_LOGI(TAG, "Enregistrement du décodeur TS...");
    audio_ret = esp_ts_dec_register();
    if (audio_ret != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "Échec d'enregistrement du décodeur TS: %d", audio_ret);
        return;
    }

    // 4. Initialiser le driver audio
    ESP_LOGI(TAG, "Initialisation du driver audio...");
    ret = init_audio_driver();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Échec d'initialisation du driver audio");
        return;
    }

    // 5. Démarrer le stream player
    ESP_LOGI(TAG, "Démarrage du stream player...");
    ESP_LOGI(TAG, "URL: %s", HLS_STREAM_URL);

    // Buffer augmenté à 100KB grâce à la PSRAM
    // Assez grand pour contenir ~2 segments complets sans risque de perte de données
    const size_t buffer_size = 100 * 1024;
    ESP_LOGI(TAG, "Buffer: %d KB (PSRAM activée)", buffer_size / 1024);

    stream_player_config_t player_config = {
        .stream_url = HLS_STREAM_URL,
        .driver = s_driver,
        .buffer_size = buffer_size,
    };

    ret = stream_player_start(&player_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Échec de démarrage du stream player");
        drv_max98357a_del(s_driver);
        return;
    }

    ESP_LOGI(TAG, "Stream player démarré avec succès");
    ESP_LOGI(TAG, "===========================================");

    // 6. Lancer la task de monitoring
    xTaskCreate(monitor_task, "monitor", 3072, NULL, 2, NULL);

    // Loop principale (reste en vie)
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
