/**
 * @file    motion_types.h
 * @brief   Shared data types for the CNC5AX-ETH motion subsystem.
 *
 * No STM32 / HAL / CMSIS dependency, so the motion core can be compiled and
 * unit-tested on a host.
 */
#ifndef MOTION_TYPES_H
#define MOTION_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#include "cnc_motion_config.h"

/**
 * Per-axis rate as a 32-bit DDA accumulator increment (ADR-007, step domain):
 *
 *      step_rate_hz = |rate_q32| * STEPGEN_TICK_HZ / 2^32
 *
 * Clamped to 2^31 = STEPGEN_TICK_HZ / 2 = 2 MHz. That clamp is what
 * guarantees no two consecutive ticks can both carry a STEP edge, hence a
 * one-tick pulse with at least a one-tick gap. Sign selects direction.
 */
typedef int64_t motion_rate_q32_t;

#define STEPGEN_RATE_MAX_Q32        ((motion_rate_q32_t)0x80000000LL)

/**
 * A motion segment: constant commanded rate per axis for a fixed number of
 * base ticks. Step-domain and 5-axis per ADR-007 - all mm<->step conversion
 * and GMoves decomposition happens in the PC-side plugin.
 *
 * NOTE: a segment boundary and a DMA half-boundary are unrelated (ADR-006
 * terminology); a segment generally spans many halves.
 */
typedef struct {
    uint32_t          duration_ticks;               /**< > 0                */
    motion_rate_q32_t rate[MOTION_AXIS_COUNT];      /**< signed Q32 per tick*/
    uint32_t          seq;                          /**< host sequence id   */
} motion_segment_t;

/** Per-axis static configuration. All STEP pins are on GPIOA (ADR-005). */
typedef struct {
    uint16_t step_bit;      /**< bit mask within GPIOA BSRR set half        */
    uint16_t dir_bit;       /**< bit mask within GPIOD BSRR set half        */
    bool     dir_invert;    /**< false: positive motion => DIR HIGH         */
} stepgen_axis_config_t;

/**
 * Fault bits. ADR-010 splits these into two classes, because the two get
 * opposite drive-enable treatment:
 *
 *  - COMMAND-STREAM faults (queue starvation, comm timeout): hold position,
 *    keep EN asserted. Confirmed by the project owner precisely so a paused
 *    link never de-energises a stepper and lets an axis drift or drop.
 *  - INTEGRITY faults (DMA/timer fault, underrun, bad config): deassert EN,
 *    because the system can no longer trust its own step generation.
 */
typedef enum {
    STEPGEN_FAULT_NONE            = 0u,
    /** DMA reached a half the engine had not refilled. Stale BSRR words
     *  would have replayed as uncommanded motion. INTEGRITY. */
    STEPGEN_FAULT_BUFFER_UNDERRUN = (1u << 0),
    /** Segment queue ran dry while an axis had a non-zero rate.
     *  COMMAND-STREAM: hold position, drives stay enabled (ADR-010). */
    STEPGEN_FAULT_SEGMENT_STARVED = (1u << 1),
    /** A submitted segment failed validation. INTEGRITY. */
    STEPGEN_FAULT_BAD_SEGMENT     = (1u << 2),
    /** Port-level hardware fault: DMA transfer error, or the stream and the
     *  timer disagreeing about where playback is. INTEGRITY. */
    STEPGEN_FAULT_HW              = (1u << 3),
    /** Axis map contradicts Docs/PINOUT.md. INTEGRITY. */
    STEPGEN_FAULT_CONFIG          = (1u << 4)
} stepgen_fault_t;

/** Faults that must deassert EN. Everything else holds position. */
#define STEPGEN_FAULTS_INTEGRITY  (STEPGEN_FAULT_BUFFER_UNDERRUN | \
                                   STEPGEN_FAULT_BAD_SEGMENT     | \
                                   STEPGEN_FAULT_HW              | \
                                   STEPGEN_FAULT_CONFIG)

/**
 * Engine state (ADR-010). BOOT/INIT belong to the system-level state
 * machine; the engine exposes the subset it owns.
 *
 *   SAFE_IDLE -> READY <-> RUNNING
 *   any -> FAULT            (recoverable only by an explicit clear)
 *   any -> EMERGENCY_STOP   (latching, structurally distinct from FAULT so
 *                            a generic fault-clear can never clear it)
 */
typedef enum {
    STEPGEN_STATE_UNINIT = 0,
    STEPGEN_STATE_SAFE_IDLE,
    STEPGEN_STATE_READY,
    STEPGEN_STATE_RUNNING,
    STEPGEN_STATE_FAULT,
    STEPGEN_STATE_EMERGENCY_STOP
} stepgen_state_t;

/** Snapshot of engine status for diagnostics and host feedback. */
typedef struct {
    stepgen_state_t state;
    uint32_t        faults;
    int64_t         pos_output[MOTION_AXIS_COUNT];  /**< steps emitted, ADR-009 */
    int64_t         pos_planned[MOTION_AXIS_COUNT]; /**< steps placed in the ring */
    uint32_t        segments_consumed;
    uint32_t        queue_free;
    uint32_t        starved_ticks;
    uint32_t        underruns;
    uint32_t        last_seq;
    uint32_t        fill_cycles_max;   /**< worst-case refill, CPU cycles   */
    bool            drives_enabled;
} stepgen_status_t;

#endif /* MOTION_TYPES_H */
