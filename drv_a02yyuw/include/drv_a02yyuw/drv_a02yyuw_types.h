/**
 * @file drv_a02yyuw_types.h
 * @brief Types publics pour le driver A02YYUW
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handle opaque du driver A02YYUW
 */
typedef struct drv_a02yyuw_s drv_a02yyuw_t;

/**
 * @brief Mode de fonctionnement du capteur A02YYUW
 */
typedef enum {
    DRV_A02YYUW_MODE_REALTIME = 0,   /**< Mode temps réel (RX capteur = LOW) */
    DRV_A02YYUW_MODE_PROCESSED = 1,  /**< Mode filtré (RX capteur = HIGH/float) */
} drv_a02yyuw_mode_t;

/**
 * @brief Configuration du driver A02YYUW
 */
typedef struct {
    uart_port_t uart_num;        /**< Numéro du port UART (ex: UART_NUM_1) */
    int uart_rx_gpio;            /**< GPIO pour RX UART (connecté au TX du capteur) */
    int uart_tx_gpio;            /**< GPIO pour TX UART (typiquement UART_PIN_NO_CHANGE) */
    int baudrate;                /**< Baudrate (typiquement 9600) */
    int rx_buffer_size;          /**< Taille du buffer RX (minimum 256) */

    int gpio_mode;               /**< GPIO connecté au RX du capteur (contrôle mode), -1 si non utilisé */
    bool mode_active_high;       /**< true = HIGH pour mode processed (selon datasheet) */

    const int *gpio_en_list;     /**< Liste des GPIO EN (STMPS2141STR), un par capteur */
    int sensor_count;            /**< Nombre de capteurs gérés */

    uint32_t t_mode_settle_ms;   /**< Délai de stabilisation après changement de mode (ms) */
    uint32_t t_power_up_ms;      /**< Délai après power-up d'un capteur (ms) */
    uint32_t t_power_down_ms;    /**< Délai après power-down d'un capteur (ms) */
} drv_a02yyuw_config_t;

#ifdef __cplusplus
}
#endif
