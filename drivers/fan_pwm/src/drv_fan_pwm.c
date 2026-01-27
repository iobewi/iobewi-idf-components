#include "drv_fan_pwm/drv_fan_pwm.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "drv_fan_pwm";

#define DRV_FAN_PWM_CHECK(cond, err, fmt, ...)            \
    do {                                                  \
        if (!(cond)) {                                    \
            ESP_LOGE(TAG, fmt, ##__VA_ARGS__);            \
            return (err);                                 \
        }                                                 \
    } while (0)

struct drv_fan_pwm_s {
    drv_fan_pwm_config_t cfg;                 // copie shallow
    drv_fan_pwm_channel_cfg_t *channels;      // copie deep
    drv_fan_pwm_channel_state_t *states;      // états par canal
    uint32_t max_duty;
    SemaphoreHandle_t lock;
};

static inline void drv_fan_pwm_lock(drv_fan_pwm_t *h)
{
    if (h->lock) {
        (void)xSemaphoreTake(h->lock, portMAX_DELAY);
    }
}

static inline void drv_fan_pwm_unlock(drv_fan_pwm_t *h)
{
    if (h->lock) {
        (void)xSemaphoreGive(h->lock);
    }
}

static bool drv_fan_pwm_gpio_is_valid(int gpio_num)
{
    // gpio_num = -1 (LEDC disable) -> refusé ici pour un ventilateur PWM
    // ESP-IDF fournit GPIO_IS_VALID_OUTPUT_GPIO via driver/gpio.h, mais on évite
    // d’ajouter une dépendance publique. Ici, validation minimale:
    return (gpio_num >= 0);
}

esp_err_t drv_fan_pwm_config_init(drv_fan_pwm_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(config, 0, sizeof(*config));
    config->speed_mode = LEDC_LOW_SPEED_MODE;
    config->timer = LEDC_TIMER_0;
    config->duty_resolution = LEDC_TIMER_10_BIT;
    config->freq_hz = 25000;
    config->clk_cfg = LEDC_AUTO_CLK;
    config->channels = NULL;
    config->channel_count = 0;

    return ESP_OK;
}

esp_err_t drv_fan_pwm_new(const drv_fan_pwm_config_t *config, drv_fan_pwm_t **out)
{
    DRV_FAN_PWM_CHECK(config != NULL, ESP_ERR_INVALID_ARG, "config NULL");
    DRV_FAN_PWM_CHECK(out != NULL, ESP_ERR_INVALID_ARG, "out NULL");
    DRV_FAN_PWM_CHECK(config->channels != NULL, ESP_ERR_INVALID_ARG, "channels NULL");
    DRV_FAN_PWM_CHECK(config->channel_count > 0, ESP_ERR_INVALID_ARG, "channel_count=0");
    DRV_FAN_PWM_CHECK(config->freq_hz > 0, ESP_ERR_INVALID_ARG, "freq_hz=0");

    drv_fan_pwm_t *h = (drv_fan_pwm_t *)calloc(1, sizeof(*h));
    DRV_FAN_PWM_CHECK(h != NULL, ESP_ERR_NO_MEM, "no mem handle");

    h->cfg = *config;

    h->channels = (drv_fan_pwm_channel_cfg_t *)calloc(config->channel_count, sizeof(*h->channels));
    if (h->channels == NULL) {
        free(h);
        return ESP_ERR_NO_MEM;
    }

    h->states = (drv_fan_pwm_channel_state_t *)calloc(config->channel_count, sizeof(*h->states));
    if (h->states == NULL) {
        free(h->channels);
        free(h);
        return ESP_ERR_NO_MEM;
    }

    memcpy(h->channels, config->channels, config->channel_count * sizeof(*h->channels));

    const uint32_t res_bits = (uint32_t)config->duty_resolution;
    DRV_FAN_PWM_CHECK(res_bits < 31, ESP_ERR_INVALID_ARG, "invalid duty_resolution enum=%u", (unsigned)res_bits);
    h->max_duty = (1u << res_bits) - 1u;

    h->lock = xSemaphoreCreateMutex();
    if (h->lock == NULL) {
        free(h->states);
        free(h->channels);
        free(h);
        return ESP_ERR_NO_MEM;
    }

    // Validation minimale GPIO
    for (size_t i = 0; i < config->channel_count; i++) {
        DRV_FAN_PWM_CHECK(drv_fan_pwm_gpio_is_valid(h->channels[i].gpio_num),
                          ESP_ERR_INVALID_ARG,
                          "invalid gpio for channel[%u]=%d",
                          (unsigned)i, h->channels[i].gpio_num);
    }

    // Configure timer (commun)
    ledc_timer_config_t tcfg = {
        .speed_mode = config->speed_mode,
        .duty_resolution = config->duty_resolution,
        .timer_num = config->timer,
        .freq_hz = config->freq_hz,
        .clk_cfg = config->clk_cfg,
    };

    esp_err_t err = ledc_timer_config(&tcfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(err));
        (void)drv_fan_pwm_del(h);
        return err;
    }

    // Configure channels
    for (size_t i = 0; i < config->channel_count; i++) {
        ledc_channel_config_t ccfg = {
            .gpio_num = h->channels[i].gpio_num,
            .speed_mode = config->speed_mode,
            .channel = h->channels[i].channel,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = config->timer,
            .duty = 0,
            .hpoint = h->channels[i].hpoint,
            .flags.output_invert = 0,
        };

        err = ledc_channel_config(&ccfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ledc_channel_config[%u] failed: %s", (unsigned)i, esp_err_to_name(err));
            (void)drv_fan_pwm_del(h);
            return err;
        }

        h->states[i].duty_raw = 0;
        h->states[i].enabled = true;
    }

    *out = h;
    ESP_LOGI(TAG, "init ok: freq=%uHz res=%u bits channels=%u",
             (unsigned)config->freq_hz, (unsigned)res_bits, (unsigned)config->channel_count);
    return ESP_OK;
}

