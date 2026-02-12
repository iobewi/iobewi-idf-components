/**
 * @file main.c
 * @brief Test harness minimal pour app_hls_player (mock sink write_cb)
 *
 * Permet de tester sans consumer I2S :
 * - Download + ringbuffer + decode loop
 * - Stabilité heap
 * - STOP/START cycles
 * - Stack HWM avec CONFIG_APP_HLS_PLAYER_STACK_DIAG=y
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "app_hls_player/app_hls_player.h"

static const char *TAG = "hls_test";

// Statistiques globales pour le mock sink
static uint64_t g_total_bytes_written = 0;
static uint64_t g_total_frames = 0;
static int64_t g_last_stats_us = 0;

/**
 * @brief Mock write callback - comptabilise les bytes PCM sans jouer
 *
 * Simule un consumer I2S en incrémentant des compteurs.
 * Permet de tester la chaîne complète download → decode → callback.
 */
static esp_err_t mock_sink_write_cb(void *ctx, void *data, size_t size)
{
    (void)ctx;  // Unused
    (void)data; // On ne joue pas réellement les samples

    if (size == 0) {
        return ESP_OK;
    }

    // Comptabiliser bytes PCM
    g_total_bytes_written += size;
    g_total_frames++;

    // Stats toutes les 10s
    int64_t now = esp_timer_get_time();
    if (now - g_last_stats_us > 10 * 1000 * 1000) {
        g_last_stats_us = now;

        // Calculer débit PCM (bytes/min)
        uint32_t elapsed_s = (uint32_t)((now - g_last_stats_us) / 1000000);
        if (elapsed_s == 0) elapsed_s = 10;  // Première itération
        uint64_t bytes_per_min = (g_total_bytes_written * 60) / elapsed_s;

        size_t free_heap = esp_get_free_heap_size();
        size_t min_heap = esp_get_minimum_free_heap_size();

        ESP_LOGI(TAG, "[MOCK SINK] Frames=%llu, Bytes=%llu, Rate=%llu KB/min | Heap: libre=%zu, min=%zu",
                 g_total_frames, g_total_bytes_written, bytes_per_min / 1024,
                 free_heap, min_heap);
    }

    return ESP_OK;
}

/**
 * @brief Test start/stop cycles
 */
static void test_start_stop_cycles(app_hls_player_t *player)
{
    ESP_LOGI(TAG, "=== TEST: Start/Stop Cycles (3x) ===");

    for (int i = 0; i < 3; i++) {
        ESP_LOGI(TAG, "Cycle %d/3: Starting player...", i + 1);
        ESP_ERR_CHECK(app_hls_player_start(player));

        vTaskDelay(pdMS_TO_TICKS(30000));  // Run 30s

        ESP_LOGI(TAG, "Cycle %d/3: Stopping player...", i + 1);
        ESP_ERR_CHECK(app_hls_player_stop(player));

        vTaskDelay(pdMS_TO_TICKS(2000));  // Pause 2s

        app_hls_player_stats_t stats;
        ESP_ERR_CHECK(app_hls_player_get_stats(player, &stats));
        ESP_LOGI(TAG, "Stats after cycle %d: bytes_downloaded=%llu, buffer_fill=%u%%",
                 i + 1, stats.bytes_downloaded, stats.buffer_fill_percent);
    }

    ESP_LOGI(TAG, "=== TEST PASSED: Start/Stop Cycles ===");
}

/**
 * @brief Test long-run (configurable durée)
 */
static void test_long_run(app_hls_player_t *player, uint32_t duration_s)
{
    ESP_LOGI(TAG, "=== TEST: Long-Run (%u seconds) ===", duration_s);

    ESP_ERR_CHECK(app_hls_player_start(player));

    // Stats périodiques toutes les 30s
    for (uint32_t elapsed = 0; elapsed < duration_s; elapsed += 30) {
        vTaskDelay(pdMS_TO_TICKS(30000));

        app_hls_player_stats_t stats;
        ESP_ERR_CHECK(app_hls_player_get_stats(player, &stats));

        size_t free_heap = esp_get_free_heap_size();
        size_t min_heap = esp_get_minimum_free_heap_size();

        ESP_LOGI(TAG, "[%u/%us] Downloaded=%llu bytes, Buffer=%u%% | Heap: libre=%zu, min=%zu",
                 elapsed + 30, duration_s,
                 stats.bytes_downloaded, stats.buffer_fill_percent,
                 free_heap, min_heap);
    }

    ESP_ERR_CHECK(app_hls_player_stop(player));
    ESP_LOGI(TAG, "=== TEST PASSED: Long-Run (%u seconds) ===", duration_s);
}

void app_main(void)
{
    ESP_LOGI(TAG, "HLS Player Test Harness (Mock Sink)");
    ESP_LOGI(TAG, "Build: %s %s", __DATE__, __TIME__);

    // Init NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Init Wi-Fi (protocol_examples_common helper)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    ESP_LOGI(TAG, "Wi-Fi connected");

    // Créer player avec mock sink
    app_hls_player_config_t config;
    ESP_ERROR_CHECK(app_hls_player_config_init(&config));

    // URL France Inter (AAC/TS ~128 kbps, stable)
    config.stream_url = "https://stream.radiofrance.fr/franceinter/franceinter_midfi.m3u8";
    config.write_cb = mock_sink_write_cb;
    config.write_ctx = NULL;
    config.buffer_size = 0;  // Use Kconfig default (128 KB with PSRAM, 64 KB without)

    app_hls_player_t *player = NULL;
    ESP_ERROR_CHECK(app_hls_player_new(&config, &player));

    ESP_LOGI(TAG, "Player created successfully");

    // === TESTS ===

    // Choix du test via menuconfig ou hardcoded
#ifdef CONFIG_HLS_TEST_START_STOP_CYCLES
    test_start_stop_cycles(player);
#elif CONFIG_HLS_TEST_LONG_RUN
    uint32_t duration = CONFIG_HLS_TEST_LONG_RUN_DURATION_S;
    test_long_run(player, duration);
#else
    // Défaut : run simple 60s
    ESP_LOGI(TAG, "=== TEST: Simple Run (60s) ===");
    ESP_ERR_CHECK(app_hls_player_start(player));
    vTaskDelay(pdMS_TO_TICKS(60000));
    ESP_ERR_CHECK(app_hls_player_stop(player));
    ESP_LOGI(TAG, "=== TEST PASSED: Simple Run ===");
#endif

    // Cleanup
    ESP_ERROR_CHECK(app_hls_player_del(player));
    ESP_LOGI(TAG, "Player deleted - test complete");

    // Stats finales mock sink
    ESP_LOGI(TAG, "[FINAL] Mock sink: frames=%llu, bytes=%llu (%.2f MB)",
             g_total_frames, g_total_bytes_written,
             (double)g_total_bytes_written / (1024 * 1024));

    size_t final_heap = esp_get_free_heap_size();
    size_t min_heap = esp_get_minimum_free_heap_size();
    ESP_LOGI(TAG, "[FINAL] Heap: libre=%zu, min=%zu", final_heap, min_heap);
}
