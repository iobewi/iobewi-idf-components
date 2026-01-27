#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief États applicatifs supportés par le middleware status LED
 */
typedef enum {
    LIB_STATUS_LED_OFF = 0,      /**< LED éteinte */
    LIB_STATUS_LED_WAITING,      /**< Attente connexion (bleu) */
    LIB_STATUS_LED_CONNECTED,    /**< Connecté et opérationnel (vert) */
    LIB_STATUS_LED_ERROR         /**< Erreur critique (rouge) */
} lib_status_led_state_t;

#ifdef __cplusplus
}
#endif
