/**
 * @file test_app_hls_player.c
 * @brief Tests unitaires pour le player HLS
 */

#include "unity.h"
#include "app_hls_player/app_hls_player.h"
#include <string.h>

// Mock callback pour les tests
static esp_err_t mock_write_callback(void *user_ctx, const void *data, size_t size,
                                      size_t *bytes_written, uint32_t timeout_ms)
{
    (void)user_ctx;
    (void)data;
    (void)timeout_ms;

    if (bytes_written) {
        *bytes_written = size;
    }
    return ESP_OK;
}

// ============================================================================
// Tests de app_hls_player_config_init
// ============================================================================

TEST_CASE("app_hls_player_config_init rejects NULL", "[app_hls_player]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, app_hls_player_config_init(NULL));
}

TEST_CASE("app_hls_player_config_init sets defaults", "[app_hls_player]")
{
    app_hls_player_config_t config;
    memset(&config, 0xFF, sizeof(config));  // Remplir avec des 1

    TEST_ASSERT_EQUAL(ESP_OK, app_hls_player_config_init(&config));

    TEST_ASSERT_EQUAL(100 * 1024, config.buffer_size);  // 100 KB
    TEST_ASSERT_NULL(config.stream_url);
    TEST_ASSERT_NULL(config.write_cb);
    TEST_ASSERT_NULL(config.write_ctx);
}

// ============================================================================
// Tests de app_hls_player_new
// ============================================================================

TEST_CASE("app_hls_player_new rejects NULL config", "[app_hls_player]")
{
    app_hls_player_t *handle = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, app_hls_player_new(NULL, &handle));
}

TEST_CASE("app_hls_player_new rejects NULL out", "[app_hls_player]")
{
    app_hls_player_config_t config;
    app_hls_player_config_init(&config);
    config.stream_url = "https://example.com/stream.m3u8";
    config.write_cb = mock_write_callback;

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, app_hls_player_new(&config, NULL));
}

TEST_CASE("app_hls_player_new rejects NULL stream_url", "[app_hls_player]")
{
    app_hls_player_config_t config;
    app_hls_player_config_init(&config);
    config.write_cb = mock_write_callback;

    app_hls_player_t *handle = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, app_hls_player_new(&config, &handle));
}

TEST_CASE("app_hls_player_new rejects NULL write_cb", "[app_hls_player]")
{
    app_hls_player_config_t config;
    app_hls_player_config_init(&config);
    config.stream_url = "https://example.com/stream.m3u8";

    app_hls_player_t *handle = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, app_hls_player_new(&config, &handle));
}

TEST_CASE("app_hls_player_new creates handle successfully", "[app_hls_player]")
{
    app_hls_player_config_t config;
    app_hls_player_config_init(&config);
    config.stream_url = "https://example.com/stream.m3u8";
    config.write_cb = mock_write_callback;
    config.write_ctx = (void *)0x12345678;  // Mock context

    app_hls_player_t *handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, app_hls_player_new(&config, &handle));
    TEST_ASSERT_NOT_NULL(handle);

    // Cleanup
    app_hls_player_del(handle);
}

// ============================================================================
// Tests de app_hls_player_start
// ============================================================================

TEST_CASE("app_hls_player_start rejects NULL handle", "[app_hls_player]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, app_hls_player_start(NULL));
}

// ============================================================================
// Tests de app_hls_player_stop
// ============================================================================

TEST_CASE("app_hls_player_stop rejects NULL handle", "[app_hls_player]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, app_hls_player_stop(NULL));
}

TEST_CASE("app_hls_player_stop is idempotent", "[app_hls_player]")
{
    app_hls_player_config_t config;
    app_hls_player_config_init(&config);
    config.stream_url = "https://example.com/stream.m3u8";
    config.write_cb = mock_write_callback;

    app_hls_player_t *handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, app_hls_player_new(&config, &handle));

    // Arrêter sans avoir démarré (devrait réussir)
    TEST_ASSERT_EQUAL(ESP_OK, app_hls_player_stop(handle));

    // Arrêter à nouveau (devrait réussir aussi)
    TEST_ASSERT_EQUAL(ESP_OK, app_hls_player_stop(handle));

    app_hls_player_del(handle);
}

