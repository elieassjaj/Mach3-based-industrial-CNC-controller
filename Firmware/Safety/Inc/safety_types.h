/**
 * @file    safety_types.h
 * @brief   Shared data types for the CNC5AX-ETH safety / input subsystem.
 *
 * No STM32 / HAL / CMSIS dependency, so the filter and the E-STOP state
 * machine can be compiled and unit-tested on a host.
 */
#ifndef SAFETY_TYPES_H
#define SAFETY_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#include "cnc_safety_config.h"

/**
 * Snapshot of the input subsystem, for diagnostics and host feedback.
 *
 * Every bitfield here uses the SAME encoding, bit n = PEn:
 *
 *      asserted / stable / changed : 1 = the signal is ASSERTED
 *      raw_level                   : 1 = the PIN reads HIGH
 *
 * The two differ because the inputs are active low (Docs/PINOUT.md): an
 * idle input is HIGH at the pin and 0 in `asserted`. Publishing the
 * normalised form is what lets the protocol layer forward the word to
 * Mach3 without re-deriving the board's electrical polarity - ADR-015.
 */
typedef struct {
    uint16_t asserted;        /**< debounced, 1 = asserted                  */
    uint16_t raw_level;       /**< last raw pin sample, 1 = HIGH            */
    uint16_t settling;        /**< inputs whose filter has not settled yet  */
    uint16_t ever_asserted;   /**< sticky since init / clear                */
    uint16_t chattering;      /**< over SAFETY_CHATTER_EDGES_PER_S          */

    bool     estop_asserted;  /**< live, debounced-on-release only          */
    bool     estop_latched;   /**< sticky: E-STOP has fired since the clear */

    uint32_t estop_asserts;   /**< assertion events since init              */
    uint32_t estop_last_ms;   /**< timestamp of the last assertion          */
    uint32_t edges;           /**< EXTI edge deliveries since init          */
    uint32_t transitions;     /**< filtered state changes since init        */
    uint32_t last_change_ms;  /**< timestamp of the last filtered change    */
    bool     present;         /**< safety_input_init() has succeeded        */
} safety_status_t;

#endif /* SAFETY_TYPES_H */
