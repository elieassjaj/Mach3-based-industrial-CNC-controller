/**
 * @file    cnc_protocol.c
 * @brief   C5P1 wire codec (see cnc_protocol.h, Docs/PROTOCOL.md).
 *
 * Byte-wise little-endian access throughout: no packed structs, no casting
 * a buffer to a struct pointer. That costs a few instructions per field and
 * buys freedom from alignment traps and from the compiler's struct padding
 * ever silently changing the wire format.
 */
#include "cnc_protocol.h"

#include <string.h>

/* ------------------------------------------------------------- CRC ----- */

/* IEEE 802.3, reflected polynomial 0xEDB88320. Nibble-wise: 16 entries
 * (64 bytes of flash) instead of 256, at two lookups per byte. At the
 * protocol's ~35 KB/s that is well under 0.1% of the CPU either way, so the
 * smaller table wins. */
static const uint32_t k_crc_nibble[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
    0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
    0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu
};

uint32_t cnc_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        crc = (crc >> 4) ^ k_crc_nibble[crc & 0x0Fu];
        crc = (crc >> 4) ^ k_crc_nibble[crc & 0x0Fu];
    }
    return crc ^ 0xFFFFFFFFu;
}

/* -------------------------------------------------- little-endian io --- */

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0]        | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t rd32s(const uint8_t *p)
{
    return (int32_t)rd32(p);
}

static int64_t rd64s(const uint8_t *p)
{
    return (int64_t)((uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32));
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void wr64s(uint8_t *p, int64_t v)
{
    const uint64_t u = (uint64_t)v;
    wr32(p,     (uint32_t)(u & 0xFFFFFFFFu));
    wr32(p + 4, (uint32_t)((u >> 32) & 0xFFFFFFFFu));
}

/* --------------------------------------------------------- opcodes ----- */

static bool opcode_known(uint8_t op)
{
    switch (op) {
    case CNC_OP_HELLO:
    case CNC_OP_MOTION:
    case CNC_OP_CONTROL:
    case CNC_OP_OUTPUTS:
    case CNC_OP_STATUS_REQ:
    case CNC_OP_STATUS:
    case CNC_OP_INFO:
        return true;
    default:
        return false;
    }
}

/* ----------------------------------------------------------- parse ----- */

cnc_parse_result_t cnc_parse_header(const uint8_t *buf, size_t len,
                                    cnc_header_t *out)
{
    if (buf == NULL || out == NULL || len < CNC_OVERHEAD) {
        return CNC_PARSE_TOO_SHORT;
    }
    if (rd32(buf) != CNC_PROTO_MAGIC) {
        return CNC_PARSE_BAD_MAGIC;
    }
    if (buf[4] != CNC_PROTO_VERSION) {
        /* Never guess at another version's layout. */
        return CNC_PARSE_BAD_VERSION;
    }

    const uint16_t plen = rd16(&buf[6]);
    if (plen > CNC_MAX_PAYLOAD) {
        return CNC_PARSE_BAD_LENGTH;
    }
    /* Exact, not "at least": a longer datagram is a spliced or padded one,
     * and accepting its prefix would mean acting on a packet nobody sent. */
    if (len != (size_t)CNC_OVERHEAD + plen) {
        return CNC_PARSE_BAD_LENGTH;
    }
    if (!opcode_known(buf[5])) {
        return CNC_PARSE_BAD_OPCODE;
    }

    const size_t   crc_off = CNC_HDR_SIZE + (size_t)plen;
    const uint32_t want    = rd32(&buf[crc_off]);
    if (cnc_crc32(buf, crc_off) != want) {
        return CNC_PARSE_BAD_CRC;
    }

    out->version     = buf[4];
    out->opcode      = buf[5];
    out->payload_len = plen;
    out->seq         = rd32(&buf[8]);
    out->payload     = (plen > 0u) ? &buf[CNC_HDR_SIZE] : NULL;
    return CNC_PARSE_OK;
}

bool cnc_parse_motion(const cnc_header_t *hdr, cnc_motion_block_t *out)
{
    if (hdr == NULL || out == NULL || hdr->opcode != CNC_OP_MOTION) {
        return false;
    }
    if (hdr->payload_len < CNC_MOTION_HDR_SIZE || hdr->payload == NULL) {
        return false;
    }

    const uint8_t n = hdr->payload[6];
    if (n == 0u || n > CNC_MAX_RECORDS) {
        return false;
    }
    /* The declared record count has to account for every payload byte. */
    if (hdr->payload_len !=
        (uint16_t)(CNC_MOTION_HDR_SIZE + (uint16_t)n * CNC_RECORD_SIZE)) {
        return false;
    }

    out->block_seq    = rd32(&hdr->payload[0]);
    out->slice_us     = rd16(&hdr->payload[4]);
    out->record_count = n;
    out->flags        = hdr->payload[7];
    out->records      = &hdr->payload[CNC_MOTION_HDR_SIZE];

    if (out->slice_us == 0u) {
        return false;
    }
    /* Reserved bits must be zero: a host setting one means it believes in a
     * feature this version does not have. */
    if ((out->flags & (uint8_t)~CNC_MOTION_FLAG_END_OF_PROGRAM) != 0u) {
        return false;
    }
    return true;
}

void cnc_motion_record(const cnc_motion_block_t *blk, uint8_t index,
                       cnc_motion_record_t *out)
{
    const uint8_t *p = &blk->records[(size_t)index * CNC_RECORD_SIZE];

    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        out->steps_q16[a] = rd32s(&p[a * 4u]);
    }
}

