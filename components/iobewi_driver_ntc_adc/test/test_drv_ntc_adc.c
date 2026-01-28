#include "unity.h"
#include "drv_ntc_adc/drv_ntc_adc.h"

TEST_CASE("drv_ntc_adc_new rejects NULL config", "[drv_ntc_adc]")
{
    drv_ntc_adc_t *handle = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_ntc_adc_new(NULL, &handle));
}

TEST_CASE("drv_ntc_adc_new rejects NULL out", "[drv_ntc_adc]")
{
    drv_ntc_adc_config_t config = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_ntc_adc_new(&config, NULL));
}

TEST_CASE("drv_ntc_adc_del rejects NULL handle", "[drv_ntc_adc]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_ntc_adc_del(NULL));
}

TEST_CASE("drv_ntc_adc lifecycle OK", "[drv_ntc_adc]")
{
    drv_ntc_adc_channel_cfg_t channels[1] = {
        {
            .channel = ADC_CHANNEL_0,
            .atten = ADC_ATTEN_DB_11,
        },
    };
    drv_ntc_adc_config_t config;
    drv_ntc_adc_t *handle = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, drv_ntc_adc_config_init(&config));
    config.unit = ADC_UNIT_1;
    config.channels = channels;
    config.channel_count = 1;

    TEST_ASSERT_EQUAL(ESP_OK, drv_ntc_adc_new(&config, &handle));
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(ESP_OK, drv_ntc_adc_del(handle));
}

TEST_CASE("drv_ntc_adc repeated lifecycle OK", "[drv_ntc_adc]")
{
    drv_ntc_adc_channel_cfg_t channels[1] = {
        {
            .channel = ADC_CHANNEL_0,
            .atten = ADC_ATTEN_DB_11,
        },
    };
    drv_ntc_adc_config_t config;
    drv_ntc_adc_t *h1 = NULL;
    drv_ntc_adc_t *h2 = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, drv_ntc_adc_config_init(&config));
    config.unit = ADC_UNIT_1;
    config.channels = channels;
    config.channel_count = 1;

    TEST_ASSERT_EQUAL(ESP_OK, drv_ntc_adc_new(&config, &h1));
    TEST_ASSERT_NOT_NULL(h1);
    TEST_ASSERT_EQUAL(ESP_OK, drv_ntc_adc_del(h1));

    TEST_ASSERT_EQUAL(ESP_OK, drv_ntc_adc_new(&config, &h2));
    TEST_ASSERT_NOT_NULL(h2);
    TEST_ASSERT_EQUAL(ESP_OK, drv_ntc_adc_del(h2));
}
