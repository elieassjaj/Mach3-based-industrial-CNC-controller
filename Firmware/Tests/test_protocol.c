/**
 * @file    test_protocol.c
 * @brief   Host verification of the C5P1 protocol layer (Phase 3, M13/M14).
 *
 * These tests drive the session against the **real** Phase 1 motion engine
 * on its simulation port, not against a mock. So "the queue is full" means
 * the real 64-slot queue is full, "abort" really stops the real engine, and
 * "this motion packet produces these steps" is checked by counting the
 * pulses the reconstructed waveform actually contains.
 *
 * What this suite can and cannot establish:
 *
 *   CAN  - every framing and validation rule in Docs/PROTOCOL.md §4/§5;
 *          the two sequence spaces and every adverse case in §7.5;
 *          that a motion packet becomes exactly the steps it asked for,
 *          on exactly the axes it named;
 *          that backpressure is atomic and that faults need explicit clears.
 *   CANNOT - anything about UDP, lwIP, the PHY or the wire. The socket
 *          binding is target-only; HV-30 covers it on the board.
 */
#include "cnc_protocol.h"
#include "cnc_session.h"
#include "net_config.h"
#include "stepgen.h"
#include "sim_trace.h"
#include "test_util.h"

#include <string.h>

int g_fail = 0, g_checks = 0, g_case_failed = 0;
const char *g_case = "";

/* Slice used throughout: 4 ms, the ncPod-derived recommendation. At the
 * 4 MHz base tick that is 16000 ticks exactly. */
#define SLICE_US   4000u
#define SLICE_TICKS (SLICE_US * (STEPGEN_TICK_HZ / 1000000u))

/* The engine fills the STEP DMA ring a half at a time, so the last ticks of
 * a segment are still in the pipeline when the segment's own tick count has
 * elapsed. The pulse-counting tests therefore run a tail of two full rings
 * past the segment to let playback drain.
 *
 * The tail cannot invent pulses: with the queue empty the engine commands
 * zero rate, so a count that was genuinely short would stay short however
 * long the tail ran. */
#define TRACE_TAIL  (2u * STEPGEN_RING_TICKS)

static uint8_t  tx[CNC_MAX_PACKET];
static uint8_t  rx[CNC_MAX_PACKET];
static uint32_t host_seq;

/* ------------------------------------------------------------------ */
/* Harness                                                             */
/* ------------------------------------------------------------------ */

static void engine_reset(void)
{
    sim_trace_reset(&g_sim_trace);
    CHECK(stepgen_init());
    cnc_session_init();
    host_seq = 0;
}

/** Send a prepared packet into the session; returns the reply length. */
static size_t deliver(const uint8_t *pkt, size_t len, uint32_t now_ms)
{
    return cnc_session_on_datagram(pkt, len, now_ms, rx, sizeof(rx));
}

/** Decode a reply into a status struct. Fails the case if it is not one. */
static bool reply_status(size_t len, cnc_status_t *out)
{
    cnc_header_t h;
    if (cnc_parse_header(rx, len, &h) != CNC_PARSE_OK) { return false; }
    if (h.opcode != CNC_OP_STATUS) { return false; }
    return cnc_parse_status(&h, out);
}

static size_t send_control(uint8_t cmd, uint32_t now_ms)
{
    const size_t n = cnc_encode_control(tx, sizeof(tx), ++host_seq, cmd, 0);
    return deliver(tx, n, now_ms);
}

/** One record with a single axis moving whole steps. */
static cnc_motion_record_t rec_axis(uint32_t axis, int32_t steps)
{
    cnc_motion_record_t r;
    memset(&r, 0, sizeof(r));
    r.steps_q16[axis] = steps * 65536;
    return r;
}

static size_t send_motion(uint32_t block_seq, const cnc_motion_record_t *recs,
                          uint8_t n, uint8_t flags, uint32_t now_ms)
{
    const size_t len = cnc_encode_motion(tx, sizeof(tx), ++host_seq,
                                         block_seq, SLICE_US, recs, n, flags);
    CHECK(len > 0);
    return deliver(tx, len, now_ms);
}

/**
 * Assert that an axis emitted what was commanded, to the one step a DDA is
 * allowed to be behind.
 *
 * The engine carries a fractional phase accumulator across segments, so the
 * final step of a segment generally lands at the start of the next one. A
 * run that ends is therefore up to one step short; a run that continues
 * makes it up. Two things are asserted and both matter:
 *
 *   - never MORE than commanded. Overshoot would be uncommanded motion,
 *     which is a safety property, not an accuracy one.
 *   - never more than one step short, which is what rules out a scale
 *     error: getting the Q16 unit or the tick conversion wrong would miss
 *     by a factor, not by one.
 */
#define CHECK_STEPS(axis, commanded) do {                                     \
    const uint32_t _got = pulses(axis);                                       \
    const uint32_t _want = (uint32_t)(commanded);                             \
    CHECK(_got <= _want);                                                     \
    CHECK_GE(_got, (_want > 0u) ? _want - 1u : 0u);                           \
} while (0)

