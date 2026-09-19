/**
 * @file    safety_input.c
 * @brief   Digital-input manager and E-STOP path (M3), portable core.
 *
 * Two writers, and the split between them is the whole design:
 *
 *   interrupt context   safety_input_on_estop_edge() / _on_edge()
 *                       writes ONLY s.raw, s.edges, s.edge_count[],
 *                       s.estop_asserted, s.estop_latched, s.estop_asserts.
 *
 *   superloop           safety_input_poll()
 *                       composes everything else from a fresh port read.
 *
 * The interrupt never performs a read-modify-write on a word the superloop
 * also writes, so no ISR observation can be lost to a half-finished update
 * in the main loop - which is the failure mode that would matter here,
 * since the observation that gets lost would be an E-STOP.
 *
 * The one remaining shared bool, s.estop_asserted, is set by the ISR and
 * cleared by the release timer in poll. That clear re-reads the pin
 * immediately afterwards, so an assertion arriving inside the write window
 * is picked up rather than dropped. Masking EXTI2 to close the window
 * instead would make the E-STOP line briefly deaf, which §8 does not allow
 * at any price.
 */
#include "safety_input.h"
#include "safety_port.h"
#include "stepgen.h"

#include <string.h>

#define ESTOP_BIT   ((uint16_t)SAFETY_ESTOP_MASK)
#define IN_MASK     ((uint16_t)SAFETY_INPUT_MASK)

#define CHATTER_WINDOW_MS  1000u

/* The filter below reads an idle input as HIGH. That is a board fact
 * (external pull-ups, Docs/PINOUT.md), not a preference, so it is asserted
 * rather than made configurable behind a runtime branch. */
_Static_assert(SAFETY_INPUT_ACTIVE_LOW == 1,
               "the input filter assumes active-low inputs (Docs/PINOUT.md)");
_Static_assert(SAFETY_ESTOP_INDEX < SAFETY_INPUT_COUNT,
               "E-STOP must be one of the managed inputs");
_Static_assert(SAFETY_INPUT_MASK == ((1u << SAFETY_INPUT_COUNT) - 1u),
               "input mask disagrees with the input count");

static struct {
    /* --- written in interrupt context ------------------------------- */
    volatile uint16_t raw;                          /* ISR's last sample  */
    volatile uint32_t edges;
    volatile uint32_t edge_count[SAFETY_INPUT_COUNT];
    volatile bool     estop_asserted;
    volatile bool     estop_latched;
    volatile uint32_t estop_asserts;

    /* --- written in the superloop ----------------------------------- */
    uint16_t asserted;
    uint16_t raw_level;
    uint16_t pending;                               /* filter in progress */
    uint16_t changed;                               /* read-and-clear     */
    uint16_t ever;
    uint16_t chattering;
    uint32_t since_ms[SAFETY_INPUT_COUNT];
    uint32_t transitions;
    uint32_t last_change_ms;
    uint32_t estop_last_ms;
    uint32_t estop_idle_since_ms;
    bool     estop_idle_timing;
    uint32_t chatter_mark_ms;
    uint32_t chatter_base[SAFETY_INPUT_COUNT];
    bool     present;

    /* Registered by the output subsystem (M9/M10) so a relay and a spindle
     * de-energise on the PE2 edge rather than at the next poll. Set once at
     * init and never cleared, so the ISR never reads a half-written
     * pointer. */
    safety_estop_action_t estop_action;
} s;

/* ---------------------------------------------------------------------- */
/* Interrupt context                                                       */
/* ---------------------------------------------------------------------- */

/**
 * Record an edge against every line whose level differs from what the last
 * ISR saw. Level-based, so a coalesced or missed edge costs a count and
 * nothing else - the next entry re-reads the port and resynchronises.
 */
static void capture_edges(uint16_t now)
{
    uint16_t ch = (uint16_t)((s.raw ^ now) & IN_MASK);

    s.raw = now;
    s.edges++;

    while (ch != 0u) {
        const unsigned b = (unsigned)__builtin_ctz((unsigned)ch);
        if (b < SAFETY_INPUT_COUNT) {
            s.edge_count[b]++;
        }
        ch = (uint16_t)(ch & (uint16_t)(ch - 1u));
    }
}

