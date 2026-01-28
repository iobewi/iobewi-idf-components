#include "unity.h"
#include "drv_a02yyuw/drv_a02yyuw.h"

TEST_CASE("drv_a02yyuw_new rejects NULL config", "[drv_a02yyuw]")
{
    drv_a02yyuw_t *handle = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_a02yyuw_new(NULL, &handle));
}

TEST_CASE("drv_a02yyuw_new rejects NULL out", "[drv_a02yyuw]")
{
    drv_a02yyuw_config_t config = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_a02yyuw_new(&config, NULL));
}

TEST_CASE("drv_a02yyuw_del rejects NULL handle", "[drv_a02yyuw]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_a02yyuw_del(NULL));
}

TEST_CASE("drv_a02yyuw lifecycle OK", "[drv_a02yyuw]")
{
    int gpio_en_list[1] = {18};
    drv_a02yyuw_config_t config;
    drv_a02yyuw_t *handle = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, drv_a02yyuw_config_init(&config));
    config.uart_num = UART_NUM_1;
    config.uart_rx_gpio = 16;
    config.uart_tx_gpio = 17;
    config.gpio_en_list = gpio_en_list;
    config.sensor_count = 1;

    TEST_ASSERT_EQUAL(ESP_OK, drv_a02yyuw_new(&config, &handle));
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(ESP_OK, drv_a02yyuw_del(handle));
}

TEST_CASE("drv_a02yyuw repeated lifecycle OK", "[drv_a02yyuw]")
{
    int gpio_en_list[1] = {18};
    drv_a02yyuw_config_t config;
    drv_a02yyuw_t *h1 = NULL;
    drv_a02yyuw_t *h2 = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, drv_a02yyuw_config_init(&config));
    config.uart_num = UART_NUM_1;
    config.uart_rx_gpio = 16;
    config.uart_tx_gpio = 17;
    config.gpio_en_list = gpio_en_list;
    config.sensor_count = 1;

    TEST_ASSERT_EQUAL(ESP_OK, drv_a02yyuw_new(&config, &h1));
    TEST_ASSERT_NOT_NULL(h1);
    TEST_ASSERT_EQUAL(ESP_OK, drv_a02yyuw_del(h1));

    TEST_ASSERT_EQUAL(ESP_OK, drv_a02yyuw_new(&config, &h2));
    TEST_ASSERT_NOT_NULL(h2);
    TEST_ASSERT_EQUAL(ESP_OK, drv_a02yyuw_del(h2));
}
