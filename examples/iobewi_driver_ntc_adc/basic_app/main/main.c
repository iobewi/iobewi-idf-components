#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "drv_ntc_adc/drv_ntc_adc.h"

static const char *TAG = "basic_app";

void app_main(void)
{
    // Exemple minimal : adapte les canaux à ton câblage (ADC unit + channels)
    static const drv_ntc_adc_channel_cfg_t channels[] = {
        { .channel = ADC_CHANNEL_8, .atten = ADC_ATTEN_DB_12 }, // ex: ADC1 CH8 (GPIO9 sur certains mappings)
        { .channel = ADC_CHANNEL_6, .atten = ADC_ATTEN_DB_12 }, // ex: ADC1 CH6
    };

    drv_ntc_adc_config_t cfg;
    ESP_ERROR_CHECK(drv_ntc_adc_config_init(&cfg));
    cfg.unit = ADC_UNIT_1;
    cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    cfg.channels = channels;
    cfg.channel_count = sizeof(channels) / sizeof(channels[0]);

    drv_ntc_adc_t *adc = NULL;
    ESP_ERROR_CHECK(drv_ntc_adc_new(&cfg, &adc));

    while (1) {
        for (size_t i = 0; i < cfg.channel_count; i++) {
            drv_ntc_adc_sample_t s = {0};
            esp_err_t ret = drv_ntc_adc_read(adc, i, &s);
            if (ret == ESP_OK && s.valid) {
                ESP_LOGI(TAG, "ch[%u] raw=%" PRIu32, (unsigned)i, s.raw);
            } else {
                ESP_LOGW(TAG, "ch[%u] read err: %s", (unsigned)i, esp_err_to_name(ret));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
