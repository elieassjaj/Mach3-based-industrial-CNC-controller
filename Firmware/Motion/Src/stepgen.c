/**
 * @file    stepgen.c
 * @brief   STEP/DIR motion engine facade and ADR-010 state machine.
 */
#include "stepgen.h"
#include "stepgen_core.h"
#include "stepgen_port.h"

#include <string.h>

/* ---------------------------------------------------------------------- */
/* STEP DMA ring.
 *
 * Placed in SRAM2 by the linker fragment in Platform/STM32F407/linker/.
 * SRAM1 and SRAM2 are separate slave ports on the STM32F407 bus matrix
 * (RM0090 §2.1), and the Ethernet MAC's DMA is a separate master, so
 * keeping this ring in SRAM2 while Ethernet descriptors and the LwIP pbuf
 * pool stay in SRAM1 means the two never contend for the same slave port.
 * That is the structural half of the Ethernet-isolation requirement; the
 * NVIC priority table in stepgen_hw_map.h is the other half.
 * ---------------------------------------------------------------------- */
#if defined(STEPGEN_BUFFERS_SECTION)
#  define STEPGEN_BUF_ATTR __attribute__((section(STEPGEN_BUFFERS_SECTION), aligned(32)))
#else
#  define STEPGEN_BUF_ATTR __attribute__((aligned(32)))
#endif

static uint32_t g_step_buf[STEPGEN_RING_TICKS] STEPGEN_BUF_ATTR;

static stepgen_core_t           g_core;
static motion_segment_queue_t   g_queue;
static volatile stepgen_state_t g_state = STEPGEN_STATE_UNINIT;

/* Underrun detection: set by the fill, cleared when the DMA enters a half. */
static volatile uint8_t  g_half_ready[2];
static volatile uint32_t g_underruns;
static volatile uint32_t g_fill_cycles_max;
static volatile uint32_t g_next_logical;     /**< logical index being filled */

/* Axis map, transcribed from Docs/PINOUT.md via cnc_motion_config.h. */
static const stepgen_axis_config_t g_axis_cfg[MOTION_AXIS_COUNT] = {
    [MOTION_AXIS_X] = { (1u << STEP_X_PIN), (1u << DIR_X_PIN), false },
    [MOTION_AXIS_Y] = { (1u << STEP_Y_PIN), (1u << DIR_Y_PIN), false },
    [MOTION_AXIS_Z] = { (1u << STEP_Z_PIN), (1u << DIR_Z_PIN), false },
    [MOTION_AXIS_A] = { (1u << STEP_A_PIN), (1u << DIR_A_PIN), false },
    [MOTION_AXIS_B] = { (1u << STEP_B_PIN), (1u << DIR_B_PIN), false },
};

_Static_assert(((1u << STEP_X_PIN) | (1u << STEP_Y_PIN) | (1u << STEP_Z_PIN) |
                (1u << STEP_A_PIN) | (1u << STEP_B_PIN)) == STEP_PINS_MASK_GPIOA,
               "GPIOA STEP mask disagrees with Docs/PINOUT.md");
_Static_assert(((1u << DIR_X_PIN) | (1u << DIR_Y_PIN) | (1u << DIR_Z_PIN) |
                (1u << DIR_A_PIN) | (1u << DIR_B_PIN)) == DIR_PINS_MASK_GPIOD,
               "GPIOD DIR mask disagrees with Docs/PINOUT.md");
_Static_assert((STEPGEN_RING_TICKS & (STEPGEN_RING_TICKS - 1u)) == 0u,
               "ring size must be a power of two");
_Static_assert(STEPGEN_TICK_HZ == 2u * MOTION_STEP_RATE_MAX_HZ,
               "a one-tick pulse requires tick = 2 x max step rate");
_Static_assert(STEPGEN_HALF_TICKS > STEPGEN_DIR_GUARD_TICKS,
               "the DIR guard must fit inside a ring half");

/* ---------------------------------------------------------------------- */
/* ADR-010 fault handling                                                  */
/* ---------------------------------------------------------------------- */

/**
 * Enter FAULT. Integrity faults deassert the drives; a merely paused or
 * empty command stream holds position with the drives still energised, so
 * a stalled link never lets an axis drift or drop under load.
 */
