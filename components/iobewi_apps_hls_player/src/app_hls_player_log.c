#include "app_hls_player/app_hls_player_log.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <string.h>

#ifndef CONFIG_HLS_LOG_THROTTLE_MS
#define CONFIG_HLS_LOG_THROTTLE_MS 5000
#endif
#ifndef CONFIG_HLS_LOG_BURST_COUNT
#define CONFIG_HLS_LOG_BURST_COUNT 3
#endif
#ifndef CONFIG_HLS_LOG_BURST_WINDOW_MS
#define CONFIG_HLS_LOG_BURST_WINDOW_MS 10000
#endif

#define HLS_LOG_KEY_MAX 48

typedef struct {
    const char *key;
    int64_t last_emit_us;
    uint32_t count;
    uint32_t burst_count;
    int64_t burst_window_start_us;
} hls_log_slot_t;

static hls_log_slot_t s_slots[HLS_LOG_KEY_MAX];
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static hls_log_slot_t *hls_log_get_slot(const char *key)
{
    hls_log_slot_t *free_slot = NULL;
    for (int i = 0; i < HLS_LOG_KEY_MAX; i++) {
        if (s_slots[i].key == key) {
            return &s_slots[i];
        }
        if (!s_slots[i].key && !free_slot) {
            free_slot = &s_slots[i];
        }
    }
    if (free_slot) {
        free_slot->key = key;
    }
    return free_slot;
}

hls_log_mode_t hls_log_get_mode(void)
{
#if CONFIG_HLS_LOG_MODE_DIAG_HEAVY
    return HLS_LOG_MODE_DIAG_HEAVY;
#elif CONFIG_HLS_LOG_MODE_DIAG_LIGHT
    return HLS_LOG_MODE_DIAG_LIGHT;
#elif CONFIG_HLS_LOG_MODE_RUN
    return HLS_LOG_MODE_RUN;
#else
    return HLS_LOG_MODE_PROD;
#endif
}

bool hls_log_mode_at_least(hls_log_mode_t mode)
{
    return hls_log_get_mode() >= mode;
}

bool hls_log_throttle_time(const char *key, int64_t min_interval_ms)
{
    if (!key) {
        return false;
    }

    const int64_t now = esp_timer_get_time();
    bool allowed = false;

    portENTER_CRITICAL(&s_mux);
    hls_log_slot_t *slot = hls_log_get_slot(key);
    if (slot) {
        const int64_t min_interval_us = min_interval_ms * 1000;
        if (slot->last_emit_us == 0 || (now - slot->last_emit_us) >= min_interval_us) {
            slot->last_emit_us = now;
            allowed = true;
        }
    }
    portEXIT_CRITICAL(&s_mux);
    return allowed;
}

bool hls_log_throttle_every_n(const char *key, uint32_t n)
{
    if (!key || n == 0) {
        return false;
    }

    bool allowed = false;
    portENTER_CRITICAL(&s_mux);
    hls_log_slot_t *slot = hls_log_get_slot(key);
    if (slot) {
        slot->count++;
        allowed = (slot->count % n) == 0;
    }
    portEXIT_CRITICAL(&s_mux);
    return allowed;
}

bool hls_log_throttle_burst(const char *key, uint32_t burst_count, int64_t window_ms)
{
    if (!key || burst_count == 0 || window_ms <= 0) {
        return false;
    }

    const int64_t now = esp_timer_get_time();
    const int64_t window_us = window_ms * 1000;
    bool allowed = false;

    portENTER_CRITICAL(&s_mux);
    hls_log_slot_t *slot = hls_log_get_slot(key);
    if (slot) {
        if (slot->burst_window_start_us == 0 || (now - slot->burst_window_start_us) >= window_us) {
            slot->burst_window_start_us = now;
            slot->burst_count = 0;
        }
        if (slot->burst_count < burst_count) {
            slot->burst_count++;
            allowed = true;
        }
    }
    portEXIT_CRITICAL(&s_mux);
    return allowed;
}
