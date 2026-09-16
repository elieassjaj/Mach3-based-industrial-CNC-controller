/**
 * @file    stepgen.h
 * @brief   STEP/DIR motion engine facade.
 *
 * The only interface the rest of the firmware - and, from Phase 3, the UDP
 * protocol layer - may use. Nothing above this line touches a timer, a DMA
 * stream or a GPIO register.
 */
#ifndef STEPGEN_H
#define STEPGEN_H

#include "motion_types.h"
#include "motion_segment_queue.h"

/** Initialise engine and hardware. Ends in SAFE_IDLE with drives disabled. */
bool stepgen_init(void);

/**
 * Set the machine's maximum per-axis STEP rate, which selects the engine
 * base tick (tick = 2 x max_step_rate_hz).
 *
 * This is the one lever that trades capability for CPU. The refill cost is
 * proportional to the TICK rate, not to how fast the axes are actually
 * commanded to move: at a fixed 4 MHz tick the engine costs the same
 * whether an axis is running at 2 MHz or at 100 Hz. So a machine that
 * never needs 2 MHz should say so here and get the CPU back.
 *
 *     max rate    tick      refill cost (relative)
 *     2 MHz       4 MHz     1.00   <- the project ceiling
 *     1 MHz       2 MHz     0.50
 *     500 kHz     1 MHz     0.25
 *
 * Only exact integer dividers of the timer clock are accepted, so no
 * commanded feed rate ever carries a systematic divider error. Legal only
 * while stopped (SAFE_IDLE). Rates above MOTION_STEP_RATE_MAX_HZ are
 * rejected.
 */
bool stepgen_configure_max_rate(uint32_t max_step_rate_hz);

/** Maximum per-axis STEP rate the engine is currently configured for. */
uint32_t stepgen_max_rate_hz(void);

/** SAFE_IDLE -> READY: energise the drives. */
bool stepgen_enable_drives(void);

/** READY -> RUNNING: start the timebase. */
bool stepgen_start(void);

/** RUNNING -> READY: stop at the next tick boundary, drives stay enabled. */
void stepgen_stop(void);

/** Interrupt-safe latching emergency stop. Callable from the E-STOP handler. */
void stepgen_emergency_stop(void);

/**
 * Explicit fault clear (ADR-010: recovery is never automatic).
 * Cannot clear EMERGENCY_STOP - that is structurally distinct and needs
 * stepgen_clear_emergency_stop().
 */
bool stepgen_clear_fault(void);

/** Explicit E-STOP reset. Only legal once the physical input has released. */
bool stepgen_clear_emergency_stop(void);

/** Queue a motion segment. False if invalid, or the queue is full. */
bool stepgen_submit_segment(const motion_segment_t *seg);

/** Free slots - the protocol layer's backpressure input (ADR-008). */
uint32_t stepgen_queue_free(void);

void stepgen_get_status(stepgen_status_t *out);
uint32_t stepgen_tick_hz(void);

/** Convert a step rate in Hz to the Q32 increment. Saturates at 2 MHz. */
motion_rate_q32_t stepgen_rate_from_hz(double hz);

/** Ring-boundary worker. Invoked by the port's DMA HT/TC interrupt. */
void stepgen_on_boundary(uint32_t phalf);

/** Reported by the port when a refill deadline was provably missed. */
void stepgen_on_overrun(void);

/** Base address of the ring, for the HV-04 placement check. */
uint32_t *stepgen_debug_buffer_base(void);

#endif /* STEPGEN_H */
