/**
 * @file    net_selftest_stm32f4.c
 * @brief   On-target Ethernet validation, HV-20..HV-25 (see net_selftest.h).
 */
#include "net_selftest.h"
#include "net_port_stm32f4.h"
#include "net_hw_map.h"
#include "net_config.h"
#include "net_link.h"

#include "stm32f4xx_hal.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"

#include <string.h>

/* Owned by the generated ethernetif.c and lwip.c. Declared rather than
 * included so this file does not depend on the generated headers' contents,
 * which a CubeMX run may reshape. */
extern ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT];
extern ETH_DMADescTypeDef DMATxDscrTab[ETH_TX_DESC_CNT];
extern ETH_HandleTypeDef  heth;
extern struct netif       gnetif;

/* lwIP's LWIP_MEMPOOL_DECLARE(RX_POOL, ...) in ethernetif.c expands to a
 * plain u8_t array at file scope, so the zero-copy RX pool is reachable by
 * name. This is the buffer ADR-012 most cares about: it is the one that
 * grows, and the one a future edit is most likely to relocate. */
extern uint8_t memp_memory_RX_POOL_base[];

static net_hv_result_t s_res[NET_HV_TEST_COUNT];

static void record(net_hv_test_t t, bool pass, uint32_t measured,
                   uint32_t expected)
{
    s_res[t].run      = true;
    s_res[t].pass     = pass;
    s_res[t].measured = measured;
    s_res[t].expected = expected;
}

/* ---------------------------------------------------------------------- */
/* HV-20  The ADR-013 reset pulse ran, and nRST is actually released.
 *
 * Two distinct failures hide here. The pulse may never have run at all - a
 * regeneration dropping the USER CODE call would do that, and the PHY would
 * still work often enough to look fine, because LAN8742_Init()'s own MDIO
 * soft reset papers over it. Or the pulse ran and the line is still held low
 * by something external, in which case nothing works and the symptom is a
 * failed MDIO scan several layers away.
 *
 * The measurement is the assertion time in CPU cycles, against the 150 us
 * ADR-013 requires - so a delay loop that is silently too short (a wrong
 * SystemCoreClock, say) fails here rather than violating the datasheet's
 * 100 us trstia minimum on the bench.
 * ---------------------------------------------------------------------- */
static void hv20_phy_reset_pulse(void)
{
    const uint32_t cycles_per_us = SystemCoreClock / 1000000u;
    const uint32_t required      = NET_PHY_RESET_ASSERT_US * cycles_per_us;
    const uint32_t measured      = net_port_phy_reset_assert_cycles();

    const bool released = (NET_PHY_NRST_GPIO->IDR & NET_PHY_NRST_MASK) != 0u;
    const bool pass     = net_port_phy_reset_done()
                       && released
                       && (measured >= required);

    record(HV_20_PHY_RESET_PULSE, pass, measured, required);
}

/* ---------------------------------------------------------------------- */
/* HV-21  Every Ethernet and lwIP buffer is in SRAM1.
 *
 * Docs/PRE-IMPLEMENTATION-DECISIONS.md item 10 asks for exactly this test,
 * symmetric to the motion engine's HV-03. Two ways to fail:
 *
 *   SRAM2 - the STEP-DMA ring's own region. Sharing it would put Ethernet
 *           traffic back on the bus-matrix port ADR-012 cleared for the ring.
 *   CCM   - RM0090 Section 2.1: not on the bus matrix, reachable only by the
 *           CPU. No DMA master can touch it, the Ethernet MAC's own DMA
 *           included, so a descriptor there is not slow but non-functional.
 *
 * Both are invisible at build time and produce confusing symptoms at run
 * time, which is why the check is here and not only in a linker map.
 * ---------------------------------------------------------------------- */
static void hv21_buffers_in_sram1(void)
{
    const bool rx_desc = net_port_addr_in_sram1(DMARxDscrTab, sizeof(DMARxDscrTab));
    const bool tx_desc = net_port_addr_in_sram1(DMATxDscrTab, sizeof(DMATxDscrTab));
    /* Only the pool's first byte can be checked by name - its length is
     * private to lwIP's macro - but a pool straddling a region boundary is
     * not a thing the linker produces. */
    const bool rx_pool = net_port_addr_in_sram1(memp_memory_RX_POOL_base, 1u);

    /* Report the first offending address, so a failure names the culprit
     * instead of only saying that one exists. */
    uint32_t offender = 0u;
    if (!rx_desc) {
        offender = (uint32_t)(uintptr_t)DMARxDscrTab;
    } else if (!tx_desc) {
        offender = (uint32_t)(uintptr_t)DMATxDscrTab;
    } else if (!rx_pool) {
        offender = (uint32_t)(uintptr_t)memp_memory_RX_POOL_base;
    }

    record(HV_21_BUFFERS_IN_SRAM1, rx_desc && tx_desc && rx_pool,
           offender, NET_SRAM1_BASE);
}

/* ---------------------------------------------------------------------- */
/* HV-22  Ethernet interrupts sit strictly below the STEP-DMA vector.
 *
 * Docs/PRE-IMPLEMENTATION-DECISIONS.md item 11. This is the NVIC half of
 * "Ethernet must not affect STEP timing": if ETH could preempt
 * DMA2_Stream1_IRQn, a burst of packets would delay a ring refill or an
 * ADR-006 DIR write, and the only evidence would be jitter on a scope.
 *
 * Read from the NVIC itself rather than from the .ioc, because the .ioc is
 * what a regeneration rewrites.
 * ---------------------------------------------------------------------- */
