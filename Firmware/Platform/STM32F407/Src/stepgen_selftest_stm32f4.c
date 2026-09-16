/**
 * @file    stepgen_selftest_stm32f4.c
 * @brief   On-target hardware validation (see stepgen_selftest.h).
 */
#include "stepgen_selftest.h"
#include "stepgen_hw_map.h"
#include "stepgen_port_stm32f4.h"
#include "stepgen_port.h"
#include "stepgen.h"

#include <string.h>

static stepgen_hv_result_t s_res[HV_TEST_COUNT];

static void record(stepgen_hv_test_t t, bool pass, uint32_t measured,
                   uint32_t expected)
{
    s_res[t].run      = true;
    s_res[t].pass     = pass;
    s_res[t].measured = measured;
    s_res[t].expected = expected;
}

static void spin_cycles(uint32_t n)
{
    const uint32_t t0 = DWT->CYCCNT;
    while ((DWT->CYCCNT - t0) < n) { __NOP(); }
}

/* ---------------------------------------------------------------------- */
/* HV-00  DMA2 really can write GPIOA->BSRR, and the request mapping is
 *        the one RM0090 Table 44 says it is.
 *
 * This is the test that settles the ADR-004/ADR-005 deviation empirically.
 * RM0090 §2.1 says DMA1's peripheral port is not a bus-matrix master and
 * therefore cannot reach GPIOA at all, which is why this port uses TIM8 +
 * DMA2_Stream1 instead of TIM2 + DMA1_Stream1. If the reading is right,
 * this test passes here and the same test built against DMA1 would fail.
 * ---------------------------------------------------------------------- */
static void hv00_dma_path(void)
{
    const uint32_t tick_hz = stepgen_port_get_tick_hz();
    const uint32_t expect  = tick_hz / 1000u;     /* transfers in 1 ms */

    stepgen_port_start();
    const uint32_t before = stepgen_port_ndtr();
    spin_cycles(STEPGEN_TIM_CLK_HZ / 1000u);
    const uint32_t after  = stepgen_port_ndtr();
    stepgen_port_stop();

    const uint32_t moved = (before >= after)
                         ? (before - after)
                         : (before + STEPGEN_RING_TICKS - after);

    /* 5% tolerance for the spin loop's own granularity. */
    const bool pass = (moved >= expect - expect / 20u)
                   && (moved <= expect + expect / 20u)
                   && (stepgen_port_dma_errors() == 0u);
    record(HV_00_DMA_PATH, pass, moved, expect);
}

/* ---------------------------------------------------------------------- */
/* HV-01  BSRR set bits take priority over reset bits.
 *
 * The self-clearing waveform depends on it: the generator writes
 * set_bits | (all_step_pins << 16) every tick, so if the priority were the
 * other way round no STEP pulse would ever appear. Safe to run: the
 * timebase is stopped and the drives are disabled.
 * ---------------------------------------------------------------------- */
static void hv01_bsrr_priority(void)
{
    const uint32_t mask = STEP_PINS_MASK_GPIOA;

    STEPGEN_GPIO_STEP->BSRR = mask << 16;               /* all low   */
    STEPGEN_GPIO_STEP->BSRR = mask | (mask << 16);      /* set+reset */
    const uint32_t hi = STEPGEN_GPIO_STEP->ODR & mask;

    STEPGEN_GPIO_STEP->BSRR = mask << 16;
    const uint32_t lo = STEPGEN_GPIO_STEP->ODR & mask;

    record(HV_01_BSRR_PRIORITY, (hi == mask) && (lo == 0u), hi, mask);
}

/* ---------------------------------------------------------------------- */
/* HV-02  The base tick is exactly the configured frequency.               */
/* ---------------------------------------------------------------------- */
static void hv02_tick_frequency(void)
{
    const uint32_t arr    = STEPGEN_TIM->ARR;
    const uint32_t actual = STEPGEN_TIM_CLK_HZ / (arr + 1u);
    const bool exact = (actual * (arr + 1u) == STEPGEN_TIM_CLK_HZ)
                    && (actual == STEPGEN_TICK_HZ);
    record(HV_02_TICK_FREQUENCY, exact, actual, STEPGEN_TICK_HZ);
}

/* ---------------------------------------------------------------------- */
/* HV-03  The ring really is in SRAM2.
 *
 * SRAM1 and SRAM2 are separate bus-matrix slave ports (RM0090 §2.1), so
 * keeping the ring in SRAM2 while Ethernet uses SRAM1 is what stops the
 * two contending. If the linker fragment is forgotten the ring silently
 * falls back into SRAM1 and that isolation disappears unnoticed.
 * ---------------------------------------------------------------------- */
static void hv03_sram2_placement(void)
{
    const uint32_t addr = (uint32_t)(uintptr_t)stepgen_debug_buffer_base();
    const bool in_sram2 = (addr >= SRAM2_BASE) && (addr < SRAM2_BASE + 0x4000u);
    record(HV_03_SRAM2_PLACEMENT, in_sram2, addr, SRAM2_BASE);
}

/* ---------------------------------------------------------------------- */
/* HV-04  Worst-case ring refill cost.
 *
 * The only legitimate source of a CPU-load figure for this subsystem; the
 * host benchmark is an x86 algorithm proxy and proves nothing here.
 * ---------------------------------------------------------------------- */
