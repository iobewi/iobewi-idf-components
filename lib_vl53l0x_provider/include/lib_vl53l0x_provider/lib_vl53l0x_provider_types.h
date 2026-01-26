/**
 * @file lib_vl53l0x_provider_types.h
 * @brief Types publics pour le provider VL53L0X multi-capteurs
 *
 * Ce composant fournit une abstraction de haut niveau pour gérer plusieurs
 * capteurs VL53L0X en parallèle avec lectures atomiques (lock-free snapshot).
 *
 * @note Conforme au CDC iobewi-idf-components
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handle opaque du provider VL53L0X
 */
typedef struct lib_vl53l0x_provider_s lib_vl53l0x_provider_t;

/**
 * @brief Échantillon ToF d'un capteur
 */
typedef struct {
    bool valid;         ///< true si la mesure est valide
    uint8_t status;     ///< 0 = OK (ST style), sinon invalide
    float range_m;      ///< Distance en mètres (NAN si invalide)
    uint32_t seq;       ///< Numéro de séquence pour synchronisation lock-free
} lib_vl53l0x_sample_t;

/**
 * @brief Configuration du bus I2C partagé par tous les capteurs
 */
typedef struct {
    gpio_num_t sda_gpio;              ///< GPIO SDA
    gpio_num_t scl_gpio;              ///< GPIO SCL
    uint32_t i2c_freq_hz;             ///< Fréquence I2C (typiquement 400kHz)
    uint32_t timing_budget_us;        ///< Budget temps par mesure (microseconds)
    uint32_t gpio_ready_timeout_ms;   ///< Timeout GPIO data-ready (ms)
} lib_vl53l0x_bus_config_t;

/**
 * @brief Configuration hardware d'un capteur VL53L0X
 */
typedef struct {
    gpio_num_t xshut_gpio;   ///< GPIO XSHUT (power enable)
    gpio_num_t int_gpio;     ///< GPIO INT (data ready IRQ)
    uint8_t addr_7b;         ///< Adresse I2C 7-bit après assignment
    uint8_t bin_idx;         ///< Index de bin LaserScan (pour mapping)
} lib_vl53l0x_hw_config_t;

/**
 * @brief Configuration du provider VL53L0X
 */
typedef struct {
    const lib_vl53l0x_bus_config_t *bus_config;   ///< Configuration bus I2C (non NULL)
    const lib_vl53l0x_hw_config_t *hw_configs;    ///< Tableau de configs capteurs (non NULL)
    uint8_t sensor_count;                         ///< Nombre de capteurs (1-255)
} lib_vl53l0x_provider_config_t;

/**
 * @brief Configuration pour lecture snapshot
 */
typedef struct {
    TickType_t timeout_ticks;         ///< Timeout pour spin-wait
    uint32_t max_spins;               ///< Nombre max de tentatives
    uint32_t odd_yield_threshold;     ///< Seuil pour yield (évite spin infini)
    uint8_t timeout_status;           ///< Status à retourner en cas de timeout
    TickType_t log_interval_ticks;    ///< Intervalle minimum entre logs timeout
} lib_vl53l0x_snapshot_config_t;

/**
 * @brief État de logging pour snapshot
 */
typedef struct {
    portMUX_TYPE log_mux;             ///< Spinlock pour logs
    uint32_t timeout_count;           ///< Compteur de timeouts
    TickType_t timeout_last_log_tick; ///< Dernier log timeout
} lib_vl53l0x_snapshot_log_t;

#ifdef __cplusplus
}
#endif
