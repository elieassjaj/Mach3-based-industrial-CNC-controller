/**
 * @file    io_spindle.h
 * @brief   Spindle PWM duty control (M10).
 *
 * `Docs/FIRMWARE-ARCHITECTURE.md` §22 requires the spindle to be a separate
 * subsystem from the motion pulse generator, and leaves frequency, duty
 * range, scaling, command source and update mechanism to be derived from
 * the project requirements and the Mach3 SDK. ADR-016 records what was
 * derived; the short version:
 *
 *  - **Frequency** 10 kHz, fixed by `Docs/PINOUT.md`.
 *  - **Duty range** 0..1000 per mille, which is the protocol's
 *    `spindle_pmille` and a direct quantisation of the SDK's own
 *    `MainPlanner->Spindle.ratio` (0..1, `SDK/ncPod/MachDevImplementation.cpp`).
 *  - **Scaling** none. RPM never reaches this device. The host owns the
 *    RPM<->ratio map (`SpindleFM::SetSpindleSpeed`) because it is the only
 *    side that knows what spindle is fitted.
 *  - **Command source** the protocol's `OUTPUTS` opcode.
 *  - **Update mechanism** a compare-register write with the timer's
 *    preload enabled, so a duty change lands at a period boundary and
 *    never produces a short or stretched pulse.
 *
 * The safe state is 0 % duty with the generator stopped, and it is enforced
 * by `io_poll()` and by `io_emergency_off()` in `io_outputs.h` - not here.
 *
 * Portable C: no STM32, HAL or CMSIS dependency.
 */
#ifndef IO_SPINDLE_H
#define IO_SPINDLE_H

#include "io_types.h"

/**
 * Command the duty, in per mille (0..1000).
 *
 * @return false if @p pmille exceeds 1000. Out of range is refused, never
 *         clamped: a host that asked for 150 % has a bug, and quietly
 *         giving it 100 % hides that bug behind a spinning tool.
 *
 * A true return means the request was accepted, not that the spindle is
 * turning - the interlock may be holding it at zero.
 *
 * 0 is a legitimate command and means 0 % duty, not "leave unchanged". The
 * protocol spells "leave unchanged" as 0xFFFF and that is resolved by the
 * session layer before this is called.
 *
 * Note that the SDK's own ncPod plugin never sends a zero duty while the
 * spindle is on (`if (vel == 0) vel = 1;`). That is a host-side policy and
 * is deliberately not replicated here: when this device is told zero, it
 * means zero.
 */
bool io_spindle_set_pmille(uint16_t pmille);

/** What the host last asked for, whether or not it was applied. */
uint16_t io_spindle_requested(void);

/** What the timer is actually generating. This is what STATUS carries. */
uint16_t io_spindle_actual(void);

/* ---------------------------------------------------- internal ---------- */
/* Called by io_poll()/io_emergency_off() in io_outputs.c. Not host API. */

/** Push the requested duty to the hardware, or zero if @p inhibited. */
void io_spindle_apply(bool inhibited);

/** Zero the duty and stop the generator. Interrupt-safe. */
void io_spindle_force_off(void);

/** Clear state to power-on. Called by io_init(). */
void io_spindle_reset(void);

#endif /* IO_SPINDLE_H */
