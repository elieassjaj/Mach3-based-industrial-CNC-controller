/**
 * @file    safety_selftest_stm32f4.c
 * @brief   On-target validation for the digital-input manager (HV-40..HV-43).
 *
 * Nothing here asserts an E-STOP. Firing one to see whether it works would
 * put the machine into a latched EMERGENCY_STOP that then needs an operator
 * to clear, at boot, every boot - so these tests check the conditions that
 * make the E-STOP path work and leave the path itself to HV-18 on a bench.
 */
#include "safety_hw_map.h"
#include "safety_selftest.h"
#include "safety_port.h"
#include "safety_input.h"
#include "stepgen.h"

#include <string.h>

static safety_hv_result_t s_res[HV_SAFETY_TEST_COUNT];

static void record(safety_hv_test_t id, bool pass,
                   uint32_t measured, uint32_t expected)
{
    s_res[id].run      = true;
    s_res[id].pass     = pass;
    s_res[id].measured = measured;
    s_res[id].expected = expected;
}

/* ------------------------------------------------------------ HV-40 ---- */
/**
 * ADR-010 says EMERGENCY_STOP clears only once the physical input has
 * released, and stepgen.h implements that as a registered predicate. With
 * nothing registered the engine has no way to see PE2 and a clear succeeds
 * on the state machine alone - correct for the host test suite, wrong on a
 * machine. This is the test that says which of the two this board is.
 */
static bool hv40_estop_interlock(void)
{
    const bool ok = stepgen_has_estop_gate() && safety_input_present();
    record(HV_40_ESTOP_INTERLOCK, ok, ok ? 1u : 0u, 1u);
    return ok;
}

/* ------------------------------------------------------------ HV-41 ---- */
/**
 * The configuration ADR-004 and Docs/PINOUT.md require, read back from the
 * hardware rather than assumed from the .ioc:
 *
 *   - EXTI0..14 unmasked, both edges enabled
 *   - every one of those lines selected onto port E
 *   - PE0..PE14 are inputs with no internal pull
 *   - EXTI2 at priority 0, the other five vectors at 1, both above the
 *     STEP-DMA refill at 2
 */
static bool hv41_exti_config(void)
{
    bool ok = true;
    uint32_t detail = 0u;

    if ((EXTI->IMR  & SAFETY_EXTI_MASK) != SAFETY_EXTI_MASK) { ok = false; detail |= 1u; }
    if ((EXTI->RTSR & SAFETY_EXTI_MASK) != SAFETY_EXTI_MASK) { ok = false; detail |= 2u; }
    if ((EXTI->FTSR & SAFETY_EXTI_MASK) != SAFETY_EXTI_MASK) { ok = false; detail |= 4u; }

    for (unsigned pin = 0; pin < SAFETY_INPUT_COUNT; pin++) {
        const unsigned reg = pin / 4u;
        const unsigned sh  = (pin % 4u) * 4u;
        if (((SYSCFG->EXTICR[reg] >> sh) & 0xFu) != SAFETY_SYSCFG_PORT_E) {
            ok = false; detail |= 8u;
        }
        const unsigned msh = pin * 2u;
        if (((SAFETY_GPIO->MODER >> msh) & 3u) != 0u) { ok = false; detail |= 16u; }
        if (((SAFETY_GPIO->PUPDR >> msh) & 3u) != 0u) { ok = false; detail |= 32u; }
    }

    if (NVIC_GetPriority(SAFETY_IRQ_ESTOP) != SAFETY_PRIO_ESTOP) {
        ok = false; detail |= 64u;
    }

    const IRQn_Type others[] = {
        SAFETY_IRQ_LINE0, SAFETY_IRQ_LINE1, SAFETY_IRQ_LINE3,
        SAFETY_IRQ_LINE4, SAFETY_IRQ_LINE9_5, SAFETY_IRQ_LINE15_10
    };
    for (unsigned i = 0; i < (sizeof others / sizeof others[0]); i++) {
        if (NVIC_GetPriority(others[i]) != SAFETY_PRIO_INPUTS) {
            ok = false; detail |= 128u;
        }
    }

    if (NVIC_GetPriority(STEPGEN_DMA_IRQn) <= SAFETY_PRIO_INPUTS) {
        /* A numerically lower or equal value means the refill can block an
         * input - the inversion ADR-004 exists to prevent. */
        ok = false; detail |= 256u;
    }

    record(HV_41_EXTI_CONFIG, ok, detail, 0u);
    return ok;
}

/* ------------------------------------------------------------ HV-42 ---- */
/**
 * With the machine at rest and nothing holding a switch, all fifteen lines
 * should read HIGH: the board's external pull-ups say so (Docs/PINOUT.md).
 * A line stuck LOW here is a wiring or pull-up fault, and if it is PE2 the
 * controller will refuse to leave EMERGENCY_STOP - better to be told that
 * by a numbered test than to debug it as "the machine will not start".
 */
static bool hv42_inputs_idle(void)
{
    const uint16_t raw = safety_port_read_raw();
    const bool ok = (raw == (uint16_t)SAFETY_INPUT_MASK);
    record(HV_42_INPUTS_IDLE, ok, raw, (uint32_t)SAFETY_INPUT_MASK);
    return ok;
}

/* ------------------------------------------------------------ HV-43 ---- */
/**
 * Duration of the non-asserting input path, in CPU cycles: pending-bit
 * clear, port read, edge bookkeeping. Measured with PE2 idle, so it is the
 * bookkeeping cost ALONE and deliberately excludes the stop itself -
 * stepgen_emergency_stop() is measured by HV-05, and NVIC entry latency by
 * neither. The end-to-end number is HV-18's, on a scope.
 *
 * What this figure is for: these vectors sit above the STEP ring refill
 * (ADR-004), so their cost is subtracted from the refill's 128 us deadline
 * every time an input moves. HV-04 is the other half of that budget.
 */
static bool hv43_isr_cost(void)
{
    uint32_t worst = 0u;

    for (unsigned i = 0; i < 16u; i++) {
        const uint32_t t0 = safety_port_cycle_count();
        safety_input_on_edge();
        const uint32_t dt = safety_port_cycle_count() - t0;
        if (dt > worst) {
            worst = dt;
        }
    }

    /* No pass/fail threshold is invented here. 2000 cycles is 12 us at
     * 168 MHz, under a tenth of the refill deadline; anything near it means
     * the measurement, not the budget, needs looking at. */
    const bool ok = (worst < 2000u);
    record(HV_43_ISR_COST, ok, worst, 2000u);
    return ok;
}

/* ---------------------------------------------------------------------- */

bool safety_selftest_run_all(void)
{
    memset(s_res, 0, sizeof(s_res));

    (void)hv40_estop_interlock();
    (void)hv41_exti_config();
    (void)hv42_inputs_idle();
    (void)hv43_isr_cost();

    bool all = true;
    for (uint32_t i = 0; i < (uint32_t)HV_SAFETY_TEST_COUNT; i++) {
        all = all && s_res[i].pass;
    }
    return all;
}

const safety_hv_result_t *safety_selftest_results(void)
{
    return s_res;
}