void safety_input_on_estop_edge(void)
{
    /* The stop comes first, before any bookkeeping. stepgen_emergency_stop()
     * begins with register writes that halt the timebase, force every STEP
     * pin low and deassert EN, so the machine is already stopping while the
     * rest of this function runs. Repeating it on a bouncing contact is
     * harmless: the same writes land twice. */
    if (safety_port_estop_raw_asserted()) {
        stepgen_emergency_stop();

        /* Motion first, everything else second. The axes are the larger
         * hazard and their stop is pure register writes; the outputs
         * follow within the same interrupt, still far ahead of the
         * superloop. */
        if (s.estop_action != NULL) {
            s.estop_action();
        }

        if (!s.estop_asserted) {
            s.estop_asserts++;
        }
        s.estop_asserted = true;
        s.estop_latched  = true;
    }

    /* A release is recorded as an edge and nothing more. Un-stopping a
     * machine is the release timer's decision, in the superloop, and then
     * only as permission for an explicit operator clear - never an
     * automatic recovery (ADR-010). */
    capture_edges((uint16_t)(safety_port_read_raw() & IN_MASK));
}

void safety_input_on_edge(void)
{
    capture_edges((uint16_t)(safety_port_read_raw() & IN_MASK));
}

/* ---------------------------------------------------------------------- */
/* Superloop                                                               */
/* ---------------------------------------------------------------------- */

/** Assert from poll: the level says asserted but no interrupt said so. */
static void estop_assert_from_poll(uint32_t now_ms)
{
    stepgen_emergency_stop();
    if (s.estop_action != NULL) {
        s.estop_action();
    }
    if (!s.estop_asserted) {
        s.estop_asserts++;
    }
    s.estop_asserted = true;
    s.estop_latched  = true;
    s.estop_last_ms  = now_ms;
}

static void service_estop(uint16_t raw, uint32_t now_ms)
{
    const bool raw_asserted = ((raw & ESTOP_BIT) == 0u);   /* LOW = asserted */

    if (raw_asserted) {
        s.estop_idle_timing = false;
        if (!s.estop_asserted) {
            /* Covers the one case an edge cannot: PE2 already down at boot,
             * or an edge lost while the line was masked by a self-test. */
            estop_assert_from_poll(now_ms);
        }
        return;
    }

    if (!s.estop_asserted) {
        return;
    }

    if (!s.estop_idle_timing) {
        s.estop_idle_timing   = true;
        s.estop_idle_since_ms = now_ms;
        return;
    }

    if ((uint32_t)(now_ms - s.estop_idle_since_ms) >= SAFETY_ESTOP_RELEASE_MS) {
        s.estop_asserted    = false;
        s.estop_idle_timing = false;
        s.transitions++;
        s.changed        = (uint16_t)(s.changed | ESTOP_BIT);
        s.last_change_ms = now_ms;

        /* Close the race against an assertion that landed inside the write
         * above: re-read the pin rather than trusting the decision we just
         * made from a sample that is now microseconds old. */
        if (safety_port_estop_raw_asserted()) {
            estop_assert_from_poll(now_ms);
        }
    }
}

/** Stable-for-T filter on the 14 non-E-STOP inputs. */
static void service_filter(uint16_t raw, uint32_t now_ms)
{
    const uint16_t want = (uint16_t)(~raw & IN_MASK);   /* active low */

    for (unsigned b = 0; b < SAFETY_INPUT_COUNT; b++) {
        const uint16_t bit = (uint16_t)(1u << b);

        if (b == SAFETY_ESTOP_INDEX) {
            continue;                       /* owned by service_estop() */
        }

        if (((want ^ s.asserted) & bit) == 0u) {
            s.pending = (uint16_t)(s.pending & ~bit);
            continue;
        }

        if ((s.pending & bit) == 0u) {
            s.pending     = (uint16_t)(s.pending | bit);
            s.since_ms[b] = now_ms;
            continue;
        }

        if ((uint32_t)(now_ms - s.since_ms[b]) >= SAFETY_DEBOUNCE_MS) {
            s.asserted = (uint16_t)(s.asserted ^ bit);
            s.pending  = (uint16_t)(s.pending & ~bit);
            s.changed  = (uint16_t)(s.changed | bit);
            if ((s.asserted & bit) != 0u) {
                s.ever = (uint16_t)(s.ever | bit);
            }
            s.transitions++;
            s.last_change_ms = now_ms;
        }
    }
}

/**
 * Per-line edge rate over a one-second window.
 *
 * A count can land on either side of a window boundary, which is harmless:
 * this is a "that switch is failing" indicator, not a measurement. It
 * exists because these vectors sit at NVIC priority 1, ABOVE the STEP ring
 * refill at 2 (ADR-004) - a chattering contact spends real time in an
 * interrupt that can preempt motion, and that should be visible rather
 * than merely felt.
 */
