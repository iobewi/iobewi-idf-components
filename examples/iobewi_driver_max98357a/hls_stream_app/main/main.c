/**
 * @file main.c
 * @brief Exemple de streaming HLS avec MAX98357A
 *
 * Cet exemple démontre l'utilisation de app_hls_player avec le driver MAX98357A
 * pour diffuser un flux audio HLS (HTTP Live Streaming).
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "drv_max98357a/drv_max98357a.h"
#include "app_hls_player/app_hls_player.h"
#include "decoder/impl/esp_aac_dec.h"
#include "simple_dec/impl/esp_ts_dec.h"

static const char *TAG = "hls_stream_app";

// Configuration depuis menuconfig
#define WIFI_SSID           CONFIG_WIFI_SSID
#define WIFI_PASSWORD       CONFIG_WIFI_PASSWORD
#define WIFI_MAX_RETRY      CONFIG_WIFI_MAXIMUM_RETRY
#define HLS_STREAM_URL      CONFIG_HLS_STREAM_URL

#define I2S_BCLK_PIN        CONFIG_I2S_BCLK_PIN
#define I2S_WS_PIN          CONFIG_I2S_WS_PIN
#define I2S_DOUT_PIN        CONFIG_I2S_DOUT_PIN
#define I2S_SD_MODE_PIN     CONFIG_I2S_SD_MODE_PIN
#define AUDIO_SAMPLE_RATE   CONFIG_AUDIO_SAMPLE_RATE

static int s_wifi_retry_num = 0;
static bool s_wifi_connected = false;

/**
 * @brief Callback d'écriture audio pour app_hls_player
 *
 * Ce callback reçoit les données PCM décodées et les envoie au driver MAX98357A
 * Applique une réduction de volume numérique (-6dB, divise par 2)
 */
static esp_err_t audio_write_callback(void *user_ctx, const void *data,
                                       size_t size, size_t *bytes_written,
                                       uint32_t timeout_ms)
{
    drv_max98357a_t *driver = (drv_max98357a_t *)user_ctx;

    // Réduction volume numérique : diviser amplitude par 2 (-6dB)
    int16_t *samples = (int16_t *)data;
    size_t sample_count = size / sizeof(int16_t);
    for (size_t i = 0; i < sample_count; i++) {
        samples[i] = samples[i] / 8;
    }

    return drv_max98357a_write(driver, data, size, bytes_written, timeout_ms);
}

/**
 * @brief Gestionnaire d'événements WiFi
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_wifi_retry_num++;
            ESP_LOGI(TAG, "Tentative de reconnexion WiFi (%d/%d)", s_wifi_retry_num, WIFI_MAX_RETRY);
        } else {
            ESP_LOGE(TAG, "Échec de connexion WiFi après %d tentatives", WIFI_MAX_RETRY);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "WiFi connecté, IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_retry_num = 0;
        s_wifi_connected = true;  // Signal de connexion réussie
    }
}

/**
 * @brief Initialise et connecte le WiFi
 */
static esp_err_t init_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Fix #3: Désactiver WiFi power save pour stabilité streaming
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_LOGI(TAG, "WiFi power save: DISABLED (streaming audio)");

    ESP_LOGI(TAG, "Connexion au WiFi SSID: %s", WIFI_SSID);

    // Attendre la connexion (max 30 secondes)
    int wait_time = 0;
    while (!s_wifi_connected && s_wifi_retry_num < WIFI_MAX_RETRY && wait_time < 30) {
        vTaskDelay(pdMS_TO_TICKS(100));  // Check toutes les 100ms (pas 1s)
        wait_time++;
    }

    if (!s_wifi_connected) {
        ESP_LOGE(TAG, "Timeout connexion WiFi (%d secondes)", wait_time / 10);
        return ESP_FAIL;
    }

    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "  MAX98357A HLS Stream Player");
    ESP_LOGI(TAG, "===========================================");

    // 1. Initialiser NVS
    ESP_LOGI(TAG, "Initialisation NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Effacement de la partition NVS");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Connecter WiFi
    ESP_LOGI(TAG, "Initialisation WiFi...");
    if (init_wifi() != ESP_OK) {
        ESP_LOGE(TAG, "Échec de connexion WiFi");
        return;
    }
    ESP_LOGI(TAG, "WiFi connecté avec succès");

    // 3. Enregistrer les décodeurs AAC et TS
    ESP_LOGI(TAG, "Enregistrement des décodeurs audio...");
    esp_aac_dec_register();
    esp_ts_dec_register();

    // 4. Initialiser le driver audio MAX98357A
    ESP_LOGI(TAG, "Initialisation du driver MAX98357A...");
    drv_max98357a_config_t driver_config = {
        .bclk_gpio = I2S_BCLK_PIN,
        .ws_gpio = I2S_WS_PIN,
        .dout_gpio = I2S_DOUT_PIN,
        .sd_mode_gpio = (I2S_SD_MODE_PIN >= 0) ? I2S_SD_MODE_PIN : GPIO_NUM_NC,
        .sample_rate = AUDIO_SAMPLE_RATE,
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .slot_mode = I2S_SLOT_MODE_STEREO,
        .gain = DRV_MAX98357A_GAIN_3DB,
        .dma_buf_count = 8,
        .dma_buf_len = 512,
    };

    drv_max98357a_t *driver;
    ESP_ERROR_CHECK(drv_max98357a_new(&driver_config, &driver));
    ESP_ERROR_CHECK(drv_max98357a_enable(driver));
    ESP_LOGI(TAG, "Driver MAX98357A initialisé (GPIO: BCLK=%d, WS=%d, DOUT=%d)",
             I2S_BCLK_PIN, I2S_WS_PIN, I2S_DOUT_PIN);

    // 5. Créer et démarrer le player HLS
    ESP_LOGI(TAG, "Démarrage du stream HLS...");
    ESP_LOGI(TAG, "URL: %s", HLS_STREAM_URL);

    app_hls_player_config_t player_cfg;
    app_hls_player_config_init(&player_cfg);
    player_cfg.stream_url = HLS_STREAM_URL;
    // buffer_size = 0 → utilise CONFIG_APP_HLS_PLAYER_RING_BUFFER_SIZE du Kconfig
    player_cfg.write_cb = audio_write_callback;
    player_cfg.write_ctx = driver;  // Passer le driver comme contexte

    app_hls_player_t *player;
    ESP_ERROR_CHECK(app_hls_player_new(&player_cfg, &player));
    ESP_ERROR_CHECK(app_hls_player_start(player));

    ESP_LOGI(TAG, "Stream HLS démarré avec succès");
    ESP_LOGI(TAG, "===========================================");

    // 6. Boucle de monitoring
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));

        app_hls_player_stats_t stats;
        app_hls_player_get_stats(player, &stats);

        size_t free_heap = esp_get_free_heap_size();
        size_t min_heap = esp_get_minimum_free_heap_size();

        ESP_LOGI(TAG, "=== État ===");
        ESP_LOGI(TAG, "Stream: %u KB téléchargés, buffer: %d%%",
                 stats.bytes_downloaded / 1024, stats.buffer_fill_percent);
        ESP_LOGI(TAG, "Heap: %u bytes libre (min: %u bytes)", free_heap, min_heap);
    }
}
