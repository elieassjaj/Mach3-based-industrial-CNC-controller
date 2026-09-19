/**
 * @file    cnc_session.c
 * @brief   C5P1 session layer (see cnc_session.h, Docs/PROTOCOL.md).
 */
#include "cnc_session.h"

#include "net_config.h"
#include "net_link.h"
#include "safety_input.h"
#include "io_outputs.h"
#include "io_spindle.h"
#include "stepgen.h"

#include <string.h>

/* ---------------------------------------------------------------------- */

typedef struct {
    /* Sequence spaces. Docs/PROTOCOL.md §4.4 and §5.2. */
    uint32_t last_seq_seen;
    uint32_t last_seq_accepted;
    bool     seq_started;

    uint32_t last_block_seq;
    bool     motion_synced;
    bool     motion_active;

    /* Latched, cleared only by CONTROL:CLEAR_FAULT. */
    uint16_t proto_faults;
    uint16_t last_reject;

    /* Supervision. */
    bool     host_known;
    uint32_t last_rx_ms;
    uint32_t last_status_ms;
    bool     status_started;
    uint32_t boot_ms;
    bool     boot_ms_valid;

    /* Counters. */
    uint32_t rx_accepted;
    uint32_t rx_rejected;
    uint32_t rx_dropped;
} session_t;

static session_t s;

void cnc_session_init(void)
{
    memset(&s, 0, sizeof(s));
}

bool cnc_session_host_known(void)
{
    return s.host_known;
}

/* ---------------------------------------------------------------------- */

static void reject(uint16_t reason, uint16_t latch)
{
    s.last_reject = reason;
    s.proto_faults |= latch;
    s.rx_rejected++;
}

/**
 * The response to a command-stream problem, and the only place it is
 * decided. ADR-010: hold position with the drives still energised, because
 * a paused or broken link must never de-energise a stepper and let an axis
 * drift or drop under load. Explicitly not an emergency stop - the local
 * PE2 path is the only thing that may do that, and it does not involve the
 * network at all (Docs/FIRMWARE-ARCHITECTURE.md §8).
 */
static void controlled_stop(void)
{
    stepgen_stop();
    s.motion_active = false;
}

/* ------------------------------------------------------- motion ------- */

/**
 * slice_us -> engine ticks, exactly or not at all.
 *
 * A slice that does not land on a whole number of ticks would put a
 * systematic timing error on every feed it encodes - the same reason
 * stepgen_configure_max_rate() accepts only exact dividers of the timer
 * clock. Rejecting is the only honest option; rounding here would be
 * invisible and permanent.
 */
static bool slice_to_ticks(uint16_t slice_us, uint32_t *out_ticks)
{
    const uint64_t prod = (uint64_t)slice_us * (uint64_t)stepgen_tick_hz();

    if ((prod % 1000000u) != 0u) {
        return false;
    }
    const uint64_t ticks = prod / 1000000u;
    if (ticks == 0u || ticks > 0xFFFFFFFFu) {
        return false;
    }
    *out_ticks = (uint32_t)ticks;
    return true;
}

/**
 * One axis value -> the engine's Q32 rate.
 *
 *      rate = round( steps_q16 * 65536 / duration_ticks )
 *
 * Round to nearest rather than truncate: truncation is biased, and the bias
 * accumulates in one direction over a program. The residual is still
 * non-zero (bounded by half a Q32 LSB per tick) which is why the host is
 * expected to close the loop from pos_output[] - Docs/PROTOCOL.md §5.3.
 */
static motion_rate_q32_t steps_to_rate(int32_t steps_q16, uint32_t ticks)
{
    const int64_t num = (int64_t)steps_q16 << 16;
    const int64_t den = (int64_t)ticks;

    return (num >= 0) ? (num + den / 2) / den
                      : (num - den / 2) / den;
}

/**
 * Cheap pre-check that a record cannot exceed the 2 MHz ceiling, done
 * without dividing.
 *
 *      |round(steps << 16 / ticks)| <= 2^31   <=>   |steps| <= 32768 * ticks
 *
 * so the whole block can be validated with a multiply per axis, and the
 * expensive division happens once per axis only after the block is known
 * to be acceptable in full. That matters because a block is atomic: not one
 * record may be enqueued until every record has passed.
 */
