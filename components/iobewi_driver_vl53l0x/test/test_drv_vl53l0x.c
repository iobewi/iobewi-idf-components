#include "unity.h"
#include "drv_vl53l0x/drv_vl53l0x.h"

TEST_CASE("drv_vl53l0x_new rejects NULL config", "[drv_vl53l0x]")
{
    drv_vl53l0x_t *handle = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_vl53l0x_new(NULL, &handle));
}

TEST_CASE("drv_vl53l0x_new rejects NULL out", "[drv_vl53l0x]")
{
    drv_vl53l0x_config_t config = {
        .i2c_port = I2C_NUM_0,
        .i2c_addr = 0x29,
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_vl53l0x_new(&config, NULL));
}

TEST_CASE("drv_vl53l0x_del rejects NULL handle", "[drv_vl53l0x]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, drv_vl53l0x_del(NULL));
}

TEST_CASE("drv_vl53l0x lifecycle OK", "[drv_vl53l0x]")
{
    drv_vl53l0x_config_t config = {
        .i2c_port = I2C_NUM_0,
        .i2c_addr = 0x29,
    };
    drv_vl53l0x_t *handle = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, drv_vl53l0x_new(&config, &handle));
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(ESP_OK, drv_vl53l0x_del(handle));
}
