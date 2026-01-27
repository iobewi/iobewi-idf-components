/**
 * @file drv_vl53l0x_types.h
 * @brief Types publics pour le driver VL53L0X
 *
 * Driver bas niveau pour capteurs VL53L0X (ToF laser ranging).
 * Fournit accès I2C direct et gestion multi-capteurs via XSHUT.
 *
 * @note Conforme au CDC iobewi-idf-components
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Include ST API headers (vendor code)
#include "vl53l0x_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Adresse I2C par défaut du VL53L0X (7-bit)
 */
#define DRV_VL53L0X_I2C_ADDRESS_DEFAULT_7B  (0x29)

/**
 * @brief Device VL53L0X avec état ST API
 *
 * Représente un capteur VL53L0X déjà adressé et initialisé.
 */
typedef struct {
    uint8_t addr_7b;                  ///< Adresse I2C 7-bit
    VL53L0X_Dev_t st;                 ///< État ST API (vendor)
    bool inited;                      ///< Indique si initialisé
    bool gpio_ready_enabled;          ///< GPIO data-ready actif
    bool gpio_active_high;            ///< Polarité GPIO
    gpio_num_t int_gpio;              ///< GPIO INT (data ready)
    SemaphoreHandle_t gpio_ready_sem; ///< Sémaphore pour IRQ
} drv_vl53l0x_dev_t;

/**
 * @brief Slot pour assignation d'adresse multi-capteurs via XSHUT
 *
 * Utilisé par drv_vl53l0x_multi_assign_addresses pour assigner
 * des adresses I2C uniques à plusieurs capteurs VL53L0X.
 */
typedef struct {
    gpio_num_t xshut_gpio;   ///< GPIO XSHUT (power enable)
    uint8_t new_addr_7b;     ///< Nouvelle adresse I2C 7-bit à assigner
} drv_vl53l0x_slot_t;

#ifdef __cplusplus
}
#endif
