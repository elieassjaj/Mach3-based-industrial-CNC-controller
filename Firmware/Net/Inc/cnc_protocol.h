/**
 * @file    cnc_protocol.h
 * @brief   CNC5AX-ETH Motion Protocol v1 ("C5P1") - wire format and codec.
 *
 * Docs/PROTOCOL.md is authoritative. This header implements exactly that
 * and nothing else; if the two disagree, the document is right.
 *
 * Portable C: no STM32, HAL, CMSIS or lwIP dependency, so every parsing and
 * validation rule is exercised by the host test suite rather than only on a
 * bench. The codec is also pure - it decodes and encodes buffers and has no
 * state and no side effects. Session state (sequence tracking, faults,
 * dispatch to the motion engine) lives in cnc_session.h.
 *
 * All multi-byte fields are little-endian (ADR-014): both ends are
 * little-endian, so neither side byte-swaps. The encode/decode helpers here
 * do byte-wise access anyway, so the codec itself is endian-agnostic and a
 * big-endian host could still use it.
 */
#ifndef CNC_PROTOCOL_H
#define CNC_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cnc_motion_config.h"   /* MOTION_AXIS_COUNT */

/* ----------------------------------------------------------- framing --- */

/** ASCII "C5P1", read as a little-endian uint32. */
#define CNC_PROTO_MAGIC          0x31503543u
#define CNC_PROTO_VERSION        1u

#define CNC_HDR_SIZE             12u
#define CNC_CRC_SIZE             4u
#define CNC_OVERHEAD             (CNC_HDR_SIZE + CNC_CRC_SIZE)

/** Docs/PROTOCOL.md §8: 32 records is the ncPod-derived block size. */
#define CNC_MAX_RECORDS          32u
#define CNC_RECORD_SIZE          (4u * MOTION_AXIS_COUNT)
#define CNC_MOTION_HDR_SIZE      8u

#define CNC_MAX_PAYLOAD          (CNC_MOTION_HDR_SIZE + \
                                  CNC_MAX_RECORDS * CNC_RECORD_SIZE)
#define CNC_MAX_PACKET           (CNC_OVERHEAD + CNC_MAX_PAYLOAD)

/* Docs/PROTOCOL.md §8. Published to the host in INFO so it never has to
 * assume either number. */
#define CNC_STATUS_PERIOD_MS     20u    /**< 50 Hz, the ncPod cadence  */
#define CNC_COMM_TIMEOUT_MS      200u   /**< ten host cycles           */

#define CNC_STATUS_PAYLOAD_SIZE  96u
#define CNC_INFO_PAYLOAD_SIZE    32u
#define CNC_CONTROL_PAYLOAD_SIZE 4u
#define CNC_OUTPUTS_PAYLOAD_SIZE 8u

/* ----------------------------------------------------------- opcodes --- */

typedef enum {
    CNC_OP_HELLO      = 0x01,
    CNC_OP_MOTION     = 0x02,
    CNC_OP_CONTROL    = 0x03,
    CNC_OP_OUTPUTS    = 0x04,
    CNC_OP_STATUS_REQ = 0x05,

    CNC_OP_STATUS     = 0x81,
    CNC_OP_INFO       = 0x82
} cnc_opcode_t;

/** CONTROL commands. Docs/PROTOCOL.md §4.3. */
typedef enum {
    CNC_CTL_ENABLE_DRIVES  = 1,
    CNC_CTL_DISABLE_DRIVES = 2,
    CNC_CTL_START          = 3,
    CNC_CTL_STOP           = 4,
    CNC_CTL_ABORT          = 5,
    CNC_CTL_CLEAR_FAULT    = 6,
    CNC_CTL_CLEAR_ESTOP    = 7,
    CNC_CTL_FLUSH_MOTION   = 8
} cnc_control_cmd_t;

/** MOTION flags. */
#define CNC_MOTION_FLAG_END_OF_PROGRAM  (1u << 0)

/* ------------------------------------------------------- reject codes -- */
/* Docs/PROTOCOL.md §6.4. Reported in STATUS.last_reject_reason. */
typedef enum {
    CNC_REJECT_NONE            = 0,
    CNC_REJECT_QUEUE_FULL      = 1,
    CNC_REJECT_SEQ_GAP         = 2,
    CNC_REJECT_STALE           = 3,
    CNC_REJECT_BAD_PARAM       = 4,
    CNC_REJECT_NOT_IMPLEMENTED = 5,
    CNC_REJECT_WRONG_STATE     = 6,
    CNC_REJECT_RATE_TOO_HIGH   = 7,
    CNC_REJECT_SLICE_NOT_EXACT = 8
} cnc_reject_t;

/** Why a datagram was thrown away before it could be trusted at all. */
typedef enum {
    CNC_PARSE_OK = 0,
    CNC_PARSE_TOO_SHORT,
    CNC_PARSE_BAD_MAGIC,
    CNC_PARSE_BAD_VERSION,
    CNC_PARSE_BAD_LENGTH,
    CNC_PARSE_BAD_OPCODE,
    CNC_PARSE_BAD_CRC
} cnc_parse_result_t;

/* --------------------------------------------------------- latched ----- */
/* Docs/PROTOCOL.md §6.3. Cleared only by CONTROL:CLEAR_FAULT. */
#define CNC_PFAULT_SEQ_GAP       (1u << 0)
#define CNC_PFAULT_COMM_TIMEOUT  (1u << 1)
#define CNC_PFAULT_QUEUE_FULL    (1u << 2)
#define CNC_PFAULT_BAD_PARAM     (1u << 3)

