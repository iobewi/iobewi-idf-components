#include "unity.h"
#include "mw_uros_transport_usb/mw_uros_transport_usb.h"

TEST_CASE("mw_uros_transport_usb_new rejects NULL out", "[mw_uros_transport_usb]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mw_uros_transport_usb_new(NULL));
}

TEST_CASE("mw_uros_transport_usb_del rejects NULL handle", "[mw_uros_transport_usb]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mw_uros_transport_usb_del(NULL));
}

TEST_CASE("mw_uros_transport_usb lifecycle OK", "[mw_uros_transport_usb]")
{
    mw_uros_transport_usb_t *handle = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, mw_uros_transport_usb_new(&handle));
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(ESP_OK, mw_uros_transport_usb_del(handle));
}
