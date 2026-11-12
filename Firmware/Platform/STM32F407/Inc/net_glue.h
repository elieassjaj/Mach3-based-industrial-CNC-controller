/**
 * @file    net_glue.h
 * @brief   The three calls the CubeMX-generated network code makes into M12.
 *
 * Phase 1 established the rule the hard way: anything written outside a
 * USER CODE block is undone by the next regeneration. It already cost this
 * repository its static IP configuration once (commit 438f59f, reverted by
 * the regeneration in b409ba5, with Docs/ETHERNET.md Section 15 still
 * describing the lost state as [FW-CONFIRMED]).
 *
 * So all of M12's logic lives in project-owned files, and the generated
 * files carry only these three one-line calls, each inside a USER CODE
 * block:
 *
 *   ethernetif.c  USER CODE BEGIN MACADDRESS   -> net_glue_phy_reset()
 *   lwip.c        USER CODE BEGIN 3            -> net_glue_apply_static_ip()
 *   lwip.c        USER CODE BEGIN 4_4          -> net_glue_poll_link()
 *
 * If a future regeneration drops one of them, HV-20, HV-24 and HV-25
 * respectively fail - which is the point.
 */
#ifndef NET_GLUE_H
#define NET_GLUE_H

#include <stdbool.h>
#include <stdint.h>

/**
 * ADR-013 PHY reset, and the first initialisation of the link observer.
 *
 * Called at the top of low_level_init(), before HAL_ETH_Init(), so the PHY
 * is out of reset and settled before the MAC's DMA soft reset runs and
 * before the first MDIO transaction.
 *
 * @return false if nRST did not read back released.
 */
bool net_glue_phy_reset(void);

/**
 * Apply the static IPv4 configuration to the default interface.
 *
 * Called at the end of MX_LWIP_Init(). Idempotent, and correct whether or
 * not the generated code above it already did the right thing: if a
 * regeneration has brought DHCP back, this stops it and overwrites the
 * address it was about to negotiate for.
 */
void net_glue_apply_static_ip(void);

/**
 * Sample the link and feed the observer.
 *
 * Called from Ethernet_Link_Periodic_Handle()'s trailing USER CODE block,
 * which runs at main-loop rate rather than every 100 ms - CubeMX preserves
 * only the markers it emits, and there is none inside its timer block. So
 * this call rate-limits itself to NET_LINK_POLL_MS and is cheap to call as
 * often as the main loop likes.
 *
 * Reads the MAC's own configuration rather than the PHY over MDIO: the
 * speed and duplex there are what the MAC is actually using, and it costs
 * no management transaction.
 */
void net_glue_poll_link(void);

#endif /* NET_GLUE_H */
