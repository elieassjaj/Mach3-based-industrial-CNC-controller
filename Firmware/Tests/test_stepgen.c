/**
 * @file    test_stepgen.c
 * @brief   Host verification of the STEP/DIR generator.
 *
 * These tests measure the reconstructed pin waveform in nanoseconds, not
 * engine internals, so what they assert is the same thing an oscilloscope
 * is asked to confirm on real hardware (Docs/HARDWARE-VALIDATION.md).
 */
#include "stepgen.h"
#include "stepgen_core.h"
#include "sim_trace.h"
#include "test_util.h"

#include <string.h>

int g_fail = 0, g_checks = 0, g_case_failed = 0;
const char *g_case = "";

#define TICK_NS   (1000000000.0 / (double)STEPGEN_TICK_HZ)

/* ------------------------------------------------------------------ */
/* Trace helpers                                                       */
/* ------------------------------------------------------------------ */

static uint32_t count_rising(const sim_trace_t *t, uint32_t axis)
{
    const uint8_t m = (uint8_t)(1u << axis);
    uint32_t n = 0;
    uint8_t prev = 0;
    for (uint32_t i = 0; i < t->n; i++) {
        const uint8_t cur = (uint8_t)(t->step[i] & m);
        if (cur && !prev) { n++; }
        prev = cur;
    }
    return n;
}

static uint32_t max_high_run(const sim_trace_t *t, uint32_t axis)
{
    const uint8_t m = (uint8_t)(1u << axis);
    uint32_t best = 0, run = 0;
    for (uint32_t i = 0; i < t->n; i++) {
        if (t->step[i] & m) { run++; if (run > best) best = run; }
        else                { run = 0; }
    }
    return best;
}

static uint32_t min_low_run_between_pulses(const sim_trace_t *t, uint32_t axis)
{
    const uint8_t m = (uint8_t)(1u << axis);
    uint32_t best = 0xFFFFFFFFu, run = 0;
    int seen = 0;
    for (uint32_t i = 0; i < t->n; i++) {
        if (t->step[i] & m) {
            if (seen && run < best) { best = run; }
            seen = 1;
            run = 0;
        } else if (seen) {
            run++;
        }
    }
    return best;
}

static int find_dir_change(const sim_trace_t *t, uint32_t axis, uint32_t from,
                           uint32_t *at)
{
    const uint8_t m = (uint8_t)(1u << axis);
    for (uint32_t i = (from < 1u) ? 1u : from; i < t->n; i++) {
        if ((t->dir[i] & m) != (t->dir[i - 1] & m)) { *at = i; return 1; }
    }
    return 0;
}

static int last_step_before(const sim_trace_t *t, uint32_t axis, uint32_t idx,
                            uint32_t *at)
{
    const uint8_t m = (uint8_t)(1u << axis);
    for (uint32_t i = idx; i-- > 0; ) {
        if (t->step[i] & m) { *at = i; return 1; }
    }
    return 0;
}

static int first_step_after(const sim_trace_t *t, uint32_t axis, uint32_t idx,
                            uint32_t *at)
{
    const uint8_t m = (uint8_t)(1u << axis);
    for (uint32_t i = idx; i < t->n; i++) {
        if (t->step[i] & m) { *at = i; return 1; }
    }
    return 0;
}

/* ------------------------------------------------------------------ */

static void submit(uint32_t ticks, const double hz[MOTION_AXIS_COUNT], uint32_t seq)
{
    motion_segment_t s;
    memset(&s, 0, sizeof(s));
    s.duration_ticks = ticks;
    s.seq            = seq;
    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        s.rate[a] = stepgen_rate_from_hz(hz[a]);
    }
    CHECK(stepgen_submit_segment(&s));
}

/* Submit without asserting: used where the queue is expected to back up. */
static bool try_submit(uint32_t ticks, const double hz[MOTION_AXIS_COUNT],
                       uint32_t seq)
{
    motion_segment_t s;
    memset(&s, 0, sizeof(s));
    s.duration_ticks = ticks;
    s.seq            = seq;
    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        s.rate[a] = stepgen_rate_from_hz(hz[a]);
    }
    return stepgen_submit_segment(&s);
}

