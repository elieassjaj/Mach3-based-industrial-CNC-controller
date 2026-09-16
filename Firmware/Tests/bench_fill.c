#define _POSIX_C_SOURCE 199309L
/**
 * @file    bench_fill.c
 * @brief   Host benchmark of the ring-refill cost.
 *
 * Calls stepgen_core_fill_half() directly, with no simulated DMA and no
 * waveform trace, so the number reflects only the generator's work.
 *
 * This measures the ALGORITHM on an x86 host. It is NOT an STM32F407 CPU
 * load figure and no compliance claim may be derived from it; the
 * authoritative Cortex-M4 number comes from the on-target DWT measurement
 * in Docs/HARDWARE-VALIDATION.md test HV-04.
 *
 * What it does establish, and what the architecture depends on, is that
 * the cost per tick is essentially independent of the commanded step rate.
 */
#include "stepgen_core.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static uint32_t buf[STEPGEN_RING_TICKS];
static stepgen_core_t         core;
static motion_segment_queue_t queue;

static const stepgen_axis_config_t axis_cfg[MOTION_AXIS_COUNT] = {
    { (1u << STEP_X_PIN), (1u << DIR_X_PIN), false },
    { (1u << STEP_Y_PIN), (1u << DIR_Y_PIN), false },
    { (1u << STEP_Z_PIN), (1u << DIR_Z_PIN), false },
    { (1u << STEP_A_PIN), (1u << DIR_A_PIN), false },
    { (1u << STEP_B_PIN), (1u << DIR_B_PIN), false },
};

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static motion_rate_q32_t q32_from_hz(double hz)
{
    return (motion_rate_q32_t)((hz / (double)STEPGEN_TICK_HZ) * 4294967296.0 + 0.5);
}

static void run(const char *label, uint32_t naxes, double hz, int reversing)
{
    const uint32_t halves = 16384u;          /* 8.4 Mi ticks */
    motion_segment_t seg;

    msq_init(&queue);
    stepgen_core_init(&core, axis_cfg, &queue, buf, STEPGEN_RING_TICKS);

    memset(&seg, 0, sizeof(seg));
    seg.duration_ticks = reversing ? 64u : STEPGEN_HALF_TICKS;

    const double t0 = now_s();
    for (uint32_t h = 0; h < halves; h++) {
        while (msq_free(&queue) > 0u) {
            for (uint32_t a = 0; a < naxes; a++) {
                seg.rate[a] = (reversing && (seg.seq & 1u))
                            ? -q32_from_hz(hz) : q32_from_hz(hz);
            }
            (void)msq_push(&queue, &seg);
            seg.seq++;
        }
        stepgen_core_fill_half(&core, h & 1u, h);
        stepgen_core_commit_half(&core, h & 1u);
    }
    const double dt = now_s() - t0;
    const double ticks = (double)halves * (double)STEPGEN_HALF_TICKS;

    printf("  %-36s %7.3f ns/tick\n", label, dt / ticks * 1e9);
}

int main(void)
{
    printf("CNC5AX-ETH refill benchmark (x86 host, algorithm cost only)\n");
    printf("  tick = %u Hz, ring = %u ticks, half = %u ticks\n",
           STEPGEN_TICK_HZ, STEPGEN_RING_TICKS, STEPGEN_HALF_TICKS);
    printf("  NOT an STM32F407 CPU-load claim - see HV-04.\n\n");

    run("idle (0 axes)",             0, 0.0,       0);
    run("1 axis  @ 2 MHz",           1, 2000000.0, 0);
    run("3 axes  @ 2 MHz",           3, 2000000.0, 0);
    run("5 axes  @ 2 MHz",           5, 2000000.0, 0);
    run("5 axes  @ 1 kHz",           5, 1000.0,    0);
    run("5 axes  @ 2 MHz, reversing", 5, 2000000.0, 1);
    return 0;
}