// ============================================================================
// Tests de app_hls_player_get_stats
// ============================================================================

TEST_CASE("app_hls_player_get_stats rejects NULL handle", "[app_hls_player]")
{
    app_hls_player_stats_t stats;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, app_hls_player_get_stats(NULL, &stats));
}

TEST_CASE("app_hls_player_get_stats rejects NULL stats", "[app_hls_player]")
{
    app_hls_player_config_t config;
    app_hls_player_config_init(&config);
    config.stream_url = "https://example.com/stream.m3u8";
    config.write_cb = mock_write_callback;

    app_hls_player_t *handle = NULL;
    app_hls_player_new(&config, &handle);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, app_hls_player_get_stats(handle, NULL));

    app_hls_player_del(handle);
}

TEST_CASE("app_hls_player_get_stats returns initial values", "[app_hls_player]")
{
    app_hls_player_config_t config;
    app_hls_player_config_init(&config);
    config.stream_url = "https://example.com/stream.m3u8";
    config.write_cb = mock_write_callback;

    app_hls_player_t *handle = NULL;
    app_hls_player_new(&config, &handle);

    app_hls_player_stats_t stats;
    TEST_ASSERT_EQUAL(ESP_OK, app_hls_player_get_stats(handle, &stats));

    TEST_ASSERT_EQUAL(0, stats.bytes_downloaded);
    TEST_ASSERT_FALSE(stats.is_playing);
    TEST_ASSERT_EQUAL(0, stats.buffer_fill_percent);

    app_hls_player_del(handle);
}

// ============================================================================
// Tests de app_hls_player_del
// ============================================================================

TEST_CASE("app_hls_player_del rejects NULL handle", "[app_hls_player]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, app_hls_player_del(NULL));
}

TEST_CASE("app_hls_player_del cleans up properly", "[app_hls_player]")
{
    app_hls_player_config_t config;
    app_hls_player_config_init(&config);
    config.stream_url = "https://example.com/stream.m3u8";
    config.write_cb = mock_write_callback;

    app_hls_player_t *handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, app_hls_player_new(&config, &handle));
    TEST_ASSERT_NOT_NULL(handle);

    TEST_ASSERT_EQUAL(ESP_OK, app_hls_player_del(handle));
}

// ============================================================================
// Tests de lifecycle complet
// ============================================================================

TEST_CASE("app_hls_player full lifecycle without start", "[app_hls_player]")
{
    app_hls_player_config_t config;
    app_hls_player_config_init(&config);
    config.stream_url = "https://example.com/stream.m3u8";
    config.write_cb = mock_write_callback;
    config.buffer_size = 50 * 1024;  // 50 KB

    app_hls_player_t *handle = NULL;

    // Create
    TEST_ASSERT_EQUAL(ESP_OK, app_hls_player_new(&config, &handle));
    TEST_ASSERT_NOT_NULL(handle);

    // Get stats (should be all zeros)
    app_hls_player_stats_t stats;
    TEST_ASSERT_EQUAL(ESP_OK, app_hls_player_get_stats(handle, &stats));
    TEST_ASSERT_EQUAL(0, stats.bytes_downloaded);
    TEST_ASSERT_FALSE(stats.is_playing);

    // Delete
    TEST_ASSERT_EQUAL(ESP_OK, app_hls_player_del(handle));
}

TEST_CASE("app_hls_player config preserves user context", "[app_hls_player]")
{
    int user_data = 42;

    app_hls_player_config_t config;
    app_hls_player_config_init(&config);
    config.stream_url = "https://example.com/stream.m3u8";
    config.write_cb = mock_write_callback;
    config.write_ctx = &user_data;

    app_hls_player_t *handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, app_hls_player_new(&config, &handle));

    // Le contexte devrait être préservé (mais on ne peut pas le vérifier directement
    // sans exposer la structure interne)

    app_hls_player_del(handle);
}
