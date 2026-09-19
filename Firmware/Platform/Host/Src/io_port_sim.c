/**
 * @file    io_port_sim.c
 * @brief   Host-side simulation port for the output subsystem.
 *
 * Replaces GPIOB and TIM3 with plain variables. It models the one
 * distinction that matters and that a naive mock would lose: a stopped PWM
 * generator is not the same thing as a running one at zero duty, and the
 * core is expected to reach for the former.
 *
 * Polarity is applied here exactly as the real port applies it, so the
 * tests exercise the same CNC_RELAY_ACTIVE_HIGH / CNC_LED_ACTIVE_HIGH
 * decisions rather than a simplified version of them.
 */
#include "io_port.h"
#include "io_sim.h"
#include "cnc_io_config.h"

/* Pin levels, as they would be on the port. */
static bool     s_relay_pin;
static bool     s_led_run_pin;
static bool     s_led_err_pin;

static bool     s_pwm_running;
static uint32_t s_ccr;
static uint32_t s_emergency_offs;
static uint32_t s_relay_changes;
static bool     s_relay_logical;

/* The simulated period matches the real port's, so the per-mille
 * arithmetic under test is the arithmetic that will run on the board. */
#define SIM_PERIOD   ((uint32_t)SPINDLE_PWM_PERIOD_COUNTS)

static void set_relay_logical(bool on)
{
    if (on != s_relay_logical) {
        s_relay_changes++;
        s_relay_logical = on;
    }
}

/* ------------------------------------------------------------- port ---- */

bool io_port_init(void)
{
    s_relay_pin   = (CNC_RELAY_ACTIVE_HIGH == 0);   /* inactive level */
    s_led_run_pin = (CNC_LED_ACTIVE_HIGH == 0);
    s_led_err_pin = (CNC_LED_ACTIVE_HIGH == 0);
    s_relay_logical = false;
    s_pwm_running = false;
    s_ccr         = 0u;
    return true;
}

void io_port_set_relay(bool on)
{
    s_relay_pin = (CNC_RELAY_ACTIVE_HIGH != 0) ? on : !on;
    set_relay_logical(on);
}

bool io_port_get_relay(void)
{
    return (CNC_RELAY_ACTIVE_HIGH != 0) ? s_relay_pin : !s_relay_pin;
}

void io_port_set_led_run(bool on)
{
    s_led_run_pin = (CNC_LED_ACTIVE_HIGH != 0) ? on : !on;
}

void io_port_set_led_err(bool on)
{
    s_led_err_pin = (CNC_LED_ACTIVE_HIGH != 0) ? on : !on;
}

void io_port_spindle_set_ccr(uint32_t ccr)
{
    /* Same ceiling as the real port: CCR == period is 100 % duty, and is
     * legitimate. Clamping to period-1 here would have hidden the fact
     * that the hardware port did the same thing. */
    s_ccr         = (ccr > SIM_PERIOD) ? SIM_PERIOD : ccr;
    s_pwm_running = true;
}

void io_port_spindle_stop(void)
{
    s_ccr         = 0u;
    s_pwm_running = false;
}

bool     io_port_spindle_running(void) { return s_pwm_running; }
uint32_t io_port_spindle_ccr(void)     { return s_ccr; }
uint32_t io_port_spindle_period(void)  { return SIM_PERIOD; }

void io_port_emergency_off(void)
{
    s_emergency_offs++;
    io_port_set_relay(false);
    io_port_spindle_stop();
}

/* -------------------------------------------------------------- sim ---- */

void sim_io_reset(void)
{
    s_pwm_running    = false;
    s_ccr            = 0u;
    s_emergency_offs = 0u;
    s_relay_changes  = 0u;
    s_relay_logical  = false;
    s_relay_pin      = (CNC_RELAY_ACTIVE_HIGH == 0);
    s_led_run_pin    = (CNC_LED_ACTIVE_HIGH == 0);
    s_led_err_pin    = (CNC_LED_ACTIVE_HIGH == 0);
}

bool sim_io_relay(void)   { return io_port_get_relay(); }

bool sim_io_led_run(void)
{
    return (CNC_LED_ACTIVE_HIGH != 0) ? s_led_run_pin : !s_led_run_pin;
}

bool sim_io_led_err(void)
{
    return (CNC_LED_ACTIVE_HIGH != 0) ? s_led_err_pin : !s_led_err_pin;
}

bool     sim_io_pwm_running(void)    { return s_pwm_running; }
uint32_t sim_io_pwm_ccr(void)        { return s_ccr; }
uint32_t sim_io_pwm_period(void)     { return SIM_PERIOD; }
uint32_t sim_io_emergency_offs(void) { return s_emergency_offs; }
uint32_t sim_io_relay_changes(void)  { return s_relay_changes; }