static void hv22_eth_irq_priority(void)
{
    const uint32_t step = net_port_irq_priority(NET_STEP_DMA_IRQn);
    const uint32_t eth  = net_port_irq_priority(NET_ETH_IRQn);
    const uint32_t wkup = net_port_irq_priority(NET_ETH_WKUP_IRQn);

    const bool pass = (eth > step) && (wkup > step);

    /* measured packs both Ethernet vectors so one word tells the whole
     * story: 0xEEWWSS = ETH, ETH_WKUP, STEP-DMA. */
    const uint32_t measured = ((eth & 0xFFu) << 16)
                            | ((wkup & 0xFFu) << 8)
                            |  (step & 0xFFu);

    record(HV_22_ETH_IRQ_PRIORITY, pass, measured,
           ((uint32_t)NET_PRIO_ETH << 16) | ((uint32_t)NET_PRIO_ETH << 8)
           | (uint32_t)NET_PRIO_STEP_DMA);
}

/* ---------------------------------------------------------------------- */
/* HV-23  The MAC in the running MAC filter is ADR-011's address.
 *
 * Checks the hardware register, not the C literal: the literal lives in
 * ethernetif.c, which CubeMX rewrites, and the placeholder it regenerates
 * (00:80:E1:...) is a real vendor's OUI. Reading MACA0HR/MACA0LR is the only
 * way to know what the MAC is actually sourcing frames with.
 * ---------------------------------------------------------------------- */
static void hv23_mac_address(void)
{
    const uint32_t lo = heth.Instance->MACA0LR;
    const uint32_t hi = heth.Instance->MACA0HR;

    const uint8_t mac[6] = {
        (uint8_t)( lo        & 0xFFu),
        (uint8_t)((lo >>  8) & 0xFFu),
        (uint8_t)((lo >> 16) & 0xFFu),
        (uint8_t)((lo >> 24) & 0xFFu),
        (uint8_t)( hi        & 0xFFu),
        (uint8_t)((hi >>  8) & 0xFFu),
    };

    const uint8_t want[6] = { NET_MAC_0, NET_MAC_1, NET_MAC_2,
                              NET_MAC_3, NET_MAC_4, NET_MAC_5 };

    const bool pass = (memcmp(mac, want, sizeof(want)) == 0);

    /* The first three octets identify the failure at a glance: 0x0080E1
     * means the placeholder came back. */
    record(HV_23_MAC_ADDRESS, pass,
           ((uint32_t)mac[0] << 16) | ((uint32_t)mac[1] << 8) | mac[2],
           ((uint32_t)NET_MAC_0 << 16) | ((uint32_t)NET_MAC_1 << 8) | NET_MAC_2);
}

/* ---------------------------------------------------------------------- */
/* HV-24  The interface carries the static address, and DHCP is not running.
 *
 * This regression has already happened once in this repository: the static
 * configuration was written into CubeMX-generated regions of lwip.c/lwip.h
 * and a later regeneration silently restored dhcp_start(). On this link
 * there is no DHCP server, so the symptom is an unreachable controller with
 * a 0.0.0.0 address and nothing in the build to say why.
 * ---------------------------------------------------------------------- */
static void hv24_static_ip(void)
{
    const uint32_t want = (uint32_t)NET_IP_ADDR0
                        | ((uint32_t)NET_IP_ADDR1 <<  8)
                        | ((uint32_t)NET_IP_ADDR2 << 16)
                        | ((uint32_t)NET_IP_ADDR3 << 24);

    const uint32_t have = netif_ip4_addr(&gnetif)->addr;

    const uint32_t want_mask = (uint32_t)NET_NETMASK0
                             | ((uint32_t)NET_NETMASK1 <<  8)
                             | ((uint32_t)NET_NETMASK2 << 16)
                             | ((uint32_t)NET_NETMASK3 << 24);

    bool pass = (have == want)
             && (netif_ip4_netmask(&gnetif)->addr == want_mask);

#if LWIP_DHCP
    /* If a regeneration brought DHCP back, the runtime guard in
     * MX_LWIP_Init()'s USER CODE block should have stopped it. Fail anyway:
     * the build is no longer the one the documents describe. */
    pass = pass && (netif_dhcp_data(&gnetif) == NULL);
#endif

    record(HV_24_STATIC_IP, pass, have, want);
}

/* ---------------------------------------------------------------------- */
/* HV-25  Link state. Informational: needs a cable, so it is reported but
 * does not decide the overall verdict.
 * ---------------------------------------------------------------------- */
static void hv25_link_state(void)
{
    net_link_status_t st;
    net_link_get(&st);

    /* 100BASE-TX full duplex is what this link should negotiate; anything
     * less is a cable or auto-negotiation problem worth seeing. */
    const bool pass = st.link_up
                   && (st.speed == NET_SPEED_100M)
                   && st.full_duplex;

    record(HV_25_LINK_STATE, pass,
           ((uint32_t)st.link_up << 16) | ((uint32_t)st.speed << 8)
           | (uint32_t)st.full_duplex,
           (1u << 16) | ((uint32_t)NET_SPEED_100M << 8) | 1u);
}

/* ---------------------------------------------------------------------- */

bool net_selftest_run_all(void)
{
    memset(s_res, 0, sizeof(s_res));

    hv20_phy_reset_pulse();
    hv21_buffers_in_sram1();
    hv22_eth_irq_priority();
    hv23_mac_address();
    hv24_static_ip();
    hv25_link_state();

    bool all = true;
    for (uint32_t i = 0; i < (uint32_t)NET_HV_TEST_COUNT; i++) {
        if (i == (uint32_t)HV_25_LINK_STATE) {
            continue;                 /* needs a cable; see net_selftest.h */
        }
        all = all && s_res[i].pass;
    }
    return all;
}

const net_hv_result_t *net_selftest_results(void)
{
    return s_res;
}