static void service_chatter(uint32_t now_ms)
{
    if ((uint32_t)(now_ms - s.chatter_mark_ms) < CHATTER_WINDOW_MS) {
        return;
    }

    uint16_t mask = 0u;
    for (unsigned b = 0; b < SAFETY_INPUT_COUNT; b++) {
        const uint32_t n = s.edge_count[b];
        if ((uint32_t)(n - s.chatter_base[b]) >= SAFETY_CHATTER_EDGES_PER_S) {
            mask = (uint16_t)(mask | (uint16_t)(1u << b));
        }
        s.chatter_base[b] = n;
    }
    s.chattering      = mask;
    s.chatter_mark_ms = now_ms;
}

void safety_input_poll(uint32_t now_ms)
{
    if (!s.present) {
        return;
    }

    /* Ground truth, every poll. The filter never runs off the ISR shadow,
     * so it cannot inherit a missed edge. */
    const uint16_t raw = (uint16_t)(safety_port_read_raw() & IN_MASK);
    s.raw_level = raw;

    service_estop(raw, now_ms);
    service_filter(raw, now_ms);
    service_chatter(now_ms);

    /* PE2's published bit is the E-STOP state machine's, not the filter's:
     * asserted the instant the interrupt saw it, released only after the
     * stable-idle window. */
    if (s.estop_asserted) {
        s.asserted = (uint16_t)(s.asserted | ESTOP_BIT);
        s.ever     = (uint16_t)(s.ever | ESTOP_BIT);
    } else {
        s.asserted = (uint16_t)(s.asserted & ~ESTOP_BIT);
    }
}

/* ---------------------------------------------------------------------- */
/* Init                                                                    */
/* ---------------------------------------------------------------------- */

void safety_set_estop_action(safety_estop_action_t fn)
{
    s.estop_action = fn;
}

bool safety_has_estop_action(void)
{
    return s.estop_action != NULL;
}

bool safety_input_init(void)
{
    /* Clears any registered E-STOP action too, which is why io_init()
     * registers its kill AFTER this runs - see main.c's ordering. */
    memset(&s, 0, sizeof(s));

    if (!safety_port_init()) {
        return false;
    }

    /* Establish the initial state before any edge can arrive. Everything
     * currently asserted is published as asserted straight away rather than
     * waiting for the filter: at boot there is no previous state for an
     * input to be bouncing away from. */
    const uint16_t raw = (uint16_t)(safety_port_read_raw() & IN_MASK);
    s.raw       = raw;
    s.raw_level = raw;
    s.asserted  = (uint16_t)(~raw & IN_MASK);
    s.ever      = s.asserted;
    s.present   = true;

    /* §32 startup safety: an E-STOP that is already down at power-on has
     * no edge left to give. Honour the level. */
    if ((raw & ESTOP_BIT) == 0u) {
        estop_assert_from_poll(0u);
    }

    /* ADR-010's "never while the input still reads asserted" clause. The
     * motion engine cannot see PE2, so it is handed the question as a
     * predicate; registering it here means the interlock exists as soon as
     * the subsystem does, and cannot be forgotten at a call site. */
    stepgen_set_estop_gate(safety_estop_released);

    safety_port_arm();
    return true;
}

/* ---------------------------------------------------------------------- */
/* Readers                                                                 */
/* ---------------------------------------------------------------------- */

uint16_t safety_inputs(void)           { return s.asserted;  }
uint16_t safety_inputs_raw_level(void) { return s.raw_level; }
bool     safety_estop_asserted(void)   { return s.estop_asserted; }
bool     safety_estop_released(void)   { return !s.estop_asserted; }
bool     safety_input_present(void)    { return s.present; }

uint16_t safety_input_take_changed(void)
{
    const uint16_t c = s.changed;
    s.changed = 0u;
    return c;
}

void safety_input_get_status(safety_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->asserted       = s.asserted;
    out->raw_level      = s.raw_level;
    out->settling       = s.pending;
    out->ever_asserted  = s.ever;
    out->chattering     = s.chattering;
    out->estop_asserted = s.estop_asserted;
    out->estop_latched  = s.estop_latched;
    out->estop_asserts  = s.estop_asserts;
    out->estop_last_ms  = s.estop_last_ms;
    out->edges          = s.edges;
    out->transitions    = s.transitions;
    out->last_change_ms = s.last_change_ms;
    out->present        = s.present;
}

bool safety_input_clear_latches(void)
{
    /* Refused while the condition is live, for the same reason ADR-010
     * refuses to clear an E-stop that is still down: a latch you can wipe
     * without fixing anything is not a latch. */
    if (s.estop_asserted) {
        return false;
    }
    s.estop_latched = false;
    s.ever          = s.asserted;
    s.chattering    = 0u;
    return true;
}
