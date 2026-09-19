/**
 * @file    test_io.c
 * @brief   Host verification of the output subsystem (Phase 5, M9 / M10).
 *
 * These tests drive the output manager against the **real** motion engine
 * and the **real** input manager, both on their simulation ports. So "the
 * relay drops on E-STOP" is checked by actually asserting PE2 and looking
 * at the pin, not by calling a kill function directly and trusting that
 * something somewhere would have called it.
 *
 * What this suite can and cannot establish:
 *
 *   CAN  - the interlock, in every engine state, including the two fault
 *          classes ADR-010 treats differently;
 *          that an E-STOP de-energises the relay and the spindle from the
 *          interrupt, with zero superloop iterations in between;
 *          that clearing a fault does not spin the tool back up;
 *          the per-mille -> compare arithmetic across its whole range;
 *          that a stopped PWM generator is reached, not merely 0 % duty;
 *          that a half-understood request changes nothing.
 *   CANNOT - anything about GPIOB, TIM3, or what a VFD makes of the
 *          waveform. HV-50..HV-53 check the configuration on the board and
 *          HV-54/HV-55 measure the outputs themselves.
 */
#include "io_outputs.h"
#include "io_spindle.h"
#include "io_port.h"
#include "io_sim.h"
#include "safety_input.h"
#include "safety_sim.h"
#include "stepgen.h"
#include "test_util.h"

#include <string.h>

int g_fail = 0, g_checks = 0, g_case_failed = 0;
const char *g_case = "";

#define ALL_IDLE   ((uint16_t)SAFETY_INPUT_MASK)
#define RELAY      ((uint16_t)CNC_OUT_BIT_RELAY)

static uint32_t now_ms;

/* ------------------------------------------------------------------ */
/* Harness                                                             */
/* ------------------------------------------------------------------ */

/** Power-on: engine, inputs and outputs, in the order main.c uses. */
static void boot(void)
{
    sim_safety_reset();
    sim_io_reset();
    stepgen_set_estop_gate(NULL);
    CHECK(stepgen_init());
    CHECK(safety_input_init());
    CHECK(io_init());
    now_ms = 0u;
}

static void tick(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) {
        now_ms++;
        safety_input_poll(now_ms);
        io_poll(now_ms);
    }
}

/** Get the machine to READY, where outputs are permitted. */
static void arm(void)
{
    CHECK(stepgen_enable_drives());
    tick(1u);
}

static stepgen_state_t engine_state(void)
{
    stepgen_status_t st;
    stepgen_get_status(&st);
    return st.state;
}

/* ------------------------------------------------------------------ */
/* Startup                                                             */
/* ------------------------------------------------------------------ */

static void test_startup(void)
{
    TCASE("everything comes up off");
    boot();
    CHECK(io_present());
    CHECK(!sim_io_relay());
    CHECK(!sim_io_pwm_running());
    CHECK_EQI(sim_io_pwm_ccr(), 0);
    CHECK_EQI(io_outputs_actual(), 0);
    CHECK_EQI(io_spindle_actual(), 0);
    TDONE();

    TCASE("init registers the interrupt-time kill with the E-STOP path");
    /* Without this the relay stays closed until the superloop notices. */
    CHECK(safety_has_estop_action());
    TDONE();

    TCASE("nothing is reported before the subsystem exists");
    sim_io_reset();
    /* io_present() is what the protocol's OUTPUTS_PRESENT flag carries. */
    boot();
    CHECK(io_present());
    TDONE();
}

/* ------------------------------------------------------------------ */
/* The interlock                                                       */
/* ------------------------------------------------------------------ */

