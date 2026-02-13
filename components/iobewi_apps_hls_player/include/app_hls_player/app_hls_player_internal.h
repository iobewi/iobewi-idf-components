/**
 * @file app_hls_player_internal.h
 * @brief Définitions internes partagées entre les modules du player HLS
 *
 * Ce header contient la structure opaque complète et les constantes partagées.
 * Il est inclus uniquement par les modules internes (.c dans src/).
 * L'API publique reste dans include/app_hls_player/app_hls_player.h
 */

#ifndef APP_HLS_PLAYER_INTERNAL_H
#define APP_HLS_PLAYER_INTERNAL_H

#include "app_hls_player/app_hls_player.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

// Task Notification bits (remplace volatile flags pour thread-safety stricte SMP)
#define NOTIF_STOP   (1u << 0)  /**< Signal d'arrêt */
#define NOTIF_RESET  (1u << 1)  /**< Signal reset décodeur (DISCONTINUITY) */
#define NOTIF_RESYNC (1u << 2)  /**< Signal resync soft (drop-old, pas de reset décodeur) */

// Tailles de buffers depuis Kconfig (avec fallback si non défini)
#ifndef CONFIG_APP_HLS_PLAYER_RING_BUFFER_SIZE
    #define CONFIG_APP_HLS_PLAYER_RING_BUFFER_SIZE 128
#endif
#ifndef CONFIG_APP_HLS_PLAYER_REM_BUFFER_SIZE
    #define CONFIG_APP_HLS_PLAYER_REM_BUFFER_SIZE 8
#endif
#ifndef CONFIG_APP_HLS_PLAYER_DEC_BUFFER_SIZE
    #define CONFIG_APP_HLS_PLAYER_DEC_BUFFER_SIZE 16
#endif
#ifndef CONFIG_APP_HLS_PLAYER_DROP_OLD_ON_FULL
    #define CONFIG_APP_HLS_PLAYER_DROP_OLD_ON_FULL 1
#endif
#ifndef CONFIG_APP_HLS_PLAYER_GATHER_BUFFER_SIZE
    #define CONFIG_APP_HLS_PLAYER_GATHER_BUFFER_SIZE 8
#endif

/**
 * @brief Structure interne du player HLS (définition complète)
 *
 * Cette structure est opaque pour l'API publique.
 * Les modules internes y accèdent directement.
 */
struct app_hls_player_s {
    char *stream_url;                       /**< URL du stream (copie allouée) */
    RingbufHandle_t ring_buffer;            /**< Buffer circulaire pour données encodées */
    size_t buffer_size;                     /**< Taille du buffer */
    TaskHandle_t fetch_task;                /**< Tâche de téléchargement */
    TaskHandle_t play_task;                 /**< Tâche de lecture/décodage */
    size_t bytes_downloaded;                /**< Statistiques: bytes téléchargés */
    SemaphoreHandle_t download_semaphore;   /**< Sémaphore pour contrôle téléchargement */
    SemaphoreHandle_t stats_mutex;          /**< Mutex pour bytes_downloaded */
    SemaphoreHandle_t fetch_done;           /**< Sémaphore signalant fin fetch_task */
    SemaphoreHandle_t play_done;            /**< Sémaphore signalant fin play_task */
    volatile bool is_downloading;           /**< Flag téléchargement en cours (lu par stats, OK volatile) */
    app_hls_player_write_cb_t write_cb;     /**< Callback d'écriture audio */
    void *write_ctx;                        /**< Contexte utilisateur pour callback */
    int target_duration;                    /**< Target duration pour timeout refresh */
    uint32_t drop_count;                    /**< Compteur pertes ring buffer (rate limit log) */
    uint32_t drop_old_count;                /**< Compteur drop-old (stratégie live-ness) */
    uint32_t drop_recover_count;            /**< Compteur recoveries après drop-old réussi */
    uint32_t bad_item_size_count;           /**< Compteur corruption ringbuffer (drop_sz != 188) */
    uint32_t resync_notif_guard;            /**< Rate-limit NOTIF_RESYNC fallback */
    uint8_t ts_carry[188];                  /**< Carry buffer pour alignement TS 188-byte */
    size_t ts_carry_len;                    /**< Nombre de bytes dans ts_carry */
    esp_http_client_handle_t ts_http_client;/**< Client HTTP persistant pour segments TS */
    char ts_host[128];                      /**< Host cache du client TS */
    int ts_port;                            /**< Port cache du client TS */
    esp_http_client_transport_t ts_transport; /**< Transport cache (HTTP/HTTPS) */
    bool ts_client_ready;                   /**< Indique si le cache TS est valide */
    struct {
        int64_t total_ms;                   /**< Temps total hls_http_download_segment() */
        int64_t open_ms;                    /**< Temps esp_http_client_open() (connect/TLS) */
        int64_t headers_ms;                 /**< Temps fetch_headers/status */
        int64_t body_read_ms;               /**< Temps lecture corps HTTP */
        int64_t rb_wait_ms;                 /**< Temps cumulé en attente ringbuffer */
        int64_t max_read_block_ms;          /**< Plus long blocage d'un read() */
        size_t body_bytes;                  /**< Bytes lus sur le corps HTTP */
        int read_calls;                     /**< Nombre d'appels esp_http_client_read() */
        bool reuse;                         /**< true si handle HTTP TS réutilisé */
        bool retried;                       /**< true si retry reconnect effectué */
    } last_seg_metrics;                     /**< Instrumentation segment (dernier téléchargement) */
};

