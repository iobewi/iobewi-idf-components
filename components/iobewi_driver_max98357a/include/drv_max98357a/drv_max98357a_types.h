#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/i2s_std.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handle opaque du driver MAX98357A.
 */
typedef struct drv_max98357a_s drv_max98357a_t;

/**
 * @brief Gain du MAX98357A (configuré via GPIO GAIN slot)
 *
 * Le gain est configuré selon le niveau du signal pendant le time slot GAIN
 * du format I2S. En mode standalone (pas de contrôle GAIN), le gain est de 9dB.
 */
typedef enum {
    DRV_MAX98357A_GAIN_3DB = 0,    /**< Gain de 3dB */
    DRV_MAX98357A_GAIN_6DB,         /**< Gain de 6dB */
    DRV_MAX98357A_GAIN_9DB,         /**< Gain de 9dB (défaut) */
    DRV_MAX98357A_GAIN_12DB,        /**< Gain de 12dB */
    DRV_MAX98357A_GAIN_15DB,        /**< Gain de 15dB */
} drv_max98357a_gain_t;

/**
 * @brief Configuration du driver MAX98357A
 */
typedef struct {
    // Pins I2S
    gpio_num_t bclk_gpio;           /**< GPIO pour I2S bit clock (BCLK) */
    gpio_num_t ws_gpio;             /**< GPIO pour I2S word select (LRCLK/WS) */
    gpio_num_t dout_gpio;           /**< GPIO pour I2S data out (DIN du MAX98357A) */

    // Pin de contrôle
    gpio_num_t sd_mode_gpio;        /**< GPIO pour SD_MODE (shutdown), -1 si non utilisé */

    // Configuration I2S
    uint32_t sample_rate;           /**< Taux d'échantillonnage (ex: 16000, 44100, 48000 Hz) */
    i2s_data_bit_width_t bits_per_sample; /**< Bits par échantillon (16, 24, 32) */
    i2s_slot_mode_t slot_mode;      /**< Mode mono/stéréo (I2S_SLOT_MODE_MONO ou I2S_SLOT_MODE_STEREO) */

    // Gain (optionnel, nécessite configuration matérielle spéciale)
    drv_max98357a_gain_t gain;      /**< Gain de l'amplificateur (9dB par défaut) */

    // Buffer I2S
    uint32_t dma_buf_count;         /**< Nombre de buffers DMA (recommandé: 6) */
    uint32_t dma_buf_len;           /**< Taille d'un buffer DMA en échantillons (recommandé: 512) */
} drv_max98357a_config_t;

/**
 * @brief Configuration par défaut du MAX98357A
 *
 * Configuration standard:
 * - 16 kHz, 16 bits, mono
 * - 6 buffers DMA de 512 échantillons
 * - Gain 9dB
 * - SD_MODE non utilisé (GPIO_NUM_NC)
 */
#define DRV_MAX98357A_CONFIG_DEFAULT() { \
    .bclk_gpio = GPIO_NUM_NC, \
    .ws_gpio = GPIO_NUM_NC, \
    .dout_gpio = GPIO_NUM_NC, \
    .sd_mode_gpio = GPIO_NUM_NC, \
    .sample_rate = 16000, \
    .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT, \
    .slot_mode = I2S_SLOT_MODE_MONO, \
    .gain = DRV_MAX98357A_GAIN_9DB, \
    .dma_buf_count = 6, \
    .dma_buf_len = 512, \
}

#ifdef __cplusplus
}
#endif
