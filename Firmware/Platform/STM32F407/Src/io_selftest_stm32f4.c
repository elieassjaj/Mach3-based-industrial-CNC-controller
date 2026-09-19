/**
 * @file    io_selftest_stm32f4.c
 * @brief   On-target validation for the output subsystem (HV-50..HV-53).
 *
 * Read back from the hardware rather than assumed from the .ioc, because
 * the .ioc is exactly what a regeneration can change.
 */
#include "io_hw_map.h"
#include "io_selftest.h"
#include "io_outputs.h"
#include "io_spindle.h"
#include "io_port.h"
#include "safety_input.h"

#include <string.h>

static io_hv_result_t s_res[HV_IO_TEST_COUNT];

static void record(io_hv_test_t id, bool pass,
                   uint32_t measured, uint32_t expected)
{
    s_res[id].run      = true;
    s_res[id].pass     = pass;
    s_res[id].measured = measured;
    s_res[id].expected = expected;
}

/* ------------------------------------------------------------ HV-50 ---- */
/**
 * Docs/PINOUT.md requires PB8 to be driven LOW at boot so the relay
 * defaults off, and §32 requires nothing to energise before a known-safe
 * state. Both LEDs should also be at their inactive level, and the PWM
 * generator should be stopped outright rather than running at zero duty.
 */
static void hv50_outputs_safe(void)
{
    bool ok = true;
    uint32_t detail = 0u;

    if (io_port_get_relay())               { ok = false; detail |= 1u; }
    if (io_port_spindle_running())         { ok = false; detail |= 2u; }
    if (io_port_spindle_ccr() != 0u)       { ok = false; detail |= 4u; }
    if (io_outputs_actual() != 0u)         { ok = false; detail |= 8u; }

    /* And the pin itself, not just this module's opinion of it. */
    const bool relay_pin_high = (IO_GPIO->ODR & RELAY_PIN_MASK_GPIOB) != 0u;
    if (relay_pin_high != (CNC_RELAY_ACTIVE_HIGH == 0)) {
        ok = false; detail |= 16u;
    }

    record(HV_50_OUTPUTS_SAFE, ok, detail, 0u);
}

/* ------------------------------------------------------------ HV-51 ---- */
/**
 * The spindle timer as actually programmed:
 *
 *   - (PSC+1)*(ARR+1) divides the timer clock to exactly SPINDLE_PWM_HZ
 *   - ARR and CCR1 are both preloaded, so a duty change lands at a period
 *     boundary instead of mid-pulse
 *   - channel 1 output is enabled
 *   - PB4 really is in alternate-function mode on AF2
 *
 * `measured` carries the computed PWM frequency in Hz, so a failure says
 * what the spindle is actually running at rather than only that it is
 * wrong.
 */
static void hv51_pwm_config(void)
{
    const uint32_t psc  = IO_SPINDLE_TIM->PSC;
    const uint32_t arr  = IO_SPINDLE_TIM->ARR;
    const uint32_t div  = (psc + 1u) * (arr + 1u);
    const uint32_t freq = (div != 0u) ? (SPINDLE_TIMER_CLK_HZ / div) : 0u;

    bool ok = (div != 0u)
           && (SPINDLE_TIMER_CLK_HZ % div == 0u)
           && (freq == SPINDLE_PWM_HZ);

    if ((IO_SPINDLE_TIM->CR1 & TIM_CR1_ARPE) == 0u)      { ok = false; }
    if ((IO_SPINDLE_TIM->CCMR1 & TIM_CCMR1_OC1PE) == 0u) { ok = false; }
    if ((IO_SPINDLE_TIM->CCER & TIM_CCER_CC1E) == 0u)    { ok = false; }

    const uint32_t sh  = IO_SPINDLE_PIN * 2u;
    const uint32_t ash = (IO_SPINDLE_PIN % 8u) * 4u;
    if (((IO_GPIO->MODER >> sh) & 3u) != 2u)                   { ok = false; }
    if (((IO_GPIO->AFR[0] >> ash) & 0xFu) != IO_SPINDLE_AF)    { ok = false; }

    record(HV_51_PWM_CONFIG, ok, freq, SPINDLE_PWM_HZ);
}

/* ------------------------------------------------------------ HV-52 ---- */
/**
 * Without this registration an E-STOP stops the axes and leaves the relay
 * closed and the spindle turning until the superloop next runs io_poll().
 * On a machine whose relay is the spindle contactor, that is the whole
 * difference between a safe stop and a dangerous one.
 */
static void hv52_estop_kill(void)
{
    const bool ok = io_present() && safety_has_estop_action();
    record(HV_52_ESTOP_KILL, ok, ok ? 1u : 0u, 1u);
}

/* ------------------------------------------------------------ HV-53 ---- */
/**
 * The interlock, exercised for real and safely: at boot the engine is in
 * SAFE_IDLE, so a duty request must be accepted and then go nowhere.
 *
 * This is the safe half of the interlock. The dangerous half - that the
 * outputs DO come on once the machine is READY, and drop again on an
 * E-STOP - is HV-55, on a bench, with a meter and nothing dangerous wired
 * to the relay.
 */
static void hv53_interlock_holds(void)
{
    const uint16_t saved_out = io_outputs_requested();
    const uint16_t saved_spn = io_spindle_requested();

    bool ok = true;

    ok = ok && io_spindle_set_pmille(500u);
    ok = ok && io_request_outputs(CNC_OUT_BIT_RELAY, CNC_OUT_BIT_RELAY);

    io_poll(0u);

    if (io_port_spindle_running()) { ok = false; }
    if (io_port_get_relay())       { ok = false; }
    if (io_outputs_actual() != 0u) { ok = false; }

    /* Put the subsystem back exactly as it was found. */
    (void)io_spindle_set_pmille(saved_spn);
    (void)io_request_outputs((uint16_t)CNC_OUT_MASK_SUPPORTED, saved_out);
    io_poll(0u);

    record(HV_53_INTERLOCK_HOLDS, ok, ok ? 1u : 0u, 1u);
}

/* ---------------------------------------------------------------------- */

bool io_selftest_run_all(void)
{
    memset(s_res, 0, sizeof(s_res));

    hv50_outputs_safe();
    hv51_pwm_config();
    hv52_estop_kill();
    hv53_interlock_holds();

    bool all = true;
    for (uint32_t i = 0; i < (uint32_t)HV_IO_TEST_COUNT; i++) {
        all = all && s_res[i].pass;
    }
    return all;
}

const io_hv_result_t *io_selftest_results(void)
{
    return s_res;
}
