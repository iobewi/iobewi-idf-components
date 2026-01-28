#pragma once

#include "mw_uros_transport_usb/mw_uros_transport_usb_types.h"
#include "uxr/client/profile/transport/custom/custom_transport.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "tusb_console.h"
#include "sdkconfig.h"

#if (CONFIG_TINYUSB_CDC_COUNT < 2)
    #warning "Define CONFIG_TINYUSB_CDC_COUNT to 2 in menuconfig if you want log over USB-CDC. Otherwise, disable log output in menuconfig."
#endif

#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize TinyUSB once (singleton pattern)
 *
 * This function ensures TinyUSB is initialized only once, even if called multiple times.
 *
 * @param tinyusb_config TinyUSB configuration
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already initialized with different config
 */
esp_err_t mw_uros_transport_usb_init(const tinyusb_config_t *tinyusb_config);

/**
 * @brief Create a transport handle.
 *
 * @param out Output pointer receiving the allocated handle.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on invalid parameters, ESP_ERR_NO_MEM on allocation failure.
 */
esp_err_t mw_uros_transport_usb_new(mw_uros_transport_usb_t **out);

/**
 * @brief Destroy a transport handle created with mw_uros_transport_usb_new().
 *
 * @param handle Transport handle.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on invalid parameters.
 */
esp_err_t mw_uros_transport_usb_del(mw_uros_transport_usb_t *handle);

/**
 * @brief Initialize USB-CDC logging (optional)
 *
 * Redirects ESP_LOG output to USB-CDC port 1 (port 0 is used for micro-ROS transport).
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mw_uros_transport_usb_logging_init(void);

/**
 * @brief Deinitialize USB-CDC logging
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mw_uros_transport_usb_logging_deinit(void);

/**
 * EXCEPTION AU CDC (section 6.2) :
 *
 * Les fonctions suivantes sont des callbacks imposés par l'interface
 * uxrCustomTransport de micro-ROS. Leurs signatures (bool/size_t) sont
 * définies par micro-ROS et NE PEUVENT PAS être modifiées sans casser
 * l'intégration avec le transport custom micro-ROS.
 *
 * Ces fonctions ne sont pas destinées à être appelées directement par
 * l'application mais uniquement par le runtime micro-ROS.
 */

bool mw_uros_transport_usb_open(struct uxrCustomTransport* transport);
bool mw_uros_transport_usb_close(struct uxrCustomTransport* transport);
size_t mw_uros_transport_usb_write(struct uxrCustomTransport* transport, const uint8_t* buf, size_t len, uint8_t* err);
size_t mw_uros_transport_usb_read(struct uxrCustomTransport* transport, uint8_t* buf, size_t len, int timeout, uint8_t* err);

// Backward compatibility aliases
#define esp_usbcdc_tinyusb_init_once mw_uros_transport_usb_init
#define esp_usbcdc_logging_init mw_uros_transport_usb_logging_init
#define esp_usbcdc_logging_deinit mw_uros_transport_usb_logging_deinit
#define esp_usbcdc_open mw_uros_transport_usb_open
#define esp_usbcdc_close mw_uros_transport_usb_close
#define esp_usbcdc_write mw_uros_transport_usb_write
#define esp_usbcdc_read mw_uros_transport_usb_read

#ifdef __cplusplus
}
#endif

#else
#error "This transport is only supported on ESP32-S2 or ESP32-S3 targets"
#endif // CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3
