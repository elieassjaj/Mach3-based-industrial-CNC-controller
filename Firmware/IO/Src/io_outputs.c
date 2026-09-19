/**
 * @file    io_outputs.c
 * @brief   Output Manager (M9) and the subsystem interlock for M9 + M10.
 *
 * The interlock is the whole point of this file. Everything else - one
 * GPIO bit and two LEDs - is trivial; deciding when that bit is allowed to
 * be high is not, and getting it wrong means a contactor stays closed
 * through an emergency stop.
 *
 * Two writers, split the same way the safety subsystem splits them:
 *
 *   interrupt context   io_emergency_off()   - port kill + drop the requests
 *   superloop           io_poll()            - recompute and apply
 *
 * io_poll() recomputes the inhibit mask from the motion engine's state on
 * every iteration and never from a cached decision, so an E-STOP that
 * lands between two of this file's own statements cannot leave an output
 * energised: the next poll reads EMERGENCY_STOP from the engine and holds
 * everything off regardless of what this file thought a moment earlier.
 */
#include "io_outputs.h"
#include "io_spindle.h"
#include "io_port.h"
#include "safety_input.h"
#include "stepgen.h"

#include <string.h>

static struct {
    uint16_t         out_requested;
    uint16_t         out_actual;
    uint32_t         inhibit;
    uint32_t         inhibited_count;
    io_led_pattern_t led_run;
    io_led_pattern_t led_err;
    bool             present;
} s;

_Static_assert(CNC_OUT_MASK_SUPPORTED == CNC_OUT_BIT_RELAY,
               "only the relay is implemented; widen both or neither");

/* ---------------------------------------------------------------------- */
/* Interlock                                                               */
/* ---------------------------------------------------------------------- */

/**
 * ADR-016: outputs are permitted only in READY and RUNNING.
 *
 * Every fault class inhibits them, including the two that ADR-010 lets
 * hold position with the drives still energised (COMM_TIMEOUT and buffer
 * underflow). That exception exists so an axis does not drift or drop
 * under gravity when the link pauses - it is about holding force, and a
 * spinning tool is not holding force. When the command stream stops, the
 * axes stop moving; leaving the spindle turning at that point burns a
 * stationary tool into the work and leaves a machine doing work with
 * nobody driving it. So the exception is not extended here.
 *
 * SAFE_IDLE inhibits too: reaching READY takes an explicit
 * CONTROL:ENABLE_DRIVES from the host, and that deliberate act is the
 * point at which the machine is live (§32).
 */
static uint32_t compute_inhibit(stepgen_state_t st)
{
    switch (st) {
    case STEPGEN_STATE_EMERGENCY_STOP: return IO_INHIBIT_ESTOP;
    case STEPGEN_STATE_FAULT:          return IO_INHIBIT_FAULT;
    case STEPGEN_STATE_READY:
    case STEPGEN_STATE_RUNNING:        return IO_INHIBIT_NONE;
    case STEPGEN_STATE_UNINIT:
    case STEPGEN_STATE_SAFE_IDLE:
    default:                           return IO_INHIBIT_NOT_READY;
    }
}

/* ---------------------------------------------------------------------- */
/* LEDs                                                                    */
/* ---------------------------------------------------------------------- */

/**
 * The LEDs report the engine state and are never commandable by the host.
 *
 * A host-controllable "error" light can be made to lie, and this pair is
 * the only diagnosis available on a board with no display - during
 * bring-up it is frequently the only thing that says anything at all.
 *
 * FAULT blinks and EMERGENCY_STOP is solid, deliberately: one is
 * recoverable with a clear, the other needs somebody to release a physical
 * button first, and telling them apart from across a workshop is worth a
 * distinct pattern.
 */
static void led_patterns_for(stepgen_state_t st,
                             io_led_pattern_t *run, io_led_pattern_t *err)
{
    switch (st) {
    case STEPGEN_STATE_RUNNING:
        *run = IO_LED_ON;          *err = IO_LED_OFF;        break;
    case STEPGEN_STATE_READY:
        *run = IO_LED_BLINK_SLOW;  *err = IO_LED_OFF;        break;
    case STEPGEN_STATE_FAULT:
        *run = IO_LED_OFF;         *err = IO_LED_BLINK_FAST; break;
    case STEPGEN_STATE_EMERGENCY_STOP:
        *run = IO_LED_OFF;         *err = IO_LED_ON;         break;
    case STEPGEN_STATE_SAFE_IDLE:
    case STEPGEN_STATE_UNINIT:
    default:
        *run = IO_LED_OFF;         *err = IO_LED_OFF;        break;
    }
}

