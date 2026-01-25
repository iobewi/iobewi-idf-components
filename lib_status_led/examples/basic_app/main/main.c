#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lib_status_led/lib_status_led.h"

static const char *TAG = "basic_app";

/**
 * @brief Exemple basique d'utilisation du middleware lib_status_led
 *
 * Démontre:
 * - Initialisation du middleware
 * - Changement d'états applicatifs
 * - Mapping automatique état → couleur
 * - Nettoyage
 */
void app_main(void)
{
    ESP_LOGI(TAG, "lib_status_led basic example");

    // Configuration pour LED status sur GPIO 38
    lib_status_led_config_t cfg = {
        .gpio = GPIO_NUM_38,
        .brightness = 100,
    };

    // Création du middleware
    lib_status_led_t *status = NULL;
    esp_err_t ret = lib_status_led_new(&cfg, &status);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create status LED middleware: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Status LED middleware created on GPIO %d", cfg.gpio);

    // Simulation d'un cycle de vie applicatif typique
    ESP_LOGI(TAG, "=== Application lifecycle simulation ===");

    // 1. Démarrage - attente initialisation
    ESP_LOGI(TAG, "State: WAITING (application starting...)");
    lib_status_led_set_state(status, LIB_STATUS_LED_WAITING);  // Bleu
    vTaskDelay(pdMS_TO_TICKS(3000));

    // 2. Connexion réussie
    ESP_LOGI(TAG, "State: CONNECTED (connected to server)");
    lib_status_led_set_state(status, LIB_STATUS_LED_CONNECTED);  // Vert
    vTaskDelay(pdMS_TO_TICKS(5000));

    // 3. Erreur temporaire
    ESP_LOGI(TAG, "State: ERROR (connection lost!)");
    lib_status_led_set_state(status, LIB_STATUS_LED_ERROR);  // Rouge
    vTaskDelay(pdMS_TO_TICKS(2000));

    // 4. Reconnexion
    ESP_LOGI(TAG, "State: WAITING (reconnecting...)");
    lib_status_led_set_state(status, LIB_STATUS_LED_WAITING);  // Bleu
    vTaskDelay(pdMS_TO_TICKS(2000));

    // 5. Retour à la normale
    ESP_LOGI(TAG, "State: CONNECTED (reconnected successfully)");
    lib_status_led_set_state(status, LIB_STATUS_LED_CONNECTED);  // Vert
    vTaskDelay(pdMS_TO_TICKS(3000));

    // 6. Arrêt propre
    ESP_LOGI(TAG, "State: OFF (shutting down)");
    lib_status_led_set_state(status, LIB_STATUS_LED_OFF);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Cycle rapide pour démo
    ESP_LOGI(TAG, "=== Fast state cycle demo ===");
    lib_status_led_state_t states[] = {
        LIB_STATUS_LED_WAITING,
        LIB_STATUS_LED_CONNECTED,
        LIB_STATUS_LED_ERROR,
        LIB_STATUS_LED_OFF,
    };

    for (int cycle = 0; cycle < 3; cycle++) {
        ESP_LOGI(TAG, "Cycle %d/3", cycle + 1);
        for (int i = 0; i < 4; i++) {
            lib_status_led_set_state(status, states[i]);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    // Nettoyage
    ESP_LOGI(TAG, "Cleaning up and exiting");
    lib_status_led_del(status);

    ESP_LOGI(TAG, "Example finished");
}
