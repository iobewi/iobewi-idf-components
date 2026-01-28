#pragma once

#include "esp_err.h"
#include "tinyusb.h"

#ifdef __cplusplus
extern "C" {
#endif

// Actuellement, ce composant ne définit pas de types publics complexes.
// Ce fichier est créé pour respecter la convention CDC (section 5.1).
// Les types seront ajoutés ici si nécessaire dans le futur.

/**
 * @brief Handle opaque du transport USB micro-ROS.
 */
typedef struct mw_uros_transport_usb_s mw_uros_transport_usb_t;

#ifdef __cplusplus
}
#endif
