#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "drv_fan_tach/drv_fan_tach.h"

static const char *TAG = "TACH";

void app_main(void)
{
    // ⚠️ Adapter à ton câblage
    static const drv_fan_tach_channel_cfg_t channels[] = {
        {
            .gpio_num = 4,
            .counter_high_limit = 30000,
            .counter_low_limit = -30000,
            .pullup_enable = true,
        },
    };

    drv_fan_tach_config_t cfg;
    drv_fan_tach_config_init(&cfg);
    cfg.channels = channels;
    cfg.channel_count = 1;

    drv_fan_tach_t *tach = NULL;
    ESP_ERROR_CHECK(drv_fan_tach_new(&cfg, &tach));

    while (1) {
        drv_fan_tach_sample_t s;
        ESP_ERROR_CHECK(drv_fan_tach_read(tach, 0, 1000, &s));
        ESP_LOGI(TAG, "ch=0 pulses=%ld period=%lums valid=%d",
                 (long)s.pulse_count, (unsigned long)s.period_ms, (int)s.valid);
    }
}