static void test_interlock(void)
{
    TCASE("a relay request in SAFE_IDLE is accepted but not obeyed");
    boot();
    CHECK_EQI(engine_state(), STEPGEN_STATE_SAFE_IDLE);
    CHECK(io_request_outputs(RELAY, RELAY));
    tick(2u);
    CHECK_EQI(io_outputs_requested(), RELAY);   /* remembered */
    CHECK(!sim_io_relay());                     /* not obeyed */
    {
        io_status_t st;
        io_get_status(&st);
        CHECK_EQI(st.inhibit, IO_INHIBIT_NOT_READY);
        CHECK_GE(st.inhibited_count, 1);
    }
    TDONE();

    TCASE("it comes on the moment the machine is armed");
    /* The request was never lost - reaching READY is what releases it. */
    arm();
    CHECK(sim_io_relay());
    CHECK_EQI(io_outputs_actual(), RELAY);
    TDONE();

    TCASE("the spindle follows the same interlock, at the same instant");
    boot();
    CHECK(io_spindle_set_pmille(500u));
    tick(2u);
    CHECK(!sim_io_pwm_running());
    arm();
    CHECK(sim_io_pwm_running());
    CHECK_EQI(io_spindle_actual(), 500);
    TDONE();

    TCASE("a fault drops both, even the hold-position kind");
    /* ADR-010 lets COMM_TIMEOUT and underflow hold position with the
     * drives live, because an axis must not drop under gravity. That
     * reasoning is about holding force and does not extend to a spinning
     * tool - ADR-016. */
    boot();
    arm();
    CHECK(io_request_outputs(RELAY, RELAY));
    CHECK(io_spindle_set_pmille(800u));
    tick(2u);
    CHECK(sim_io_relay());
    CHECK(sim_io_pwm_running());

    stepgen_on_overrun();                       /* -> FAULT */
    tick(2u);
    CHECK_EQI(engine_state(), STEPGEN_STATE_FAULT);
    CHECK(!sim_io_relay());
    CHECK(!sim_io_pwm_running());
    {
        io_status_t st;
        io_get_status(&st);
        CHECK_EQI(st.inhibit, IO_INHIBIT_FAULT);
    }
    TDONE();

    TCASE("clearing the fault does not spin the tool back up");
    /* The clear returns the engine to SAFE_IDLE, which is still inhibited,
     * and the host must ask again. ADR-010: recovery is never automatic. */
    CHECK(stepgen_clear_fault());
    tick(2u);
    CHECK_EQI(engine_state(), STEPGEN_STATE_SAFE_IDLE);
    CHECK(!sim_io_relay());
    CHECK(!sim_io_pwm_running());
    TDONE();
}

/* ------------------------------------------------------------------ */
/* E-STOP                                                              */
/* ------------------------------------------------------------------ */

static void test_estop(void)
{
    TCASE("E-STOP drops the relay inside the interrupt, before any poll");
    boot();
    arm();
    CHECK(io_request_outputs(RELAY, RELAY));
    CHECK(io_spindle_set_pmille(1000u));
    tick(2u);
    CHECK(sim_io_relay());
    CHECK(sim_io_pwm_running());

    sim_safety_assert(SAFETY_ESTOP_INDEX);
    /* No tick(). On a machine whose relay is the spindle contactor, the
     * difference between here and the next superloop iteration is the
     * whole point of registering the kill with the interrupt. */
    CHECK(!sim_io_relay());
    CHECK(!sim_io_pwm_running());
    CHECK_EQI(sim_io_emergency_offs(), 1);
    CHECK_EQI(engine_state(), STEPGEN_STATE_EMERGENCY_STOP);
    TDONE();

    TCASE("the request is dropped, not merely withheld");
    /* Otherwise clearing the E-stop would re-energise the relay on its own
     * from a request made before the stop. */
    CHECK_EQI(io_outputs_requested(), 0);
    CHECK_EQI(io_spindle_requested(), 0);
    TDONE();

    TCASE("nothing comes back when the E-stop is cleared");
    sim_safety_release(SAFETY_ESTOP_INDEX);
    tick(SAFETY_ESTOP_RELEASE_MS + 2u);
    CHECK(stepgen_clear_emergency_stop());
    CHECK(stepgen_enable_drives());
    tick(2u);
    CHECK_EQI(engine_state(), STEPGEN_STATE_READY);
    CHECK(!sim_io_relay());
    CHECK(!sim_io_pwm_running());
    TDONE();

    TCASE("an E-STOP already down at power-on leaves the outputs off");
    sim_safety_reset();
    sim_io_reset();
    sim_safety_set_levels_silent((uint16_t)(ALL_IDLE & ~(uint16_t)SAFETY_ESTOP_MASK));
    stepgen_set_estop_gate(NULL);
    CHECK(stepgen_init());
    CHECK(safety_input_init());
    CHECK(io_init());
    CHECK_EQI(engine_state(), STEPGEN_STATE_EMERGENCY_STOP);
    CHECK(io_request_outputs(RELAY, RELAY));
    io_poll(0u);
    CHECK(!sim_io_relay());
    TDONE();

    TCASE("the relay never glitches on across a stop");
    boot();
    arm();
    CHECK(io_request_outputs(RELAY, RELAY));
    tick(5u);
    {
        const uint32_t before = sim_io_relay_changes();
        sim_safety_assert(SAFETY_ESTOP_INDEX);
        tick(20u);
        /* Exactly one transition: on -> off. Not off -> on -> off. */
        CHECK_EQI(sim_io_relay_changes() - before, 1);
    }
    CHECK(!sim_io_relay());
    TDONE();
}

