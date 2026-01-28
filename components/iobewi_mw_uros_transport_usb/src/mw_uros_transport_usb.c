#include "mw_uros_transport_usb/mw_uros_transport_usb.h"

#include <stdbool.h>
#include <stdlib.h>

struct mw_uros_transport_usb_s {
    bool tinyusb_initialized;
};

esp_err_t mw_uros_transport_usb_new(mw_uros_transport_usb_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }

    *out = NULL;
    mw_uros_transport_usb_t *handle = calloc(1, sizeof(*handle));
    if (!handle) {
        return ESP_ERR_NO_MEM;
    }

    handle->tinyusb_initialized = false;
    *out = handle;
    return ESP_OK;
}

esp_err_t mw_uros_transport_usb_del(mw_uros_transport_usb_t *handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }

    free(handle);
    return ESP_OK;
}

esp_err_t mw_uros_transport_usb_init(const tinyusb_config_t *tinyusb_config)
{
    static bool tinyusb_initialized = false;

    if (tinyusb_initialized) {
        return ESP_OK;
    }

    if (tinyusb_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = tinyusb_driver_install(tinyusb_config);

    if (ret == ESP_OK) {
        tinyusb_initialized = true;
    }

    return ret;
}
