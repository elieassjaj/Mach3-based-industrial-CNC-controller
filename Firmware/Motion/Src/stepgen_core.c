/**
 * @file    stepgen_core.c
 * @brief   Portable STEP/DIR waveform generator (see stepgen_core.h).
 */
#include "stepgen_core.h"

#include <string.h>

/* The emission loop below is the only piece of this subsystem with a hard
 * deadline, and it is entirely register-resident at -O2. STM32CubeIDE's
 * Debug configuration builds at -Og, which spills three values back to the
 * stack and costs roughly 30% more per tick - margin that vanishes without
 * anything visibly failing, and only on the configuration people actually
 * debug with. This translation unit therefore pins its own optimisation
 * level instead of inheriting one. Define STEPGEN_NO_OPT_PRAGMA to opt out
 * (e.g. to single-step through the generator). */
#if defined(__GNUC__) && !defined(STEPGEN_NO_OPT_PRAGMA)
#pragma GCC optimize ("O2")
#endif

/* The emission loop shifts the five accumulator carries straight into
 * PA8..PA12 instead of OR-ing per-axis masks, which is only valid while the
 * pinout keeps those pins consecutive and in axis order X,Y,Z,A,B. */
_Static_assert(STEP_Y_PIN == STEP_X_PIN + 1u &&
               STEP_Z_PIN == STEP_X_PIN + 2u &&
               STEP_A_PIN == STEP_X_PIN + 3u &&
               STEP_B_PIN == STEP_X_PIN + 4u,
               "GPIOA STEP pins must stay consecutive in axis order "
               "X,Y,Z,A,B - see Docs/PINOUT.md, ADR-005 and emit_run()");

/* Wrap-safe tick comparison: true when a is at or after b. */
static inline bool tick_ge(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) >= 0;
}

static inline uint32_t abs_rate(motion_rate_q32_t r)
{
    return (uint32_t)((r < 0) ? -r : r);
}

/* Absolute tick at which logical half @p h starts playing. */
static inline uint32_t half_start_tick(const stepgen_core_t *c, uint32_t h)
{
    return h * c->half_ticks;
}

uint32_t stepgen_core_step_idle_word(const stepgen_core_t *c)
{
    return ((uint32_t)c->step_mask) << 16;      /* reset all, set none */
}

uint32_t stepgen_core_dir_word(const stepgen_core_t *c)
{
    uint32_t set = 0;
    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        if (c->axis[a].dir_level) {
            set |= c->axis_cfg[a].dir_bit;
        }
    }
    return set | (((uint32_t)c->dir_mask) << 16);
}

bool stepgen_core_validate_segment(const motion_segment_t *seg)
{
    if (seg->duration_ticks == 0u) {
        return false;
    }
    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        const motion_rate_q32_t r = seg->rate[a];
        if (r > STEPGEN_RATE_MAX_Q32 || r < -STEPGEN_RATE_MAX_Q32) {
            return false;       /* would exceed 2 MHz / break the 1-tick gap */
        }
    }
    return true;
}

void stepgen_core_init(stepgen_core_t              *c,
                       const stepgen_axis_config_t *axis_cfg,
                       motion_segment_queue_t      *queue,
                       uint32_t                    *step_buf,
                       uint32_t                     ring_ticks)
{
    memset(c, 0, sizeof(*c));

    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        c->axis_cfg[a] = axis_cfg[a];
        c->step_mask  |= axis_cfg[a].step_bit;
        c->dir_mask   |= axis_cfg[a].dir_bit;
        /* Power-on assumption only; dir_known stays false so the first
         * commanded move still arms an explicit DIR write with a guard. */
        c->axis[a].dir_level = axis_cfg[a].dir_invert ? 1u : 0u;
    }

    /* emit_run() uses the PINOUT.md mask as a compile-time constant; the
     * configured axis map must agree with it. */
    c->cfg_ok = (c->step_mask == STEP_PINS_MASK_GPIOA)
             && (c->dir_mask  == DIR_PINS_MASK_GPIOD);

    c->guard_ticks = STEPGEN_DIR_GUARD_TICKS;
    c->step_buf    = step_buf;
    c->ring_ticks  = ring_ticks;
    c->half_ticks  = ring_ticks / 2u;
    c->queue       = queue;

    stepgen_core_blank(c);
}

void stepgen_core_rewind(stepgen_core_t *c)
{
    c->fill_half_index = 0;
    c->play_half_index = 0;
    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        c->axis[a].block_until_tick = 0u;
        c->axis[a].armed            = 0u;
        c->axis[a].steps_in_half[0] = 0;
        c->axis[a].steps_in_half[1] = 0;
    }
}

