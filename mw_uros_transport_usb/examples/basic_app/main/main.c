#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "mw_uros_transport_usb/mw_uros_transport_usb.h"

#include <uxr/client/transport.h>

static const char *TAG = "usb_transport_example";

/**
 * @brief Basic example demonstrating USB-CDC transport initialization
 *
 * This example shows how to initialize the USB-CDC transport for micro-ROS.
 * The transport layer provides callbacks for micro-ROS to communicate over USB.
 */
void app_main(void)
{
    ESP_LOGI(TAG, "mw_uros_transport_usb basic example");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
    // Initialize USB-CDC logging (optional)
    ret = mw_uros_transport_usb_logging_init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "USB-CDC logging initialized");
    } else {
        ESP_LOGW(TAG, "USB-CDC logging initialization failed: %s", esp_err_to_name(ret));
    }

    // Setup custom transport structure for micro-ROS
    tinyusb_cdcacm_itf_t cdc_port = TINYUSB_CDC_ACM_0;

    // Note: In a real application, you would create a uxrCustomTransport
    // and register the callbacks (mw_uros_transport_usb_open, mw_uros_transport_usb_close,
    // mw_uros_transport_usb_write, mw_uros_transport_usb_read) with micro-ROS.
    //
    // Example:
    // struct uxrCustomTransport transport;
    // transport.args = &cdc_port;
    // uxr_set_custom_transport_callbacks(
    //     &transport,
    //     mw_uros_transport_usb_open,
    //     mw_uros_transport_usb_close,
    //     mw_uros_transport_usb_write,
    //     mw_uros_transport_usb_read
    // );

    ESP_LOGI(TAG, "USB-CDC transport ready for micro-ROS integration");
    ESP_LOGI(TAG, "Use this component with mw_uros_core to create a complete micro-ROS application");
    ESP_LOGI(TAG, "Example: Connect USB cable and run micro-ROS agent on host");
#else
    ESP_LOGE(TAG, "This example requires ESP32-S2 or ESP32-S3 target");
#endif

    ESP_LOGI(TAG, "Example completed - transport layer demonstrated");
}