/* ----------------------------------------------------- status flags ---- */
/* Docs/PROTOCOL.md §6.2. */
#define CNC_SFLAG_DRIVES_ENABLED (1u << 0)
#define CNC_SFLAG_LINK_UP        (1u << 1)
#define CNC_SFLAG_HOST_KNOWN     (1u << 2)
#define CNC_SFLAG_MOTION_SYNCED  (1u << 3)
#define CNC_SFLAG_MOTION_ACTIVE  (1u << 4)
#define CNC_SFLAG_INPUTS_PRESENT (1u << 5)   /**< M3 has initialised */
#define CNC_SFLAG_OUTPUTS_PRESENT (1u << 6)  /**< 0 until M9/M10     */

/* ------------------------------------------------- decoded structures -- */

typedef struct {
    uint8_t  version;
    uint8_t  opcode;
    uint16_t payload_len;
    uint32_t seq;
    const uint8_t *payload;      /**< into the caller's buffer, not copied */
} cnc_header_t;

/** One motion slice: signed 1/65536-step per axis. Docs/PROTOCOL.md §5.3. */
typedef struct {
    int32_t steps_q16[MOTION_AXIS_COUNT];
} cnc_motion_record_t;

typedef struct {
    uint32_t block_seq;
    uint16_t slice_us;
    uint8_t  record_count;
    uint8_t  flags;
    const uint8_t *records;      /**< record_count * CNC_RECORD_SIZE bytes */
} cnc_motion_block_t;

typedef struct {
    uint8_t  cmd;
    uint8_t  arg;
} cnc_control_t;

typedef struct {
    uint16_t out_mask;
    uint16_t out_value;
    uint16_t spindle_pmille;     /**< 0xFFFF = leave unchanged */
} cnc_outputs_t;

/** Everything STATUS carries. Docs/PROTOCOL.md §6. */
typedef struct {
    uint8_t  state;
    uint8_t  faults;
    uint16_t flags;
    uint8_t  queue_free;
    uint8_t  queue_depth;
    uint16_t proto_faults;
    uint32_t last_seq_seen;
    uint32_t last_seq_accepted;
    uint32_t last_block_seq;
    uint16_t last_reject_reason;
    uint16_t inputs;
    uint16_t outputs;
    uint16_t spindle_pmille;
    uint32_t segments_consumed;
    uint32_t underruns;
    uint32_t starved_ticks;
    uint32_t rx_accepted;
    uint32_t rx_rejected;
    uint32_t rx_dropped;
    uint32_t uptime_ms;
    int64_t  pos_output[MOTION_AXIS_COUNT];
} cnc_status_t;

/** Everything INFO carries. Docs/PROTOCOL.md §4.5. */
typedef struct {
    uint8_t  proto_version;
    uint8_t  axis_count;
    uint16_t queue_depth;
    uint32_t tick_hz;
    uint32_t max_step_rate_hz;
    uint16_t max_records_per_block;
    uint16_t status_period_ms;
    uint32_t comm_timeout_ms;
    uint8_t  mac[6];
    uint8_t  ip[4];
    uint16_t udp_port;
} cnc_info_t;

/* ------------------------------------------------------------- CRC ----- */

/** IEEE 802.3 CRC-32 - identical to zlib.crc32 / Python binascii.crc32. */
uint32_t cnc_crc32(const uint8_t *data, size_t len);

/* ----------------------------------------------------------- codec ----- */

/**
 * Validate framing and CRC, and hand back a view of the payload.
 *
 * Checks in Docs/PROTOCOL.md §4 order: length, magic, version, declared
 * length against actual, known opcode, CRC. Nothing downstream may look at
 * a packet this rejects.
 *
 * @p out->payload points into @p buf; nothing is copied.
 */
cnc_parse_result_t cnc_parse_header(const uint8_t *buf, size_t len,
                                    cnc_header_t *out);

/** Decode a MOTION payload. False if it is not self-consistent. */
bool cnc_parse_motion(const cnc_header_t *hdr, cnc_motion_block_t *out);

/** Read record @p index out of an already-validated block. */
void cnc_motion_record(const cnc_motion_block_t *blk, uint8_t index,
                       cnc_motion_record_t *out);

bool cnc_parse_control(const cnc_header_t *hdr, cnc_control_t *out);
bool cnc_parse_outputs(const cnc_header_t *hdr, cnc_outputs_t *out);

/**
 * Build a complete STATUS packet into @p buf.
 * @return bytes written, or 0 if the buffer is too small.
 */
size_t cnc_encode_status(uint8_t *buf, size_t cap, uint32_t seq,
                         const cnc_status_t *st);

/** Build a complete INFO packet. Same contract. */
size_t cnc_encode_info(uint8_t *buf, size_t cap, uint32_t seq,
                       const cnc_info_t *info);

/**
 * Build a MOTION packet. Host-side helper, and what the test suite uses to
 * generate the packets it then feeds back through the parser - so the two
 * directions are checked against each other rather than against a
 * hand-written byte string that could drift from the spec.
 */
size_t cnc_encode_motion(uint8_t *buf, size_t cap, uint32_t seq,
                         uint32_t block_seq, uint16_t slice_us,
                         const cnc_motion_record_t *records,
                         uint8_t record_count, uint8_t flags);

size_t cnc_encode_control(uint8_t *buf, size_t cap, uint32_t seq,
                          uint8_t cmd, uint8_t arg);

/** HELLO and STATUS_REQ - both are a bare header. */
size_t cnc_encode_simple(uint8_t *buf, size_t cap, uint32_t seq,
                         uint8_t opcode);

/** Decode STATUS / INFO. Host-side and test-suite use. */
bool cnc_parse_status(const cnc_header_t *hdr, cnc_status_t *out);
bool cnc_parse_info(const cnc_header_t *hdr, cnc_info_t *out);

#endif /* CNC_PROTOCOL_H */