static bool rate_fits(int32_t steps_q16, uint32_t ticks)
{
    int64_t mag = (int64_t)steps_q16;
    if (mag < 0) {
        mag = -mag;
    }
    return mag <= (int64_t)32768 * (int64_t)ticks;
}

/** Docs/PROTOCOL.md §5.2 and §7.1. */
static void handle_motion(const cnc_header_t *hdr, bool *out_accepted)
{
    cnc_motion_block_t blk;

    *out_accepted = false;

    if (!cnc_parse_motion(hdr, &blk)) {
        reject(CNC_REJECT_BAD_PARAM, CNC_PFAULT_BAD_PARAM);
        return;
    }

    /* Duplicate retry of the block we already hold. Idempotent by design:
     * this is what makes the host's retry-after-buffer-full loop safe. */
    if (s.motion_synced && blk.block_seq == s.last_block_seq) {
        s.last_reject = CNC_REJECT_STALE;
        return;
    }

    if (s.motion_synced) {
        if (blk.block_seq < s.last_block_seq) {
            s.last_reject = CNC_REJECT_STALE;      /* straggler, harmless */
            return;
        }
        if (blk.block_seq != s.last_block_seq + 1u) {
            /* A motion block was lost. The missing block IS a missing piece
             * of the toolpath, so continuing would cut the wrong shape.
             * Stop and require the host to intervene. */
            reject(CNC_REJECT_SEQ_GAP, CNC_PFAULT_SEQ_GAP);
            controlled_stop();
            return;
        }
    }

    /* The engine only accepts segments in READY or RUNNING. Checking here,
     * before anything is enqueued, is what keeps the block atomic: finding
     * out on the first stepgen_submit_segment() would mean discovering it
     * after deciding to commit. */
    {
        stepgen_status_t es;
        stepgen_get_status(&es);
        if (es.state != STEPGEN_STATE_READY && es.state != STEPGEN_STATE_RUNNING) {
            reject(CNC_REJECT_WRONG_STATE, 0u);
            return;
        }
    }

    uint32_t ticks;
    if (!slice_to_ticks(blk.slice_us, &ticks)) {
        reject(CNC_REJECT_SLICE_NOT_EXACT, CNC_PFAULT_BAD_PARAM);
        return;
    }

    /* Atomic: room for the whole block, or nothing at all. Partial
     * acceptance would leave a retry ambiguous. */
    if (stepgen_queue_free() < (uint32_t)blk.record_count) {
        reject(CNC_REJECT_QUEUE_FULL, CNC_PFAULT_QUEUE_FULL);
        return;
    }

    /* Pass 1: every record, every axis, inside the ceiling. The engine
     * would clamp; clamping one axis of a coordinated move turns a straight
     * line into a curve, so the block is refused instead. */
    for (uint8_t r = 0; r < blk.record_count; r++) {
        cnc_motion_record_t rec;
        cnc_motion_record(&blk, r, &rec);
        for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
            if (!rate_fits(rec.steps_q16[a], ticks)) {
                reject(CNC_REJECT_RATE_TOO_HIGH, CNC_PFAULT_BAD_PARAM);
                return;
            }
        }
    }

    /* Pass 2: commit. Cannot fail - the free-slot count was checked above
     * and only the consumer can change it, in the direction that helps. The
     * check stays because a silent partial enqueue is the one outcome this
     * function exists to prevent. */
    for (uint8_t r = 0; r < blk.record_count; r++) {
        cnc_motion_record_t rec;
        cnc_motion_record(&blk, r, &rec);

        motion_segment_t seg;
        memset(&seg, 0, sizeof(seg));
        seg.duration_ticks = ticks;
        seg.seq            = blk.block_seq;
        for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
            seg.rate[a] = steps_to_rate(rec.steps_q16[a], ticks);
        }

        if (!stepgen_submit_segment(&seg)) {
            reject(CNC_REJECT_BAD_PARAM, CNC_PFAULT_BAD_PARAM);
            controlled_stop();
            return;
        }
    }

    s.last_block_seq = blk.block_seq;
    s.motion_synced  = true;
    s.motion_active  = ((blk.flags & CNC_MOTION_FLAG_END_OF_PROGRAM) == 0u);
    s.last_reject    = CNC_REJECT_NONE;
    *out_accepted    = true;
}

/* ------------------------------------------------------ control ------- */

