/**
 * @file    sim_trace.h
 * @brief   Virtual logic analyser for host-side STEP/DIR verification.
 *
 * Records the STEP BSRR word stream tick by tick and the CPU-timed DIR
 * writes at the tick they occur, then reconstructs the pin levels, so
 * tests can assert real timing (pulse width, DIR setup/hold) in
 * nanoseconds rather than inspecting engine state.
 */
#ifndef SIM_TRACE_H
#define SIM_TRACE_H

#include <stdint.h>
#include <stdbool.h>
#include "cnc_motion_config.h"

#ifndef SIM_TRACE_CAP
#define SIM_TRACE_CAP (1u << 22)      /* 4 Mi ticks = 1.048 s @ 4 MHz */
#endif

typedef struct {
    uint8_t  step[SIM_TRACE_CAP];     /* bit a = STEP level of axis a */
    uint8_t  dir [SIM_TRACE_CAP];     /* bit a = DIR  level of axis a */
    uint32_t n;
    uint32_t estop_tick;              /* UINT32_MAX if never */
    uint16_t lvl_step;                /* GPIOA output image */
    uint16_t lvl_dir;                 /* GPIOD output image */
} sim_trace_t;

extern sim_trace_t g_sim_trace;

void sim_trace_reset(sim_trace_t *t);
void sim_trace_step (sim_trace_t *t, uint32_t bsrr_gpioa);
void sim_trace_dir  (sim_trace_t *t, uint32_t bsrr_gpiod);
void sim_trace_estop(sim_trace_t *t);

/* Simulated DMA driver, implemented by stepgen_port_sim.c */
void sim_port_run_ticks(uint32_t ticks);
void sim_port_run_ticks_no_refill(uint32_t ticks);

#endif /* SIM_TRACE_H */
