#include "unity.h"
#include "lib_a02_provider/lib_a02_provider.h"
#include "drv_a02yyuw/drv_a02yyuw.h"

TEST_CASE("lib_a02_provider_new rejects NULL config", "[lib_a02_provider]")
{
    lib_a02_provider_t *handle = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, lib_a02_provider_new(NULL, &handle));
}

TEST_CASE("lib_a02_provider_new rejects NULL out", "[lib_a02_provider]")
{
    lib_a02_provider_config_t config = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, lib_a02_provider_new(&config, NULL));
}

TEST_CASE("lib_a02_provider_del rejects NULL handle", "[lib_a02_provider]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, lib_a02_provider_del(NULL));
}

TEST_CASE("lib_a02_provider lifecycle OK", "[lib_a02_provider]")
{
    int gpio_en_list[1] = {18};
    drv_a02yyuw_config_t drv_config;
    drv_a02yyuw_t *driver = NULL;
    lib_a02_provider_config_t config;
    lib_a02_provider_t *handle = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, drv_a02yyuw_config_init(&drv_config));
    drv_config.uart_num = UART_NUM_1;
    drv_config.uart_rx_gpio = 16;
    drv_config.uart_tx_gpio = 17;
    drv_config.gpio_en_list = gpio_en_list;
    drv_config.sensor_count = 1;

    TEST_ASSERT_EQUAL(ESP_OK, drv_a02yyuw_new(&drv_config, &driver));
    TEST_ASSERT_NOT_NULL(driver);

    TEST_ASSERT_EQUAL(ESP_OK, lib_a02_provider_config_init(&config));
    config.driver = driver;
    config.sensor_count = 1;
    config.median_filter_size = 3;
    config.range_min_m = 0.3f;
    config.range_max_m = 4.5f;
    config.mode = DRV_A02YYUW_MODE_PROCESSED;
    config.read_timeout_ms = 500;
    config.discard_first_sample = true;

    TEST_ASSERT_EQUAL(ESP_OK, lib_a02_provider_new(&config, &handle));
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(ESP_OK, lib_a02_provider_del(handle));
    TEST_ASSERT_EQUAL(ESP_OK, drv_a02yyuw_del(driver));
}

TEST_CASE("lib_a02_provider repeated lifecycle OK", "[lib_a02_provider]")
{
    int gpio_en_list[1] = {18};
    drv_a02yyuw_config_t drv_config;
    lib_a02_provider_config_t config;
    drv_a02yyuw_t *driver1 = NULL;
    drv_a02yyuw_t *driver2 = NULL;
    lib_a02_provider_t *h1 = NULL;
    lib_a02_provider_t *h2 = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, drv_a02yyuw_config_init(&drv_config));
    drv_config.uart_num = UART_NUM_1;
    drv_config.uart_rx_gpio = 16;
    drv_config.uart_tx_gpio = 17;
    drv_config.gpio_en_list = gpio_en_list;
    drv_config.sensor_count = 1;

    TEST_ASSERT_EQUAL(ESP_OK, drv_a02yyuw_new(&drv_config, &driver1));
    TEST_ASSERT_NOT_NULL(driver1);

    TEST_ASSERT_EQUAL(ESP_OK, lib_a02_provider_config_init(&config));
    config.driver = driver1;
    config.sensor_count = 1;
    config.median_filter_size = 3;
    config.range_min_m = 0.3f;
    config.range_max_m = 4.5f;
    config.mode = DRV_A02YYUW_MODE_PROCESSED;
    config.read_timeout_ms = 500;
    config.discard_first_sample = true;

    TEST_ASSERT_EQUAL(ESP_OK, lib_a02_provider_new(&config, &h1));
    TEST_ASSERT_NOT_NULL(h1);
    TEST_ASSERT_EQUAL(ESP_OK, lib_a02_provider_del(h1));
    TEST_ASSERT_EQUAL(ESP_OK, drv_a02yyuw_del(driver1));

    TEST_ASSERT_EQUAL(ESP_OK, drv_a02yyuw_new(&drv_config, &driver2));
    TEST_ASSERT_NOT_NULL(driver2);

    config.driver = driver2;
    TEST_ASSERT_EQUAL(ESP_OK, lib_a02_provider_new(&config, &h2));
    TEST_ASSERT_NOT_NULL(h2);
    TEST_ASSERT_EQUAL(ESP_OK, lib_a02_provider_del(h2));
    TEST_ASSERT_EQUAL(ESP_OK, drv_a02yyuw_del(driver2));
}
