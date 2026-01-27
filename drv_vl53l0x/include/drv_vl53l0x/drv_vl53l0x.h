/**
 * @file drv_vl53l0x.h
 * @brief API publique du driver VL53L0X
 *
 * Driver bas niveau pour capteurs VL53L0X (ToF laser ranging).
 * Fournit accès I2C direct et gestion multi-capteurs via XSHUT.
 *
 * @note Conforme au CDC iobewi-idf-components
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#include "drv_vl53l0x/drv_vl53l0x_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================
 *  Typedef pour compatibilité
 * ========================= */

/**
 * @brief Alias pour compatibilité (utilisez drv_vl53l0x_dev_t dans nouveau code)
 * @deprecated Utilisez drv_vl53l0x_dev_t
 */
typedef drv_vl53l0x_dev_t vl53l0x_dev_t;

/**
 * @brief Alias pour compatibilité (utilisez drv_vl53l0x_slot_t dans nouveau code)
 * @deprecated Utilisez drv_vl53l0x_slot_t
 */
typedef drv_vl53l0x_slot_t vl53l0x_slot_t;

/**
 * @brief Alias pour compatibilité (utilisez DRV_VL53L0X_I2C_ADDRESS_DEFAULT_7B dans nouveau code)
 * @deprecated Utilisez DRV_VL53L0X_I2C_ADDRESS_DEFAULT_7B
 */
#define VL53L0X_I2C_ADDRESS_DEFAULT_7B  DRV_VL53L0X_I2C_ADDRESS_DEFAULT_7B

/* =========================
 *  I2C – initialization (new driver)
 * ========================= */

/**
 * @brief Initializes the I2C master bus (new ESP-IDF driver).
 *
 * Call once before any sensor usage.
 *
 * @param sda SDA GPIO.
 * @param scl SCL GPIO.
 * @param clk_hz I2C frequency (typically 400000).
 */
esp_err_t drv_vl53l0x_i2c_init(gpio_num_t sda,
                               gpio_num_t scl,
                               uint32_t clk_hz);

/**
 * @brief Probes an I2C address (ACK/NACK).
 *
 * @param addr_7b 7-bit address.
 */
esp_err_t drv_vl53l0x_probe(uint8_t addr_7b);

/* =========================
 *  I2C – primitives used by the ST platform layer
 * ========================= */

/**
 * @brief I2C register write (used by the ST API).
 */
esp_err_t drv_vl53l0x_write_reg(uint8_t addr_7b,
                                uint8_t reg,
                                const uint8_t *data,
                                size_t len,
                                uint32_t clk_hz);

/**
 * @brief I2C register read (used by the ST API).
 */
esp_err_t drv_vl53l0x_read_reg(uint8_t addr_7b,
                               uint8_t reg,
                               uint8_t *data,
                               size_t len,
                               uint32_t clk_hz);

/* =========================
 *  Multi-sensor (XSHUT)
 * ========================= */

/**
 * @brief Assigns unique I2C addresses to multiple VL53L0X sensors.
 *
 * All sensors are held in XSHUT low, then enabled one by one:
 *  - release XSHUT
 *  - use default address 0x29
 *  - call VL53L0X_SetDeviceAddress()
 *
 * @param slots Sensor slot array.
 * @param slot_count Number of sensors.
 * @param boot_delay_ms Delay after XSHUT release (typ. 2–10 ms).
 */
esp_err_t drv_vl53l0x_multi_assign(const drv_vl53l0x_slot_t *slots,
                                   int slot_count,
                                   uint32_t boot_delay_ms);

/* =========================
 *  Sensor – high-level API
 * ========================= */

/**
 * @brief Initializes an already-addressed VL53L0X sensor.
 *
 * @param dev Sensor handle.
 * @param timing_budget_us Measurement budget in microseconds (e.g., 33000).
 */
esp_err_t drv_vl53l0x_init(drv_vl53l0x_dev_t *dev,
                           uint32_t timing_budget_us);

/**
 * @brief Performs a distance measurement (mm).
 *
 * @param dev Sensor handle.
 * @param out_mm Measured distance in millimeters.
 */
esp_err_t drv_vl53l0x_read(drv_vl53l0x_dev_t *dev,
                           uint16_t *out_mm);

/**
 * @brief Enables the GPIO "data ready" mode (GPIO/INT).
 *
 * Configures the sensor via the ST API to signal "new measure ready"
 * and sets up the ESP-IDF ISR/queue for event waiting.
 *
 * @param dev Sensor handle.
 * @param int_gpio ESP-IDF GPIO connected to the VL53L0X GPIO/INT pin.
 * @param active_high True if the signal is active high.
 */
esp_err_t drv_vl53l0x_enable_gpio_ready(drv_vl53l0x_dev_t *dev,
                                        gpio_num_t int_gpio,
                                        bool active_high);

/**
 * @brief Waits for a GPIO "data ready" edge.
 *
 * @param dev Sensor handle.
 * @param timeout FreeRTOS timeout (ticks).
 */
esp_err_t drv_vl53l0x_wait_gpio_ready(drv_vl53l0x_dev_t *dev,
                                      TickType_t timeout);

/* =========================
 *  Backward compatibility aliases
 * ========================= */

#define vl53l0x_i2c_master_init drv_vl53l0x_i2c_init
#define vl53l0x_i2c_probe drv_vl53l0x_probe
#define vl53l0x_i2c_write_reg drv_vl53l0x_write_reg
#define vl53l0x_i2c_read_reg drv_vl53l0x_read_reg
#define vl53l0x_multi_assign_addresses drv_vl53l0x_multi_assign
#define vl53l0x_init drv_vl53l0x_init
#define vl53l0x_read_mm drv_vl53l0x_read
#define vl53l0x_enable_gpio_ready drv_vl53l0x_enable_gpio_ready
#define vl53l0x_wait_gpio_ready drv_vl53l0x_wait_gpio_ready

#ifdef __cplusplus
}
#endif
