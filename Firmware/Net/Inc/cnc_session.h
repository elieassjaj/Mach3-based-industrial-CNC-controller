/**
 * @file    cnc_session.h
 * @brief   C5P1 session layer (M14): sequence rules, dispatch, supervision.
 *
 * Sits between the UDP socket and the Phase 1 motion engine:
 *
 *      UDP RX  ->  cnc_parse_* (codec)  ->  cnc_session  ->  stepgen.h
 *
 * The session owns everything stateful about the protocol - the two
 * sequence spaces, the latched protocol faults, the comm-timeout clock, the
 * counters - and it is the only thing here that talks to the motion engine.
 * It reaches the engine solely through `stepgen.h`, the facade Phase 1
 * declared for exactly this purpose, so nothing in the network path touches
 * a timer, a DMA stream or a GPIO register.
 *
 * Portable C: no STM32, HAL, CMSIS or lwIP dependency. The host test suite
 * drives this against the *real* motion engine on its simulation port, so
 * "queue full" and "abort" are exercised against the real queue rather than
 * against a mock that could agree with a wrong assumption.
 *
 * Single-threaded by contract. Every entry point is called from the
 * MX_LWIP_Process() superloop, never from an interrupt - in lwIP's NO_SYS
 * configuration the receive path runs in ethernetif_input(), which the main
 * loop calls. The one thing that does run in interrupt context, the STEP
 * ring boundary handler, touches the motion queue from the far side and is
 * wait-free against this one by design (motion_segment_queue.h).
 */
#ifndef CNC_SESSION_H
#define CNC_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cnc_protocol.h"

/** Reset all protocol state. Does not touch the motion engine. */
void cnc_session_init(void);

/**
 * Process one received datagram.
 *
 * Performs every check in Docs/PROTOCOL.md §4 and §5 before anything can
 * reach the motion queue, applies the sequence rules, dispatches the
 * command, and builds the reply.
 *
 * @param buf        the datagram
 * @param len        its length
 * @param now_ms     caller's millisecond timebase
 * @param reply      buffer for the outgoing packet
 * @param reply_cap  its capacity; CNC_MAX_PACKET is always enough
 * @return bytes to transmit, or 0 for "say nothing" - which is the correct
 *         answer to a packet that failed its CRC, since the sequence number
 *         in it cannot be trusted either.
 */
size_t cnc_session_on_datagram(const uint8_t *buf, size_t len, uint32_t now_ms,
                               uint8_t *reply, size_t reply_cap);

/**
 * Periodic work: comm-timeout supervision and the unsolicited status
 * stream. Call from the superloop as often as convenient.
 *
 * @return bytes to transmit, or 0 if nothing is due.
 */
size_t cnc_session_tick(uint32_t now_ms, uint8_t *reply, size_t reply_cap);

/** Current status, as it would be encoded. For diagnostics and tests. */
void cnc_session_get_status(cnc_status_t *out);

/** Identity block, as answered to HELLO. */
void cnc_session_get_info(cnc_info_t *out);

/** True once a valid datagram has arrived and a reply target is known. */
bool cnc_session_host_known(void);

#endif /* CNC_SESSION_H */
