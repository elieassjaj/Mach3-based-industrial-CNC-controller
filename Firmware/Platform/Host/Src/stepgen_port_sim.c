/**
 * @file    stepgen_port_sim.c
 * @brief   Host-side simulation port for the STEP generator.
 *
 * Replaces TIM8 + DMA2 + GPIO with an in-memory consumer that records
 * every BSRR word the DMA would write, and every CPU-timed DIR write, in
 * order. Tests reconstruct the pin waveforms from that trace and measure
 * them in nanoseconds - which is what lets the DIR setup/hold and
 * pulse-width requirements be checked without hardware.
 *
 * The simulated DMA latches half-transfer and transfer-complete flags the
 * way the hardware does, so a missed refill deadline is detected here by
 * exactly the same rule the real ISR uses.
 */
#include "stepgen_port.h"
#include "stepgen.h"
#include "sim_trace.h"

#include <stddef.h>

static stepgen_boundary_cb_t s_cb;
static uint32_t             *s_buf;
static uint32_t              s_ring;
static uint32_t              s_tick_hz = STEPGEN_TICK_HZ;
static bool                  s_running;
static bool                  s_enabled;
static uint32_t              s_cycles;
static uint32_t              s_idx;
static uint8_t               s_ht_flag, s_tc_flag;

sim_trace_t g_sim_trace;

bool stepgen_port_init(stepgen_boundary_cb_t cb, uint32_t *step_buf,
                       uint32_t ring_ticks)
{
    if (cb == NULL || step_buf == NULL) {
        return false;
    }
    s_cb      = cb;
    s_buf     = step_buf;
    s_ring    = ring_ticks;
    s_running = false;
    s_cycles  = 0;
    s_idx     = 0;
    s_ht_flag = 0;
    s_tc_flag = 0;
    sim_trace_reset(&g_sim_trace);
    return true;
}

bool stepgen_port_set_tick_hz(uint32_t tick_hz)
{
    if (s_running || tick_hz == 0u) {
        return false;
    }
    s_tick_hz = tick_hz;
    return true;
}

uint32_t stepgen_port_get_tick_hz(void) { return s_tick_hz; }

void stepgen_port_start(void)
{
    s_idx = 0; s_ht_flag = 0; s_tc_flag = 0; s_running = true;
}

void stepgen_port_stop(void)      { s_running = false; }
void stepgen_port_halt_hold(void) { s_running = false; }

void stepgen_port_emergency_stop(void)
{
    s_running = false;
    s_enabled = false;
    sim_trace_estop(&g_sim_trace);
}

void stepgen_port_set_enable(bool enable) { s_enabled = enable; }
bool stepgen_port_get_enable(void)        { return s_enabled;   }

void stepgen_port_write_dir(uint32_t bsrr) { sim_trace_dir(&g_sim_trace, bsrr); }

uint32_t stepgen_port_ndtr(void)        { return s_ring - s_idx; }
uint32_t stepgen_port_cycle_count(void) { return ++s_cycles; }

/* ---------------------------------------------------------------------- */

static void sim_service_dma_flags(void)
{
    if (s_ht_flag && s_tc_flag) {
        s_ht_flag = 0; s_tc_flag = 0;
        stepgen_on_overrun();
        return;
    }
    if (s_ht_flag) { s_ht_flag = 0; s_cb(0u); }
    if (s_tc_flag) { s_tc_flag = 0; s_cb(1u); }
}

void sim_port_run_ticks(uint32_t ticks)
{
    const uint32_t half = s_ring / 2u;

    sim_service_dma_flags();

    for (uint32_t t = 0; t < ticks && s_running; t++) {
        sim_trace_step(&g_sim_trace, s_buf[s_idx]);
        s_idx++;
        if (s_idx == half) {
            s_ht_flag = 1u;
            sim_service_dma_flags();
        } else if (s_idx == s_ring) {
            s_idx     = 0;
            s_tc_flag = 1u;
            sim_service_dma_flags();
        }
    }
}

/* Emit ticks while the boundary handler is blocked, to reproduce a missed
 * deadline. The DMA flags still latch, exactly as they do in hardware. */
void sim_port_run_ticks_no_refill(uint32_t ticks)
{
    const uint32_t half = s_ring / 2u;

    for (uint32_t t = 0; t < ticks && s_running; t++) {
        sim_trace_step(&g_sim_trace, s_buf[s_idx]);
        s_idx++;
        if (s_idx == half)        { s_ht_flag = 1u; }
        else if (s_idx == s_ring) { s_idx = 0; s_tc_flag = 1u; }
    }
}
