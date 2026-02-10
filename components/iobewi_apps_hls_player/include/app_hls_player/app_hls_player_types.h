/**
 * @file app_hls_player_types.h
 * @brief Types publics pour le player HLS
 */

#ifndef APP_HLS_PLAYER_TYPES_H
#define APP_HLS_PLAYER_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handle opaque du player HLS
 */
typedef struct app_hls_player_s app_hls_player_t;

/**
 * @brief Callback pour l'écriture des données audio décodées
 *
 * Ce callback permet d'abstraire la sortie audio. L'utilisateur peut ainsi :
 * - Écrire vers un driver I2S (ex: MAX98357A, PCM5102, etc.)
 * - Écrire vers un fichier pour enregistrement
 * - Envoyer vers un buffer partagé pour traitement
 *
 * @param[in]  user_ctx      Contexte utilisateur fourni lors de la configuration
 * @param[in]  data          Données PCM stéréo 16-bit (little-endian)
 * @param[in]  size          Taille des données en bytes
 * @param[out] bytes_written Nombre de bytes effectivement écrits
 * @param[in]  timeout_ms    Timeout en millisecondes
 *
 * @return
 *     - ESP_OK : Écriture réussie
 *     - ESP_ERR_TIMEOUT : Timeout dépassé
 *     - ESP_ERR_INVALID_ARG : Paramètres invalides
 *     - Autre : Erreur spécifique au driver
 */
typedef esp_err_t (*app_hls_player_write_cb_t)(
    void *user_ctx,
    const void *data,
    size_t size,
    size_t *bytes_written,
    uint32_t timeout_ms
);

/**
 * @brief Configuration du player HLS
 */
typedef struct {
    const char *stream_url;                 /**< URL de la playlist M3U8/HLS (master ou media) */
    size_t buffer_size;                     /**< Taille du ring buffer interne (bytes), recommandé: 100 KB */
    app_hls_player_write_cb_t write_cb;     /**< Callback d'écriture audio (obligatoire) */
    void *write_ctx;                        /**< Contexte utilisateur passé au callback (ex: handle driver) */
} app_hls_player_config_t;

/**
 * @brief Statistiques du player HLS
 */
typedef struct {
    size_t bytes_downloaded;    /**< Nombre total de bytes téléchargés depuis le démarrage */
    int buffer_fill_percent;    /**< Niveau de remplissage du buffer (0-100%) */
    bool is_playing;            /**< true si le player est actif */
} app_hls_player_stats_t;

#ifdef __cplusplus
}
#endif

#endif // APP_HLS_PLAYER_TYPES_H