/**
 * @brief Helper inline pour envoyer NOTIF_RESYNC rate-limited (% 10)
 *
 * Utilisé par les modules HTTP et ORCHESTRATION pour limiter les notifications
 * de resynchronisation soft vers la task audio.
 *
 * @param[in] handle Handle du player
 */
static inline void hls_rate_limited_resync(app_hls_player_t *handle)
{
    if (!handle || !handle->play_task) return;

    uint32_t g = handle->resync_notif_guard + 1;
    handle->resync_notif_guard = g;
    if ((g % 10) == 0) {
        xTaskNotify(handle->play_task, NOTIF_RESYNC, eSetBits);
    }
}

/**
 * @brief Attente interruptible avec check notifications NOTIF_STOP
 *
 * xTaskNotifyWait travaille toujours sur la tâche courante, pas besoin de handle.
 * Retourne false si NOTIF_STOP reçue, true si délai complet.
 *
 * @param[in] ms Durée d'attente en millisecondes
 * @return true si délai complet, false si NOTIF_STOP reçue
 */
static inline bool hls_interruptible_delay_ms(uint32_t ms)
{
    const uint32_t step = 100;
    while (ms > 0) {
        uint32_t this_step = (ms > step) ? step : ms;

        // Check notification sans bloquer (xTaskNotifyWait sur tâche courante)
        uint32_t notif = 0;
        if (xTaskNotifyWait(0, NOTIF_STOP, &notif, pdMS_TO_TICKS(this_step)) == pdTRUE) {
            if (notif & NOTIF_STOP) {
                return false;  // Stop demandé
            }
        }

        ms -= this_step;
    }
    return true;  // Délai complet
}

/**
 * @brief Check immédiat si NOTIF_STOP est présente (sans bloquer)
 *
 * ⚠️ WARNING: Cette fonction **CONSOMME** le bit NOTIF_STOP (xTaskNotifyWait l'efface).
 * À n'utiliser que si l'appelant sort IMMÉDIATEMENT de la task (break/goto task_exit).
 *
 * Si appelé dans une fonction utilitaire ou suivi d'un continue, les checks suivants
 * ne verront plus STOP → risque de tâche zombie.
 *
 * @return true si STOP demandé (et consommé), false sinon
 */
static inline bool hls_should_stop_now(void)
{
    uint32_t notif = 0;
    return (xTaskNotifyWait(0, NOTIF_STOP, &notif, 0) == pdTRUE) && (notif & NOTIF_STOP);
}

#ifdef __cplusplus
}
#endif

#endif // APP_HLS_PLAYER_INTERNAL_H
