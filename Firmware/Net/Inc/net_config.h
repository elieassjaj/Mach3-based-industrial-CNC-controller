/**
 * @file    net_config.h
 * @brief   Phase 2 (M12) network configuration - one source of truth.
 *
 * Every value here is already decided elsewhere in the repository; this
 * header exists so the firmware reads each one from a single place instead
 * of repeating literals across ethernetif.c, lwip.c and lwipopts.h. The
 * citation next to each value is the document that owns it - change the
 * document first, then this header.
 *
 * Portable by design: no STM32, HAL, CMSIS or lwIP dependency, so the host
 * test suite can check the invariants at the bottom of this file without a
 * target toolchain.
 */
#ifndef NET_CONFIG_H
#define NET_CONFIG_H

#include <stdint.h>

/* ------------------------------------------------------------ IPv4 ------ */
/* Docs/ETHERNET.md Section 15: static IPv4, no DHCP, no AUTOIP. Isolated
 * point-to-point link to one Mach3 PC NIC - there is no DHCP server on this
 * link and there never will be, so DHCP could only ever time out.          */
#define NET_IP_ADDR0        192u
#define NET_IP_ADDR1        168u
#define NET_IP_ADDR2          5u
#define NET_IP_ADDR3         10u

#define NET_NETMASK0        255u
#define NET_NETMASK1        255u
#define NET_NETMASK2        255u
#define NET_NETMASK3          0u

/* No router on this link and nothing outside the /24 to reach. */
#define NET_GW_ADDR0          0u
#define NET_GW_ADDR1          0u
#define NET_GW_ADDR2          0u
#define NET_GW_ADDR3          0u

/* The host end, for documentation and for the bring-up procedure only - the
 * firmware never needs to know it (Docs/ETHERNET.md Section 15).           */
#define NET_HOST_IP_ADDR3   100u

/* -------------------------------------------------------------- MAC ----- */
/* ADR-011 (Docs/FIRMWARE-ARCHITECTURE.md Section 41): locally-administered
 * unicast address for bench testing. The CubeMX placeholder 00:80:E1:...
 * is a real vendor's OUI and was never this project's to use. Production
 * units derive a per-unit address from the STM32 96-bit unique ID; that is
 * an M12 implementation detail and is not this bench value.               */
#define NET_MAC_0          0x02u   /* bit1 set => locally administered     */
#define NET_MAC_1          0x00u
#define NET_MAC_2          0x05u   /* echoes 192.168.5.10, no other meaning */
#define NET_MAC_3          0x10u
#define NET_MAC_4          0x00u
#define NET_MAC_5          0x01u

/* -------------------------------------------------- PHY reset (ADR-013) - */
/* On the FINAL PCB the LAN8720A's nRST is wired to PB0 and this reset is the
 * firmware's job. On the PROTOTYPE (Waveshare LAN8720 module) nRST is not on
 * the module's header at all and the module resets itself through its own
 * 4.7k + 100nF RC, so this pulse reaches nothing there
 * (Docs/PROTOTYPE-BOARD.md section 5).
 *
 * Known gap for the final PCB: the datasheet also requires nRST to stay
 * asserted until >= 25 ms after the supplies reach 80 % (tpurstd, Table
 * 5-9). This pulse is released a few milliseconds into boot and does not
 * yet meet that (Docs/PROTOTYPE-BOARD.md section 7).
 *
 * Assertion time: LAN8720A datasheet DS00002165C Table 5-9 gives
 * trstia = 100 us minimum ("nRST input assertion time"). ADR-013 adopts
 * 150 us, confirmed by the project owner, for margin over that minimum.
 *
 * During the pulse the PHY still needs its CLKIN: the module's own 50 MHz
 * oscillator supplies it and is independent of nRST (Docs/ETHERNET.md
 * Section 2.2, verified against the schematic), so datasheet Section 3.8.5.1
 * ("during a Hardware reset, an external clock must be supplied to
 * XTAL1/CLKIN") is satisfied. The same oscillator - not the PHY - feeds the
 * MCU's PA1 RMII reference clock, so asserting nRST does not stop the MAC's
 * clock either.                                                            */