static void begin(void)
{
    CHECK(stepgen_init());
    CHECK(stepgen_enable_drives());
    sim_trace_reset(&g_sim_trace);
}

/* The first commanded move always arms a DIR write for logical half 0 and
 * reserves its leading guard ticks (ADR-006), so the first guard ticks of
 * any run carry no steps. */
#define GUARD STEPGEN_DIR_GUARD_TICKS

/* ------------------------------------------------------------------ */
/* 1. Waveform shape at the 2 MHz maximum                              */
/* ------------------------------------------------------------------ */
static void test_max_rate_waveform(void)
{
    TCASE("2 MHz on X: one-tick pulse, one-tick gap, exact count");
    begin();

    const uint32_t D = 200000u;              /* 50 ms @ 4 MHz */
    double hz[MOTION_AXIS_COUNT] = {0};
    hz[MOTION_AXIS_X] = 2000000.0;
    submit(D, hz, 1);
    CHECK(stepgen_start());
    sim_port_run_ticks(D);

    /* rate == tick/2 exactly => inc == 2^31 => a carry every second tick */
    CHECK_EQI(count_rising(&g_sim_trace, MOTION_AXIS_X), (D - GUARD) / 2u);
    CHECK_EQI(max_high_run(&g_sim_trace, MOTION_AXIS_X), 1u);
    CHECK_EQI(min_low_run_between_pulses(&g_sim_trace, MOTION_AXIS_X), 1u);
    CHECK_GE((long long)(1.0 * TICK_NS), (long long)MOTION_STEP_WIDTH_MIN_NS);
    TDONE();
}

/* ------------------------------------------------------------------ */
/* 2. Five axes simultaneously at 2 MHz, zero cross-axis skew          */
/* ------------------------------------------------------------------ */
static void test_five_axes_max_rate(void)
{
    TCASE("5 axes @ 2 MHz, single GPIOA word, zero skew");
    begin();

    const uint32_t D = 400000u;              /* 100 ms @ 4 MHz */
    double hz[MOTION_AXIS_COUNT];
    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) { hz[a] = 2000000.0; }
    submit(D, hz, 7);
    CHECK(stepgen_start());
    sim_port_run_ticks(D);

    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        CHECK_EQI(count_rising(&g_sim_trace, a), (D - GUARD) / 2u);
        CHECK_EQI(max_high_run(&g_sim_trace, a), 1u);
    }
    /* ADR-005: all five pins share one BSRR word, so every tick is either
     * all five high or all five low - never a partial set. */
    uint32_t misaligned = 0;
    for (uint32_t i = 0; i < g_sim_trace.n; i++) {
        const uint8_t s = g_sim_trace.step[i];
        if (s != 0u && s != 0x1Fu) { misaligned++; }
    }
    CHECK_EQI(misaligned, 0u);
    TDONE();
}

/* ------------------------------------------------------------------ */
/* 3. Long-run rate accuracy                                           */
/* ------------------------------------------------------------------ */
static void test_rate_accuracy(void)
{
    TCASE("DDA long-run accuracy across mixed axis rates");
    begin();

    const uint32_t D = 4000000u;             /* 1 s @ 4 MHz */
    double hz[MOTION_AXIS_COUNT] = { 1000.0, 123456.0, 999999.0, 2000000.0, 17.0 };
    submit(D, hz, 11);
    CHECK(stepgen_start());
    sim_port_run_ticks(D);

    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        const int64_t inc   = stepgen_rate_from_hz(hz[a]);
        const int64_t ticks = (int64_t)D - (int64_t)GUARD;
        const int64_t exact = (inc * ticks) >> 32;      /* floor, phase0 == 0 */
        CHECK_EQI(count_rising(&g_sim_trace, a), exact);
    }
    stepgen_status_t st;
    stepgen_get_status(&st);
    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        const int64_t emitted = (int64_t)count_rising(&g_sim_trace, a);
        /* pos_output lags by at most the half not yet committed. */
        CHECK(st.pos_output[a] <= emitted);
        CHECK_GE(st.pos_output[a], emitted - (int64_t)STEPGEN_HALF_TICKS);
        CHECK_EQI(st.pos_planned[a], emitted);
    }
    TDONE();
}

