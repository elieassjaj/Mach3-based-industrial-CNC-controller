/**
 * @file    net_glue_stm32f4.c
 * @brief   Bridge between the generated network code and M12 (see net_glue.h).
 */
#include "net_glue.h"
#include "net_port_stm32f4.h"
#include "net_config.h"
#include "net_link.h"

#include "stm32f4xx_hal.h"
#include "lan8742.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"

/* Owned by the generated ethernetif.c / lwip.c. */
extern ETH_HandleTypeDef  heth;
extern lan8742_Object_t   LAN8742;
extern struct netif       gnetif;

bool net_glue_phy_reset(void)
{
    /* Earliest point in the network bring-up, so the observer starts clean
     * here rather than needing a fourth hook of its own. */
    net_link_init();

    return net_port_phy_hw_reset();
}

void net_glue_apply_static_ip(void)
{
    ip4_addr_t ip, mask, gw;

    IP4_ADDR(&ip,   NET_IP_ADDR0, NET_IP_ADDR1, NET_IP_ADDR2, NET_IP_ADDR3);
    IP4_ADDR(&mask, NET_NETMASK0, NET_NETMASK1, NET_NETMASK2, NET_NETMASK3);
    IP4_ADDR(&gw,   NET_GW_ADDR0, NET_GW_ADDR1, NET_GW_ADDR2, NET_GW_ADDR3);

#if LWIP_DHCP
    /* Only reachable if a regeneration re-enabled DHCP. There is no DHCP
     * server on this link, so a negotiation could only ever time out with
     * the interface left at 0.0.0.0. Stop it before setting the address,
     * since dhcp_stop() clears the address on its way out. */
    dhcp_stop(&gnetif);
#endif

    netif_set_addr(&gnetif, &ip, &mask, &gw);
}

void net_glue_poll_link(void)
{
    /* Called from the main loop, so rate-limit here rather than relying on a
     * CubeMX marker inside the generated 100 ms timer block - CubeMX only
     * preserves the markers it emits, and that is not one of them. Matching
     * the generated poll period means one sample per link check, no more. */
    static uint32_t s_last_ms;
    static bool     s_sampled;

    const uint32_t now = HAL_GetTick();
    if (s_sampled && ((now - s_last_ms) < NET_LINK_POLL_MS)) {
        return;
    }
    s_last_ms = now;
    s_sampled = true;

    net_phy_report_t report = { false, NET_SPEED_NONE, false };

    /* netif link state is the MAC's view, already reconciled with the PHY by
     * the generated ethernet_link_check_state() immediately before this
     * call. Asking the PHY again over MDIO would be a second source of
     * truth, and a slower one. */
    if (netif_is_link_up(&gnetif)) {
        ETH_MACConfigTypeDef conf = {0};
        HAL_ETH_GetMACConfig(&heth, &conf);

        report.link_up     = true;
        report.speed       = (conf.Speed == ETH_SPEED_100M) ? NET_SPEED_100M
                                                            : NET_SPEED_10M;
        report.full_duplex = (conf.DuplexMode == ETH_FULLDUPLEX_MODE);
    }

    net_link_on_report(&report, now);

    /* The PHY driver discovers its own address by scanning (the LAN8720A
     * strap allows 0 or 1 and this repository's sources disagree about which
     * the module uses - Docs/ETHERNET.md Section 2.2), so the address is
     * read back here rather than assumed anywhere. */
    net_link_set_phy_init(LAN8742.Is_Initialized != 0u,
                          (uint8_t)LAN8742.DevAddr);
}