static void handle_control(const cnc_header_t *hdr, bool *out_accepted)
{
    cnc_control_t ctl;

    *out_accepted = false;

    if (!cnc_parse_control(hdr, &ctl)) {
        reject(CNC_REJECT_BAD_PARAM, CNC_PFAULT_BAD_PARAM);
        return;
    }

    bool ok = false;

    switch (ctl.cmd) {
    case CNC_CTL_ENABLE_DRIVES:
        ok = stepgen_enable_drives();
        break;

    case CNC_CTL_DISABLE_DRIVES:
        stepgen_stop();
        s.motion_active = false;
        ok = true;
        break;

    case CNC_CTL_START:
        ok = stepgen_start();
        break;

    case CNC_CTL_STOP:
        /* Controlled stop. The queue is left intact so the host can resume
         * without re-sending what it already handed over. */
        controlled_stop();
        ok = true;
        break;

    case CNC_CTL_ABORT:
        /* Stop and discard. The motion stream is no longer continuous
         * afterwards, so the block sequence must be re-established. */
        controlled_stop();
        s.motion_synced = false;
        ok = true;
        break;

    case CNC_CTL_CLEAR_FAULT:
        /* ADR-010: recovery is always explicit, and clearing a fault never
         * clears an E-stop. Clearing the protocol's own latched faults is
         * part of the same explicit act. */
        s.proto_faults = 0u;
        s.last_reject  = CNC_REJECT_NONE;
        s.motion_synced = false;
        ok = stepgen_clear_fault();
        break;

    case CNC_CTL_CLEAR_ESTOP:
        /* The engine refuses this while the physical input is still
         * asserted; no packet can override that. */
        ok = stepgen_clear_emergency_stop();
        break;

    case CNC_CTL_FLUSH_MOTION:
        controlled_stop();
        s.motion_synced = false;
        ok = true;
        break;

    default:
        reject(CNC_REJECT_BAD_PARAM, CNC_PFAULT_BAD_PARAM);
        return;
    }

    if (ok) {
        s.last_reject = CNC_REJECT_NONE;
        *out_accepted = true;
    } else {
        /* The engine refused the transition - e.g. START while in FAULT, or
         * CLEAR_ESTOP with the input still down. Not a protocol error. */
        reject(CNC_REJECT_WRONG_STATE, 0u);
    }
}

/* ------------------------------------------------------ outputs ------- */

/**
 * OUTPUTS (Docs/PROTOCOL.md §9): relay bits and spindle duty, M9 + M10.
 *
 * Three ways to be refused, and they are deliberately distinguishable:
 *
 *   NOT_IMPLEMENTED  the subsystem is absent from this build
 *   BAD_PARAM        the payload is malformed, names an output bit this
 *                    firmware does not implement, or asks for a duty above
 *                    100 %
 *   (accepted)       the request was taken - which is NOT a promise that a
 *                    pin moved. The interlock may hold it off, and STATUS
 *                    reports what the pins are actually doing.
 *
 * Nothing is applied partially. A packet that sets the relay and an
 * out-of-range duty changes neither, because a host that got half of what
 * it asked for cannot tell which half.
 */
static void handle_outputs(const cnc_header_t *hdr, bool *out_accepted)
{
    cnc_outputs_t out;

    if (!cnc_parse_outputs(hdr, &out)) {
        reject(CNC_REJECT_BAD_PARAM, CNC_PFAULT_BAD_PARAM);
        return;
    }

    if (!io_present()) {
        /* The wire format is fixed so the plugin can be written against it,
         * but this build cannot drive anything. Saying so is the honest
         * reply; accepting silently would tell the host a relay switched. */
        reject(CNC_REJECT_NOT_IMPLEMENTED, 0u);
        return;
    }

    /* Validate everything before applying anything. */
    const bool set_spindle = (out.spindle_pmille != 0xFFFFu);
    if (set_spindle && out.spindle_pmille > SPINDLE_PMILLE_MAX) {
        reject(CNC_REJECT_BAD_PARAM, CNC_PFAULT_BAD_PARAM);
        return;
    }
    if ((out.out_mask & (uint16_t)~CNC_OUT_MASK_SUPPORTED) != 0u) {
        reject(CNC_REJECT_BAD_PARAM, CNC_PFAULT_BAD_PARAM);
        return;
    }

    if (!io_request_outputs(out.out_mask, out.out_value)) {
        reject(CNC_REJECT_BAD_PARAM, CNC_PFAULT_BAD_PARAM);
        return;
    }
    if (set_spindle && !io_spindle_set_pmille(out.spindle_pmille)) {
        reject(CNC_REJECT_BAD_PARAM, CNC_PFAULT_BAD_PARAM);
        return;
    }

    s.last_reject  = CNC_REJECT_NONE;
    *out_accepted  = true;
}

