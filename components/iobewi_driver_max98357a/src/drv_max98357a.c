#include "drv_max98357a/drv_max98357a.h"

#include <string.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"

static const char *TAG = "drv_max98357a";

/**
 * @brief Contexte interne du driver
 */
struct drv_max98357a_s {
    i2s_chan_handle_t tx_chan;       // Canal I2S TX
    drv_max98357a_config_t config;   // Configuration
    bool is_enabled;                  // État enabled/disabled
};

esp_err_t drv_max98357a_new(const drv_max98357a_config_t *cfg, drv_max98357a_t **out)
{
    if (cfg == NULL || out == NULL) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    // Validation des GPIO I2S
    if (cfg->bclk_gpio == GPIO_NUM_NC || cfg->ws_gpio == GPIO_NUM_NC ||
        cfg->dout_gpio == GPIO_NUM_NC) {
        ESP_LOGE(TAG, "I2S GPIOs must be configured");
        return ESP_ERR_INVALID_ARG;
    }

    // Validation du sample rate
    if (cfg->sample_rate == 0) {
        ESP_LOGE(TAG, "Invalid sample rate");
        return ESP_ERR_INVALID_ARG;
    }

    // Allocation du contexte
    drv_max98357a_t *ctx = (drv_max98357a_t *)calloc(1, sizeof(drv_max98357a_t));
    if (ctx == NULL) {
        ESP_LOGE(TAG, "Failed to allocate driver context");
        return ESP_ERR_NO_MEM;
    }

    // Copie de la configuration
    memcpy(&ctx->config, cfg, sizeof(drv_max98357a_config_t));

    // Configuration du canal I2S TX
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = cfg->dma_buf_count;
    chan_cfg.dma_frame_num = cfg->dma_buf_len;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &ctx->tx_chan, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel: %s", esp_err_to_name(ret));
        free(ctx);
        return ret;
    }

    // Configuration standard I2S
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(cfg->sample_rate),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(cfg->bits_per_sample, cfg->slot_mode),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = cfg->bclk_gpio,
            .ws = cfg->ws_gpio,
            .dout = cfg->dout_gpio,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(ctx->tx_chan, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2S channel: %s", esp_err_to_name(ret));
        i2s_del_channel(ctx->tx_chan);
        free(ctx);
        return ret;
    }

    // Configuration du GPIO SD_MODE si présent
    if (cfg->sd_mode_gpio != GPIO_NUM_NC) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << cfg->sd_mode_gpio),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ret = gpio_config(&io_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure SD_MODE GPIO: %s", esp_err_to_name(ret));
            i2s_del_channel(ctx->tx_chan);
            free(ctx);
            return ret;
        }

        // Activer par défaut
        gpio_set_level(cfg->sd_mode_gpio, 1);
        ctx->is_enabled = true;
    } else {
        // Toujours enabled si pas de contrôle SD_MODE
        ctx->is_enabled = true;
    }

    // Activer le canal I2S
    ret = i2s_channel_enable(ctx->tx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S channel: %s", esp_err_to_name(ret));
        i2s_del_channel(ctx->tx_chan);
        free(ctx);
        return ret;
    }

    *out = ctx;
    ESP_LOGI(TAG, "MAX98357A driver created (sample_rate=%lu, bits=%d, %s)",
             cfg->sample_rate, cfg->bits_per_sample,
             cfg->slot_mode == I2S_SLOT_MODE_MONO ? "mono" : "stereo");
    return ESP_OK;
}

esp_err_t drv_max98357a_write(drv_max98357a_t *handle, const void *data,
                               size_t size, size_t *bytes_written,
                               uint32_t timeout_ms)
{
    if (handle == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!handle->is_enabled) {
        ESP_LOGW(TAG, "Writing to disabled amplifier");
    }

    esp_err_t ret = i2s_channel_write(handle->tx_chan, data, size, bytes_written,
                                      timeout_ms == portMAX_DELAY ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms));
    if (ret != ESP_OK && ret != ESP_ERR_TIMEOUT) {
        ESP_LOGE(TAG, "Failed to write I2S data: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t drv_max98357a_enable(drv_max98357a_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (handle->config.sd_mode_gpio == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "SD_MODE GPIO not configured");
        return ESP_ERR_NOT_SUPPORTED;
    }

    gpio_set_level(handle->config.sd_mode_gpio, 1);
    handle->is_enabled = true;

    ESP_LOGI(TAG, "MAX98357A enabled");
    return ESP_OK;
}

esp_err_t drv_max98357a_disable(drv_max98357a_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (handle->config.sd_mode_gpio == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "SD_MODE GPIO not configured");
        return ESP_ERR_NOT_SUPPORTED;
    }

    gpio_set_level(handle->config.sd_mode_gpio, 0);
    handle->is_enabled = false;

    ESP_LOGI(TAG, "MAX98357A disabled");
    return ESP_OK;
}

esp_err_t drv_max98357a_get_i2s_handle(drv_max98357a_t *handle,
                                        i2s_chan_handle_t *i2s_handle)
{
    if (handle == NULL || i2s_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *i2s_handle = handle->tx_chan;
    return ESP_OK;
}

esp_err_t drv_max98357a_del(drv_max98357a_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Désactiver si possible
    if (handle->config.sd_mode_gpio != GPIO_NUM_NC) {
        gpio_set_level(handle->config.sd_mode_gpio, 0);
    }

    // Désactiver et supprimer le canal I2S
    i2s_channel_disable(handle->tx_chan);
    i2s_del_channel(handle->tx_chan);

    free(handle);
    ESP_LOGI(TAG, "MAX98357A driver deleted");
    return ESP_OK;
}
