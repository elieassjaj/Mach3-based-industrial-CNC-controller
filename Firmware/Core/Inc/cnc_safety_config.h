/**
 * @file    cnc_safety_config.h
 * @brief   CNC5AX-ETH safety / digital-input subsystem configuration (M3).
 *
 * Values are tagged the same way as cnc_motion_config.h:
 *   FIXED    - mandated by Docs/PINOUT.md or Docs/FIRMWARE-ARCHITECTURE.md
 *   ADR      - set by a frozen Architecture Decision Record
 *   OPEN     - a documented default, not a decision; tune against real
 *              hardware and record the result
 */
#ifndef CNC_SAFETY_CONFIG_H
#define CNC_SAFETY_CONFIG_H

/* ---------------------------------------------------------- inputs ----- */
/* FIXED, Docs/PINOUT.md: 15 active-low inputs on PE0..PE14, external
 * pull-ups fitted on the board, so firmware must use GPIO_NOPULL and must
 * read HIGH as idle. Both edges are captured (GPIO_MODE_IT_RISING_FALLING)
 * so the firmware tracks each input's CURRENT STATE, not just assertions. */
#define SAFETY_INPUT_COUNT          15u
#define SAFETY_INPUT_MASK           0x7FFFu        /* PE0..PE14            */

/* FIXED, Docs/PINOUT.md + FIRMWARE-ARCHITECTURE §8: PE2 is E-STOP and has
 * its own EXTI vector at NVIC priority 0 (ADR-004).                       */
#define SAFETY_ESTOP_INDEX          2u             /* PE2                  */
#define SAFETY_ESTOP_MASK           (1u << SAFETY_ESTOP_INDEX)

/* FIXED: inputs are ACTIVE LOW. A pin reading LOW means the signal is
 * asserted. Every bitfield this subsystem publishes is normalised to
 * "1 = asserted" so no consumer has to know the electrical polarity - see
 * ADR-015 and Docs/PROTOCOL.md §6.                                        */
#define SAFETY_INPUT_ACTIVE_LOW     1

/* -------------------------------------------------------- debounce ----- */
/* OPEN (ADR-015). Docs/FIRMWARE-ARCHITECTURE.md §20 requires the
 * implementation to decide whether filtering is needed and leaves the
 * value open. No switch datasheet exists in this repository, so these are
 * documented defaults chosen to sit above typical mechanical bounce
 * (~1 ms) without adding noticeable command latency - NOT measured values.
 * HV-42 is the test that replaces them with measured ones.
 *
 * The filter is a stable-for-T filter evaluated on the millisecond tick,
 * so the effective window is [T, T+1] ms given the 1 ms timebase.         */
#ifndef SAFETY_DEBOUNCE_MS
#define SAFETY_DEBOUNCE_MS          3u
#endif

/* E-STOP is deliberately asymmetric and this asymmetry IS the safety
 * property (ADR-015):
 *
 *   assertion  - acted on immediately, in the EXTI2 ISR, with no filtering
 *                at all. A filter here would add latency to the one path
 *                §8 requires to be the fastest in the system.
 *   release    - only accepted after the input has read idle continuously
 *                for SAFETY_ESTOP_RELEASE_MS, so a bouncing or intermittent
 *                E-STOP contact can never present itself as "released" and
 *                unlock the machine.
 *
 * Longer than SAFETY_DEBOUNCE_MS on purpose: releasing an E-STOP unlocks a
 * machine, and nothing about that is time-critical.                       */
#ifndef SAFETY_ESTOP_RELEASE_MS
#define SAFETY_ESTOP_RELEASE_MS     50u
#endif

/* ------------------------------------------------------ diagnostics ---- */
/* OPEN. An input that produces more than this many edges inside one
 * second is reported as chattering. It does not change the filtered state
 * - the debounce already handles that - it exists so a failing switch or a
 * noisy cable is visible instead of silently eating CPU in an ISR.        */
#ifndef SAFETY_CHATTER_EDGES_PER_S
#define SAFETY_CHATTER_EDGES_PER_S  200u
#endif

#endif /* CNC_SAFETY_CONFIG_H */