bool cnc_parse_control(const cnc_header_t *hdr, cnc_control_t *out)
{
    if (hdr == NULL || out == NULL || hdr->opcode != CNC_OP_CONTROL) {
        return false;
    }
    if (hdr->payload_len != CNC_CONTROL_PAYLOAD_SIZE || hdr->payload == NULL) {
        return false;
    }
    out->cmd = hdr->payload[0];
    out->arg = hdr->payload[1];
    return true;
}

bool cnc_parse_outputs(const cnc_header_t *hdr, cnc_outputs_t *out)
{
    if (hdr == NULL || out == NULL || hdr->opcode != CNC_OP_OUTPUTS) {
        return false;
    }
    if (hdr->payload_len != CNC_OUTPUTS_PAYLOAD_SIZE || hdr->payload == NULL) {
        return false;
    }
    out->out_mask       = rd16(&hdr->payload[0]);
    out->out_value      = rd16(&hdr->payload[2]);
    out->spindle_pmille = rd16(&hdr->payload[4]);
    return true;
}

/* ---------------------------------------------------------- encode ----- */

/** Write the header, returning the payload offset. */
static size_t begin(uint8_t *buf, uint32_t seq, uint8_t opcode, uint16_t plen)
{
    wr32(buf, CNC_PROTO_MAGIC);
    buf[4] = (uint8_t)CNC_PROTO_VERSION;
    buf[5] = opcode;
    wr16(&buf[6], plen);
    wr32(&buf[8], seq);
    return CNC_HDR_SIZE;
}

/** Append the CRC and return the total packet size. */
static size_t finish(uint8_t *buf, uint16_t plen)
{
    const size_t crc_off = CNC_HDR_SIZE + (size_t)plen;
    wr32(&buf[crc_off], cnc_crc32(buf, crc_off));
    return crc_off + CNC_CRC_SIZE;
}

size_t cnc_encode_status(uint8_t *buf, size_t cap, uint32_t seq,
                         const cnc_status_t *st)
{
    if (buf == NULL || st == NULL ||
        cap < (size_t)CNC_OVERHEAD + CNC_STATUS_PAYLOAD_SIZE) {
        return 0;
    }

    uint8_t *p = &buf[begin(buf, seq, CNC_OP_STATUS, CNC_STATUS_PAYLOAD_SIZE)];

    p[0] = st->state;
    p[1] = st->faults;
    wr16(&p[2],  st->flags);
    p[4] = st->queue_free;
    p[5] = st->queue_depth;
    wr16(&p[6],  st->proto_faults);
    wr32(&p[8],  st->last_seq_seen);
    wr32(&p[12], st->last_seq_accepted);
    wr32(&p[16], st->last_block_seq);
    wr16(&p[20], st->last_reject_reason);
    wr16(&p[22], st->inputs);
    wr16(&p[24], st->outputs);
    wr16(&p[26], st->spindle_pmille);
    wr32(&p[28], st->segments_consumed);
    wr32(&p[32], st->underruns);
    wr32(&p[36], st->starved_ticks);
    wr32(&p[40], st->rx_accepted);
    wr32(&p[44], st->rx_rejected);
    wr32(&p[48], st->rx_dropped);
    wr32(&p[52], st->uptime_ms);
    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        wr64s(&p[56 + a * 8u], st->pos_output[a]);
    }

    return finish(buf, CNC_STATUS_PAYLOAD_SIZE);
}

bool cnc_parse_status(const cnc_header_t *hdr, cnc_status_t *out)
{
    if (hdr == NULL || out == NULL || hdr->opcode != CNC_OP_STATUS) {
        return false;
    }
    if (hdr->payload_len != CNC_STATUS_PAYLOAD_SIZE || hdr->payload == NULL) {
        return false;
    }

    const uint8_t *p = hdr->payload;

    out->state              = p[0];
    out->faults             = p[1];
    out->flags              = rd16(&p[2]);
    out->queue_free         = p[4];
    out->queue_depth        = p[5];
    out->proto_faults       = rd16(&p[6]);
    out->last_seq_seen      = rd32(&p[8]);
    out->last_seq_accepted  = rd32(&p[12]);
    out->last_block_seq     = rd32(&p[16]);
    out->last_reject_reason = rd16(&p[20]);
    out->inputs             = rd16(&p[22]);
    out->outputs            = rd16(&p[24]);
    out->spindle_pmille     = rd16(&p[26]);
    out->segments_consumed  = rd32(&p[28]);
    out->underruns          = rd32(&p[32]);
    out->starved_ticks      = rd32(&p[36]);
    out->rx_accepted        = rd32(&p[40]);
    out->rx_rejected        = rd32(&p[44]);
    out->rx_dropped         = rd32(&p[48]);
    out->uptime_ms          = rd32(&p[52]);
    for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
        out->pos_output[a] = rd64s(&p[56 + a * 8u]);
    }
    return true;
}

