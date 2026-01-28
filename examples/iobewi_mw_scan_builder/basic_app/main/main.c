#include <stdio.h>
#include "esp_log.h"
#include "mw_scan_builder/mw_scan_builder.h"

static const char *TAG = "basic_app";

void app_main(void)
{
    ESP_LOGI(TAG, "Démarrage basic_app mw_scan_builder");

    mw_scan_builder_config_t config = {
        .angle_min = 0.0f,
        .angle_inc = 0.1f,
        .bins = 8,
        .range_min = 0.02f,
        .range_max = 2.0f,
        .scan_time = 0.1f,
        .time_increment = 0.0125f,
        .frame_id = "base_link",
    };

    mw_scan_builder_t *builder = NULL;
    esp_err_t err = mw_scan_builder_new(&config, &builder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Échec création builder: %d", err);
        return;
    }

    ESP_LOGI(TAG, "Builder créé avec succès");

    mw_scan_builder_del(builder);
    ESP_LOGI(TAG, "Builder détruit");
}
