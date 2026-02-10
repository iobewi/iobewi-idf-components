/**
 * @file app_hls_player.h
 * @brief Player HLS (HTTP Live Streaming) pour ESP-IDF
 *
 * Ce composant fournit une orchestration complète pour le streaming HLS :
 * - Téléchargement et parsing de playlists M3U8 (master et media)
 * - Téléchargement des segments audio
 * - Décodage AAC/MPEG-TS vers PCM
 * - Buffering intelligent avec resynchronisation automatique
 * - Abstraction de la sortie audio via callback
 *
 * Architecture :
 * - 2 tâches concurrentes (fetch et play)
 * - Ring buffer partagé avec sémaphore de contrôle
 * - Gestion automatique de la synchronisation et des erreurs
 *
 * Limitations actuelles :
 * - Codec supporté : AAC uniquement (pas MP3)
 * - Container supporté : MPEG-TS uniquement
 * - Chiffrement : Non supporté (pas AES-128)
 * - Master playlists : Première variante uniquement
 */

#ifndef APP_HLS_PLAYER_H
#define APP_HLS_PLAYER_H

#include "app_hls_player_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise une configuration avec valeurs par défaut
 *
 * Valeurs par défaut :
 * - buffer_size = 100 * 1024 (100 KB)
 * - write_cb = NULL (doit être défini par l'utilisateur)
 * - write_ctx = NULL
 *
 * @param[out] config Configuration à initialiser
 *
 * @return
 *     - ESP_OK : Succès
 *     - ESP_ERR_INVALID_ARG : config est NULL
 */
esp_err_t app_hls_player_config_init(app_hls_player_config_t *config);

/**
 * @brief Crée une nouvelle instance du player HLS
 *
 * Cette fonction alloue les ressources nécessaires (handle, ring buffer, sémaphore)
 * mais ne démarre PAS le streaming. Utilisez app_hls_player_start() pour démarrer.
 *
 * @param[in]  config Configuration du player
 * @param[out] out    Pointeur pour recevoir le handle (à libérer avec app_hls_player_del)
 *
 * @return
 *     - ESP_OK : Succès
 *     - ESP_ERR_INVALID_ARG : Paramètres invalides (config NULL, stream_url NULL, write_cb NULL, out NULL)
 *     - ESP_ERR_NO_MEM : Échec d'allocation mémoire
 */
esp_err_t app_hls_player_new(const app_hls_player_config_t *config, app_hls_player_t **out);

/**
 * @brief Démarre le streaming HLS
 *
 * Cette fonction crée les tâches de téléchargement et de lecture audio.
 * Le player télécharge automatiquement la playlist et commence le streaming.
 *
 * @param[in] handle Handle du player (créé avec app_hls_player_new)
 *
 * @return
 *     - ESP_OK : Succès
 *     - ESP_ERR_INVALID_ARG : handle NULL
 *     - ESP_ERR_INVALID_STATE : Le player est déjà démarré
 *     - ESP_FAIL : Échec de création des tâches
 */
esp_err_t app_hls_player_start(app_hls_player_t *handle);

/**
 * @brief Arrête le streaming HLS
 *
 * Cette fonction stoppe les tâches de téléchargement et de lecture.
 * Le ring buffer est vidé mais les ressources du handle sont préservées.
 * Le player peut être redémarré avec app_hls_player_start().
 *
 * Note : Cette fonction attend jusqu'à 2 secondes pour la terminaison des tâches.
 *
 * @param[in] handle Handle du player
 *
 * @return
 *     - ESP_OK : Succès
 *     - ESP_ERR_INVALID_ARG : handle NULL
 */
esp_err_t app_hls_player_stop(app_hls_player_t *handle);

/**
 * @brief Récupère les statistiques du player
 *
 * @param[in]  handle Handle du player
 * @param[out] stats  Structure pour recevoir les statistiques
 *
 * @return
 *     - ESP_OK : Succès
 *     - ESP_ERR_INVALID_ARG : handle ou stats NULL
 */
esp_err_t app_hls_player_get_stats(app_hls_player_t *handle, app_hls_player_stats_t *stats);

/**
 * @brief Détruit le player et libère toutes les ressources
 *
 * Cette fonction appelle automatiquement app_hls_player_stop() si nécessaire,
 * puis libère le handle et toutes les ressources allouées.
 *
 * @param[in] handle Handle du player à détruire
 *
 * @return
 *     - ESP_OK : Succès
 *     - ESP_ERR_INVALID_ARG : handle NULL
 */
esp_err_t app_hls_player_del(app_hls_player_t *handle);

#ifdef __cplusplus
}
#endif

#endif // APP_HLS_PLAYER_H
