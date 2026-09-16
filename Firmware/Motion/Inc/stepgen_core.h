/**
 * @file    stepgen_core.h
 * @brief   Portable, hardware-independent STEP/DIR waveform generator.
 *
 * Turns a stream of motion segments into the exact sequence of 32-bit
 * GPIOA BSRR words the DMA will push, one word per base tick, plus the
 * armed-direction bookkeeping ADR-006 requires. It contains no STM32, HAL,
 * CMSIS or register knowledge, which is what makes it unit-testable on a
 * host and what keeps the Phase 2/3 network layers independent of it.
 *
 * Waveform contract (Docs/MOTION-ENGINE.md §4, §5, §7, §10):
 *
 *   - Every word written to GPIOA->BSRR is
 *         (set bits of the axes stepping this tick)
 *       | (reset bits of all five STEP pins, in BSRR[31:16])
 *     BSRR gives BS priority over BR for the same pin, so one write both
 *     raises the stepping axes and lowers the rest. The waveform is
 *     self-clearing: a pulse is exactly one tick and cannot stretch or
 *     latch high if the CPU stalls.
 *
 *   - Rate magnitude is clamped to 2^31 (= tick/2), which makes two
 *     consecutive stepping ticks impossible, so every pulse is followed by
 *     at least one low tick: 250 ns high / 250 ns low at the 2 MHz ceiling.
 *
 *   - All five STEP pins are on GPIOA (ADR-005), so a multi-axis tick is a
 *     single bus cycle with no cross-port skew whatsoever.
 *
 * DIR (ADR-006) is NOT in the DMA stream. A reversal is *armed* while the
 * target half is being filled, and the GPIOD write happens at *play time*,
 * as the first action of the boundary interrupt for that half. The first
 * STEPGEN_DIR_GUARD_TICKS ticks of the target half are reserved idle for
 * the reversing axis, which is what buys the setup margin.
 */
#ifndef STEPGEN_CORE_H
#define STEPGEN_CORE_H

#include "motion_types.h"
#include "motion_segment_queue.h"

typedef struct {
    uint32_t phase;             /**< DDA accumulator                        */
    uint32_t inc;               /**< |rate|, <= STEPGEN_RATE_MAX_Q32        */
    int8_t   dir_sign;          /**< -1 / 0 / +1, commanded motion sign     */
    uint8_t  dir_level;         /**< level currently on the DIR pin         */
    uint8_t  dir_known;         /**< a DIR level has been committed once    */
    uint8_t  armed;             /**< a reversal is armed (queue depth 1)    */
    uint8_t  armed_level;       /**< level to write at the target half      */
    uint32_t armed_half;        /**< logical half index this is armed for   */
    uint32_t block_until_tick;  /**< no STEP before this absolute tick      */
    int64_t  pos_planned;       /**< steps written into the ring (ADR-009)  */
    int64_t  pos_output;        /**< steps the DMA has actually emitted     */
    int32_t  steps_in_half[2];  /**< staged step delta per physical half    */
} stepgen_axis_t;

typedef struct {
    /* Static configuration ------------------------------------------- */
    stepgen_axis_config_t   axis_cfg[MOTION_AXIS_COUNT];
    uint16_t                step_mask;
    uint16_t                dir_mask;
    uint32_t                guard_ticks;
    bool                    cfg_ok;

    /* DMA ring (owned by the port) ----------------------------------- */
    uint32_t               *step_buf;       /**< [ring_ticks] GPIOA BSRR    */
    uint32_t                ring_ticks;
    uint32_t                half_ticks;

    /* Runtime state ---------------------------------------------------- */
    stepgen_axis_t          axis[MOTION_AXIS_COUNT];
    motion_segment_queue_t *queue;
    motion_segment_t        cur_seg;
    uint32_t                seg_remaining;
    bool                    seg_valid;
    /* A segment popped but not yet applied, because applying it would
     * overwrite a DIR reversal that has been armed and not yet realized.
     * ADR-006 gives each axis an armed slot of depth one, so a second
     * reversal waits for the first rather than replacing it. */
    motion_segment_t        seg_pending;
    bool                    seg_pending_valid;
    uint32_t                fill_half_index; /**< logical index being filled */
    uint32_t                play_half_index; /**< logical index now playing  */
    uint32_t                faults;
    uint32_t                starved_ticks;
    uint32_t                segments_consumed;
    uint32_t                last_seq;
    bool                    moving;
    bool                    starved_now;
} stepgen_core_t;

void stepgen_core_init(stepgen_core_t              *c,
                       const stepgen_axis_config_t *axis_cfg,
                       motion_segment_queue_t      *queue,
                       uint32_t                    *step_buf,
                       uint32_t                     ring_ticks);

bool stepgen_core_validate_segment(const motion_segment_t *seg);

/** Reset the logical half counters. Only legal while stopped. */
void stepgen_core_rewind(stepgen_core_t *c);

/**
 * Fill the physical half @p phalf, which will play as logical half
 * @p logical_index. Must complete before the DMA reaches it.
 */
void stepgen_core_fill_half(stepgen_core_t *c, uint32_t phalf,
                            uint32_t logical_index);

/** Commit the step counts staged for a physical half once it has played. */
void stepgen_core_commit_half(stepgen_core_t *c, uint32_t phalf);

/**
 * ADR-006 play-time step. Returns true and sets @p bsrr if any axis is
 * armed for logical half @p logical_index, i.e. if GPIOD must be written
 * now. Called as the FIRST action of the boundary interrupt.
 */
bool stepgen_core_take_dir_write(stepgen_core_t *c, uint32_t logical_index,
                                 uint32_t *bsrr);

/** Write the idle (all STEP low) pattern over the whole ring. */
void stepgen_core_blank(stepgen_core_t *c);

/** True while a reversal is armed and not yet written to GPIOD. */
bool stepgen_core_reversal_armed(const stepgen_core_t *c);

/** Immediate stop: drop the queue, zero all rates, blank the ring. */
void stepgen_core_abort(stepgen_core_t *c, uint32_t fault_bits);

/** BSRR word that clears every STEP pin. */
uint32_t stepgen_core_step_idle_word(const stepgen_core_t *c);

/** BSRR word asserting the current DIR levels of all five axes. */
uint32_t stepgen_core_dir_word(const stepgen_core_t *c);

#endif /* STEPGEN_CORE_H */