void stepgen_core_blank(stepgen_core_t *c)
{
    const uint32_t iw = stepgen_core_step_idle_word(c);
    for (uint32_t i = 0; i < c->ring_ticks; i++) {
        c->step_buf[i] = iw;
    }
}

void stepgen_core_abort(stepgen_core_t *c, uint32_t fault_bits)
{
    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        c->axis[a].inc              = 0;
        c->axis[a].dir_sign         = 0;
        c->axis[a].armed            = 0;
        c->axis[a].steps_in_half[0] = 0;
        c->axis[a].steps_in_half[1] = 0;
    }
    c->seg_valid         = false;
    c->seg_pending_valid = false;
    c->seg_remaining     = 0;
    c->moving            = false;
    c->faults       |= fault_bits;
    if (c->queue != NULL) {
        msq_flush(c->queue);
    }
    stepgen_core_blank(c);
}

/* ---------------------------------------------------------------------- */
/* ADR-006 play-time DIR write                                             */
/* ---------------------------------------------------------------------- */

bool stepgen_core_take_dir_write(stepgen_core_t *c, uint32_t logical_index,
                                 uint32_t *bsrr)
{
    bool any = false;

    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        stepgen_axis_t *ax = &c->axis[a];
        if (ax->armed && ax->armed_half == logical_index) {
            ax->dir_level = ax->armed_level;
            ax->dir_known = 1u;
            ax->armed     = 0u;
            any           = true;
        }
    }
    if (any) {
        *bsrr = stepgen_core_dir_word(c);
    }
    c->play_half_index = logical_index;
    return any;
}

/* ---------------------------------------------------------------------- */
/* Segment loading and reversal arming                                     */
/* ---------------------------------------------------------------------- */

/**
 * Apply a new segment's rates, arming any required DIR change.
 *
 * ADR-006, fill time: the reversal targets the half currently being filled
 * if its leading guard ticks are still free, otherwise the next one. Either
 * way the axis emits no STEP until guard_ticks into the target half, and
 * this function never touches GPIOD - doing so here would flip the pin
 * while the previous half is still autonomously playing, which is the exact
 * hazard the two-stage mechanism exists to avoid.
 */
bool stepgen_core_reversal_armed(const stepgen_core_t *c)
{
    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        if (c->axis[a].armed) { return true; }
    }
    return false;
}

/** The DIR level axis @p a needs for the rate in @p seg, or -1 if the rate
 *  is zero and the line may stay where it is. */
static int wanted_dir_level(const stepgen_core_t *c, const motion_segment_t *seg,
                            uint32_t a)
{
    const motion_rate_q32_t r = seg->rate[a];
    if (r == 0) {
        return -1;
    }
    const bool positive = (r > 0);
    return c->axis_cfg[a].dir_invert ? (positive ? 0 : 1) : (positive ? 1 : 0);
}

/**
 * True if applying @p seg would have to overwrite a reversal that is armed
 * but not yet written to GPIOD. The armed slot is depth one (ADR-006), so
 * the caller holds the segment back rather than discarding the pending
 * direction change - discarding it would run the axis the wrong way.
 */
static bool conflicts_with_armed(const stepgen_core_t *c,
                                 const motion_segment_t *seg)
{
    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        const stepgen_axis_t *ax = &c->axis[a];
        if (!ax->armed) {
            continue;
        }
        const int want = wanted_dir_level(c, seg, a);
        if (want >= 0 && (uint8_t)want != ax->armed_level) {
            return true;
        }
    }
    return false;
}

static void apply_segment(stepgen_core_t *c, uint32_t at_tick)
{
    const uint32_t fill_start = half_start_tick(c, c->fill_half_index);

    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        stepgen_axis_t *ax = &c->axis[a];
        const motion_rate_q32_t r = c->cur_seg.rate[a];
        const int8_t new_sign = (r > 0) ? (int8_t)1 : ((r < 0) ? (int8_t)-1 : (int8_t)0);

        ax->inc = abs_rate(r);

        if (new_sign == 0) {
            ax->dir_sign = 0;       /* rate zero: leave DIR where it is */
            continue;
        }

        const uint8_t want = c->axis_cfg[a].dir_invert
                           ? (uint8_t)(new_sign < 0)
                           : (uint8_t)(new_sign > 0);
        ax->dir_sign = new_sign;

        if (ax->dir_known && want == ax->dir_level && !ax->armed) {
            continue;                           /* no reversal, no guard */
        }
        if (ax->armed && ax->armed_level == want) {
            continue;                           /* already arming this way */
        }

        /* Queue depth one (ADR-006): if a reversal is already armed, the
         * new one must wait for it, so target the following half. */
        uint32_t target = c->fill_half_index;
        if (at_tick != fill_start || ax->armed) {
            target = c->fill_half_index + 1u;   /* leading guard already spent */
        }

        ax->armed            = 1u;
        ax->armed_level      = want;
        ax->armed_half       = target;
        ax->block_until_tick = half_start_tick(c, target) + c->guard_ticks;
    }
}

