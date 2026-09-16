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
