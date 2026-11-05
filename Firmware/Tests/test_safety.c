/**
 * @file    test_safety.c
 * @brief   Host verification of the digital-input manager (Phase 4, M3).
 *
 * These tests drive the safety core against the **real** Phase 1 motion
 * engine on its simulation port. So "the E-STOP fires" is checked by asking
 * the engine what state it ended up in, not by counting calls to a mock,
 * and "the machine will not restart" is checked by actually trying to
 * clear the emergency stop and being refused.
 *
 * What this suite can and cannot establish:
 *
 *   CAN  - the filter's timing, in milliseconds, including the cases a
 *          bench makes awkward: a bounce of an exact length, an E-STOP
 *          already down at power-on, an edge that is never delivered;
 *          the active-low normalisation the protocol depends on;
 *          that assertion is immediate and release is not;
 *          that ADR-010's physical-release interlock really blocks a clear.
 *   CANNOT - anything about EXTI, the NVIC, or how long any of it takes on
 *          silicon. HV-40..HV-43 check the configuration on the board and
 *          HV-18 measures the E-STOP on a scope.
 */
#include "safety_input.h"
#include "safety_port.h"
#include "safety_sim.h"
#include "stepgen.h"
#include "test_util.h"

#include <string.h>

int g_fail = 0, g_checks = 0, g_case_failed = 0;
const char *g_case = "";

#define ALL_IDLE    ((uint16_t)SAFETY_INPUT_MASK)
#define ESTOP_BIT   ((uint16_t)SAFETY_ESTOP_MASK)

/* A non-E-STOP input to exercise the ordinary filter with. */
#define IN_A        7u
#define IN_A_BIT    ((uint16_t)(1u << IN_A))
#define IN_B        11u
#define IN_B_BIT    ((uint16_t)(1u << IN_B))

/* ------------------------------------------------------------------ */
/* Harness                                                             */
/* ------------------------------------------------------------------ */

static uint32_t now_ms;

/** Power-on with every input in the state @p levels describes. */
static void boot(uint16_t levels)
{
    sim_safety_reset();
    sim_safety_set_levels_silent(levels);
    stepgen_set_estop_gate(NULL);
    CHECK(stepgen_init());
    CHECK(safety_input_init());
    now_ms = 0u;
}

/** Advance the millisecond clock, polling once per millisecond. */
static void advance(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) {
        now_ms++;
        safety_input_poll(now_ms);
    }
}

static stepgen_state_t engine_state(void)
{
    stepgen_status_t st;
    stepgen_get_status(&st);
    return st.state;
}

/* ------------------------------------------------------------------ */
/* Initialisation                                                      */
/* ------------------------------------------------------------------ */

static void test_init(void)
{
    TCASE("a quiet board comes up with nothing asserted");
    boot(ALL_IDLE);
    CHECK(safety_input_present());
    CHECK_EQI(safety_inputs(), 0);
    CHECK_EQI(safety_inputs_raw_level(), ALL_IDLE);
    CHECK(!safety_estop_asserted());
    CHECK(safety_estop_released());
    CHECK_EQI(engine_state(), STEPGEN_STATE_SAFE_IDLE);
    TDONE();

    TCASE("interrupts are armed only after the initial state is known");
    /* If a line could interrupt before init sampled it, the very first
     * edge would be compared against a state nobody had established. */
    CHECK(sim_safety_armed());
    CHECK_EQI(sim_safety_estop_isr_count(), 0);
    CHECK_EQI(sim_safety_input_isr_count(), 0);
    TDONE();

    TCASE("init registers the ADR-010 physical-release interlock");
    CHECK(stepgen_has_estop_gate());
    TDONE();

    TCASE("an input already held at boot is published without waiting");
    /* There is no previous state for it to be bouncing away from, so
     * making the operator wait out a debounce window would be theatre. */
    boot((uint16_t)(ALL_IDLE & ~IN_A_BIT));
    CHECK_EQI(safety_inputs(), IN_A_BIT);
    TDONE();

    TCASE("nothing is reported before the subsystem exists");
    sim_safety_reset();
    /* safety_input_present() is what the protocol's INPUTS_PRESENT flag
     * carries; a host must be able to tell "no switch is closed" from
     * "this firmware cannot see switches" - Docs/PROTOCOL.md §6.2. */
    CHECK(!sim_safety_armed());
    boot(ALL_IDLE);
    CHECK(safety_input_present());
    TDONE();
}

/* ------------------------------------------------------------------ */
/* The ordinary inputs                                                 */
/* ------------------------------------------------------------------ */