esp_err_t drv_fan_pwm_enable(drv_fan_pwm_t *handle, size_t index, bool enable)
{
    DRV_FAN_PWM_CHECK(handle != NULL, ESP_ERR_INVALID_ARG, "handle NULL");
    DRV_FAN_PWM_CHECK(index < handle->cfg.channel_count, ESP_ERR_INVALID_ARG, "index OOR");

    drv_fan_pwm_lock(handle);

    const ledc_mode_t mode = handle->cfg.speed_mode;
    const ledc_channel_t ch = handle->channels[index].channel;
    esp_err_t err;

    if (enable) {
        // Restaure duty
        err = ledc_set_duty(mode, ch, handle->states[index].duty_raw);
        if (err == ESP_OK) {
            err = ledc_update_duty(mode, ch);
        }
    } else {
        // Met duty à 0
        err = ledc_set_duty(mode, ch, 0);
        if (err == ESP_OK) {
            err = ledc_update_duty(mode, ch);
        }
    }

    if (err == ESP_OK) {
        handle->states[index].enabled = enable;
    }

    drv_fan_pwm_unlock(handle);
    return err;
}

esp_err_t drv_fan_pwm_set_duty_pct(drv_fan_pwm_t *handle, size_t index, float duty_pct)
{
    DRV_FAN_PWM_CHECK(handle != NULL, ESP_ERR_INVALID_ARG, "handle NULL");
    DRV_FAN_PWM_CHECK(index < handle->cfg.channel_count, ESP_ERR_INVALID_ARG, "index OOR");

    if (isnan(duty_pct) || isinf(duty_pct)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (duty_pct < 0.0f) duty_pct = 0.0f;
    if (duty_pct > 100.0f) duty_pct = 100.0f;

    const float raw_f = (duty_pct * (float)handle->max_duty) / 100.0f;
    uint32_t duty_raw = (uint32_t)lroundf(raw_f);
    if (duty_raw > handle->max_duty) {
        duty_raw = handle->max_duty;
    }

    drv_fan_pwm_lock(handle);

    handle->states[index].duty_raw = duty_raw;

    const ledc_mode_t mode = handle->cfg.speed_mode;
    const ledc_channel_t ch = handle->channels[index].channel;

    esp_err_t err = ESP_OK;
    if (handle->states[index].enabled) {
        err = ledc_set_duty(mode, ch, duty_raw);
        if (err == ESP_OK) {
            err = ledc_update_duty(mode, ch);
        }
    }

    drv_fan_pwm_unlock(handle);
    return err;
}

esp_err_t drv_fan_pwm_get_state(drv_fan_pwm_t *handle, size_t index, drv_fan_pwm_channel_state_t *out)
{
    DRV_FAN_PWM_CHECK(handle != NULL, ESP_ERR_INVALID_ARG, "handle NULL");
    DRV_FAN_PWM_CHECK(out != NULL, ESP_ERR_INVALID_ARG, "out NULL");
    DRV_FAN_PWM_CHECK(index < handle->cfg.channel_count, ESP_ERR_INVALID_ARG, "index OOR");

    drv_fan_pwm_lock(handle);
    *out = handle->states[index];
    drv_fan_pwm_unlock(handle);

    return ESP_OK;
}

esp_err_t drv_fan_pwm_del(drv_fan_pwm_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Best-effort stop PWM to 0
    for (size_t i = 0; i < handle->cfg.channel_count; i++) {
        (void)ledc_set_duty(handle->cfg.speed_mode, handle->channels[i].channel, 0);
        (void)ledc_update_duty(handle->cfg.speed_mode, handle->channels[i].channel);
    }

    if (handle->lock) {
        vSemaphoreDelete(handle->lock);
        handle->lock = NULL;
    }

    free(handle->states);
    free(handle->channels);
    free(handle);
    return ESP_OK;
}
