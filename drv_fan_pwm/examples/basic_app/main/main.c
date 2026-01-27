#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "drv_fan_pwm/drv_fan_pwm.h"

static const char *TAG = "basic_app";

//  Change ça selon ton board / pinout
#define FAN_PWM_GPIO     (4)
#define FAN_PWM_FREQ_HZ  (25000)

static void fan_demo_task(void *arg)
{
    drv_fan_pwm_t *fan = (drv_fan_pwm_t *)arg;

    while (1) {
        // Rampe simple
        const float steps[] = {0.0f, 30.0f, 60.0f, 100.0f, 0.0f};

        for (size_t i = 0; i < sizeof(steps)/sizeof(steps[0]); i++) {
            ESP_ERROR_CHECK(drv_fan_pwm_set_duty_pct(fan, 0, steps[i]));

            drv_fan_pwm_channel_state_t st;
            ESP_ERROR_CHECK(drv_fan_pwm_get_state(fan, 0, &st));

            ESP_LOGI(TAG, "set duty=%.1f%% -> raw=%u enabled=%d",
                     (double)steps[i], (unsigned)st.duty_raw, (int)st.enabled);

            vTaskDelay(pdMS_TO_TICKS(1500));
        }

        // Test disable/enable
        ESP_LOGI(TAG, "disable channel for 2s");
        ESP_ERROR_CHECK(drv_fan_pwm_enable(fan, 0, false));
        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGI(TAG, "enable channel, restore previous duty for 2s");
        ESP_ERROR_CHECK(drv_fan_pwm_enable(fan, 0, true));
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void app_main(void)
{
    drv_fan_pwm_config_t cfg;
    ESP_ERROR_CHECK(drv_fan_pwm_config_init(&cfg));

    const drv_fan_pwm_channel_cfg_t channels[] = {
        {
            .gpio_num = FAN_PWM_GPIO,
            .channel = LEDC_CHANNEL_0,
            .hpoint = 0,
        }
    };

    cfg.channels = channels;
    cfg.channel_count = 1;

    cfg.freq_hz = FAN_PWM_FREQ_HZ;
    cfg.duty_resolution = LEDC_TIMER_10_BIT;
    cfg.timer = LEDC_TIMER_0;
    cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    cfg.clk_cfg = LEDC_AUTO_CLK;

    drv_fan_pwm_t *fan = NULL;
    ESP_ERROR_CHECK(drv_fan_pwm_new(&cfg, &fan));

    xTaskCreate(fan_demo_task, "fan_demo", 4096, fan, 5, NULL);
}
