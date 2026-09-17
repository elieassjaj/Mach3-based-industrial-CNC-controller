/**
 * @file    net_udp.h
 * @brief   lwIP socket binding for the C5P1 protocol (M13).
 *
 * The thinnest possible layer: it owns one UDP PCB, copies datagrams out of
 * lwIP's pbufs, hands them to cnc_session, and transmits whatever the
 * session hands back. Every protocol decision lives in cnc_session.c, which
 * is portable and host-tested; nothing that could be tested on a host is
 * implemented here.
 */
#ifndef NET_UDP_H
#define NET_UDP_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Create and bind the PCB on NET_UDP_PORT, and reset the session.
 *
 * Call after MX_LWIP_Init(). Safe to call when the link is down - binding
 * does not require a live link.
 *
 * @return false if lwIP could not allocate or bind the PCB.
 */
bool net_udp_init(void);

/**
 * Periodic work: the comm-timeout supervision and the unsolicited status
 * stream. Call from the superloop alongside MX_LWIP_Process().
 */
void net_udp_poll(void);

/** True once the PCB is bound. */
bool net_udp_ready(void);

/** Datagrams transmitted, and transmit failures, for diagnostics. */
uint32_t net_udp_tx_count(void);
uint32_t net_udp_tx_errors(void);

#endif /* NET_UDP_H */