/* ------------------------------------------------------- status ------- */

void cnc_session_get_status(cnc_status_t *out)
{
    stepgen_status_t es;
    stepgen_get_status(&es);

    memset(out, 0, sizeof(*out));

    out->state       = (uint8_t)es.state;
    out->faults      = (uint8_t)es.faults;
    out->queue_free  = (uint8_t)stepgen_queue_free();
    out->queue_depth = (uint8_t)MOTION_SEGMENT_QUEUE_DEPTH;

    uint16_t flags = 0u;
    if (es.drives_enabled) { flags |= CNC_SFLAG_DRIVES_ENABLED; }
    if (net_link_is_up())  { flags |= CNC_SFLAG_LINK_UP; }
    if (s.host_known)      { flags |= CNC_SFLAG_HOST_KNOWN; }
    if (s.motion_synced)   { flags |= CNC_SFLAG_MOTION_SYNCED; }
    if (s.motion_active)   { flags |= CNC_SFLAG_MOTION_ACTIVE; }
    /* M3 exists, so the input word is real - but only once the subsystem
     * has actually been initialised. A build that links M3 without calling
     * safety_input_init() still reports absent rather than reporting
     * fifteen zeroes as if they were readings; an all-zero word that a host
     * cannot distinguish from "no data" looks exactly like a machine with
     * every switch open - Docs/PROTOCOL.md §6.2, §11.
     * Both flags track initialisation, not the build. */
    if (safety_input_present()) {
        flags     |= CNC_SFLAG_INPUTS_PRESENT;
        out->inputs = safety_inputs();
    }
    /* M9/M10 report what the pins are ACTUALLY doing, not what the host
     * asked for. The two differ whenever the interlock is holding an
     * output off, and that difference is the single most useful thing this
     * packet can tell an operator wondering why the relay did not click. */
    if (io_present()) {
        flags                |= CNC_SFLAG_OUTPUTS_PRESENT;
        out->outputs          = io_outputs_actual();
        out->spindle_pmille   = io_spindle_actual();
    }
    out->flags = flags;

    out->proto_faults       = s.proto_faults;
    out->last_seq_seen      = s.last_seq_seen;
    out->last_seq_accepted  = s.last_seq_accepted;
    out->last_block_seq     = s.motion_synced ? s.last_block_seq : 0u;
    out->last_reject_reason = s.last_reject;

    out->segments_consumed = es.segments_consumed;
    out->underruns         = es.underruns;
    out->starved_ticks     = es.starved_ticks;
    out->rx_accepted       = s.rx_accepted;
    out->rx_rejected       = s.rx_rejected;
    out->rx_dropped        = s.rx_dropped;

    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        out->pos_output[a] = es.pos_output[a];
    }
}

void cnc_session_get_info(cnc_info_t *out)
{
    static const uint8_t mac[6] = { NET_MAC_0, NET_MAC_1, NET_MAC_2,
                                    NET_MAC_3, NET_MAC_4, NET_MAC_5 };
    static const uint8_t ip[4]  = { NET_IP_ADDR0, NET_IP_ADDR1,
                                    NET_IP_ADDR2, NET_IP_ADDR3 };

    memset(out, 0, sizeof(*out));

    out->proto_version         = (uint8_t)CNC_PROTO_VERSION;
    out->axis_count            = (uint8_t)MOTION_AXIS_COUNT;
    out->queue_depth           = (uint16_t)MOTION_SEGMENT_QUEUE_DEPTH;
    out->tick_hz               = stepgen_tick_hz();
    out->max_step_rate_hz      = stepgen_max_rate_hz();
    out->max_records_per_block = (uint16_t)CNC_MAX_RECORDS;
    out->status_period_ms      = (uint16_t)CNC_STATUS_PERIOD_MS;
    out->comm_timeout_ms       = CNC_COMM_TIMEOUT_MS;
    memcpy(out->mac, mac, sizeof(mac));
    memcpy(out->ip, ip, sizeof(ip));
    out->udp_port              = (uint16_t)NET_UDP_PORT;
}