size_t cnc_encode_info(uint8_t *buf, size_t cap, uint32_t seq,
                       const cnc_info_t *info)
{
    if (buf == NULL || info == NULL ||
        cap < (size_t)CNC_OVERHEAD + CNC_INFO_PAYLOAD_SIZE) {
        return 0;
    }

    uint8_t *p = &buf[begin(buf, seq, CNC_OP_INFO, CNC_INFO_PAYLOAD_SIZE)];

    p[0] = info->proto_version;
    p[1] = info->axis_count;
    wr16(&p[2],  info->queue_depth);
    wr32(&p[4],  info->tick_hz);
    wr32(&p[8],  info->max_step_rate_hz);
    wr16(&p[12], info->max_records_per_block);
    wr16(&p[14], info->status_period_ms);
    wr32(&p[16], info->comm_timeout_ms);
    memcpy(&p[20], info->mac, 6);
    memcpy(&p[26], info->ip, 4);
    wr16(&p[30], info->udp_port);

    return finish(buf, CNC_INFO_PAYLOAD_SIZE);
}

bool cnc_parse_info(const cnc_header_t *hdr, cnc_info_t *out)
{
    if (hdr == NULL || out == NULL || hdr->opcode != CNC_OP_INFO) {
        return false;
    }
    if (hdr->payload_len != CNC_INFO_PAYLOAD_SIZE || hdr->payload == NULL) {
        return false;
    }

    const uint8_t *p = hdr->payload;

    out->proto_version         = p[0];
    out->axis_count            = p[1];
    out->queue_depth           = rd16(&p[2]);
    out->tick_hz               = rd32(&p[4]);
    out->max_step_rate_hz      = rd32(&p[8]);
    out->max_records_per_block = rd16(&p[12]);
    out->status_period_ms      = rd16(&p[14]);
    out->comm_timeout_ms       = rd32(&p[16]);
    memcpy(out->mac, &p[20], 6);
    memcpy(out->ip,  &p[26], 4);
    out->udp_port              = rd16(&p[30]);
    return true;
}

size_t cnc_encode_motion(uint8_t *buf, size_t cap, uint32_t seq,
                         uint32_t block_seq, uint16_t slice_us,
                         const cnc_motion_record_t *records,
                         uint8_t record_count, uint8_t flags)
{
    if (buf == NULL || records == NULL ||
        record_count == 0u || record_count > CNC_MAX_RECORDS) {
        return 0;
    }

    const uint16_t plen = (uint16_t)(CNC_MOTION_HDR_SIZE +
                                     (uint16_t)record_count * CNC_RECORD_SIZE);
    if (cap < (size_t)CNC_OVERHEAD + plen) {
        return 0;
    }

    uint8_t *p = &buf[begin(buf, seq, CNC_OP_MOTION, plen)];

    wr32(&p[0], block_seq);
    wr16(&p[4], slice_us);
    p[6] = record_count;
    p[7] = flags;

    for (uint8_t r = 0; r < record_count; r++) {
        uint8_t *rp = &p[CNC_MOTION_HDR_SIZE + (size_t)r * CNC_RECORD_SIZE];
        for (uint32_t a = 0; a < MOTION_AXIS_COUNT; a++) {
            wr32(&rp[a * 4u], (uint32_t)records[r].steps_q16[a]);
        }
    }

    return finish(buf, plen);
}

size_t cnc_encode_control(uint8_t *buf, size_t cap, uint32_t seq,
                          uint8_t cmd, uint8_t arg)
{
    if (buf == NULL || cap < (size_t)CNC_OVERHEAD + CNC_CONTROL_PAYLOAD_SIZE) {
        return 0;
    }

    uint8_t *p = &buf[begin(buf, seq, CNC_OP_CONTROL, CNC_CONTROL_PAYLOAD_SIZE)];

    p[0] = cmd;
    p[1] = arg;
    p[2] = 0;
    p[3] = 0;

    return finish(buf, CNC_CONTROL_PAYLOAD_SIZE);
}

size_t cnc_encode_simple(uint8_t *buf, size_t cap, uint32_t seq,
                         uint8_t opcode)
{
    if (buf == NULL || cap < CNC_OVERHEAD) {
        return 0;
    }
    (void)begin(buf, seq, opcode, 0u);
    return finish(buf, 0u);
}