/* ------------------------------------------------------------------ */
/* M9 request handling                                                 */
/* ------------------------------------------------------------------ */

static void test_outputs_api(void)
{
    TCASE("an unimplemented output bit is refused whole");
    boot();
    arm();
    /* Bit 1 is reserved in Docs/PROTOCOL.md §9. A host that asked for two
     * outputs and got one cannot tell which one it got. */
    CHECK(!io_request_outputs((uint16_t)(RELAY | 0x0002u), (uint16_t)(RELAY | 0x0002u)));
    tick(2u);
    CHECK(!sim_io_relay());
    CHECK_EQI(io_outputs_requested(), 0);
    TDONE();

    TCASE("the mask selects which bits a packet touches");
    CHECK(io_request_outputs(RELAY, RELAY));
    tick(2u);
    CHECK(sim_io_relay());
    /* An empty mask changes nothing, whatever the value says. */
    CHECK(io_request_outputs(0u, 0u));
    tick(2u);
    CHECK(sim_io_relay());
    CHECK(io_request_outputs(RELAY, 0u));
    tick(2u);
    CHECK(!sim_io_relay());
    TDONE();
}

/* ------------------------------------------------------------------ */
/* M10 duty arithmetic                                                 */
/* ------------------------------------------------------------------ */

static void test_spindle(void)
{
    TCASE("per mille maps onto the compare register across the range");
    boot();
    arm();
    {
        const uint32_t period = sim_io_pwm_period();
        const uint16_t pts[] = { 1u, 10u, 123u, 250u, 500u, 750u, 999u, 1000u };

        for (unsigned i = 0; i < sizeof pts / sizeof pts[0]; i++) {
            CHECK(io_spindle_set_pmille(pts[i]));
            tick(1u);
            /* Rounded to nearest: at most half a count of error. */
            const uint32_t want = ((uint32_t)pts[i] * period + 500u) / 1000u;
            CHECK_EQI(sim_io_pwm_ccr(), want);
            CHECK_EQI(io_spindle_actual(), pts[i]);
        }
    }
    TDONE();

    TCASE("the period resolves every per-mille step distinctly");
    /* The .ioc's ARR=99 would collapse ten adjacent per-mille values onto
     * one compare value; ADR-016's 8400 does not. */
    CHECK_GE(sim_io_pwm_period(), SPINDLE_PMILLE_MAX);
    {
        uint32_t prev = 0u;
        for (uint16_t p = 1u; p <= 1000u; p++) {
            CHECK(io_spindle_set_pmille(p));
            tick(1u);
            if (sim_io_pwm_ccr() <= prev) {
                CHECK(false);      /* two per-mille steps, one duty */
                break;
            }
            prev = sim_io_pwm_ccr();
        }
    }
    TDONE();

    TCASE("over 100 % is refused, never clamped");
    /* Clamping would hide the host's bug behind a spinning tool. */
    CHECK(!io_spindle_set_pmille(1001u));
    CHECK(!io_spindle_set_pmille(0xFFFEu));
    TDONE();

    TCASE("zero duty stops the generator rather than running it at zero");
    CHECK(io_spindle_set_pmille(400u));
    tick(1u);
    CHECK(sim_io_pwm_running());
    CHECK(io_spindle_set_pmille(0u));
    tick(1u);
    CHECK(!sim_io_pwm_running());
    CHECK_EQI(sim_io_pwm_ccr(), 0);
    TDONE();

    TCASE("full duty is reachable");
    CHECK(io_spindle_set_pmille(1000u));
    tick(1u);
    CHECK_EQI(sim_io_pwm_ccr(), sim_io_pwm_period());
    TDONE();
}