typedef enum { SEG_LOADED, SEG_STALLED, SEG_NONE } seg_result_t;

static seg_result_t next_segment(stepgen_core_t *c, uint32_t at_tick)
{
    motion_segment_t seg;
    bool have;

    if (c->seg_pending_valid) {
        seg  = c->seg_pending;
        have = true;
    } else {
        have = (c->queue != NULL) && msq_pop(c->queue, &seg);
    }

    if (have) {
        if (!stepgen_core_validate_segment(&seg)) {
            c->seg_pending_valid = false;
            c->faults |= STEPGEN_FAULT_BAD_SEGMENT;
            return SEG_NONE;
        }
        if (conflicts_with_armed(c, &seg)) {
            /* Hold it until the armed reversal is realized at the next
             * half-boundary. At most one half-period of delay. */
            c->seg_pending       = seg;
            c->seg_pending_valid = true;
            return SEG_STALLED;
        }
        c->seg_pending_valid = false;
        c->cur_seg       = seg;
        c->seg_remaining = seg.duration_ticks;
        c->seg_valid     = true;
        c->last_seq      = seg.seq;
        c->segments_consumed++;
        apply_segment(c, at_tick);
        c->moving = false;
        for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
            if (c->axis[a].inc != 0u) { c->moving = true; break; }
        }
        return SEG_LOADED;
    }

    /* Queue empty. Starving while moving is a COMMAND-STREAM fault: the
     * engine reports it and emits nothing further. ADR-010 fixes the
     * reaction (hold position, drives stay enabled); the core only reports,
     * the facade applies the policy. */
    if (c->moving) {
        c->faults     |= STEPGEN_FAULT_SEGMENT_STARVED;
        c->starved_now = true;
        c->moving      = false;
    }
    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        c->axis[a].inc      = 0;
        c->axis[a].dir_sign = 0;
    }
    c->seg_valid = false;
    return SEG_NONE;
}

/* ---------------------------------------------------------------------- */
/* STEP waveform emission                                                  */
/*                                                                          */
/* One pass builds the GPIOA word for each tick in registers and stores it  */
/* once. An earlier per-axis formulation that OR-ed into a pre-blanked      */
/* buffer compiled to roughly 8 Cortex-M4 cycles per axis per tick - about  */
/* 95% of a 168 MHz core at the 4 MHz tick, which fails the CPU-headroom    */
/* requirement of Docs/MOTION-ENGINE.md §19. This form removes the          */
/* read-modify-write, the blanking pass, and all per-tick bookkeeping.      */
/*                                                                          */
/* The cost is an ESTIMATE from the generated code until HV-04 measures it. */
/* ---------------------------------------------------------------------- */