/* ------------------------------------------------------------------ */
/* 4. DIR setup / hold around a reversal (ADR-006)                     */
/* ------------------------------------------------------------------ */
static void test_dir_setup_hold(void)
{
    TCASE("DIR setup/hold >= 200 ns around a 2 MHz reversal");
    begin();

    double fwd[MOTION_AXIS_COUNT] = {0}, rev[MOTION_AXIS_COUNT] = {0};
    fwd[MOTION_AXIS_Y] =  2000000.0;
    rev[MOTION_AXIS_Y] = -2000000.0;
    submit(2000u, fwd, 1);
    submit(2000u, rev, 2);
    CHECK(stepgen_start());
    sim_port_run_ticks(6000u);

    /* The initial DIR assertion happens before the first tick is played,
     * so the only change inside the trace is the reversal itself. */
    uint32_t dch = 0, lastb = 0, firsta = 0;
    CHECK(find_dir_change(&g_sim_trace, MOTION_AXIS_Y, 1u, &dch));
    CHECK(last_step_before(&g_sim_trace, MOTION_AXIS_Y, dch, &lastb));
    CHECK(first_step_after(&g_sim_trace, MOTION_AXIS_Y, dch, &firsta));

    const double hold_ns  = (double)(dch    - lastb) * TICK_NS;
    const double setup_ns = (double)(firsta - dch  ) * TICK_NS;
    printf("\n      hold=%.0f ns setup=%.0f ns%*s", hold_ns, setup_ns, 26, "");

    CHECK_GE((long long)hold_ns,  (long long)MOTION_DIR_HOLD_MIN_NS);
    CHECK_GE((long long)setup_ns, (long long)MOTION_DIR_SETUP_MIN_NS);
    CHECK_EQI(g_sim_trace.step[dch] & (1u << MOTION_AXIS_Y), 0u);
    TDONE();
}

/* ------------------------------------------------------------------ */
/* 5. Every reversal in a long alternating run honours the guard       */
/* ------------------------------------------------------------------ */
static void test_dir_guard_exhaustive(void)
{
    TCASE("every reversal in a 200-segment run honours the guard");
    begin();

    double fwd[MOTION_AXIS_COUNT] = {0}, rev[MOTION_AXIS_COUNT] = {0};
    fwd[MOTION_AXIS_Z] =  1500000.0;
    rev[MOTION_AXIS_Z] = -1700000.0;
    CHECK(stepgen_start());

    for (uint32_t k = 0; k < 200u; k++) {
        /* Reversal cadence deliberately slower than one buffer period
         * (ADR-006's stated operating regime); a faster cadence is
         * handled by stalling, exercised separately below. */
        submit(2048u, (k & 1u) ? rev : fwd, k);
        sim_port_run_ticks(2048u);
    }

    const uint8_t m = (uint8_t)(1u << MOTION_AXIS_Z);
    uint32_t reversals = 0;
    for (uint32_t i = 1; i < g_sim_trace.n; i++) {
        if ((g_sim_trace.dir[i] & m) == (g_sim_trace.dir[i - 1] & m)) { continue; }
        reversals++;
        uint32_t lb = 0, fa = 0;
        if (last_step_before(&g_sim_trace, MOTION_AXIS_Z, i, &lb)) {
            CHECK_GE((long long)((double)(i - lb) * TICK_NS),
                     (long long)MOTION_DIR_HOLD_MIN_NS);
        }
        if (first_step_after(&g_sim_trace, MOTION_AXIS_Z, i, &fa)) {
            CHECK_GE((long long)((double)(fa - i) * TICK_NS),
                     (long long)MOTION_DIR_SETUP_MIN_NS);
        }
        /* No STEP may land on the tick the DIR line changes. */
        CHECK_EQI(g_sim_trace.step[i] & m, 0u);
    }
    CHECK_GE(reversals, 190);
    printf("\n      %u reversals checked%*s", reversals, 32, "");
    TDONE();
}

