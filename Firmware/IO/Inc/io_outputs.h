/**
 * @file    io_outputs.h
 * @brief   Output Manager: relay and status LEDs (M9), and the subsystem
 *          entry points that also service the spindle (M10).
 *
 * `Docs/FIRMWARE-ARCHITECTURE.md` §21 requires the non-motion outputs to be
 * separated from the hard real-time STEP/DIR subsystem and to be reachable
 * "without allowing background software to interfere with STEP/DIR timing".
 * That is why this module exists and why it touches nothing the motion
 * engine owns: its writes are single GPIO stores and one timer compare
 * register, none of which the STEP DMA path reads.
 *
 *      host ──CONTROL/OUTPUTS──► io_request_outputs() ─┐
 *                                                      ├─► port ─► PB8/PB1/PB2
 *      superloop ──────────────► io_poll() ────────────┘         TIM3_CH1/PB4
 *                                    │
 *                                    └─ reads the engine state and enforces
 *                                       the safe state, every iteration
 *
 * Two rules shape everything here:
 *
 *  1. **A request is not a guarantee.** The host asks; the interlock
 *     decides. A relay request made while the machine is in FAULT or
 *     EMERGENCY_STOP is remembered but not obeyed, and the refusal is
 *     reported (`io_status_t.inhibit`) rather than silently swallowed.
 *  2. **Off is the safe state, and it is reached without the superloop.**
 *     `io_emergency_off()` is interrupt-safe and is registered with the
 *     E-STOP path, so a contactor drops on the PE2 edge, not one main-loop
 *     iteration later.
 *
 * Portable C: no STM32, HAL, CMSIS or lwIP dependency. It reaches the
 * motion engine only through `stepgen.h`, so the dependency runs IO ->
 * Motion and never back.
 */
#ifndef IO_OUTPUTS_H
#define IO_OUTPUTS_H

#include "io_types.h"

/**
 * Initialise the port with everything off, and register the interrupt-safe
 * kill with the safety subsystem so an E-STOP de-energises the relay and
 * the spindle without waiting for the superloop.
 *
 * Call after `stepgen_init()` and after `safety_input_init()` - the second
 * because that is what owns the E-STOP path this hooks into.
 */
bool io_init(void);

/**
 * Periodic work: read the engine state, recompute the inhibit mask, apply
 * or withdraw the requested outputs, and drive the LED patterns.
 *
 * Call from the superloop. Nothing here is hard real-time; the one thing
 * that is - de-energising on an E-STOP - does not happen here.
 */
void io_poll(uint32_t now_ms);

/* --------------------------------------------------------- M9 API ------ */

/**
 * Apply the protocol's mask/value pair (Docs/PROTOCOL.md §9).
 *
 * @param mask  which output bits this call sets
 * @param value their values
 * @return false if @p mask names a bit this firmware does not implement.
 *         Partial application is never performed: a request that is half
 *         understood is refused whole, because a host that asked for two
 *         things and got one has no way to find out which.
 *
 * A true return means the request was accepted, NOT that the pin moved -
 * the interlock may be holding it off. Read `io_get_status()` to see what
 * the pins are actually doing.
 */
bool io_request_outputs(uint16_t mask, uint16_t value);

/** What the host last asked for, whether or not it was applied. */
uint16_t io_outputs_requested(void);

/** What the pins are actually doing right now. This is what STATUS carries. */
uint16_t io_outputs_actual(void);

/**
 * Interrupt-safe emergency off: relay de-energised, spindle duty to zero,
 * register writes only. Registered with the safety subsystem by io_init();
 * also callable directly.
 *
 * It also latches the requests off, so nothing reappears at the next poll
 * merely because the host had asked for it before the stop.
 */
void io_emergency_off(void);

/** False until io_init() has succeeded. Drives the protocol's
 *  OUTPUTS_PRESENT flag, so a host can tell "everything off" from "this
 *  firmware cannot drive outputs" - Docs/PROTOCOL.md §6.2. */
bool io_present(void);

void io_get_status(io_status_t *out);

#endif /* IO_OUTPUTS_H */