static void test_polarity(void)
{
    TCASE("a pin pulled LOW reads as ASSERTED, not as a zero bit");
    boot(ALL_IDLE);
    sim_safety_assert(IN_A);
    advance(SAFETY_DEBOUNCE_MS + 2u);
    /* Active low at the pin, 1 = asserted on the wire: the normalisation
     * the protocol layer forwards unchanged (ADR-015). */
    CHECK_EQI(safety_inputs(), IN_A_BIT);
    CHECK_EQI(safety_inputs_raw_level(), (uint16_t)(ALL_IDLE & ~IN_A_BIT));
    TDONE();

    TCASE("two inputs are tracked independently");
    sim_safety_assert(IN_B);
    advance(SAFETY_DEBOUNCE_MS + 2u);
    CHECK_EQI(safety_inputs(), (uint16_t)(IN_A_BIT | IN_B_BIT));
    sim_safety_release(IN_A);
    advance(SAFETY_DEBOUNCE_MS + 2u);
    CHECK_EQI(safety_inputs(), IN_B_BIT);
    TDONE();
}

static void test_debounce(void)
{
    TCASE("an assertion is not published until it has been stable");
    boot(ALL_IDLE);
    sim_safety_assert(IN_A);
    advance(SAFETY_DEBOUNCE_MS - 1u);
    CHECK_EQI(safety_inputs(), 0);
    advance(2u);
    CHECK_EQI(safety_inputs(), IN_A_BIT);
    TDONE();

    TCASE("a bounce shorter than the window is never published at all");
    boot(ALL_IDLE);
    for (unsigned i = 0; i < 20u; i++) {
        sim_safety_assert(IN_A);
        advance(1u);
        sim_safety_release(IN_A);
        advance(1u);
        CHECK_EQI(safety_inputs(), 0);
    }
    /* The edges were real and were counted - the filter suppressed the
     * state change, it did not pretend the switch was quiet. */
    {
        safety_status_t st;
        safety_input_get_status(&st);
        CHECK_GE(st.edges, 40);
        CHECK_EQI(st.transitions, 0);
    }
    TDONE();

    TCASE("a release is filtered the same way an assertion is");
    boot(ALL_IDLE);
    sim_safety_assert(IN_A);
    advance(SAFETY_DEBOUNCE_MS + 2u);
    CHECK_EQI(safety_inputs(), IN_A_BIT);
    sim_safety_release(IN_A);
    advance(SAFETY_DEBOUNCE_MS - 1u);
    CHECK_EQI(safety_inputs(), IN_A_BIT);
    advance(2u);
    CHECK_EQI(safety_inputs(), 0);
    TDONE();

    TCASE("a settling input is reported as settling, not as a reading");
    boot(ALL_IDLE);
    sim_safety_assert(IN_A);
    advance(1u);
    {
        safety_status_t st;
        safety_input_get_status(&st);
        CHECK_EQI(st.settling, IN_A_BIT);
        CHECK_EQI(st.asserted, 0);
    }
    advance(SAFETY_DEBOUNCE_MS + 1u);
    {
        safety_status_t st;
        safety_input_get_status(&st);
        CHECK_EQI(st.settling, 0);
        CHECK_EQI(st.asserted, IN_A_BIT);
    }
    TDONE();
}

static void test_missed_edge(void)
{
    TCASE("a level change that delivers no interrupt is still picked up");
    /* Five of the six input vectors are shared, and a line can be masked
     * by a self-test, so the subsystem is level-based on purpose: the poll
     * re-reads the port and never trusts an edge count. */
    boot(ALL_IDLE);
    sim_safety_set_levels_silent((uint16_t)(ALL_IDLE & ~IN_A_BIT));
    CHECK_EQI(sim_safety_input_isr_count(), 0);
    advance(SAFETY_DEBOUNCE_MS + 2u);
    CHECK_EQI(safety_inputs(), IN_A_BIT);
    TDONE();
}

static void test_changed_word(void)
{
    TCASE("filtered changes are readable once and then cleared");
    boot(ALL_IDLE);
    (void)safety_input_take_changed();
    sim_safety_assert(IN_A);
    advance(SAFETY_DEBOUNCE_MS + 2u);
    CHECK_EQI(safety_input_take_changed(), IN_A_BIT);
    CHECK_EQI(safety_input_take_changed(), 0);
    TDONE();
}

/* ------------------------------------------------------------------ */
/* E-STOP                                                              */
/* ------------------------------------------------------------------ */