static bool led_level(io_led_pattern_t p, uint32_t now_ms)
{
    switch (p) {
    case IO_LED_ON:         return true;
    case IO_LED_BLINK_SLOW: return ((now_ms / LED_BLINK_SLOW_MS) & 1u) != 0u;
    case IO_LED_BLINK_FAST: return ((now_ms / LED_BLINK_FAST_MS) & 1u) != 0u;
    case IO_LED_OFF:
    default:                return false;
    }
}

/* ---------------------------------------------------------------------- */
/* Init                                                                    */
/* ---------------------------------------------------------------------- */

bool io_init(void)
{
    memset(&s, 0, sizeof(s));
    io_spindle_reset();

    if (!io_port_init()) {
        return false;
    }

    s.inhibit = IO_INHIBIT_NOT_READY;
    s.present = true;

    /* The E-STOP must de-energise a contactor on the PE2 edge, not one
     * superloop iteration later, so the kill is registered with the
     * interrupt that already runs at NVIC priority 0. Registering it here
     * rather than asking a caller to do it means it cannot be forgotten -
     * the same reasoning as the ADR-010 release interlock in M3. */
    safety_set_estop_action(io_emergency_off);

    return true;
}

bool io_present(void) { return s.present; }

/* ---------------------------------------------------------------------- */
/* Superloop                                                               */
/* ---------------------------------------------------------------------- */

void io_poll(uint32_t now_ms)
{
    if (!s.present) {
        return;
    }

    stepgen_status_t es;
    stepgen_get_status(&es);

    const uint32_t inhibit = compute_inhibit(es.state);
    s.inhibit = inhibit;

    const bool blocked = (inhibit != IO_INHIBIT_NONE);

    /* Relay. */
    const bool want_relay = !blocked
                          && ((s.out_requested & CNC_OUT_BIT_RELAY) != 0u);
    io_port_set_relay(want_relay);
    s.out_actual = want_relay ? (uint16_t)CNC_OUT_BIT_RELAY : 0u;

    /* Spindle - same interlock, same instant. */
    io_spindle_apply(blocked);

    /* LEDs. */
    led_patterns_for(es.state, &s.led_run, &s.led_err);
    io_port_set_led_run(led_level(s.led_run, now_ms));
    io_port_set_led_err(led_level(s.led_err, now_ms));
}

/* ---------------------------------------------------------------------- */
/* Host requests                                                           */
/* ---------------------------------------------------------------------- */

bool io_request_outputs(uint16_t mask, uint16_t value)
{
    if ((mask & (uint16_t)~CNC_OUT_MASK_SUPPORTED) != 0u) {
        /* Refused whole, never in part. A host that asked for two outputs
         * and got one has no way to discover which one it got. */
        return false;
    }

    s.out_requested = (uint16_t)((s.out_requested & ~mask) | (value & mask));

    if (s.inhibit != IO_INHIBIT_NONE) {
        /* Accepted and recorded, but it will not reach the pin until the
         * machine is in a state that permits it. Counted so "I set the
         * relay and nothing happened" has an answer in the status. */
        s.inhibited_count++;
    }
    return true;
}

uint16_t io_outputs_requested(void) { return s.out_requested; }
uint16_t io_outputs_actual(void)    { return s.out_actual;    }

void io_emergency_off(void)
{
    /* Register writes first: the relay drops and the PWM generator stops
     * before any of the bookkeeping below runs. */
    io_port_emergency_off();
    io_spindle_force_off();

    /* Drop the requests, do not merely withhold them. Otherwise clearing
     * the E-stop would re-energise the relay on its own, from a request
     * made before the stop - and ADR-010 is explicit that recovery is
     * never automatic. */
    s.out_requested = 0u;
    s.out_actual    = 0u;
    s.inhibit       = IO_INHIBIT_ESTOP;
}

void io_get_status(io_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->out_requested     = s.out_requested;
    out->out_actual        = s.out_actual;
    out->inhibit           = s.inhibit;
    out->inhibited_count   = s.inhibited_count;
    out->led_run           = s.led_run;
    out->led_err           = s.led_err;
    out->spindle_requested = io_spindle_requested();
    out->spindle_actual    = io_spindle_actual();
    out->spindle_ccr       = io_port_spindle_ccr();
    out->spindle_running   = io_port_spindle_running();
    out->present           = s.present;
}
