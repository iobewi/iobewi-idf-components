#include "unity.h"
#include "mw_uros_core/mw_uros_core.h"

TEST_CASE("mw_uros_core_new rejects NULL config", "[mw_uros_core]")
{
    mw_uros_core_t *handle = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mw_uros_core_new(NULL, &handle));
}

TEST_CASE("mw_uros_core_new rejects NULL out", "[mw_uros_core]")
{
    uros_core_config_t config = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mw_uros_core_new(&config, NULL));
}

TEST_CASE("mw_uros_core_del rejects NULL handle", "[mw_uros_core]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mw_uros_core_del(NULL));
}

TEST_CASE("mw_uros_core lifecycle OK", "[mw_uros_core]")
{
    uros_core_config_t config = {0};
    mw_uros_core_t *handle = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, mw_uros_core_new(&config, &handle));
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(ESP_OK, mw_uros_core_del(handle));
}