#define NET_PHY_RESET_ASSERT_US     150u

/* Settle time between releasing nRST and the first MAC/MDIO access.
 *
 * IMPLEMENTATION CHOICE, not a datasheet minimum - flagged as such per
 * Docs/MOTION-ENGINE.md Rule 2. The datasheet's only post-deassertion
 * numbers are todad (output drive, <= 800 ns, Table 5-9) and the Section
 * 3.8.5 note that "for the first 16 us after coming out of reset, the RMII
 * interface will run at 2.5 MHz". 1 ms is ~60x that 16 us figure, costs
 * nothing at boot, and removes the question from the bring-up entirely. If
 * HV-20 ever shows the PHY needs longer, raise it here.                    */
#define NET_PHY_RESET_SETTLE_US    1000u

/* --------------------------------------------------------- priorities --- */
/* ADR-012's binding rule: ETH_IRQn and ETH_WKUP_IRQn must sit at a strictly
 * numerically HIGHER preempt priority (= lower urgency) than the STEP-DMA
 * vector, so Ethernet servicing can never preempt a ring refill or a DIR
 * write. The full ADR-004 order is E-STOP 0, inputs 1, STEP-DMA 2,
 * Ethernet 5. HV-22 checks this on the running target.                     */
#define NET_PRIO_ETH                 5u
#define NET_PRIO_STEP_DMA            2u   /* mirrors STEPGEN_PRIO_DMA       */

/* -------------------------------------------------------- UDP port ------ */
/* ADR-014 / Docs/PROTOCOL.md §2. Arbitrary, in IANA's dynamic range
 * (49152-65535) so it collides with no registered service, digits echoing
 * the device address 192.168.5.10. No SDK evidence exists for a port
 * number: ncPod is a USB device, and Galil's 13887 is that vendor's.
 *
 * This is the only place the port is written. The protocol layer, the
 * status transmitter and the PC-side tool all read it from here.          */
#define NET_UDP_PORT             55010u

/* ------------------------------------------------------------- misc ----- */
/* Link poll period of the CubeMX-generated Ethernet_Link_Periodic_Handle(). */
#define NET_LINK_POLL_MS           100u

/* ------------------------------------------------- compile-time checks -- */
/* These are the invariants that cost nothing to enforce and are expensive to
 * discover on a bench. A CubeMX regeneration is the expected way for any of
 * them to be violated.                                                     */

#define NET_STATIC_ASSERT(cond, tag) \
    typedef char net_static_assert_##tag[(cond) ? 1 : -1]

/* ADR-011: bit 1 of the first octet marks a locally-administered address,
 * bit 0 clear marks it unicast. Both must hold, or the address belongs to
 * somebody else or is a multicast address that no NIC will source.         */
NET_STATIC_ASSERT((NET_MAC_0 & 0x02u) != 0u, mac_is_locally_administered);
NET_STATIC_ASSERT((NET_MAC_0 & 0x01u) == 0u, mac_is_unicast);

/* The exact placeholder ADR-011 exists to remove. */
NET_STATIC_ASSERT(!((NET_MAC_0 == 0x00u) && (NET_MAC_1 == 0x80u) &&
                    (NET_MAC_2 == 0xE1u)), mac_is_not_cubemx_placeholder);

/* ADR-013 must stay at or above the datasheet's trstia minimum. */
NET_STATIC_ASSERT(NET_PHY_RESET_ASSERT_US >= 100u, phy_reset_meets_trstia);

/* ADR-012's Ethernet-below-STEP-DMA rule, as far as it can be checked
 * without the target's NVIC. HV-22 checks the registers themselves.        */
NET_STATIC_ASSERT(NET_PRIO_ETH > NET_PRIO_STEP_DMA, eth_below_step_dma);

/* Host and controller must share the /24 but not the address. */
NET_STATIC_ASSERT(NET_HOST_IP_ADDR3 != NET_IP_ADDR3, host_ip_differs);

/* ADR-014: the port must stay in the dynamic/private range, where no
 * registered service can collide with it. */
NET_STATIC_ASSERT(NET_UDP_PORT >= 49152u, udp_port_is_dynamic_range);

#endif /* NET_CONFIG_H */
