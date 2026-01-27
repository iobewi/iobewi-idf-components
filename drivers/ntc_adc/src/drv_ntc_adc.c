#include "drv_ntc_adc/drv_ntc_adc.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_check.h"

#include "esp_adc/adc_oneshot.h"

static const char *TAG = "drv_ntc_adc";

struct drv_ntc_adc_s {
    adc_unit_t unit;
    adc_bitwidth_t bitwidth;

    size_t channel_count;
    adc_channel_t *channels;            // owned
    adc_atten_t   *attens;              // owned

    adc_oneshot_unit_handle_t oneshot;  // owned
};

esp_err_t drv_ntc_adc_config_init(drv_ntc_adc_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(config, 0, sizeof(*config));
    config->unit = ADC_UNIT_1;
    config->bitwidth = ADC_BITWIDTH_DEFAULT; // laisse l’IDF choisir la valeur par défaut
    config->channels = NULL;
    config->channel_count = 0;

    return ESP_OK;
}

static bool unit_is_supported(adc_unit_t unit)
{
    return (unit == ADC_UNIT_1) || (unit == ADC_UNIT_2);
}

esp_err_t drv_ntc_adc_new(const drv_ntc_adc_config_t *config, drv_ntc_adc_t **out)
{
    esp_err_t ret = ESP_OK;

    if (config == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!unit_is_supported(config->unit)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->channels == NULL || config->channel_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    drv_ntc_adc_t *h = calloc(1, sizeof(*h));
    if (h == NULL) {
        return ESP_ERR_NO_MEM;
    }

    h->unit = config->unit;
    h->bitwidth = config->bitwidth;
    h->channel_count = config->channel_count;

    h->channels = calloc(h->channel_count, sizeof(adc_channel_t));
    h->attens   = calloc(h->channel_count, sizeof(adc_atten_t));
    ESP_GOTO_ON_FALSE(h->channels && h->attens, ESP_ERR_NO_MEM, fail, TAG, "no mem channels/attens");

    for (size_t i = 0; i < h->channel_count; i++) {
        h->channels[i] = config->channels[i].channel;
        h->attens[i]   = config->channels[i].atten;
    }

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = h->unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    ret = adc_oneshot_new_unit(&unit_cfg, &h->oneshot);
    ESP_GOTO_ON_ERROR(ret, fail, TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(ret));

    for (size_t i = 0; i < h->channel_count; i++) {
        adc_oneshot_chan_cfg_t chan_cfg = {
            .atten = h->attens[i],
            .bitwidth = h->bitwidth,
        };

        ret = adc_oneshot_config_channel(h->oneshot, h->channels[i], &chan_cfg);
        ESP_GOTO_ON_ERROR(ret, fail, TAG, "config channel[%u] failed: %s",
                          (unsigned)i, esp_err_to_name(ret));
    }

    *out = h;
    ESP_LOGI(TAG, "init ok (unit=%d, channels=%u)", (int)h->unit, (unsigned)h->channel_count);
    return ESP_OK;

fail:
    (void)drv_ntc_adc_del(h);
    return ret;
}

esp_err_t drv_ntc_adc_read(drv_ntc_adc_t *handle, size_t index, drv_ntc_adc_sample_t *out)
{
    if (handle == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (index >= handle->channel_count) {
        return ESP_ERR_INVALID_ARG;
    }

    int raw = 0;
    esp_err_t ret = adc_oneshot_read(handle->oneshot, handle->channels[index], &raw);

    out->raw = (ret == ESP_OK) ? (uint32_t)raw : 0;
    out->valid = (ret == ESP_OK);

    return ret;
}

esp_err_t drv_ntc_adc_del(drv_ntc_adc_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (handle->oneshot) {
        esp_err_t ret = adc_oneshot_del_unit(handle->oneshot);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "adc_oneshot_del_unit: %s", esp_err_to_name(ret));
        }
        handle->oneshot = NULL;
    }

    free(handle->channels);
    free(handle->attens);

    free(handle);
    return ESP_OK;
}