static void hv04_refill_cost(void)
{
    stepgen_status_t st;
    stepgen_get_status(&st);

    const uint32_t deadline =
        (uint32_t)(((uint64_t)STEPGEN_TIM_CLK_HZ * STEPGEN_HALF_TICKS)
                   / stepgen_port_get_tick_hz());

    /* Pass only with 2x headroom (Docs/MOTION-ENGINE.md Rule 5). */
    record(HV_04_REFILL_COST,
           (st.fill_cycles_max != 0u) && (st.fill_cycles_max * 2u < deadline),
           st.fill_cycles_max, deadline);
}

/* ---------------------------------------------------------------------- */
/* HV-05  Emergency-stop path duration.                                    */
/* ---------------------------------------------------------------------- */
static void hv05_estop_latency(void)
{
    stepgen_port_start();
    const uint32_t t0 = DWT->CYCCNT;
    stepgen_port_emergency_stop();
    const uint32_t cycles = DWT->CYCCNT - t0;

    /* Must fit inside one base tick, so no STEP edge can be emitted after
     * the stop begins. */
    const uint32_t budget = STEPGEN_TIM_CLK_HZ / stepgen_port_get_tick_hz();
    record(HV_05_ESTOP_LATENCY, cycles < budget, cycles, budget);
}

/* ---------------------------------------------------------------------- */

bool stepgen_selftest_run_all(void)
{
    memset(s_res, 0, sizeof(s_res));

    hv02_tick_frequency();
    hv01_bsrr_priority();
    hv03_sram2_placement();
    hv00_dma_path();
    hv05_estop_latency();
    hv04_refill_cost();

    bool all = true;
    for (uint32_t i = 0; i < (uint32_t)HV_TEST_COUNT; i++) {
        if (s_res[i].run && !s_res[i].pass) { all = false; }
    }
    return all;
}

const stepgen_hv_result_t *stepgen_selftest_results(void) { return s_res; }

/* ---------------------------------------------------------------------- */
/* Instrument stimulus patterns                                            */
/* ---------------------------------------------------------------------- */

static bool queue_constant(const double hz[MOTION_AXIS_COUNT], uint32_t seconds)
{
    motion_segment_t seg;
    const uint32_t tick_hz = stepgen_port_get_tick_hz();
    uint64_t remaining = (uint64_t)tick_hz * seconds;

    memset(&seg, 0, sizeof(seg));
    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        seg.rate[a] = stepgen_rate_from_hz(hz[a]);
    }
    if (!stepgen_enable_drives() || !stepgen_start()) {
        return false;
    }
    /* Keep the queue fed for the whole capture. The engine runs from DMA
     * throughout; this loop only supplies it. */
    while (remaining > 0u) {
        if (stepgen_queue_free() == 0u) {
            continue;
        }
        const uint32_t d = (remaining > STEPGEN_HALF_TICKS)
                         ? STEPGEN_HALF_TICKS : (uint32_t)remaining;
        seg.duration_ticks = d;
        if (stepgen_submit_segment(&seg)) {
            seg.seq++;
            remaining -= d;
        }
    }
    return true;
}

bool stepgen_stim_max_rate(uint32_t n_axes, uint32_t seconds)
{
    double hz[MOTION_AXIS_COUNT] = {0};
    if (n_axes == 0u || n_axes > MOTION_AXIS_COUNT) {
        return false;
    }
    for (uint32_t a = 0; a < n_axes; a++) {
        hz[a] = (double)MOTION_STEP_RATE_MAX_HZ;
    }
    return queue_constant(hz, seconds);
}

bool stepgen_stim_dir_reversal(uint32_t seconds)
{
    motion_segment_t fwd, rev;
    const uint32_t tick_hz = stepgen_port_get_tick_hz();
    uint64_t remaining = (uint64_t)tick_hz * seconds;

    memset(&fwd, 0, sizeof(fwd));
    memset(&rev, 0, sizeof(rev));
    fwd.rate[MOTION_AXIS_Y] =  stepgen_rate_from_hz((double)MOTION_STEP_RATE_MAX_HZ);
    rev.rate[MOTION_AXIS_Y] = -stepgen_rate_from_hz((double)MOTION_STEP_RATE_MAX_HZ);
    /* Slower than one ring period, which is ADR-006's operating regime. */
    fwd.duration_ticks = 2u * STEPGEN_RING_TICKS;
    rev.duration_ticks = 2u * STEPGEN_RING_TICKS;

    if (!stepgen_enable_drives() || !stepgen_start()) {
        return false;
    }
    uint32_t seq = 0;
    while (remaining > 0u) {
        if (stepgen_queue_free() < 2u) {
            continue;
        }
        fwd.seq = seq++;
        rev.seq = seq++;
        if (stepgen_submit_segment(&fwd) && stepgen_submit_segment(&rev)) {
            const uint64_t used = 4u * (uint64_t)STEPGEN_RING_TICKS;
            remaining = (remaining > used) ? (remaining - used) : 0u;
        }
    }
    return true;
}

bool stepgen_stim_mixed_rates(uint32_t seconds)
{
    /* Deliberately incommensurate, so the relationship between axes is
     * exercised at every relative phase rather than only when they happen
     * to step together. */
    const double hz[MOTION_AXIS_COUNT] = {
        2000000.0, 1999999.0, 1000000.0, 666667.0, 333331.0
    };
    return queue_constant(hz, seconds);
}
