#include "drv_fan_tach/drv_fan_tach.h"

#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"

static const char *TAG = "drv_fan_tach";

typedef struct {
    drv_fan_tach_channel_cfg_t cfg;   // copie locale
    pcnt_unit_handle_t unit;
    pcnt_channel_handle_t chan;
    bool started;
} drv_fan_tach_ch_t;

struct drv_fan_tach_s {
    size_t channel_count;
    drv_fan_tach_ch_t *ch;
};

static esp_err_t drv_fan_tach_validate_cfg(const drv_fan_tach_config_t *config)
{
    if (config == NULL || config->channels == NULL || config->channel_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    // Avec l’API PCNT "new" (pulse_cnt), l’unit est allouée par le driver.
    // Donc pas de notion de "unit partagée" dans la config utilisateur.
    return ESP_OK;
}


esp_err_t drv_fan_tach_config_init(drv_fan_tach_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    config->channels = NULL;
    config->channel_count = 0;
    return ESP_OK;
}

esp_err_t drv_fan_tach_new(const drv_fan_tach_config_t *config, drv_fan_tach_t **out)
{
    esp_err_t ret = ESP_OK;

    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;

    ESP_RETURN_ON_ERROR(drv_fan_tach_validate_cfg(config), TAG, "config invalide");

    drv_fan_tach_t *h = (drv_fan_tach_t *)calloc(1, sizeof(drv_fan_tach_t));
    if (h == NULL) {
        return ESP_ERR_NO_MEM;
    }

    h->channel_count = config->channel_count;
    h->ch = (drv_fan_tach_ch_t *)calloc(h->channel_count, sizeof(drv_fan_tach_ch_t));
    if (h->ch == NULL) {
        free(h);
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < h->channel_count; i++) {
        h->ch[i].cfg = config->channels[i];

        // GPIO input + pull-up si demandé
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << (uint32_t)h->ch[i].cfg.gpio_num),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = h->ch[i].cfg.pullup_enable ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), fail, TAG, "gpio_config ch=%u", (unsigned)i);

        // PCNT unit
        pcnt_unit_config_t unit_config = {
            .low_limit = h->ch[i].cfg.counter_low_limit,
            .high_limit = h->ch[i].cfg.counter_high_limit,
            .flags.accum_count = 1, // accumule si overflow/underflow
        };

        ESP_GOTO_ON_ERROR(pcnt_new_unit(&unit_config, &h->ch[i].unit), fail, TAG, "pcnt_new_unit ch=%u", (unsigned)i);

        // PCNT channel : pulse = GPIO tachy, control non utilisé
        pcnt_chan_config_t chan_config = {
            .edge_gpio_num = h->ch[i].cfg.gpio_num,
            .level_gpio_num = -1,
        };

        ESP_GOTO_ON_ERROR(pcnt_new_channel(h->ch[i].unit, &chan_config, &h->ch[i].chan), fail, TAG, "pcnt_new_channel ch=%u", (unsigned)i);

        // front montant = +1 ; front descendant = 0 (doc)
        ESP_GOTO_ON_ERROR(pcnt_channel_set_edge_action(h->ch[i].chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD),
                          fail, TAG, "pcnt edge action ch=%u", (unsigned)i);

        // level action : hold (pas de gate)
        ESP_GOTO_ON_ERROR(pcnt_channel_set_level_action(h->ch[i].chan, PCNT_CHANNEL_LEVEL_ACTION_HOLD, PCNT_CHANNEL_LEVEL_ACTION_HOLD),
                          fail, TAG, "pcnt level action ch=%u", (unsigned)i);

        ESP_GOTO_ON_ERROR(pcnt_unit_clear_count(h->ch[i].unit), fail, TAG, "pcnt clear ch=%u", (unsigned)i);
        ESP_GOTO_ON_ERROR(pcnt_unit_stop(h->ch[i].unit), fail, TAG, "pcnt stop ch=%u", (unsigned)i);

        h->ch[i].started = false;
    }

    *out = h;
    return ESP_OK;

fail:
    drv_fan_tach_del(h);
    return ret;
}

static esp_err_t drv_fan_tach_get_ch(drv_fan_tach_t *handle, size_t index, drv_fan_tach_ch_t **out_ch)
{
    if (handle == NULL || out_ch == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (index >= handle->channel_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_ch = &handle->ch[index];
    return ESP_OK;
}

esp_err_t drv_fan_tach_start(drv_fan_tach_t *handle, size_t index)
{
    drv_fan_tach_ch_t *ch = NULL;
    ESP_RETURN_ON_ERROR(drv_fan_tach_get_ch(handle, index, &ch), TAG, "index invalide");

    ESP_RETURN_ON_ERROR(pcnt_unit_clear_count(ch->unit), TAG, "pcnt clear");
    ESP_RETURN_ON_ERROR(pcnt_unit_start(ch->unit), TAG, "pcnt start");
    ch->started = true;
    return ESP_OK;
}

esp_err_t drv_fan_tach_stop(drv_fan_tach_t *handle, size_t index)
{
    drv_fan_tach_ch_t *ch = NULL;
    ESP_RETURN_ON_ERROR(drv_fan_tach_get_ch(handle, index, &ch), TAG, "index invalide");

    ESP_RETURN_ON_ERROR(pcnt_unit_stop(ch->unit), TAG, "pcnt stop");
    ch->started = false;
    return ESP_OK;
}

esp_err_t drv_fan_tach_read(drv_fan_tach_t *handle, size_t index, uint32_t period_ms, drv_fan_tach_sample_t *out)
{
    drv_fan_tach_ch_t *ch = NULL;

    if (out == NULL || period_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->period_ms = period_ms;
    out->valid = false;

    ESP_RETURN_ON_ERROR(drv_fan_tach_get_ch(handle, index, &ch), TAG, "index invalide");

    // séquence déterministe (doc)
    ESP_RETURN_ON_ERROR(pcnt_unit_clear_count(ch->unit), TAG, "pcnt clear");
    ESP_RETURN_ON_ERROR(pcnt_unit_start(ch->unit), TAG, "pcnt start");

    vTaskDelay(pdMS_TO_TICKS(period_ms));

    ESP_RETURN_ON_ERROR(pcnt_unit_stop(ch->unit), TAG, "pcnt stop");

    int count = 0;
    ESP_RETURN_ON_ERROR(pcnt_unit_get_count(ch->unit, &count), TAG, "pcnt get");
    out->pulse_count = (int32_t)count;
    out->valid = true;

    return ESP_OK;
}

esp_err_t drv_fan_tach_del(drv_fan_tach_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (handle->ch) {
        for (size_t i = 0; i < handle->channel_count; i++) {
            if (handle->ch[i].chan) {
                pcnt_del_channel(handle->ch[i].chan);
                handle->ch[i].chan = NULL;
            }
            if (handle->ch[i].unit) {
                pcnt_unit_stop(handle->ch[i].unit);
                pcnt_del_unit(handle->ch[i].unit);
                handle->ch[i].unit = NULL;
            }
        }
        free(handle->ch);
        handle->ch = NULL;
    }

    free(handle);
    return ESP_OK;
}