static void enter_fault(uint32_t bits)
{
    if ((bits & STEPGEN_FAULTS_INTEGRITY) != 0u) {
        stepgen_port_emergency_stop();       /* stops DMA and drops EN */
    } else {
        stepgen_port_halt_hold();            /* STEP stops, EN stays on */
    }
    stepgen_core_abort(&g_core, bits);
    if (g_state != STEPGEN_STATE_EMERGENCY_STOP) {
        g_state = STEPGEN_STATE_FAULT;
    }
}

void stepgen_on_overrun(void)
{
    g_underruns++;
    enter_fault(STEPGEN_FAULT_BUFFER_UNDERRUN);
}

/* ---------------------------------------------------------------------- */
/* Ring boundary                                                           */
/* ---------------------------------------------------------------------- */

/**
 * The DMA has finished physical half @p phalf and is now playing the other
 * one. Two things happen here, in this order, and the order is the whole
 * point of ADR-006:
 *
 *   1. Play-time DIR write, FIRST, before anything else. The half now
 *      starting to play is the one any armed reversal was targeted at, and
 *      its leading guard ticks were reserved idle for that axis at fill
 *      time, so writing GPIOD here lands inside that gap.
 *   2. Commit the step counts of the half that just played, then refill it
 *      as the half after next.
 */
void stepgen_on_boundary(uint32_t phalf)
{
    const uint32_t t0 = stepgen_port_cycle_count();

    /* The half now playing is the logical successor of the one just done. */
    const uint32_t playing_logical = g_core.play_half_index + 1u;

    uint32_t dir_bsrr;
    if (stepgen_core_take_dir_write(&g_core, playing_logical, &dir_bsrr)) {
        stepgen_port_write_dir(dir_bsrr);
    }

    const uint32_t running = phalf ^ 1u;
    g_half_ready[phalf] = 0u;                 /* its contents are spent */

    if (!g_half_ready[running]) {
        /* Stale BSRR words are about to replay as uncommanded motion. */
        stepgen_on_overrun();
        return;
    }

    stepgen_core_commit_half(&g_core, phalf);
    stepgen_core_fill_half(&g_core, phalf, playing_logical + 1u);
    g_half_ready[phalf] = 1u;
    g_next_logical      = playing_logical + 2u;

    const uint32_t dt = stepgen_port_cycle_count() - t0;
    if (dt > g_fill_cycles_max) {
        g_fill_cycles_max = dt;
    }
}

/* ---------------------------------------------------------------------- */
/* Lifecycle                                                               */
/* ---------------------------------------------------------------------- */

bool stepgen_init(void)
{
    msq_init(&g_queue);
    stepgen_core_init(&g_core, g_axis_cfg, &g_queue, g_step_buf,
                      STEPGEN_RING_TICKS);

    g_underruns       = 0;
    g_fill_cycles_max = 0;
    g_half_ready[0]   = 1u;
    g_half_ready[1]   = 1u;

    if (!g_core.cfg_ok) {
        g_state = STEPGEN_STATE_FAULT;      /* axis map contradicts PINOUT */
        g_core.faults |= STEPGEN_FAULT_CONFIG;
        return false;
    }
    if (!stepgen_port_init(stepgen_on_boundary, g_step_buf, STEPGEN_RING_TICKS)) {
        g_state = STEPGEN_STATE_FAULT;
        g_core.faults |= STEPGEN_FAULT_HW;
        return false;
    }
    if (!stepgen_port_set_tick_hz(STEPGEN_TICK_HZ)) {
        g_state = STEPGEN_STATE_FAULT;
        g_core.faults |= STEPGEN_FAULT_HW;
        return false;
    }
    stepgen_port_set_enable(false);
    g_state = STEPGEN_STATE_SAFE_IDLE;
    return true;
}

bool stepgen_enable_drives(void)
{
    if (g_state != STEPGEN_STATE_SAFE_IDLE) {
        return false;
    }
    stepgen_port_set_enable(true);
    g_state = STEPGEN_STATE_READY;
    return true;
}