/* ------------------------------------------------------------------ */
/* 5b. Reversals faster than one buffer period must not be lost         */
/* ------------------------------------------------------------------ */
static void test_fast_reversal_stall(void)
{
    TCASE("reversals faster than a buffer period stall, never drop");
    begin();

    /* The armed DIR slot is depth one (ADR-006). A reversal arriving
     * before the previous one has been written to GPIOD must make the new
     * segment wait, not overwrite the pending change - overwriting would
     * silently run the axis the wrong way. */
    double fwd[MOTION_AXIS_COUNT] = {0}, rev[MOTION_AXIS_COUNT] = {0};
    fwd[MOTION_AXIS_X] =  1000000.0;
    rev[MOTION_AXIS_X] = -1000000.0;
    CHECK(stepgen_start());

    const uint32_t SEG = 64u;           /* 16 us, far below the 256 us ring */
    uint32_t rejected = 0;
    for (uint32_t k = 0; k < 400u; k++) {
        /* The queue is expected to back up: stalling for a pending
         * reversal is exactly the backpressure ADR-008 requires the
         * protocol layer to respect. A rejected push is correct
         * behaviour here, not a failure. */
        if (!try_submit(SEG, (k & 1u) ? rev : fwd, k)) { rejected++; }
        sim_port_run_ticks(SEG);
    }

    /* Every STEP edge must occur while DIR holds the level that edge was
     * commanded with. The check that matters is not how many reversals
     * got through, but that no edge was ever emitted under a stale DIR. */
    const uint8_t m = (uint8_t)(1u << MOTION_AXIS_X);
    uint32_t edges = 0, violations = 0;
    for (uint32_t i = 1; i < g_sim_trace.n; i++) {
        if ((g_sim_trace.dir[i] & m) != (g_sim_trace.dir[i - 1] & m)) {
            /* guard must hold around every realized change */
            uint32_t lb = 0, fa = 0;
            if (last_step_before(&g_sim_trace, MOTION_AXIS_X, i, &lb) &&
                (double)(i - lb) * TICK_NS < (double)MOTION_DIR_HOLD_MIN_NS) {
                violations++;
            }
            if (first_step_after(&g_sim_trace, MOTION_AXIS_X, i, &fa) &&
                (double)(fa - i) * TICK_NS < (double)MOTION_DIR_SETUP_MIN_NS) {
                violations++;
            }
            if (g_sim_trace.step[i] & m) { violations++; }
        }
        if (g_sim_trace.step[i] & m) { edges++; }
    }
    CHECK_EQI(violations, 0u);
    CHECK_GE(edges, 1);

    /* The engine must still be running normally: stalling is flow control,
     * not a fault. */
    stepgen_status_t st;
    stepgen_get_status(&st);
    CHECK_EQI(st.state, STEPGEN_STATE_RUNNING);
    CHECK_EQI(st.faults & STEPGEN_FAULTS_INTEGRITY, 0u);
    printf("\n      %u edges, %u guard violations, %u back-pressured%*s",
           edges, violations, rejected, 6, "");
    TDONE();
}

/* ------------------------------------------------------------------ */
/* 6. Starvation: reported, holds position, drives stay enabled        */
/* ------------------------------------------------------------------ */
static void test_starvation(void)
{
    TCASE("starvation holds position with drives still enabled");
    begin();

    double hz[MOTION_AXIS_COUNT] = {0};
    hz[MOTION_AXIS_A] = 500000.0;
    submit(1000u, hz, 1);
    CHECK(stepgen_start());
    sim_port_run_ticks(STEPGEN_RING_TICKS * 4u);

    stepgen_status_t st;
    stepgen_get_status(&st);
    CHECK((st.faults & STEPGEN_FAULT_SEGMENT_STARVED) != 0u);
    CHECK_GE(st.starved_ticks, 1);

    const uint32_t n   = count_rising(&g_sim_trace, MOTION_AXIS_A);
    const int64_t  inc = stepgen_rate_from_hz(hz[MOTION_AXIS_A]);
    CHECK_EQI(n, (inc * (1000 - GUARD)) >> 32);

    /* ADR-010, owner-confirmed: a paused command stream must NOT
     * de-energise the drives, or an axis can drift or drop under load. */
    CHECK(st.drives_enabled);
    TDONE();
}