static void test_estop_assert(void)
{
    TCASE("E-STOP stops the engine inside the interrupt, before any poll");
    boot(ALL_IDLE);
    CHECK(stepgen_enable_drives());
    CHECK(stepgen_start());
    CHECK_EQI(engine_state(), STEPGEN_STATE_RUNNING);

    sim_safety_assert(SAFETY_ESTOP_INDEX);
    /* No advance(), no poll: the machine is already stopped. Waiting for a
     * superloop iteration is exactly what §8 forbids. */
    CHECK_EQI(engine_state(), STEPGEN_STATE_EMERGENCY_STOP);
    CHECK(safety_estop_asserted());
    CHECK(!safety_estop_released());
    CHECK_EQI(sim_safety_estop_isr_count(), 1);
    TDONE();

    TCASE("E-STOP is not debounced on assertion");
    boot(ALL_IDLE);
    sim_safety_assert(SAFETY_ESTOP_INDEX);
    /* Zero milliseconds elapsed. A filter here would be latency added to
     * the one path that must not have any. */
    CHECK_EQI(engine_state(), STEPGEN_STATE_EMERGENCY_STOP);
    advance(1u);
    CHECK_EQI((safety_inputs() & ESTOP_BIT), ESTOP_BIT);
    TDONE();

    TCASE("an E-STOP already down at power-on is honoured");
    /* The edge happened before the firmware existed and will not repeat.
     * Only the level can tell us, and §32 says it must. */
    boot((uint16_t)(ALL_IDLE & ~ESTOP_BIT));
    CHECK_EQI(engine_state(), STEPGEN_STATE_EMERGENCY_STOP);
    CHECK(safety_estop_asserted());
    TDONE();

    TCASE("an E-STOP whose edge was lost is caught by the poll");
    boot(ALL_IDLE);
    sim_safety_set_levels_silent((uint16_t)(ALL_IDLE & ~ESTOP_BIT));
    CHECK_EQI(sim_safety_estop_isr_count(), 0);
    advance(1u);
    CHECK_EQI(engine_state(), STEPGEN_STATE_EMERGENCY_STOP);
    TDONE();

    TCASE("a bouncing E-STOP contact counts as one assertion");
    boot(ALL_IDLE);
    for (unsigned i = 0; i < 10u; i++) {
        sim_safety_assert(SAFETY_ESTOP_INDEX);
        sim_safety_release(SAFETY_ESTOP_INDEX);
    }
    {
        safety_status_t st;
        safety_input_get_status(&st);
        CHECK_EQI(st.estop_asserts, 1);
        CHECK(st.estop_latched);
    }
    CHECK_EQI(engine_state(), STEPGEN_STATE_EMERGENCY_STOP);
    TDONE();
}

static void test_estop_release(void)
{
    TCASE("release is not accepted until the contact has been stable");
    boot(ALL_IDLE);
    sim_safety_assert(SAFETY_ESTOP_INDEX);
    sim_safety_release(SAFETY_ESTOP_INDEX);
    advance(SAFETY_ESTOP_RELEASE_MS - 1u);
    CHECK(safety_estop_asserted());
    CHECK(!safety_estop_released());
    advance(2u);
    CHECK(!safety_estop_asserted());
    CHECK(safety_estop_released());
    TDONE();

    TCASE("a contact that bounces during release never counts as released");
    boot(ALL_IDLE);
    sim_safety_assert(SAFETY_ESTOP_INDEX);
    for (unsigned i = 0; i < 6u; i++) {
        sim_safety_release(SAFETY_ESTOP_INDEX);
        advance(SAFETY_ESTOP_RELEASE_MS - 2u);
        CHECK(!safety_estop_released());
        sim_safety_assert(SAFETY_ESTOP_INDEX);
        advance(1u);
        CHECK(!safety_estop_released());
    }
    TDONE();

    TCASE("release permits a clear - it never performs one");
    /* ADR-010: recovery is always explicit. The machine does not restart
     * because somebody let go of the button. */
    boot(ALL_IDLE);
    sim_safety_assert(SAFETY_ESTOP_INDEX);
    sim_safety_release(SAFETY_ESTOP_INDEX);
    advance(SAFETY_ESTOP_RELEASE_MS + 5u);
    CHECK(safety_estop_released());
    CHECK_EQI(engine_state(), STEPGEN_STATE_EMERGENCY_STOP);
    TDONE();
}