static size_t emit_status(uint8_t *reply, size_t cap, uint32_t seq,
                          uint32_t now_ms)
{
    cnc_status_t st;
    cnc_session_get_status(&st);

    if (!s.boot_ms_valid) {
        s.boot_ms       = now_ms;
        s.boot_ms_valid = true;
    }
    st.uptime_ms = now_ms - s.boot_ms;

    s.last_status_ms = now_ms;
    s.status_started = true;
    return cnc_encode_status(reply, cap, seq, &st);
}

/* --------------------------------------------------------- entry ------ */

size_t cnc_session_on_datagram(const uint8_t *buf, size_t len, uint32_t now_ms,
                               uint8_t *reply, size_t reply_cap)
{
    cnc_header_t hdr;

    if (reply == NULL || reply_cap < CNC_OVERHEAD) {
        return 0;
    }

    if (cnc_parse_header(buf, len, &hdr) != CNC_PARSE_OK) {
        /* Framing or CRC failed, so nothing in this datagram is
         * trustworthy - including the sequence number a reply would have to
         * echo. Count it and stay silent. */
        s.rx_dropped++;
        return 0;
    }

    /* Past this point the packet is structurally sound, which is proof the
     * host is alive even if the content turns out to be stale. */
    s.host_known = true;
    s.last_rx_ms = now_ms;
    if (!s.boot_ms_valid) {
        s.boot_ms       = now_ms;
        s.boot_ms_valid = true;
    }

    /* Duplicate or reordered straggler: do not act, but do answer. The
     * usual reason a host repeats itself is that it never saw the previous
     * status, and replying re-synchronises it. */
    if (s.seq_started && hdr.seq <= s.last_seq_seen) {
        s.last_reject = CNC_REJECT_STALE;
        return emit_status(reply, reply_cap, hdr.seq, now_ms);
    }
    s.last_seq_seen = hdr.seq;
    s.seq_started   = true;

    bool accepted = false;

    switch (hdr.opcode) {
    case CNC_OP_HELLO: {
        cnc_info_t info;
        cnc_session_get_info(&info);
        s.rx_accepted++;
        s.last_seq_accepted = hdr.seq;
        s.last_reject       = CNC_REJECT_NONE;
        return cnc_encode_info(reply, reply_cap, hdr.seq, &info);
    }

    case CNC_OP_STATUS_REQ:
        accepted = true;
        break;

    case CNC_OP_MOTION:
        handle_motion(&hdr, &accepted);
        break;

    case CNC_OP_CONTROL:
        handle_control(&hdr, &accepted);
        break;

    case CNC_OP_OUTPUTS:
        handle_outputs(&hdr, &accepted);
        break;

    default:
        /* Device->host opcodes arriving from the host. The codec accepted
         * them as known; this layer will not act on them. */
        reject(CNC_REJECT_BAD_PARAM, CNC_PFAULT_BAD_PARAM);
        break;
    }

    if (accepted) {
        s.rx_accepted++;
        s.last_seq_accepted = hdr.seq;
    }

    return emit_status(reply, reply_cap, hdr.seq, now_ms);
}

size_t cnc_session_tick(uint32_t now_ms, uint8_t *reply, size_t reply_cap)
{
    if (!s.host_known) {
        /* Nothing is transmitted until a valid packet has told us where to
         * send it. */
        return 0;
    }

    /* Comm-timeout supervision, but only while motion is active: a host
     * that goes quiet between programs is not a fault, because nothing is
     * moving. Docs/PROTOCOL.md §7.4. */
    bool urgent = false;

    if (s.motion_active &&
        (now_ms - s.last_rx_ms) >= CNC_COMM_TIMEOUT_MS &&
        (s.proto_faults & CNC_PFAULT_COMM_TIMEOUT) == 0u) {
        s.proto_faults |= CNC_PFAULT_COMM_TIMEOUT;
        s.last_reject   = CNC_REJECT_NONE;
        controlled_stop();
        /* Report the transition now rather than at the next 20 ms slot. A
         * fault is exactly the moment the host most needs to hear from us,
         * and by definition it is not hearing anything else right now. */
        urgent = true;
    }

    if (!urgent && s.status_started &&
        (now_ms - s.last_status_ms) < CNC_STATUS_PERIOD_MS) {
        return 0;
    }

    /* Unsolicited status carries the last sequence the host used, so a
     * host that is only listening can still tell which of its packets this
     * reflects. */
    return emit_status(reply, reply_cap, s.last_seq_seen, now_ms);
}