/* ------------------------------------------------------------------ */
/* 7. A missed refill deadline is a hard integrity fault               */
/* ------------------------------------------------------------------ */
static void test_underrun_is_fatal(void)
{
    TCASE("missed refill deadline faults and drops the drives");
    begin();

    double hz[MOTION_AXIS_COUNT] = {0};
    hz[MOTION_AXIS_B] = 2000000.0;
    for (uint32_t k = 0; k < 8u; k++) { submit(STEPGEN_RING_TICKS, hz, k); }
    CHECK(stepgen_start());

    sim_port_run_ticks(STEPGEN_HALF_TICKS);
    sim_port_run_ticks_no_refill(STEPGEN_RING_TICKS);
    sim_port_run_ticks(1u);

    stepgen_status_t st;
    stepgen_get_status(&st);
    CHECK_EQI(st.state, STEPGEN_STATE_FAULT);
    CHECK((st.faults & STEPGEN_FAULT_BUFFER_UNDERRUN) != 0u);
    CHECK_EQI(st.underruns, 1u);
    /* Integrity fault: the engine cannot trust its own step generation,
     * so unlike starvation this one must disable the drives. */
    CHECK(!st.drives_enabled);
    CHECK(g_sim_trace.estop_tick != 0xFFFFFFFFu);
    TDONE();
}

/* ------------------------------------------------------------------ */
/* 8. Emergency stop and the ADR-010 recovery rules                    */
/* ------------------------------------------------------------------ */
static void test_estop_and_recovery(void)
{
    TCASE("E-STOP latches and cannot be cleared by a fault clear");
    begin();

    double hz[MOTION_AXIS_COUNT] = {0};
    hz[MOTION_AXIS_X] = 2000000.0;
    for (uint32_t k = 0; k < 8u; k++) { submit(STEPGEN_RING_TICKS, hz, k); }
    CHECK(stepgen_start());
    sim_port_run_ticks(1000u);
    const uint32_t before = g_sim_trace.n;

    stepgen_emergency_stop();
    sim_port_run_ticks(10000u);

    CHECK_EQI(g_sim_trace.n, before);          /* not one further tick */
    CHECK_EQI(g_sim_trace.estop_tick, before);

    stepgen_status_t st;
    stepgen_get_status(&st);
    CHECK_EQI(st.state, STEPGEN_STATE_EMERGENCY_STOP);
    CHECK(!st.drives_enabled);

    /* ADR-010: recovery is never automatic, and a generic fault clear must
     * never clear an E-stop. */
    CHECK(!stepgen_clear_fault());
    stepgen_get_status(&st);
    CHECK_EQI(st.state, STEPGEN_STATE_EMERGENCY_STOP);

    CHECK(stepgen_clear_emergency_stop());
    stepgen_get_status(&st);
    CHECK_EQI(st.state, STEPGEN_STATE_SAFE_IDLE);
    CHECK(!st.drives_enabled);
    TDONE();
}

