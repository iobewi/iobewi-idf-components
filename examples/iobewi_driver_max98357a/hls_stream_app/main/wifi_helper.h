#ifndef WIFI_HELPER_H
#define WIFI_HELPER_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bits des événements WiFi
 */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/**
 * @brief Initialise le sous-système WiFi
 *
 * @return ESP_OK en cas de succès
 */
esp_err_t wifi_helper_init(void);

/**
 * @brief Démarre la connexion WiFi en mode station
 *
 * @param ssid SSID du réseau WiFi
 * @param password Mot de passe du réseau WiFi
 * @param max_retry Nombre maximum de tentatives de connexion
 * @return ESP_OK en cas de succès
 */
esp_err_t wifi_helper_connect(const char *ssid, const char *password, int max_retry);

/**
 * @brief Récupère l'event group WiFi
 *
 * @return EventGroupHandle_t Event group pour surveiller l'état WiFi
 */
EventGroupHandle_t wifi_helper_get_event_group(void);

/**
 * @brief Vérifie si le WiFi est connecté
 *
 * @return true si connecté, false sinon
 */
bool wifi_helper_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_HELPER_H
