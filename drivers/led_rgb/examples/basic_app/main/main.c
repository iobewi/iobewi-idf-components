#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "drv_led_rgb/drv_led_rgb.h"

static const char *TAG = "basic_app";

/**
 * @brief Exemple basique d'utilisation du driver drv_led_rgb
 *
 * Démontre:
 * - Initialisation du driver
 * - Changement de couleur (rouge, vert, bleu)
 * - Contrôle de luminosité
 * - Nettoyage
 */
void app_main(void)
{
    ESP_LOGI(TAG, "drv_led_rgb basic example");

    // Configuration pour une LED WS2812 sur GPIO 38
    drv_led_rgb_config_t cfg = {
        .gpio = GPIO_NUM_38,
        .max_leds = 1,
        .led_type = DRV_LED_RGB_TYPE_WS2812,
        .default_brightness = 100,
    };

    // Création du driver
    drv_led_rgb_t *led = NULL;
    esp_err_t ret = drv_led_rgb_new(&cfg, &led);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED driver: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "LED driver created on GPIO %d", cfg.gpio);

    // Cycle de couleurs: Rouge -> Vert -> Bleu -> Jaune -> Cyan -> Magenta
    drv_led_rgb_color_t colors[] = {
        {.r = 255, .g = 0,   .b = 0},    // Rouge
        {.r = 0,   .g = 255, .b = 0},    // Vert
        {.r = 0,   .g = 0,   .b = 255},  // Bleu
        {.r = 255, .g = 255, .b = 0},    // Jaune
        {.r = 0,   .g = 255, .b = 255},  // Cyan
        {.r = 255, .g = 0,   .b = 255},  // Magenta
    };

    const char *color_names[] = {
        "Red", "Green", "Blue", "Yellow", "Cyan", "Magenta"
    };

    // Boucle de démonstration
    for (int cycle = 0; cycle < 3; cycle++) {
        ESP_LOGI(TAG, "Cycle %d/3", cycle + 1);

        for (int i = 0; i < 6; i++) {
            ESP_LOGI(TAG, "Setting color: %s (R=%d, G=%d, B=%d)",
                     color_names[i],
                     colors[i].r,
                     colors[i].g,
                     colors[i].b);

            ret = drv_led_rgb_set_color(led, 0, &colors[i]);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set color: %s", esp_err_to_name(ret));
                continue;
            }

            ret = drv_led_rgb_refresh(led);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to refresh LED: %s", esp_err_to_name(ret));
                continue;
            }

            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Démonstration contrôle luminosité
    ESP_LOGI(TAG, "Brightness control demo (white color)");
    drv_led_rgb_color_t white = {.r = 255, .g = 255, .b = 255};
    drv_led_rgb_set_color(led, 0, &white);

    uint8_t brightness_levels[] = {255, 200, 150, 100, 50, 25, 10, 0};
    for (int i = 0; i < 8; i++) {
        ESP_LOGI(TAG, "Brightness: %d/255", brightness_levels[i]);
        drv_led_rgb_set_brightness(led, brightness_levels[i]);
        drv_led_rgb_refresh(led);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // Nettoyage
    ESP_LOGI(TAG, "Clearing LED and cleaning up");
    drv_led_rgb_clear(led);
    drv_led_rgb_refresh(led);
    drv_led_rgb_del(led);

    ESP_LOGI(TAG, "Example finished");
}