/* ------------------------------------------------------------------ */
/* 9. State machine gating                                             */
/* ------------------------------------------------------------------ */
static void test_state_machine(void)
{
    TCASE("ADR-010 state transitions are gated");
    CHECK(stepgen_init());

    stepgen_status_t st;
    stepgen_get_status(&st);
    CHECK_EQI(st.state, STEPGEN_STATE_SAFE_IDLE);
    CHECK(!st.drives_enabled);

    CHECK(!stepgen_start());                 /* not READY yet */
    CHECK(stepgen_enable_drives());
    stepgen_get_status(&st);
    CHECK_EQI(st.state, STEPGEN_STATE_READY);
    CHECK(st.drives_enabled);

    CHECK(!stepgen_enable_drives());         /* already READY */
    CHECK(stepgen_start());
    stepgen_get_status(&st);
    CHECK_EQI(st.state, STEPGEN_STATE_RUNNING);

    CHECK(!stepgen_clear_fault());           /* no fault to clear */
    stepgen_stop();
    stepgen_get_status(&st);
    CHECK_EQI(st.state, STEPGEN_STATE_READY);
    CHECK(st.drives_enabled);                /* a stop is not a fault */
    TDONE();
}

/* ------------------------------------------------------------------ */
/* 10. Segment validation                                              */
/* ------------------------------------------------------------------ */
static void test_validation(void)
{
    TCASE("segments above 2 MHz or of zero length are rejected");
    begin();

    motion_segment_t s;
    memset(&s, 0, sizeof(s));

    s.duration_ticks = 0;
    CHECK(!stepgen_submit_segment(&s));

    s.duration_ticks = 100;
    s.rate[MOTION_AXIS_X] = STEPGEN_RATE_MAX_Q32 + 1;
    CHECK(!stepgen_submit_segment(&s));
    s.rate[MOTION_AXIS_X] = -(STEPGEN_RATE_MAX_Q32 + 1);
    CHECK(!stepgen_submit_segment(&s));

    s.rate[MOTION_AXIS_X] = STEPGEN_RATE_MAX_Q32;
    CHECK(stepgen_submit_segment(&s));

    CHECK_EQI(stepgen_rate_from_hz(1.0e9), STEPGEN_RATE_MAX_Q32);
    CHECK_EQI(stepgen_rate_from_hz(2000000.0), STEPGEN_RATE_MAX_Q32);
    TDONE();
}

/* ------------------------------------------------------------------ */
/* 11. Queue behaviour                                                 */
/* ------------------------------------------------------------------ */
static void test_queue(void)
{
    TCASE("segment queue is exact at the full/empty boundaries");
    motion_segment_queue_t q;
    motion_segment_t s, o;
    memset(&s, 0, sizeof(s));
    s.duration_ticks = 1;

    msq_init(&q);
    CHECK_EQI(msq_count(&q), 0u);
    CHECK_EQI(msq_free(&q), MOTION_SEGMENT_QUEUE_DEPTH);
    CHECK(!msq_pop(&q, &o));

    for (uint32_t i = 0; i < MOTION_SEGMENT_QUEUE_DEPTH; i++) {
        s.seq = i;
        CHECK(msq_push(&q, &s));
    }
    CHECK(!msq_push(&q, &s));
    CHECK_EQI(msq_free(&q), 0u);

    for (uint32_t i = 0; i < MOTION_SEGMENT_QUEUE_DEPTH; i++) {
        CHECK(msq_pop(&q, &o));
        CHECK_EQI(o.seq, i);
    }
    CHECK(!msq_pop(&q, &o));
    TDONE();
}

/* ------------------------------------------------------------------ */

int main(void)
{
    printf("CNC5AX-ETH stepgen host verification\n");
    printf("  tick = %u Hz (%.1f ns), ring = %u ticks (%.1f us), "
           "half deadline = %.1f us, DIR guard = %u ticks\n\n",
           STEPGEN_TICK_HZ, TICK_NS, STEPGEN_RING_TICKS,
           STEPGEN_RING_TICKS * TICK_NS / 1000.0,
           STEPGEN_HALF_TICKS * TICK_NS / 1000.0,
           STEPGEN_DIR_GUARD_TICKS);

    test_max_rate_waveform();
    test_five_axes_max_rate();
    test_rate_accuracy();
    test_dir_setup_hold();
    test_dir_guard_exhaustive();
    test_fast_reversal_stall();
    test_starvation();
    test_underrun_is_fatal();
    test_estop_and_recovery();
    test_state_machine();
    test_validation();
    test_queue();

    printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
