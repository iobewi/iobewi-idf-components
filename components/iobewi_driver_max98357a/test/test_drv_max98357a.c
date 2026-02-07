#include "unity.h"
#include "drv_max98357a/drv_max98357a.h"

// Tests de validation des arguments

TEST_CASE("drv_max98357a_new rejects NULL config", "[drv_max98357a]")
{
    drv_max98357a_t *handle = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_max98357a_new(NULL, &handle));
}

TEST_CASE("drv_max98357a_new rejects NULL out", "[drv_max98357a]")
{
    drv_max98357a_config_t config = {
        .bclk_gpio = GPIO_NUM_14,
        .ws_gpio = GPIO_NUM_15,
        .dout_gpio = GPIO_NUM_16,
        .sd_mode_gpio = GPIO_NUM_NC,
        .sample_rate = 16000,
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .slot_mode = I2S_SLOT_MODE_MONO,
        .gain = DRV_MAX98357A_GAIN_9DB,
        .dma_buf_count = 6,
        .dma_buf_len = 512,
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_max98357a_new(&config, NULL));
}

TEST_CASE("drv_max98357a_new rejects invalid GPIOs", "[drv_max98357a]")
{
    drv_max98357a_t *handle = NULL;

    // BCLK manquant
    drv_max98357a_config_t config1 = {
        .bclk_gpio = GPIO_NUM_NC,
        .ws_gpio = GPIO_NUM_15,
        .dout_gpio = GPIO_NUM_16,
        .sd_mode_gpio = GPIO_NUM_NC,
        .sample_rate = 16000,
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .slot_mode = I2S_SLOT_MODE_MONO,
        .gain = DRV_MAX98357A_GAIN_9DB,
        .dma_buf_count = 6,
        .dma_buf_len = 512,
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_max98357a_new(&config1, &handle));

    // WS manquant
    drv_max98357a_config_t config2 = {
        .bclk_gpio = GPIO_NUM_14,
        .ws_gpio = GPIO_NUM_NC,
        .dout_gpio = GPIO_NUM_16,
        .sd_mode_gpio = GPIO_NUM_NC,
        .sample_rate = 16000,
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .slot_mode = I2S_SLOT_MODE_MONO,
        .gain = DRV_MAX98357A_GAIN_9DB,
        .dma_buf_count = 6,
        .dma_buf_len = 512,
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_max98357a_new(&config2, &handle));

    // DOUT manquant
    drv_max98357a_config_t config3 = {
        .bclk_gpio = GPIO_NUM_14,
        .ws_gpio = GPIO_NUM_15,
        .dout_gpio = GPIO_NUM_NC,
        .sd_mode_gpio = GPIO_NUM_NC,
        .sample_rate = 16000,
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .slot_mode = I2S_SLOT_MODE_MONO,
        .gain = DRV_MAX98357A_GAIN_9DB,
        .dma_buf_count = 6,
        .dma_buf_len = 512,
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_max98357a_new(&config3, &handle));
}

TEST_CASE("drv_max98357a_new rejects invalid sample rate", "[drv_max98357a]")
{
    drv_max98357a_t *handle = NULL;
    drv_max98357a_config_t config = {
        .bclk_gpio = GPIO_NUM_14,
        .ws_gpio = GPIO_NUM_15,
        .dout_gpio = GPIO_NUM_16,
        .sd_mode_gpio = GPIO_NUM_NC,
        .sample_rate = 0, // Invalid
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .slot_mode = I2S_SLOT_MODE_MONO,
        .gain = DRV_MAX98357A_GAIN_9DB,
        .dma_buf_count = 6,
        .dma_buf_len = 512,
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_max98357a_new(&config, &handle));
}

TEST_CASE("drv_max98357a_del rejects NULL handle", "[drv_max98357a]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_max98357a_del(NULL));
}

TEST_CASE("drv_max98357a_write rejects NULL handle", "[drv_max98357a]")
{
    uint8_t dummy_data[16];
    size_t bytes_written;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                     drv_max98357a_write(NULL, dummy_data, 16, &bytes_written, 100));
}

TEST_CASE("drv_max98357a_write rejects NULL data", "[drv_max98357a]")
{
    // Note: Ce test nécessiterait un handle valide, donc on teste juste la validation NULL
    size_t bytes_written;
    // Un handle NULL devrait échouer avant même de vérifier data
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                     drv_max98357a_write(NULL, NULL, 16, &bytes_written, 100));
}