static void emit_run(stepgen_core_t *c, uint32_t abs0, uint32_t at,
                     uint32_t n, const uint32_t eff[MOTION_AXIS_COUNT],
                     uint32_t phalf)
{
    stepgen_axis_t *const ax = c->axis;

    /* Compile-time constant, not a struct read: it encodes as a Thumb-2
     * modified immediate, keeping one more value out of the register
     * budget. stepgen_core_init() asserts the configured mask matches. */
    const uint32_t idle = ((uint32_t)STEP_PINS_MASK_GPIOA) << 16;
    uint32_t *const buf = c->step_buf + at;

    uint32_t px = ax[MOTION_AXIS_X].phase, py = ax[MOTION_AXIS_Y].phase;
    uint32_t pz = ax[MOTION_AXIS_Z].phase, pA = ax[MOTION_AXIS_A].phase;
    uint32_t pB = ax[MOTION_AXIS_B].phase;

    const uint32_t ix = eff[MOTION_AXIS_X], iy = eff[MOTION_AXIS_Y];
    const uint32_t iz = eff[MOTION_AXIS_Z], iA = eff[MOTION_AXIS_A];
    const uint32_t iB = eff[MOTION_AXIS_B];

    const uint32_t s0[MOTION_AXIS_COUNT] = { px, py, pz, pA, pB };

    for (uint32_t k = 0; k < n; k++) {
        uint32_t b;
        uint32_t q;

        /* Highest pin first, so the last axis shifted in lands in bit 0.
         * `b = b + b + carry` is a single ADC-class operation and needs no
         * per-axis bit constant, because PA8..PA12 are consecutive. */
        q = pB + iB; b = (uint32_t)(q < pB);         pB = q;
        q = pA + iA; b = b + b + (uint32_t)(q < pA); pA = q;
        q = pz + iz; b = b + b + (uint32_t)(q < pz); pz = q;
        q = py + iy; b = b + b + (uint32_t)(q < py); py = q;
        q = px + ix; b = b + b + (uint32_t)(q < px); px = q;

        buf[k] = (b << STEP_X_PIN) | idle;
    }

    ax[MOTION_AXIS_X].phase = px; ax[MOTION_AXIS_Y].phase = py;
    ax[MOTION_AXIS_Z].phase = pz; ax[MOTION_AXIS_A].phase = pA;
    ax[MOTION_AXIS_B].phase = pB;

    const uint32_t s1[MOTION_AXIS_COUNT] = { px, py, pz, pA, pB };
    (void)abs0;

    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        const uint32_t inc = eff[a];
        if (inc == 0u) {
            continue;
        }
        /* Step count in closed form: the accumulator is linear, so the
         * number of carries over n ticks is exactly the high word of
         * (phase_at_start + inc*n) - the same arithmetic the loop just
         * performed, so it cannot drift from the emitted waveform, and it
         * costs nothing per tick. */
        const uint64_t total = (uint64_t)s0[a] + (uint64_t)inc * (uint64_t)n;
        const int32_t  steps = (int32_t)(uint32_t)(total >> 32);
        if (steps == 0) {
            continue;
        }
        (void)s1;
        const int32_t sgn = (ax[a].dir_sign < 0) ? -steps : steps;
        ax[a].steps_in_half[phalf] += sgn;
        ax[a].pos_planned          += sgn;
    }
}

/* ---------------------------------------------------------------------- */

void stepgen_core_fill_half(stepgen_core_t *c, uint32_t phalf,
                            uint32_t logical_index)
{
    const uint32_t start = phalf * c->half_ticks;
    const uint32_t n     = c->half_ticks;
    uint32_t eff[MOTION_AXIS_COUNT];

    c->fill_half_index = logical_index;
    const uint32_t abs_start = half_start_tick(c, logical_index);

    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        c->axis[a].steps_in_half[phalf] = 0;
    }

    uint32_t done = 0;
    while (done < n) {
        const uint32_t t0 = abs_start + done;

        if (!c->seg_valid || c->seg_remaining == 0u) {
            c->starved_now = false;
            const seg_result_t r = next_segment(c, t0);
            if (r != SEG_LOADED) {
                /* Idle, stalled behind an armed reversal, or starved: emit
                 * silent ticks for the rest of the half. Only starvation
                 * is a fault; a stall is normal flow control. */
                const uint32_t rest = n - done;
                for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) { eff[a] = 0u; }
                emit_run(c, t0, start + done, rest, eff, phalf);
                if (c->starved_now) {
                    c->starved_ticks += rest;
                    c->starved_now    = false;
                }
                done = n;
                break;
            }
            continue;               /* re-evaluate with the new segment */
        }

        uint32_t k = n - done;
        if (k > c->seg_remaining) {
            k = c->seg_remaining;
        }

        /* A DIR guard pauses one axis for a few ticks. Rather than testing
         * it inside the hot loop, the run is cut at the tick the guard
         * expires and the axis runs at zero rate until then - which also
         * freezes its accumulator, so no step is lost across a reversal. */
        for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
            stepgen_axis_t *ax = &c->axis[a];
            if (ax->inc != 0u && tick_ge(ax->block_until_tick, t0 + 1u)) {
                const uint32_t until = ax->block_until_tick - t0;
                eff[a] = 0u;
                if (until < k) { k = until; }
            } else {
                eff[a] = ax->inc;
            }
        }

        emit_run(c, t0, start + done, k, eff, phalf);

        c->seg_remaining -= k;
        done             += k;
    }
}

void stepgen_core_commit_half(stepgen_core_t *c, uint32_t phalf)
{
    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        c->axis[a].pos_output        += c->axis[a].steps_in_half[phalf];
        c->axis[a].steps_in_half[phalf] = 0;
    }
}
