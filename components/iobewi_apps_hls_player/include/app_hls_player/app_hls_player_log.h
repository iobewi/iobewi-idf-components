#ifndef APP_HLS_PLAYER_LOG_H
#define APP_HLS_PLAYER_LOG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HLS_LOG_MODE_PROD = 0,
    HLS_LOG_MODE_RUN,
    HLS_LOG_MODE_DIAG_LIGHT,
    HLS_LOG_MODE_DIAG_HEAVY,
} hls_log_mode_t;

hls_log_mode_t hls_log_get_mode(void);
bool hls_log_mode_at_least(hls_log_mode_t mode);

// IMPORTANT:
// - key MUST be a stable pointer (string literal or static const char[])
// - lookup uses pointer identity, not string content comparison
// - passing temporary/dynamic strings will create ineffective throttling entries

bool hls_log_throttle_time(const char *key, int64_t min_interval_ms);
bool hls_log_throttle_every_n(const char *key, uint32_t n);
bool hls_log_throttle_burst(const char *key, uint32_t burst_count, int64_t window_ms);

#ifdef __cplusplus
}
#endif

#endif