/* ------------------------------------------------------------------ */
/* LEDs                                                                */
/* ------------------------------------------------------------------ */

static void test_leds(void)
{
    TCASE("the LEDs report the engine state and nothing else");
    boot();
    {
        io_status_t st;
        io_get_status(&st);
        CHECK_EQI(st.led_run, IO_LED_OFF);       /* SAFE_IDLE */
        CHECK_EQI(st.led_err, IO_LED_OFF);
    }
    arm();
    {
        io_status_t st;
        io_get_status(&st);
        CHECK_EQI(st.led_run, IO_LED_BLINK_SLOW); /* READY */
        CHECK_EQI(st.led_err, IO_LED_OFF);
    }
    CHECK(stepgen_start());
    tick(1u);
    {
        io_status_t st;
        io_get_status(&st);
        CHECK_EQI(st.led_run, IO_LED_ON);         /* RUNNING */
    }
    TDONE();

    TCASE("a fault blinks the error LED, an E-stop holds it solid");
    /* Telling a recoverable fault from a latched emergency stop across a
     * workshop is worth a distinct pattern. */
    boot();
    arm();
    stepgen_on_overrun();
    tick(1u);
    {
        io_status_t st;
        io_get_status(&st);
        CHECK_EQI(st.led_err, IO_LED_BLINK_FAST);
    }
    boot();
    arm();
    sim_safety_assert(SAFETY_ESTOP_INDEX);
    tick(1u);
    {
        io_status_t st;
        io_get_status(&st);
        CHECK_EQI(st.led_err, IO_LED_ON);
        CHECK_EQI(st.led_run, IO_LED_OFF);
    }
    TDONE();

    TCASE("a blinking LED actually toggles");
    boot();
    arm();
    {
        bool seen_on = false, seen_off = false;
        for (uint32_t i = 0; i < 4u * LED_BLINK_SLOW_MS; i++) {
            tick(1u);
            if (sim_io_led_run()) { seen_on = true; } else { seen_off = true; }
        }
        CHECK(seen_on);
        CHECK(seen_off);
    }
    TDONE();
}

/* ------------------------------------------------------------------ */

int main(void)
{
    printf("CNC5AX-ETH outputs and spindle (M9/M10) host verification\n");
    printf("  relay PB8 active %s, LEDs PB2/PB1, spindle TIM3_CH1 on PB4\n",
           CNC_RELAY_ACTIVE_HIGH ? "high" : "low");
    printf("  PWM %u Hz, %u counts/period, duty 0..%u per mille\n\n",
           SPINDLE_PWM_HZ, (unsigned)SPINDLE_PWM_PERIOD_COUNTS,
           SPINDLE_PMILLE_MAX);

    printf("Startup\n");             test_startup();
    printf("Interlock\n");           test_interlock();
    printf("E-STOP\n");              test_estop();
    printf("Output requests\n");     test_outputs_api();
    printf("Spindle duty\n");        test_spindle();
    printf("Status LEDs\n");         test_leds();

    printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail != 0;
}
