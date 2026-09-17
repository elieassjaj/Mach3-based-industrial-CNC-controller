/**
 * @file    net_link.h
 * @brief   Portable Ethernet link observer (Phase 2 / M12).
 *
 * The CubeMX-generated ethernet_link_check_state() already owns the act of
 * reconfiguring the MAC when the link changes. This module deliberately does
 * NOT duplicate that. It is the observer: it turns "what the PHY reports
 * right now" into a small, stable snapshot that the rest of the firmware -
 * diagnostics today, the Phase 3 protocol layer and ADR-010's fault model
 * later - can read without knowing anything about lwIP, the HAL or the
 * LAN8742 driver.
 *
 * That separation is the point. Docs/PHASE1-STATUS.md Section 9 rule 4 keeps
 * the Ethernet path from reaching into the motion engine; this is the same
 * rule applied in the other direction, so Phase 3 never has to include
 * lwip.h to answer "is the cable in?".
 *
 * Portable C: no STM32, HAL, CMSIS or lwIP dependency, so the state machine
 * is exercised by the host test suite rather than only on a bench.
 *
 * Not thread-safe and not ISR-safe by design: every call is made from the
 * MX_LWIP_Process() pump in the main loop (NO_SYS = 1, bare metal).
 */
#ifndef NET_LINK_H
#define NET_LINK_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    NET_SPEED_NONE = 0,   /**< no link                                     */
    NET_SPEED_10M,
    NET_SPEED_100M
} net_speed_t;

/** What the platform layer saw when it last asked the PHY/MAC. */
typedef struct {
    bool        link_up;
    net_speed_t speed;
    bool        full_duplex;
} net_phy_report_t;

/** Stable snapshot for the rest of the firmware. */
typedef struct {
    bool        link_up;
    net_speed_t speed;
    bool        full_duplex;

    /* Counted since net_link_init(). A link that flaps repeatedly is a
     * cabling or auto-negotiation problem, and counting is the cheapest way
     * to tell that apart from a link that simply never came up.            */
    uint32_t    up_count;
    uint32_t    down_count;

    /* Timestamp of the last up/down transition, in the caller's own
     * millisecond timebase (HAL_GetTick() on target). Zero if the link has
     * never changed state since init.                                      */
    uint32_t    last_change_ms;

    /* Set once the PHY driver has initialised successfully, together with
     * the address the driver's own scan settled on. The address is not
     * hard-coded anywhere: the LAN8720A strap allows 0 or 1 and this
     * repository's sources disagree about which the module uses
     * (Docs/ETHERNET.md Section 2.2), so it is discovered, then recorded
     * here for HV-20 and for diagnostics.                                  */
    bool        phy_init_ok;
    uint8_t     phy_addr;
} net_link_status_t;

/** Clears all state. Call once, before the first report. */
void net_link_init(void);

/** Record the outcome of PHY driver initialisation and the scanned address. */
void net_link_set_phy_init(bool ok, uint8_t phy_addr);

/**
 * Feed one observation.
 *
 * @param report  what the PHY/MAC currently reports
 * @param now_ms  caller's millisecond timebase
 * @return true if this observation changed the up/down state, so a caller
 *         that wants to log or react only on edges can do so without
 *         keeping its own copy.
 *
 * A speed or duplex change while the link stays up updates the snapshot but
 * is not reported as a transition - only up/down is an edge here.
 */
bool net_link_on_report(const net_phy_report_t *report, uint32_t now_ms);

/** Copy out the current snapshot. */
void net_link_get(net_link_status_t *out);

/** Convenience for callers that only need the one bit. */
bool net_link_is_up(void);

#endif /* NET_LINK_H */
