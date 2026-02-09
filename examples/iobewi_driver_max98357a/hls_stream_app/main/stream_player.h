#ifndef STREAM_PLAYER_H
#define STREAM_PLAYER_H

#include "esp_err.h"
#include "drv_max98357a/drv_max98357a.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration du stream player
 */
typedef struct {
    const char *stream_url;           /**< URL du stream M3U8/HLS */
    drv_max98357a_t *driver;          /**< Driver MAX98357A initialisé */
    size_t buffer_size;               /**< Taille du buffer ring (bytes) */
} stream_player_config_t;

/**
 * @brief Initialise et démarre le stream player
 *
 * Cette fonction crée les tasks de téléchargement et de lecture audio.
 * Le player gère automatiquement :
 * - Téléchargement et parsing de la playlist M3U8
 * - Téléchargement des segments audio
 * - Décodage AAC vers PCM
 * - Lecture via le driver MAX98357A
 *
 * @param config Configuration du player
 * @return ESP_OK en cas de succès
 */
esp_err_t stream_player_start(const stream_player_config_t *config);

/**
 * @brief Arrête le stream player
 *
 * @return ESP_OK en cas de succès
 */
esp_err_t stream_player_stop(void);

/**
 * @brief Récupère les statistiques du player
 *
 * @param bytes_downloaded Pointeur pour recevoir le nombre total de bytes téléchargés
 * @param buffer_fill Pointeur pour recevoir le niveau de remplissage du buffer (0-100%)
 * @return ESP_OK en cas de succès
 */
esp_err_t stream_player_get_stats(size_t *bytes_downloaded, int *buffer_fill);

#ifdef __cplusplus
}
#endif

#endif // STREAM_PLAYER_H