static void test_interlock(void)
{
    TCASE("the engine refuses to clear while the input is still down");
    boot(ALL_IDLE);
    sim_safety_assert(SAFETY_ESTOP_INDEX);
    CHECK(!stepgen_clear_emergency_stop());
    CHECK_EQI(engine_state(), STEPGEN_STATE_EMERGENCY_STOP);
    TDONE();

    TCASE("it still refuses while the release window is running");
    sim_safety_release(SAFETY_ESTOP_INDEX);
    advance(SAFETY_ESTOP_RELEASE_MS - 1u);
    CHECK(!stepgen_clear_emergency_stop());
    CHECK_EQI(engine_state(), STEPGEN_STATE_EMERGENCY_STOP);
    TDONE();

    TCASE("once released, an explicit clear is accepted");
    advance(2u);
    CHECK(stepgen_clear_emergency_stop());
    CHECK_EQI(engine_state(), STEPGEN_STATE_SAFE_IDLE);
    TDONE();

    TCASE("the clear leaves the drives disabled, not running");
    {
        stepgen_status_t st;
        stepgen_get_status(&st);
        CHECK(!st.drives_enabled);
    }
    TDONE();

    TCASE("a generic fault clear still cannot clear an E-stop");
    boot(ALL_IDLE);
    sim_safety_assert(SAFETY_ESTOP_INDEX);
    CHECK(!stepgen_clear_fault());
    CHECK_EQI(engine_state(), STEPGEN_STATE_EMERGENCY_STOP);
    TDONE();
}

static void test_latches(void)
{
    TCASE("the E-STOP latch survives the release");
    boot(ALL_IDLE);
    sim_safety_assert(SAFETY_ESTOP_INDEX);
    sim_safety_release(SAFETY_ESTOP_INDEX);
    advance(SAFETY_ESTOP_RELEASE_MS + 2u);
    {
        safety_status_t st;
        safety_input_get_status(&st);
        CHECK(st.estop_latched);
        CHECK(!st.estop_asserted);
        CHECK_EQI((st.ever_asserted & ESTOP_BIT), ESTOP_BIT);
    }
    TDONE();

    TCASE("latches cannot be wiped while the condition is still live");
    boot(ALL_IDLE);
    sim_safety_assert(SAFETY_ESTOP_INDEX);
    CHECK(!safety_input_clear_latches());
    sim_safety_release(SAFETY_ESTOP_INDEX);
    advance(SAFETY_ESTOP_RELEASE_MS + 2u);
    CHECK(safety_input_clear_latches());
    {
        safety_status_t st;
        safety_input_get_status(&st);
        CHECK(!st.estop_latched);
    }
    TDONE();
}

static void test_chatter(void)
{
    TCASE("a chattering input is named, without being filtered differently");
    boot(ALL_IDLE);
    advance(1u);
    for (unsigned i = 0; i < SAFETY_CHATTER_EDGES_PER_S; i++) {
        sim_safety_assert(IN_A);
        sim_safety_release(IN_A);
    }
    advance(1100u);
    {
        safety_status_t st;
        safety_input_get_status(&st);
        CHECK_EQI((st.chattering & IN_A_BIT), IN_A_BIT);
        CHECK_EQI((st.chattering & IN_B_BIT), 0);
        /* And it never made it into the reported state. */
        CHECK_EQI((st.asserted & IN_A_BIT), 0);
    }
    TDONE();

    TCASE("a quiet second clears the chatter report");
    advance(1100u);
    {
        safety_status_t st;
        safety_input_get_status(&st);
        CHECK_EQI(st.chattering, 0);
    }
    TDONE();
}

/* ------------------------------------------------------------------ */

int main(void)
{
    printf("CNC5AX-ETH digital inputs and E-STOP (M3) host verification\n");
    printf("  %u inputs on PE0..PE14, E-STOP = PE%u, active low\n",
           SAFETY_INPUT_COUNT, SAFETY_ESTOP_INDEX);
    printf("  debounce %u ms, E-STOP release %u ms, chatter %u edges/s\n\n",
           SAFETY_DEBOUNCE_MS, SAFETY_ESTOP_RELEASE_MS,
           SAFETY_CHATTER_EDGES_PER_S);

    printf("Initialisation\n");           test_init();
    printf("Polarity and normalisation\n"); test_polarity();
    printf("Debounce filter\n");           test_debounce();
    printf("Level-based recovery\n");      test_missed_edge();
    printf("Change notification\n");       test_changed_word();
    printf("E-STOP assertion\n");          test_estop_assert();
    printf("E-STOP release\n");            test_estop_release();
    printf("ADR-010 release interlock\n"); test_interlock();
    printf("Latches\n");                   test_latches();
    printf("Chatter detection\n");         test_chatter();

    printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail != 0;
}
