/**
 * @file    safety_input.h
 * @brief   Digital-input manager and E-STOP path (M3).
 *
 * The only interface the rest of the firmware may use to ask about the 15
 * inputs on PE0..PE14. Nothing above this line touches GPIOE, EXTI or the
 * NVIC.
 *
 *      PE0..PE14 ──EXTI──► port ISR ──► safety_input_on_*_edge()
 *                                              │
 *      superloop ──────────────────────► safety_input_poll()
 *                                              │
 *                          filtered state ─────┴──► protocol layer, host
 *
 * Two paths, deliberately unequal (Docs/FIRMWARE-ARCHITECTURE.md §7-§8,
 * ADR-015):
 *
 *  - E-STOP (PE2) is acted on inside the interrupt, at NVIC priority 0,
 *    with no filtering and no dependence on the superloop, the network or
 *    the motion engine's state. It calls stepgen_emergency_stop(), which
 *    is register-writes-first and documented interrupt-safe.
 *  - The other 14 inputs only capture state. Deciding what an input MEANS
 *    is host-side work (Docs/MACH3-INTERFACE.md §4, ADR-010) - this module
 *    reports them accurately and promptly and assigns them no semantics.
 *
 * The module is level-based, never edge-count-based: every ISR re-reads the
 * port. A missed or coalesced edge therefore costs nothing, which matters
 * because five of the six input vectors are shared between lines.
 *
 * Portable C: no STM32, HAL, CMSIS or lwIP dependency. It reaches the
 * motion engine only through stepgen.h, the same facade the protocol layer
 * uses, so the dependency runs Safety -> Motion and never back.
 */
#ifndef SAFETY_INPUT_H
#define SAFETY_INPUT_H

#include "safety_types.h"

/**
 * Initialise the port, sample the initial state of all 15 inputs, register
 * the E-STOP release interlock with the motion engine, and then arm the
 * EXTI lines.
 *
 * The order matters. Startup safety (§32) requires that an E-STOP which is
 * ALREADY asserted at power-on be honoured: if PE2 reads asserted here, the
 * engine is driven to EMERGENCY_STOP before interrupts are ever armed,
 * rather than waiting for an edge that has already happened and will not
 * happen again.
 *
 * Call after stepgen_init().
 */
bool safety_input_init(void);

/**
 * Periodic work: the debounce filter, the E-STOP release timer and the
 * chatter counters. Call from the superloop as often as convenient; the
 * filter is expressed in milliseconds, so calling it faster only improves
 * the resolution of the timestamps.
 */
void safety_input_poll(uint32_t now_ms);

/* ------------------------------------------------------ ISR entries ---- */
/* Both are called from interrupt context by the port. Both are wait-free
 * and neither allocates, blocks or calls back into the network stack.     */

/**
 * EXTI2 / PE2. Acts on an assertion immediately - this call IS the
 * emergency-stop path, and everything it does before returning is what
 * HV-05 and HV-43 measure. A release is recorded but never acted on here:
 * un-stopping a machine is not an interrupt's decision.
 */
void safety_input_on_estop_edge(void);

/** Any other input EXTI vector. Captures state only. */
void safety_input_on_edge(void);

/* --------------------------------------------------------- readers ---- */

/**
 * Debounced input word, bit n = PEn, 1 = ASSERTED (pin LOW).
 *
 * This is exactly what Docs/PROTOCOL.md §6's `inputs` field carries, so
 * the protocol layer forwards it unchanged.
 */
uint16_t safety_inputs(void);

/** Raw pin levels, bit n = PEn, 1 = HIGH. Diagnostics and self-tests. */
uint16_t safety_inputs_raw_level(void);

/**
 * Filtered assertions that arrived since the last call, read-and-clear.
 * For a consumer that wants edges without keeping its own copy.
 */
uint16_t safety_input_take_changed(void);

/** True while E-STOP is asserted, or released but not yet stable. */
bool safety_estop_asserted(void);

/**
 * True only once PE2 has read idle continuously for
 * SAFETY_ESTOP_RELEASE_MS. This is the interlock behind ADR-010's rule
 * that EMERGENCY_STOP may only be cleared once the physical input has
 * actually been released; safety_input_init() registers it with the motion
 * engine so no packet and no call path can bypass it.
 */
bool safety_estop_released(void);

/** False until safety_input_init() has succeeded. Drives the protocol's
 *  INPUTS_PRESENT flag, so a host can tell "nothing asserted" from "this
 *  firmware cannot report inputs" - Docs/PROTOCOL.md §6.2.               */
bool safety_input_present(void);

void safety_input_get_status(safety_status_t *out);

/**
 * Clear the sticky diagnostics (`ever_asserted`, `estop_latched`,
 * `chattering`). Refused while E-STOP is still asserted, so the latch
 * cannot be wiped while the condition that set it is live.
 */
bool safety_input_clear_latches(void);

#endif /* SAFETY_INPUT_H */
