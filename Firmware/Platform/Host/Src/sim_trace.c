#include "sim_trace.h"
#include <string.h>

/* Apply one BSRR write to a 16-bit output-data image.
 * BSRR: bits 15:0 set, bits 31:16 reset, and a set bit wins over a reset
 * bit for the same pin. The self-clearing waveform depends on that
 * priority, so the simulation models it explicitly and on-target test
 * HV-01 verifies it on real silicon. */
static inline uint16_t apply_bsrr(uint16_t odr, uint32_t bsrr)
{
    const uint16_t set = (uint16_t)(bsrr & 0xFFFFu);
    const uint16_t rst = (uint16_t)(bsrr >> 16);
    return (uint16_t)((odr & (uint16_t)~rst) | set);
}

static const struct { uint16_t step; uint16_t dir; } k_map[MOTION_AXIS_COUNT] = {
    { (1u << STEP_X_PIN), (1u << DIR_X_PIN) },
    { (1u << STEP_Y_PIN), (1u << DIR_Y_PIN) },
    { (1u << STEP_Z_PIN), (1u << DIR_Z_PIN) },
    { (1u << STEP_A_PIN), (1u << DIR_A_PIN) },
    { (1u << STEP_B_PIN), (1u << DIR_B_PIN) },
};

void sim_trace_reset(sim_trace_t *t)
{
    t->n = 0;
    t->estop_tick = 0xFFFFFFFFu;
    t->lvl_step = 0;
    t->lvl_dir  = 0;
}

void sim_trace_estop(sim_trace_t *t)
{
    if (t->estop_tick == 0xFFFFFFFFu) {
        t->estop_tick = t->n;
    }
}

/* A CPU-timed DIR write takes effect immediately, i.e. before the tick that
 * is recorded next. */
void sim_trace_dir(sim_trace_t *t, uint32_t bsrr)
{
    t->lvl_dir = apply_bsrr(t->lvl_dir, bsrr);
}

void sim_trace_step(sim_trace_t *t, uint32_t bsrr)
{
    t->lvl_step = apply_bsrr(t->lvl_step, bsrr);

    if (t->n >= SIM_TRACE_CAP) {
        return;
    }
    uint8_t s = 0, d = 0;
    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        if (t->lvl_step & k_map[a].step) { s = (uint8_t)(s | (1u << a)); }
        if (t->lvl_dir  & k_map[a].dir ) { d = (uint8_t)(d | (1u << a)); }
    }
    t->step[t->n] = s;
    t->dir [t->n] = d;
    t->n++;
}