bool stepgen_start(void)
{
    if (g_state != STEPGEN_STATE_READY) {
        return false;
    }
    /* Pre-fill both halves so the first DMA pass is never stale. Logical
     * halves 0 and 1; playback begins with 0. */
    stepgen_core_rewind(&g_core);
    stepgen_core_fill_half(&g_core, 0u, 0u);
    stepgen_core_fill_half(&g_core, 1u, 1u);
    /* Logical half 0 begins playing the instant the timer starts, and no
     * boundary interrupt precedes it, so its play-time DIR write has to
     * happen here. Doing it before the timer runs gives that first write
     * unbounded setup margin, which is exactly what ADR-006 wants at the
     * start of motion. */
    {
        uint32_t dir_bsrr;
        if (stepgen_core_take_dir_write(&g_core, 0u, &dir_bsrr)) {
            stepgen_port_write_dir(dir_bsrr);
        }
    }
    g_core.play_half_index = 0u;    /* half 0 is about to play */
    g_next_logical         = 2u;
    g_half_ready[0]        = 1u;
    g_half_ready[1]        = 1u;

    g_state = STEPGEN_STATE_RUNNING;
    stepgen_port_start();
    return true;
}

void stepgen_stop(void)
{
    stepgen_port_stop();
    stepgen_core_abort(&g_core, 0u);
    if (g_state == STEPGEN_STATE_RUNNING) {
        g_state = STEPGEN_STATE_READY;
    }
}

void stepgen_emergency_stop(void)
{
    stepgen_port_emergency_stop();          /* register writes only */
    g_state = STEPGEN_STATE_EMERGENCY_STOP; /* latching, set before abort */
    stepgen_core_abort(&g_core, 0u);
}

bool stepgen_clear_fault(void)
{
    /* ADR-010: EMERGENCY_STOP is structurally distinct so a generic
     * fault-clear can never clear it. */
    if (g_state != STEPGEN_STATE_FAULT) {
        return false;
    }
    g_core.faults        = STEPGEN_FAULT_NONE;
    g_core.starved_ticks = 0;
    g_underruns          = 0;
    stepgen_core_blank(&g_core);
    stepgen_core_rewind(&g_core);
    g_half_ready[0] = 1u;
    g_half_ready[1] = 1u;
    stepgen_port_set_enable(false);
    g_state = STEPGEN_STATE_SAFE_IDLE;
    return true;
}

bool stepgen_clear_emergency_stop(void)
{
    if (g_state != STEPGEN_STATE_EMERGENCY_STOP) {
        return false;
    }
    g_state = STEPGEN_STATE_FAULT;      /* demote, then the normal clear */
    return stepgen_clear_fault();
}

bool stepgen_submit_segment(const motion_segment_t *seg)
{
    if (!stepgen_core_validate_segment(seg)) {
        return false;
    }
    if (g_state != STEPGEN_STATE_READY && g_state != STEPGEN_STATE_RUNNING) {
        return false;
    }
    return msq_push(&g_queue, seg);
}

uint32_t stepgen_queue_free(void) { return msq_free(&g_queue); }
uint32_t stepgen_tick_hz(void)    { return stepgen_port_get_tick_hz(); }

motion_rate_q32_t stepgen_rate_from_hz(double hz)
{
    const double tick = (double)stepgen_port_get_tick_hz();
    const int    neg  = (hz < 0.0);
    const double mag  = neg ? -hz : hz;
    double       q    = (mag / tick) * 4294967296.0;   /* 2^32 */

    if (q > (double)STEPGEN_RATE_MAX_Q32) {
        q = (double)STEPGEN_RATE_MAX_Q32;
    }
    const motion_rate_q32_t v = (motion_rate_q32_t)(q + 0.5);
    return neg ? -v : v;
}

void stepgen_get_status(stepgen_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->state             = g_state;
    out->faults            = g_core.faults;
    out->segments_consumed = g_core.segments_consumed;
    out->queue_free        = msq_free(&g_queue);
    out->starved_ticks     = g_core.starved_ticks;
    out->underruns         = g_underruns;
    out->last_seq          = g_core.last_seq;
    out->fill_cycles_max   = g_fill_cycles_max;
    out->drives_enabled    = stepgen_port_get_enable();
    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        out->pos_output[a]  = g_core.axis[a].pos_output;
        out->pos_planned[a] = g_core.axis[a].pos_planned;
    }
}

uint32_t *stepgen_debug_buffer_base(void) { return g_step_buf; }
