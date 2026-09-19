/**
 * @file    io_types.h
 * @brief   Shared types for the CNC5AX-ETH output subsystem (M9 / M10).
 *
 * No STM32 / HAL / CMSIS dependency, so the safety interlocks and the duty
 * arithmetic can be compiled and unit-tested on a host.
 */
#ifndef IO_TYPES_H
#define IO_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#include "cnc_io_config.h"

/**
 * Why the outputs are currently forced off, if they are.
 *
 * This is reported rather than inferred so a host - and an operator at a
 * bench - can tell "the relay is off because nobody asked for it" from
 * "the relay is off because the machine is in an emergency stop and your
 * request was refused".
 */
typedef enum {
    IO_INHIBIT_NONE       = 0u,
    /** E-stop latched. Cleared only by the ADR-010 explicit-clear path.  */
    IO_INHIBIT_ESTOP      = (1u << 0),
    /** The motion engine is in FAULT. ADR-016: outputs go off for every
     *  fault class, including the hold-position ones. */
    IO_INHIBIT_FAULT      = (1u << 1),
    /** Not yet out of SAFE_IDLE - startup safety, §32. */
    IO_INHIBIT_NOT_READY  = (1u << 2)
} io_inhibit_t;

/** LED pattern, derived from the engine state rather than commanded. */
typedef enum {
    IO_LED_OFF = 0,
    IO_LED_ON,
    IO_LED_BLINK_SLOW,
    IO_LED_BLINK_FAST
} io_led_pattern_t;

/** Snapshot of the output subsystem, for diagnostics and host feedback. */
typedef struct {
    /* --- M9 ---------------------------------------------------------- */
    uint16_t out_requested;    /**< what the host last asked for          */
    uint16_t out_actual;       /**< what the pins are actually doing      */
    uint32_t inhibit;          /**< io_inhibit_t bits, 0 when free to act */
    uint32_t inhibited_count;  /**< requests refused because of inhibit   */

    io_led_pattern_t led_run;
    io_led_pattern_t led_err;

    /* --- M10 --------------------------------------------------------- */
    uint16_t spindle_requested;/**< per mille, as commanded               */
    uint16_t spindle_actual;   /**< per mille, as the timer is programmed */
    uint32_t spindle_ccr;      /**< the compare value actually loaded     */
    bool     spindle_running;  /**< the PWM generator is enabled          */

    bool     present;          /**< io_init() has succeeded               */
} io_status_t;

#endif /* IO_TYPES_H */