/** Count rising STEP edges on one axis in the recorded trace. */
static uint32_t pulses(uint32_t axis)
{
    const uint8_t m = (uint8_t)(1u << axis);
    uint32_t n = 0;
    uint8_t prev = 0;
    for (uint32_t i = 0; i < g_sim_trace.n; i++) {
        const uint8_t cur = (uint8_t)(g_sim_trace.step[i] & m);
        if (cur && !prev) { n++; }
        prev = cur;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* CRC                                                                 */
/* ------------------------------------------------------------------ */

static void test_crc(void)
{
    TCASE("CRC-32 matches the standard check vectors");
    /* The whole point of using IEEE 802.3 CRC-32 is that the host side can
     * call zlib/binascii and get the same number. These are that CRC's
     * published check values. */
    CHECK_EQI(cnc_crc32((const uint8_t *)"123456789", 9), 0xCBF43926u);
    CHECK_EQI(cnc_crc32((const uint8_t *)"", 0), 0x00000000u);
    CHECK_EQI(cnc_crc32((const uint8_t *)"a", 1), 0xE8B7BE43u);
    TDONE();
}

/* ------------------------------------------------------------------ */
/* Framing                                                             */
/* ------------------------------------------------------------------ */

static void test_framing_roundtrip(void)
{
    cnc_header_t h;

    TCASE("every packet type survives encode -> parse");
    size_t n = cnc_encode_simple(tx, sizeof(tx), 7, CNC_OP_HELLO);
    CHECK_EQI(n, CNC_OVERHEAD);
    CHECK_EQI(cnc_parse_header(tx, n, &h), CNC_PARSE_OK);
    CHECK_EQI(h.opcode, CNC_OP_HELLO);
    CHECK_EQI(h.seq, 7);
    CHECK_EQI(h.payload_len, 0);

    n = cnc_encode_control(tx, sizeof(tx), 8, CNC_CTL_START, 0);
    CHECK_EQI(cnc_parse_header(tx, n, &h), CNC_PARSE_OK);
    cnc_control_t ctl;
    CHECK(cnc_parse_control(&h, &ctl));
    CHECK_EQI(ctl.cmd, CNC_CTL_START);

    cnc_motion_record_t r[2] = { rec_axis(0, 10), rec_axis(1, -20) };
    n = cnc_encode_motion(tx, sizeof(tx), 9, 1, SLICE_US, r, 2, 0);
    CHECK_EQI(n, CNC_OVERHEAD + CNC_MOTION_HDR_SIZE + 2 * CNC_RECORD_SIZE);
    CHECK_EQI(cnc_parse_header(tx, n, &h), CNC_PARSE_OK);
    cnc_motion_block_t blk;
    CHECK(cnc_parse_motion(&h, &blk));
    CHECK_EQI(blk.block_seq, 1);
    CHECK_EQI(blk.slice_us, SLICE_US);
    CHECK_EQI(blk.record_count, 2);
    cnc_motion_record_t got;
    cnc_motion_record(&blk, 0, &got);
    CHECK_EQI(got.steps_q16[0], 10 * 65536);
    cnc_motion_record(&blk, 1, &got);
    CHECK_EQI(got.steps_q16[1], -20 * 65536);
    TDONE();

    TCASE("a full 32-record block fits well inside one MTU");
    cnc_motion_record_t big[CNC_MAX_RECORDS];
    for (uint32_t i = 0; i < CNC_MAX_RECORDS; i++) { big[i] = rec_axis(0, 1); }
    n = cnc_encode_motion(tx, sizeof(tx), 10, 1, SLICE_US, big,
                          CNC_MAX_RECORDS, 0);
    CHECK_EQI(n, CNC_MAX_PACKET);
    CHECK(n <= 1472);                     /* no IP fragmentation, ever */
    CHECK_EQI(cnc_parse_header(tx, n, &h), CNC_PARSE_OK);
    TDONE();
}

static void test_framing_rejects(void)
{
    cnc_header_t h;
    size_t n;

    TCASE("a truncated datagram is rejected");
    /* Below the 16-byte minimum there is not even a header to read. */
    n = cnc_encode_simple(tx, sizeof(tx), 1, CNC_OP_HELLO);
    CHECK_EQI(cnc_parse_header(tx, n - 1, &h), CNC_PARSE_TOO_SHORT);
    CHECK_EQI(cnc_parse_header(tx, 0, &h), CNC_PARSE_TOO_SHORT);
    CHECK_EQI(cnc_parse_header(tx, CNC_OVERHEAD - 1, &h), CNC_PARSE_TOO_SHORT);
    /* Above it, a datagram cut short of its declared payload is a length
     * error - the case a truncating middlebox or a short read produces. */
    cnc_motion_record_t tr = rec_axis(0, 1);
    n = cnc_encode_motion(tx, sizeof(tx), 1, 1, SLICE_US, &tr, 1, 0);
    CHECK_EQI(cnc_parse_header(tx, n - 4, &h), CNC_PARSE_BAD_LENGTH);
    TDONE();

    TCASE("wrong magic is rejected");
    n = cnc_encode_simple(tx, sizeof(tx), 1, CNC_OP_HELLO);
    tx[0] ^= 0xFFu;
    CHECK_EQI(cnc_parse_header(tx, n, &h), CNC_PARSE_BAD_MAGIC);
    TDONE();

    TCASE("an unsupported version is never guessed at");
    n = cnc_encode_simple(tx, sizeof(tx), 1, CNC_OP_HELLO);
    tx[4] = 2;
    CHECK_EQI(cnc_parse_header(tx, n, &h), CNC_PARSE_BAD_VERSION);
    TDONE();

    TCASE("an unknown opcode is rejected");
    n = cnc_encode_simple(tx, sizeof(tx), 1, CNC_OP_HELLO);
    tx[5] = 0x7F;
    /* CRC is now wrong too, but the opcode check comes first by design. */
    CHECK_EQI(cnc_parse_header(tx, n, &h), CNC_PARSE_BAD_OPCODE);
    TDONE();

    TCASE("a declared length that disagrees with the datagram is rejected");
    n = cnc_encode_simple(tx, sizeof(tx), 1, CNC_OP_HELLO);
    tx[6] = 4;                                  /* claims 4 payload bytes */
    CHECK_EQI(cnc_parse_header(tx, n, &h), CNC_PARSE_BAD_LENGTH);
    /* And the other direction: a longer datagram is not silently accepted
     * for its prefix - that would mean acting on a packet nobody sent. */
    n = cnc_encode_simple(tx, sizeof(tx), 1, CNC_OP_HELLO);
    tx[n] = 0xAA;
    CHECK_EQI(cnc_parse_header(tx, n + 1, &h), CNC_PARSE_BAD_LENGTH);
    TDONE();

    TCASE("an absurd payload length cannot overflow anything");
    n = cnc_encode_simple(tx, sizeof(tx), 1, CNC_OP_HELLO);
    tx[6] = 0xFF; tx[7] = 0xFF;
    CHECK_EQI(cnc_parse_header(tx, n, &h), CNC_PARSE_BAD_LENGTH);
    TDONE();

    TCASE("a corrupted payload byte is caught by the CRC");
    cnc_motion_record_t r = rec_axis(0, 5);
    n = cnc_encode_motion(tx, sizeof(tx), 1, 1, SLICE_US, &r, 1, 0);
    CHECK_EQI(cnc_parse_header(tx, n, &h), CNC_PARSE_OK);
    tx[CNC_HDR_SIZE + 9] ^= 0x01u;           /* flip one bit of one axis */
    CHECK_EQI(cnc_parse_header(tx, n, &h), CNC_PARSE_BAD_CRC);
    TDONE();

    TCASE("a corrupted CRC field itself is caught");
    n = cnc_encode_simple(tx, sizeof(tx), 1, CNC_OP_HELLO);
    tx[n - 1] ^= 0x80u;
    CHECK_EQI(cnc_parse_header(tx, n, &h), CNC_PARSE_BAD_CRC);
    TDONE();

    TCASE("a motion block with an inconsistent record count is rejected");
    n = cnc_encode_motion(tx, sizeof(tx), 1, 1, SLICE_US, &r, 1, 0);
    tx[CNC_HDR_SIZE + 6] = 2;                /* claims 2, carries 1 */
    /* Re-CRC so the framing passes and the payload check is what fires. */
    {
        const size_t crc_off = n - CNC_CRC_SIZE;
        const uint32_t c = cnc_crc32(tx, crc_off);
        tx[crc_off]     = (uint8_t)(c & 0xFF);
        tx[crc_off + 1] = (uint8_t)((c >> 8) & 0xFF);
        tx[crc_off + 2] = (uint8_t)((c >> 16) & 0xFF);
        tx[crc_off + 3] = (uint8_t)((c >> 24) & 0xFF);
    }
    CHECK_EQI(cnc_parse_header(tx, n, &h), CNC_PARSE_OK);
    cnc_motion_block_t blk;
    CHECK(!cnc_parse_motion(&h, &blk));
    TDONE();

    TCASE("reserved motion flag bits must be zero");
    n = cnc_encode_motion(tx, sizeof(tx), 1, 1, SLICE_US, &r, 1, 0x02);
    CHECK_EQI(cnc_parse_header(tx, n, &h), CNC_PARSE_OK);
    CHECK(!cnc_parse_motion(&h, &blk));
    TDONE();
}

/* ------------------------------------------------------------------ */
/* Corrupt packets must never reach the engine                         */
/* ------------------------------------------------------------------ */

static void test_corrupt_never_reaches_engine(void)
{
    TCASE("a corrupt packet is silent, counted, and enqueues nothing");
    engine_reset();

    const uint32_t free_before = stepgen_queue_free();

    cnc_motion_record_t r = rec_axis(0, 100);
    size_t n = cnc_encode_motion(tx, sizeof(tx), ++host_seq, 1, SLICE_US,
                                 &r, 1, 0);
    tx[CNC_HDR_SIZE + 2] ^= 0x40u;                        /* corrupt it */

    /* No reply at all: with a failed CRC the sequence number in the packet
     * is not trustworthy either, so there is nothing to answer. */
    CHECK_EQI(deliver(tx, n, 1000), 0);
    CHECK_EQI(stepgen_queue_free(), free_before);

    /* And it is visible in the counters once a good packet asks. */
    size_t rl = send_control(CNC_CTL_ENABLE_DRIVES, 1001);
    cnc_status_t st;
    CHECK(reply_status(rl, &st));
    CHECK_EQI(st.rx_dropped, 1);
    CHECK_EQI(st.last_block_seq, 0);
    TDONE();
}

/* ------------------------------------------------------------------ */
/* Sequence rules                                                      */
/* ------------------------------------------------------------------ */

static void test_seq_duplicate_and_stale(void)
{
    cnc_status_t st;

    TCASE("a replayed packet is answered but not acted on");
    engine_reset();

    size_t rl = send_control(CNC_CTL_ENABLE_DRIVES, 100);
    CHECK(reply_status(rl, &st));
    CHECK_EQI(st.rx_accepted, 1);
    const uint32_t seq_used = host_seq;

    /* Replay the identical datagram - same seq. */
    rl = deliver(tx, cnc_encode_control(tx, sizeof(tx), seq_used,
                                        CNC_CTL_ENABLE_DRIVES, 0), 101);
    CHECK(rl > 0);                      /* answered, so the host re-syncs */
    CHECK(reply_status(rl, &st));
    CHECK_EQI(st.rx_accepted, 1);       /* but not counted again */
    CHECK_EQI(st.last_reject_reason, CNC_REJECT_STALE);
    TDONE();

    TCASE("an older sequence number is treated as a straggler");
    rl = deliver(tx, cnc_encode_control(tx, sizeof(tx), seq_used - 1,
                                        CNC_CTL_START, 0), 102);
    CHECK(reply_status(rl, &st));
    CHECK_EQI(st.rx_accepted, 1);
    CHECK_EQI(st.last_reject_reason, CNC_REJECT_STALE);
    CHECK_EQI(st.last_seq_seen, seq_used);
    TDONE();
}

/* ------------------------------------------------------------------ */
/* Motion: conversion, axis mapping, sequencing                        */
/* ------------------------------------------------------------------ */

static void test_motion_produces_exact_steps(void)
{
    TCASE("a motion block emits exactly the steps it commanded");
    engine_reset();
    CHECK(stepgen_enable_drives());

    /* 400 steps on X in one 4 ms slice = 100 kHz, comfortably legal. */
    cnc_motion_record_t r = rec_axis(MOTION_AXIS_X, 400);
    size_t rl = send_motion(1, &r, 1, 0, 200);

    cnc_status_t st;
    CHECK(reply_status(rl, &st));
    CHECK_EQI(st.last_block_seq, 1);
    CHECK_EQI(st.last_reject_reason, CNC_REJECT_NONE);
    CHECK((st.flags & CNC_SFLAG_MOTION_SYNCED) != 0);
    CHECK((st.flags & CNC_SFLAG_MOTION_ACTIVE) != 0);

    CHECK(stepgen_start());
    sim_port_run_ticks(SLICE_TICKS + TRACE_TAIL);

    /* The whole chain - Q16 wire value, tick conversion, Q32 rate, DDA,
     * BSRR words - is worth exactly this one number. */
    CHECK_STEPS(MOTION_AXIS_X, 400);
    TDONE();

    TCASE("a longer run does not drift away from the commanded total");
    /* If the encoding or the conversion had a scale error, the gap would
     * grow with the length of the run. It does not. */
    engine_reset();
    CHECK(stepgen_enable_drives());
    cnc_motion_record_t many[16];
    for (uint32_t i = 0; i < 16; i++) { many[i] = rec_axis(MOTION_AXIS_X, 400); }
    (void)send_motion(1, many, 16, 0, 210);
    CHECK(stepgen_start());
    sim_port_run_ticks(SLICE_TICKS * 16u + TRACE_TAIL);
    CHECK_STEPS(MOTION_AXIS_X, 16 * 400);
    TDONE();
}

static void test_axis_mapping(void)
{
    TCASE("each wire slot drives its own axis and no other");
    /* X,Y,Z,A,B are wire slots 0..4. A sixth axis does not exist at any
     * offset, which is what stops Mach3's C being mis-indexed into B. */
    for (uint32_t axis = 0; axis < MOTION_AXIS_COUNT; axis++) {
        engine_reset();
        CHECK(stepgen_enable_drives());

        cnc_motion_record_t r = rec_axis(axis, 100);
        (void)send_motion(1, &r, 1, 0, 300);
        CHECK(stepgen_start());
        sim_port_run_ticks(SLICE_TICKS + TRACE_TAIL);

        for (uint32_t other = 0; other < MOTION_AXIS_COUNT; other++) {
            if (other == axis) {
                CHECK_STEPS(other, 100);
            } else {
                /* Exactly zero, not "about zero": a mis-indexed axis is
                 * how Mach3's sixth axis would silently become this one. */
                CHECK_EQI(pulses(other), 0u);
            }
        }
    }
    TDONE();
}

static void test_negative_direction(void)
{
    TCASE("a negative wire value moves the axis backwards");
    engine_reset();
    CHECK(stepgen_enable_drives());

    cnc_motion_record_t r = rec_axis(MOTION_AXIS_Y, -250);
    (void)send_motion(1, &r, 1, 0, 400);
    CHECK(stepgen_start());
    sim_port_run_ticks(SLICE_TICKS + TRACE_TAIL);

    CHECK_STEPS(MOTION_AXIS_Y, 250);

    /* Direction is what this case is really about: the engine must report a
     * negative position, not a positive one of the same size. */
    stepgen_status_t es;
    stepgen_get_status(&es);
    CHECK(es.pos_output[MOTION_AXIS_Y] < 0);
    CHECK(es.pos_output[MOTION_AXIS_Y] >= -250);
    CHECK(es.pos_output[MOTION_AXIS_Y] <= -249);
    TDONE();
}

static void test_fractional_carry_is_hosts_job(void)
{
    TCASE("sub-step wire values accumulate rather than vanish");
    engine_reset();
    CHECK(stepgen_enable_drives());

    /* Half a step per slice, eight slices: four steps, not zero and not
     * eight. This is the device side of the host's fractional carry. */
    cnc_motion_record_t recs[8];
    for (uint32_t i = 0; i < 8; i++) {
        memset(&recs[i], 0, sizeof(recs[i]));
        recs[i].steps_q16[MOTION_AXIS_Z] = 65536 / 2;
    }
    (void)send_motion(1, recs, 8, 0, 500);
    CHECK(stepgen_start());
    sim_port_run_ticks(SLICE_TICKS * 8u + TRACE_TAIL);

    CHECK_STEPS(MOTION_AXIS_Z, 4);
    /* And nothing leaked onto an axis that was commanded zero. */
    CHECK_EQI(pulses(MOTION_AXIS_X), 0u);
    TDONE();
}

static void test_block_seq_gap_stops_motion(void)
{
    cnc_status_t st;

    TCASE("a lost motion block stops the machine instead of skipping it");
    engine_reset();
    CHECK(stepgen_enable_drives());

    cnc_motion_record_t r = rec_axis(0, 10);
    (void)send_motion(1, &r, 1, 0, 600);
    CHECK(stepgen_start());

    /* Block 2 never arrived. Block 3 must not be executed: the missing
     * block is a missing piece of the toolpath. */
    size_t rl = send_motion(3, &r, 1, 0, 601);
    CHECK(reply_status(rl, &st));
    CHECK_EQI(st.last_reject_reason, CNC_REJECT_SEQ_GAP);
    CHECK((st.proto_faults & CNC_PFAULT_SEQ_GAP) != 0);
    CHECK_EQI(st.last_block_seq, 1);              /* still block 1 */
    CHECK_EQI(st.state, STEPGEN_STATE_READY);     /* stopped, not running */
    /* ADR-010: a command-stream problem holds position with the drives
     * still energised - it is not an emergency stop. */
    CHECK((st.flags & CNC_SFLAG_DRIVES_ENABLED) != 0);
    CHECK(st.state != STEPGEN_STATE_EMERGENCY_STOP);
    TDONE();

    TCASE("the gap fault does not clear by itself when the host resumes");
    /* Sending the block that was expected must not paper over the loss. */
    rl = send_motion(2, &r, 1, 0, 700);
    CHECK(reply_status(rl, &st));
    CHECK((st.proto_faults & CNC_PFAULT_SEQ_GAP) != 0);
    TDONE();

    TCASE("an explicit clear releases it, and the stream re-syncs");
    rl = send_control(CNC_CTL_CLEAR_FAULT, 800);
    CHECK(reply_status(rl, &st));
    CHECK_EQI(st.proto_faults, 0);
    CHECK((st.flags & CNC_SFLAG_MOTION_SYNCED) == 0);

    /* Unsynced: the next block establishes a new baseline whatever its
     * number, so the two ends need not agree on where to restart. */
    rl = send_motion(900, &r, 1, 0, 801);
    CHECK(reply_status(rl, &st));
    CHECK_EQI(st.last_block_seq, 900);
    CHECK_EQI(st.last_reject_reason, CNC_REJECT_NONE);
    TDONE();
}

static void test_block_duplicate_is_idempotent(void)
{
    cnc_status_t st;

    TCASE("a retried motion block is not enqueued twice");
    engine_reset();
    CHECK(stepgen_enable_drives());

    cnc_motion_record_t r = rec_axis(0, 10);
    (void)send_motion(1, &r, 1, 0, 900);
    const uint32_t free_after_first = stepgen_queue_free();

    /* Same block_seq, fresh message seq - exactly what the host sends when
     * it retries after a buffer-full or a lost status. */
    size_t rl = send_motion(1, &r, 1, 0, 901);
    CHECK(reply_status(rl, &st));
    CHECK_EQI(stepgen_queue_free(), free_after_first);   /* not re-queued */
    CHECK_EQI(st.last_block_seq, 1);
    CHECK_EQI(st.last_reject_reason, CNC_REJECT_STALE);
    TDONE();

    TCASE("an out-of-order older block is dropped, not executed");
    (void)send_motion(2, &r, 1, 0, 902);
    const uint32_t free_at_two = stepgen_queue_free();
    rl = send_motion(1, &r, 1, 0, 903);
    CHECK(reply_status(rl, &st));
    CHECK_EQI(stepgen_queue_free(), free_at_two);
    CHECK_EQI(st.last_block_seq, 2);
    CHECK_EQI(st.last_reject_reason, CNC_REJECT_STALE);
    TDONE();
}

/* ------------------------------------------------------------------ */
/* Backpressure                                                        */
/* ------------------------------------------------------------------ */

static void test_queue_full_backpressure(void)
{
    cnc_status_t st;

    TCASE("a block that does not fit is refused whole, not in part");
    engine_reset();
    CHECK(stepgen_enable_drives());

    cnc_motion_record_t recs[CNC_MAX_RECORDS];
    for (uint32_t i = 0; i < CNC_MAX_RECORDS; i++) { recs[i] = rec_axis(0, 1); }

    /* Fill until the next full block cannot fit. */
    uint32_t block = 1;
    while (stepgen_queue_free() >= CNC_MAX_RECORDS) {
        (void)send_motion(block, recs, CNC_MAX_RECORDS, 0, 1000u + block);
        block++;
    }

    const uint32_t free_before = stepgen_queue_free();
    CHECK(free_before < CNC_MAX_RECORDS);

    size_t rl = send_motion(block, recs, CNC_MAX_RECORDS, 0, 2000);
    CHECK(reply_status(rl, &st));
    CHECK_EQI(st.last_reject_reason, CNC_REJECT_QUEUE_FULL);
    CHECK((st.proto_faults & CNC_PFAULT_QUEUE_FULL) != 0);
    /* Nothing at all was taken - not even the records that would have fit. */
    CHECK_EQI(stepgen_queue_free(), free_before);
    CHECK_EQI(st.queue_free, free_before);
    CHECK(st.last_block_seq != block);
    TDONE();

    TCASE("the same block succeeds once the queue drains");
    CHECK(stepgen_start());
    sim_port_run_ticks(SLICE_TICKS * (uint32_t)CNC_MAX_RECORDS);
    CHECK(stepgen_queue_free() >= CNC_MAX_RECORDS);

    rl = send_motion(block, recs, CNC_MAX_RECORDS, 0, 3000);
    CHECK(reply_status(rl, &st));
    CHECK_EQI(st.last_reject_reason, CNC_REJECT_NONE);
    CHECK_EQI(st.last_block_seq, block);
    TDONE();
}

/* ------------------------------------------------------------------ */
/* Parameter validation                                                */
/* ------------------------------------------------------------------ */

static void test_slice_must_convert_exactly(void)
{
    cnc_status_t st;

    TCASE("a slice that is not a whole number of ticks is refused");
    engine_reset();

    /* At the default 4 MHz tick every integer microsecond lands on a whole
     * number of ticks, so the inexact case needs a tick that is not a whole
     * number of MHz. 168 MHz / 50 = 3.36 MHz is a legal machine
     * configuration (1.68 MHz maximum step rate) and is not. */
    CHECK(stepgen_configure_max_rate(1680000u));
    CHECK_EQI(stepgen_tick_hz(), 3360000u);
    CHECK(stepgen_enable_drives());

    cnc_motion_record_t r = rec_axis(0, 1);
    /* 5 us x 3.36 MHz = 16.8 ticks - not representable. Rounding it would
     * put a permanent, invisible timing error on every feed. */
    size_t n = cnc_encode_motion(tx, sizeof(tx), ++host_seq, 1, 5u, &r, 1, 0);
    size_t rl = deliver(tx, n, 100);
    CHECK(reply_status(rl, &st));
    CHECK_EQI(st.last_reject_reason, CNC_REJECT_SLICE_NOT_EXACT);
    CHECK_EQI(st.last_block_seq, 0);

    /* 25 us at the same tick is exactly 84 ticks, and is accepted. */
    n = cnc_encode_motion(tx, sizeof(tx), ++host_seq, 1, 25u, &r, 1, 0);
    rl = deliver(tx, n, 101);
    CHECK(reply_status(rl, &st));
    CHECK_EQI(st.last_reject_reason, CNC_REJECT_NONE);
    CHECK_EQI(st.last_block_seq, 1);
    TDONE();
}

static void test_rate_ceiling_is_refused_not_clamped(void)
{
    cnc_status_t st;

    TCASE("a rate above the 2 MHz ceiling is refused, never clamped");
    engine_reset();
    CHECK(stepgen_enable_drives());

    /* 4 ms at 2 MHz is 8000 steps. Ask for one more. Clamping would turn a
     * straight coordinated move into a curve, silently. */
    cnc_motion_record_t r = rec_axis(0, 8001);
    size_t rl = send_motion(1, &r, 1, 0, 100);
    CHECK(reply_status(rl, &st));
    CHECK_EQI(st.last_reject_reason, CNC_REJECT_RATE_TOO_HIGH);
    CHECK_EQI(st.last_block_seq, 0);
    CHECK_EQI(st.queue_free, st.queue_depth);       /* nothing enqueued */
    TDONE();

    TCASE("exactly the ceiling is accepted");
    r = rec_axis(0, 8000);
    rl = send_motion(1, &r, 1, 0, 101);
    CHECK(reply_status(rl, &st));
    CHECK_EQI(st.last_reject_reason, CNC_REJECT_NONE);
    TDONE();

    TCASE("one bad axis rejects the whole block");
    engine_reset();
    CHECK(stepgen_enable_drives());
    cnc_motion_record_t bad[2];
    bad[0] = rec_axis(0, 10);
    bad[1] = rec_axis(3, 9000);                    /* over the ceiling */
    const uint32_t before = stepgen_queue_free();
    rl = send_motion(1, bad, 2, 0, 200);
    CHECK(reply_status(rl, &st));
    CHECK_EQI(st.last_reject_reason, CNC_REJECT_RATE_TOO_HIGH);
    CHECK_EQI(stepgen_queue_free(), before);       /* including record 0 */
    TDONE();
}

/* ------------------------------------------------------------------ */
/* Control                                                             */
/* ------------------------------------------------------------------ */

static void test_control_commands(void)
{
    cnc_status_t st;

    TCASE("enable, start, stop follow the ADR-010 state model");
    engine_reset();

    size_t rl = send_control(CNC_CTL_ENABLE_DRIVES, 10);
    CHECK(reply_status(rl, &st));
    CHECK_EQI(st.state, STEPGEN_STATE_READY);
    CHECK((st.flags & CNC_SFLAG_DRIVES_ENABLED) != 0);

    rl = send_control(CNC_CTL_START, 11);
    CHECK(reply_status(rl, &st));
    CHECK_EQI(st.state, STEPGEN_STATE_RUNNING);

    rl = send_control(CNC_CTL_STOP, 12);
    CHECK(reply_status(rl, &st));
    CHECK_EQI(st.state, STEPGEN_STATE_READY);
    CHECK((st.flags & CNC_SFLAG_DRIVES_ENABLED) != 0);
    TDONE();

    TCASE("abort stops, keeps the drives live, and desyncs the stream");
    engine_reset();
    CHECK(stepgen_enable_drives());
    cnc_motion_record_t r = rec_axis(0, 10);
    (void)send_motion(1, &r, 1, 0, 20);
    CHECK(stepgen_start());

    rl = send_control(CNC_CTL_ABORT, 21);
    CHECK(reply_status(rl, &st));
    CHECK_EQI(st.state, STEPGEN_STATE_READY);
    CHECK((st.flags & CNC_SFLAG_MOTION_SYNCED) == 0);
    CHECK((st.flags & CNC_SFLAG_MOTION_ACTIVE) == 0);
    CHECK((st.flags & CNC_SFLAG_DRIVES_ENABLED) != 0);
    TDONE();

    TCASE("an illegal transition is refused without faulting the protocol");
    engine_reset();
    rl = send_control(CNC_CTL_START, 30);        /* START from SAFE_IDLE */
    CHECK(reply_status(rl, &st));
    CHECK_EQI(st.last_reject_reason, CNC_REJECT_WRONG_STATE);
    CHECK_EQI(st.proto_faults, 0);
    TDONE();

    TCASE("an unknown control command is rejected");
    rl = send_control(99, 31);
    CHECK(reply_status(rl, &st));
    CHECK_EQI(st.last_reject_reason, CNC_REJECT_BAD_PARAM);
    TDONE();

    TCASE("no packet can clear an emergency stop that is still asserted");
    engine_reset();
    stepgen_emergency_stop();
    rl = send_control(CNC_CTL_CLEAR_FAULT, 40);
    CHECK(reply_status(rl, &st));
    /* A generic fault clear must never clear an E-stop - they are
     * structurally distinct states (ADR-010). */
    CHECK_EQI(st.state, STEPGEN_STATE_EMERGENCY_STOP);
    TDONE();
}

/* ------------------------------------------------------------------ */
/* Timeout and recovery                                                */
/* ------------------------------------------------------------------ */

static void test_comm_timeout(void)
{
    cnc_status_t st;
    size_t n;

    TCASE("a host that stops talking mid-program stops the machine");
    engine_reset();
    CHECK(stepgen_enable_drives());
    cnc_motion_record_t r = rec_axis(0, 10);
    (void)send_motion(1, &r, 1, 0, 1000);
    CHECK(stepgen_start());

    /* Just short of the timeout: nothing has happened yet. */
    n = cnc_session_tick(1000 + CNC_COMM_TIMEOUT_MS - 1, rx, sizeof(rx));
    if (n) { CHECK(reply_status(n, &st)); CHECK_EQI(st.proto_faults, 0); }

    n = cnc_session_tick(1000 + CNC_COMM_TIMEOUT_MS, rx, sizeof(rx));
    CHECK(n > 0);
    CHECK(reply_status(n, &st));
    CHECK((st.proto_faults & CNC_PFAULT_COMM_TIMEOUT) != 0);
    CHECK_EQI(st.state, STEPGEN_STATE_READY);
    /* A dead link is not an E-stop, and it does not drop the drives. */
    CHECK(st.state != STEPGEN_STATE_EMERGENCY_STOP);
    CHECK((st.flags & CNC_SFLAG_DRIVES_ENABLED) != 0);
    TDONE();

    TCASE("the timeout does not clear when the host comes back");
    n = cnc_encode_simple(tx, sizeof(tx), ++host_seq, CNC_OP_STATUS_REQ);
    const size_t rl = deliver(tx, n, 5001);
    CHECK(reply_status(rl, &st));
    CHECK((st.proto_faults & CNC_PFAULT_COMM_TIMEOUT) != 0);
    TDONE();

    TCASE("a quiet host between programs is not a fault");
    engine_reset();
    CHECK(stepgen_enable_drives());
    /* END_OF_PROGRAM clears motion_active, so supervision stands down. */
    (void)send_motion(1, &r, 1, CNC_MOTION_FLAG_END_OF_PROGRAM, 6000);
    n = cnc_session_tick(6000 + 10u * CNC_COMM_TIMEOUT_MS, rx, sizeof(rx));
    CHECK(n > 0);
    CHECK(reply_status(n, &st));
    CHECK_EQI(st.proto_faults, 0);
    CHECK((st.flags & CNC_SFLAG_MOTION_ACTIVE) == 0);
    TDONE();
}

static void test_status_stream(void)
{
    cnc_status_t st;

    TCASE("nothing is transmitted before a host is known");
    engine_reset();
    CHECK(!cnc_session_host_known());
    CHECK_EQI(cnc_session_tick(1, rx, sizeof(rx)), 0);
    TDONE();

    TCASE("status is emitted at the published period, not faster");
    size_t rl = send_control(CNC_CTL_ENABLE_DRIVES, 100);
    CHECK(rl > 0);
    CHECK(cnc_session_host_known());

    /* The reply above counts as this period's status. */
    CHECK_EQI(cnc_session_tick(100 + CNC_STATUS_PERIOD_MS - 1,
                               rx, sizeof(rx)), 0);
    rl = cnc_session_tick(100 + CNC_STATUS_PERIOD_MS, rx, sizeof(rx));
    CHECK(rl > 0);
    CHECK(reply_status(rl, &st));
    TDONE();
}

/* ------------------------------------------------------------------ */
/* Status and info encoding                                            */
/* ------------------------------------------------------------------ */

static void test_status_roundtrip(void)
{
    TCASE("every status field survives encode -> decode");
    cnc_status_t in, out;
    memset(&in, 0, sizeof(in));

    in.state = 3; in.faults = 0x12; in.flags = 0x5AA5;
    in.queue_free = 17; in.queue_depth = 64; in.proto_faults = 0x000B;
    in.last_seq_seen = 0xDEADBEEFu; in.last_seq_accepted = 0x12345678u;
    in.last_block_seq = 0xFEEDFACEu; in.last_reject_reason = 7;
    in.inputs = 0x7FFF; in.outputs = 0x0003; in.spindle_pmille = 1000;
    in.segments_consumed = 123456u; in.underruns = 7u; in.starved_ticks = 99u;
    in.rx_accepted = 1000u; in.rx_rejected = 20u; in.rx_dropped = 3u;
    in.uptime_ms = 0x01020304u;
    in.pos_output[0] = 9007199254740993LL;      /* > 2^53, so a double
                                                 * round-trip would lose it */
    in.pos_output[1] = -1;
    in.pos_output[4] = INT64_MIN + 1;

    const size_t n = cnc_encode_status(tx, sizeof(tx), 42, &in);
    CHECK_EQI(n, CNC_OVERHEAD + CNC_STATUS_PAYLOAD_SIZE);

    cnc_header_t h;
    CHECK_EQI(cnc_parse_header(tx, n, &h), CNC_PARSE_OK);
    CHECK_EQI(h.seq, 42);
    CHECK(cnc_parse_status(&h, &out));
    CHECK(memcmp(&in, &out, sizeof(in)) == 0);
    TDONE();

    TCASE("a status buffer that is too small is refused, not truncated");
    CHECK_EQI(cnc_encode_status(tx, CNC_OVERHEAD, 1, &in), 0);
    TDONE();
}

static void test_info(void)
{
    TCASE("HELLO returns the identity the host needs to talk safely");
    engine_reset();

    const size_t n = cnc_encode_simple(tx, sizeof(tx), ++host_seq,
                                       CNC_OP_HELLO);
    const size_t rl = deliver(tx, n, 10);
    CHECK(rl > 0);

    cnc_header_t h;
    CHECK_EQI(cnc_parse_header(rx, rl, &h), CNC_PARSE_OK);
    CHECK_EQI(h.opcode, CNC_OP_INFO);

    cnc_info_t info;
    CHECK(cnc_parse_info(&h, &info));
    CHECK_EQI(info.proto_version, CNC_PROTO_VERSION);
    CHECK_EQI(info.axis_count, MOTION_AXIS_COUNT);
    CHECK_EQI(info.queue_depth, MOTION_SEGMENT_QUEUE_DEPTH);
    /* tick_hz is the field that matters most: without it the host cannot
     * check that its slice converts exactly or that its feeds fit. */
    CHECK_EQI(info.tick_hz, stepgen_tick_hz());
    CHECK_EQI(info.max_step_rate_hz, stepgen_max_rate_hz());
    CHECK_EQI(info.max_records_per_block, CNC_MAX_RECORDS);
    CHECK_EQI(info.udp_port, NET_UDP_PORT);
    CHECK_EQI(info.comm_timeout_ms, CNC_COMM_TIMEOUT_MS);
    CHECK_EQI(info.ip[0], 192);
    CHECK_EQI(info.ip[3], 10);
    CHECK_EQI(info.mac[0], 0x02);
    TDONE();
}

static void test_outputs_not_implemented(void)
{
    cnc_status_t st;

    TCASE("OUTPUTS is answered honestly rather than accepted silently");
    engine_reset();

    /* The wire format is fixed so the plugin can be written, but M9/M10 do
     * not exist. Accepting would tell the host a relay had switched. */
    uint8_t *p = &tx[CNC_HDR_SIZE];
    const uint16_t plen = CNC_OUTPUTS_PAYLOAD_SIZE;
    tx[0] = 0x43; tx[1] = 0x35; tx[2] = 0x50; tx[3] = 0x31;
    tx[4] = CNC_PROTO_VERSION; tx[5] = CNC_OP_OUTPUTS;
    tx[6] = (uint8_t)plen; tx[7] = 0;
    tx[8] = 1; tx[9] = 0; tx[10] = 0; tx[11] = 0;
    memset(p, 0, plen);
    p[0] = 0x01; p[2] = 0x01;                     /* relay on */
    {
        const size_t crc_off = CNC_HDR_SIZE + plen;
        const uint32_t c = cnc_crc32(tx, crc_off);
        tx[crc_off]     = (uint8_t)(c & 0xFF);
        tx[crc_off + 1] = (uint8_t)((c >> 8) & 0xFF);
        tx[crc_off + 2] = (uint8_t)((c >> 16) & 0xFF);
        tx[crc_off + 3] = (uint8_t)((c >> 24) & 0xFF);
    }

    const size_t rl = deliver(tx, CNC_OVERHEAD + plen, 10);
    CHECK(reply_status(rl, &st));
    CHECK_EQI(st.last_reject_reason, CNC_REJECT_NOT_IMPLEMENTED);
    /* And the host is told the subsystem is absent, so an all-zero output
     * word cannot be mistaken for a real reading. */
    CHECK((st.flags & CNC_SFLAG_OUTPUTS_PRESENT) == 0);
    CHECK((st.flags & CNC_SFLAG_INPUTS_PRESENT) == 0);
    TDONE();
}

/* ------------------------------------------------------------------ */

int main(void)
{
    printf("CNC5AX-ETH protocol (M13/M14) host verification\n");
    printf("  C5P1 v%u on UDP %u, %u axes, %u-record blocks, "
           "status %u ms, timeout %u ms\n",
           CNC_PROTO_VERSION, NET_UDP_PORT, MOTION_AXIS_COUNT,
           CNC_MAX_RECORDS, CNC_STATUS_PERIOD_MS, CNC_COMM_TIMEOUT_MS);
    printf("  max packet %u bytes, slice %u us = %u ticks @ %u Hz\n\n",
           CNC_MAX_PACKET, SLICE_US, SLICE_TICKS, STEPGEN_TICK_HZ);

    test_crc();
    test_framing_roundtrip();
    test_framing_rejects();
    test_corrupt_never_reaches_engine();
    test_seq_duplicate_and_stale();
    test_motion_produces_exact_steps();
    test_axis_mapping();
    test_negative_direction();
    test_fractional_carry_is_hosts_job();
    test_block_seq_gap_stops_motion();
    test_block_duplicate_is_idempotent();
    test_queue_full_backpressure();
    test_slice_must_convert_exactly();
    test_rate_ceiling_is_refused_not_clamped();
    test_control_commands();
    test_comm_timeout();
    test_status_stream();
    test_status_roundtrip();
    test_info();
    test_outputs_not_implemented();

    printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
