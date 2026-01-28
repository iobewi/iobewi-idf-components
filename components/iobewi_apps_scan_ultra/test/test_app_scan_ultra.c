#include "unity.h"
#include "app_scan_ultra/app_scan_ultra.h"
#include "drv_a02yyuw/drv_a02yyuw.h"

TEST_CASE("app_scan_ultra_new rejects NULL config", "[app_scan_ultra]")
{
    app_scan_ultra_t *handle = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, app_scan_ultra_new(NULL, &handle));
}

TEST_CASE("app_scan_ultra_new rejects NULL out", "[app_scan_ultra]")
{
    app_scan_ultra_config_t config = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, app_scan_ultra_new(&config, NULL));
}

TEST_CASE("app_scan_ultra_del rejects NULL handle", "[app_scan_ultra]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, app_scan_ultra_del(NULL));
}

TEST_CASE("app_scan_ultra lifecycle OK", "[app_scan_ultra]")
{
    int gpio_en_list[1] = {18};
    drv_a02yyuw_config_t drv_config;
    drv_a02yyuw_t *driver = NULL;
    app_scan_ultra_config_t config;
    app_scan_ultra_t *handle = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, drv_a02yyuw_config_init(&drv_config));
    drv_config.uart_num = UART_NUM_1;
    drv_config.uart_rx_gpio = 16;
    drv_config.uart_tx_gpio = 17;
    drv_config.gpio_en_list = gpio_en_list;
    drv_config.sensor_count = 1;

    TEST_ASSERT_EQUAL(ESP_OK, drv_a02yyuw_new(&drv_config, &driver));
    TEST_ASSERT_NOT_NULL(driver);

    TEST_ASSERT_EQUAL(ESP_OK, app_scan_ultra_config_init(&config));
    config.provider_config.driver = driver;
    config.provider_config.sensor_count = 1;
    config.provider_config.median_filter_size = 3;
    config.provider_config.range_min_m = 0.3f;
    config.provider_config.range_max_m = 4.5f;
    config.provider_config.mode = DRV_A02YYUW_MODE_PROCESSED;
    config.provider_config.read_timeout_ms = 500;
    config.provider_config.discard_first_sample = true;
    config.bins = 36;
    config.angle_min = 0.0f;
    config.angle_max = 6.283185f;
    config.range_min = 0.3f;
    config.range_max = 4.5f;
    config.sensor_bin_mapping[0] = 0;
    config.sensor_bin_mapping[1] = 9;
    config.sensor_bin_mapping[2] = 18;
    config.sensor_bin_mapping[3] = 27;
    config.frame_id = "base_link";

    TEST_ASSERT_EQUAL(ESP_OK, app_scan_ultra_new(&config, &handle));
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(ESP_OK, app_scan_ultra_del(handle));
    TEST_ASSERT_EQUAL(ESP_OK, drv_a02yyuw_del(driver));
}

TEST_CASE("app_scan_ultra repeated lifecycle OK", "[app_scan_ultra]")
{
    int gpio_en_list[1] = {18};
    drv_a02yyuw_config_t drv_config;
    app_scan_ultra_config_t config;
    drv_a02yyuw_t *driver1 = NULL;
    drv_a02yyuw_t *driver2 = NULL;
    app_scan_ultra_t *h1 = NULL;
    app_scan_ultra_t *h2 = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, drv_a02yyuw_config_init(&drv_config));
    drv_config.uart_num = UART_NUM_1;
    drv_config.uart_rx_gpio = 16;
    drv_config.uart_tx_gpio = 17;
    drv_config.gpio_en_list = gpio_en_list;
    drv_config.sensor_count = 1;

    TEST_ASSERT_EQUAL(ESP_OK, drv_a02yyuw_new(&drv_config, &driver1));
    TEST_ASSERT_NOT_NULL(driver1);

    TEST_ASSERT_EQUAL(ESP_OK, app_scan_ultra_config_init(&config));
    config.provider_config.driver = driver1;
    config.provider_config.sensor_count = 1;
    config.provider_config.median_filter_size = 3;
    config.provider_config.range_min_m = 0.3f;
    config.provider_config.range_max_m = 4.5f;
    config.provider_config.mode = DRV_A02YYUW_MODE_PROCESSED;
    config.provider_config.read_timeout_ms = 500;
    config.provider_config.discard_first_sample = true;
    config.bins = 36;
    config.angle_min = 0.0f;
    config.angle_max = 6.283185f;
    config.range_min = 0.3f;
    config.range_max = 4.5f;
    config.sensor_bin_mapping[0] = 0;
    config.sensor_bin_mapping[1] = 9;
    config.sensor_bin_mapping[2] = 18;
    config.sensor_bin_mapping[3] = 27;
    config.frame_id = "base_link";

    TEST_ASSERT_EQUAL(ESP_OK, app_scan_ultra_new(&config, &h1));
    TEST_ASSERT_NOT_NULL(h1);
    TEST_ASSERT_EQUAL(ESP_OK, app_scan_ultra_del(h1));
    TEST_ASSERT_EQUAL(ESP_OK, drv_a02yyuw_del(driver1));

    TEST_ASSERT_EQUAL(ESP_OK, drv_a02yyuw_new(&drv_config, &driver2));
    TEST_ASSERT_NOT_NULL(driver2);

    config.provider_config.driver = driver2;
    TEST_ASSERT_EQUAL(ESP_OK, app_scan_ultra_new(&config, &h2));
    TEST_ASSERT_NOT_NULL(h2);
    TEST_ASSERT_EQUAL(ESP_OK, app_scan_ultra_del(h2));
    TEST_ASSERT_EQUAL(ESP_OK, drv_a02yyuw_del(driver2));
}