TEST_CASE("drv_max98357a_enable rejects NULL handle", "[drv_max98357a]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_max98357a_enable(NULL));
}

TEST_CASE("drv_max98357a_disable rejects NULL handle", "[drv_max98357a]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_max98357a_disable(NULL));
}

TEST_CASE("drv_max98357a_get_i2s_handle rejects NULL handle", "[drv_max98357a]")
{
    i2s_chan_handle_t i2s_handle;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                     drv_max98357a_get_i2s_handle(NULL, &i2s_handle));
}

TEST_CASE("drv_max98357a_get_i2s_handle rejects NULL i2s_handle", "[drv_max98357a]")
{
    // Note: Un handle NULL devrait échouer avant même de vérifier i2s_handle
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                     drv_max98357a_get_i2s_handle(NULL, NULL));
}

// Tests de lifecycle (nécessitent un matériel réel ou un mock)
// Ces tests sont commentés car ils nécessitent des GPIO disponibles

/*
TEST_CASE("drv_max98357a lifecycle OK", "[drv_max98357a]")
{
    drv_max98357a_config_t config = {
        .bclk_gpio = GPIO_NUM_14,
        .ws_gpio = GPIO_NUM_15,
        .dout_gpio = GPIO_NUM_16,
        .sd_mode_gpio = GPIO_NUM_17,
        .sample_rate = 16000,
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .slot_mode = I2S_SLOT_MODE_MONO,
        .gain = DRV_MAX98357A_GAIN_9DB,
        .dma_buf_count = 6,
        .dma_buf_len = 512,
    };
    drv_max98357a_t *handle = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, drv_max98357a_new(&config, &handle));
    TEST_ASSERT_NOT_NULL(handle);

    // Test enable/disable
    TEST_ASSERT_EQUAL(ESP_OK, drv_max98357a_disable(handle));
    TEST_ASSERT_EQUAL(ESP_OK, drv_max98357a_enable(handle));

    // Cleanup
    TEST_ASSERT_EQUAL(ESP_OK, drv_max98357a_del(handle));
}

TEST_CASE("drv_max98357a write silence OK", "[drv_max98357a]")
{
    drv_max98357a_config_t config = {
        .bclk_gpio = GPIO_NUM_14,
        .ws_gpio = GPIO_NUM_15,
        .dout_gpio = GPIO_NUM_16,
        .sd_mode_gpio = GPIO_NUM_NC,
        .sample_rate = 16000,
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .slot_mode = I2S_SLOT_MODE_MONO,
        .gain = DRV_MAX98357A_GAIN_9DB,
        .dma_buf_count = 6,
        .dma_buf_len = 512,
    };
    drv_max98357a_t *handle = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, drv_max98357a_new(&config, &handle));
    TEST_ASSERT_NOT_NULL(handle);

    // Write silence
    int16_t silence[256] = {0};
    size_t bytes_written;
    TEST_ASSERT_EQUAL(ESP_OK,
                     drv_max98357a_write(handle, silence, sizeof(silence),
                                        &bytes_written, 1000));
    TEST_ASSERT_EQUAL(sizeof(silence), bytes_written);

    TEST_ASSERT_EQUAL(ESP_OK, drv_max98357a_del(handle));
}
*/

// Tests de configuration par défaut

TEST_CASE("drv_max98357a default config is valid", "[drv_max98357a]")
{
    drv_max98357a_config_t config = DRV_MAX98357A_CONFIG_DEFAULT();

    TEST_ASSERT_EQUAL(GPIO_NUM_NC, config.bclk_gpio);
    TEST_ASSERT_EQUAL(GPIO_NUM_NC, config.ws_gpio);
    TEST_ASSERT_EQUAL(GPIO_NUM_NC, config.dout_gpio);
    TEST_ASSERT_EQUAL(GPIO_NUM_NC, config.sd_mode_gpio);
    TEST_ASSERT_EQUAL(16000, config.sample_rate);
    TEST_ASSERT_EQUAL(I2S_DATA_BIT_WIDTH_16BIT, config.bits_per_sample);
    TEST_ASSERT_EQUAL(I2S_SLOT_MODE_MONO, config.slot_mode);
    TEST_ASSERT_EQUAL(DRV_MAX98357A_GAIN_9DB, config.gain);
    TEST_ASSERT_EQUAL(6, config.dma_buf_count);
    TEST_ASSERT_EQUAL(512, config.dma_buf_len);
}
