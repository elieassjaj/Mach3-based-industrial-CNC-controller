/**
 * @file    test_net_link.c
 * @brief   Host verification of the Phase 2 network configuration and the
 *          portable link observer.
 *
 * What this suite can and cannot establish, stated plainly so no one reads
 * more into a green run than is there:
 *
 *   CAN  - the link state machine counts transitions correctly, never
 *          reports a speed on a down link, and treats a repeated
 *          observation as no edge at all;
 *   CAN  - the configuration constants still hold the values the documents
 *          say they hold, and still satisfy ADR-011/012/013's invariants;
 *   CANNOT - anything about the PHY, the MAC, the reset pulse or the wire.
 *          Those need the board: HV-20..HV-25 in Docs/HARDWARE-VALIDATION.md.
 */
#include "net_config.h"
#include "net_link.h"
#include "test_util.h"

#include <string.h>

int g_fail = 0, g_checks = 0, g_case_failed = 0;
const char *g_case = "";

static const net_phy_report_t k_down     = { false, NET_SPEED_NONE, false };
static const net_phy_report_t k_up_100fd = { true,  NET_SPEED_100M, true  };
static const net_phy_report_t k_up_10hd  = { true,  NET_SPEED_10M,  false };

/* ------------------------------------------------------------------ */
/* The values the documents fix. A failure here means either the code or
 * the document moved without the other.                                */
/* ------------------------------------------------------------------ */
static void test_config_values(void)
{
    TCASE("static IPv4 is 192.168.5.10/24 with no gateway");
    CHECK_EQI(NET_IP_ADDR0, 192);
    CHECK_EQI(NET_IP_ADDR1, 168);
    CHECK_EQI(NET_IP_ADDR2, 5);
    CHECK_EQI(NET_IP_ADDR3, 10);
    CHECK_EQI(NET_NETMASK0, 255);
    CHECK_EQI(NET_NETMASK1, 255);
    CHECK_EQI(NET_NETMASK2, 255);
    CHECK_EQI(NET_NETMASK3, 0);
    CHECK_EQI(NET_GW_ADDR0 | NET_GW_ADDR1 | NET_GW_ADDR2 | NET_GW_ADDR3, 0);
    TDONE();

    TCASE("MAC is ADR-011's locally-administered unicast address");
    CHECK_EQI(NET_MAC_0, 0x02);
    CHECK_EQI(NET_MAC_1, 0x00);
    CHECK_EQI(NET_MAC_2, 0x05);
    CHECK_EQI(NET_MAC_3, 0x10);
    CHECK_EQI(NET_MAC_4, 0x00);
    CHECK_EQI(NET_MAC_5, 0x01);
    /* The two bits that make it legal to use at all. */
    CHECK((NET_MAC_0 & 0x02u) != 0u);      /* locally administered */
    CHECK((NET_MAC_0 & 0x01u) == 0u);      /* unicast              */
    /* And not the vendor OUI CubeMX regenerates. */
    CHECK(!(NET_MAC_0 == 0x00u && NET_MAC_1 == 0x80u && NET_MAC_2 == 0xE1u));
    TDONE();

    TCASE("PHY reset pulse clears the datasheet's trstia minimum");
    /* LAN8720A DS00002165C Table 5-9: trstia >= 100 us. ADR-013 adopts 150. */
    CHECK_EQI(NET_PHY_RESET_ASSERT_US, 150);
    CHECK_GE(NET_PHY_RESET_ASSERT_US, 100);
    /* Settle time must clear the Section 3.8.5 note's 16 us RMII startup. */
    CHECK_GE(NET_PHY_RESET_SETTLE_US, 16);
    TDONE();

    TCASE("Ethernet stays below the STEP-DMA vector (ADR-012)");
    CHECK(NET_PRIO_ETH > NET_PRIO_STEP_DMA);
    CHECK_EQI(NET_PRIO_ETH, 5);
    CHECK_EQI(NET_PRIO_STEP_DMA, 2);
    TDONE();
}

/* ------------------------------------------------------------------ */
static void test_initial_state(void)
{
    net_link_status_t st;

    TCASE("a freshly initialised observer reports nothing up");
    net_link_init();
    net_link_get(&st);
    CHECK(!st.link_up);
    CHECK_EQI(st.speed, NET_SPEED_NONE);
    CHECK(!st.full_duplex);
    CHECK_EQI(st.up_count, 0);
    CHECK_EQI(st.down_count, 0);
    CHECK_EQI(st.last_change_ms, 0);
    CHECK(!st.phy_init_ok);
    CHECK(!net_link_is_up());
    TDONE();
}

static void test_up_transition(void)
{
    net_link_status_t st;

    TCASE("first up report is an edge, records speed and time");
    net_link_init();
    CHECK(net_link_on_report(&k_up_100fd, 1234u));
    net_link_get(&st);
    CHECK(st.link_up);
    CHECK_EQI(st.speed, NET_SPEED_100M);
    CHECK(st.full_duplex);
    CHECK_EQI(st.up_count, 1);
    CHECK_EQI(st.down_count, 0);
    CHECK_EQI(st.last_change_ms, 1234);
    CHECK(net_link_is_up());
    TDONE();

    TCASE("repeated up reports are not edges and do not re-count");
    /* The 100 ms poll delivers the same observation over and over; if each
     * one counted, the flap counter would be meaningless. */
    for (uint32_t i = 0; i < 50u; i++) {
        CHECK(!net_link_on_report(&k_up_100fd, 2000u + i));
    }
    net_link_get(&st);
    CHECK_EQI(st.up_count, 1);
    CHECK_EQI(st.last_change_ms, 1234);   /* unchanged by non-edges */
    TDONE();
}

static void test_down_transition(void)
{
    net_link_status_t st;

    TCASE("a down link reports no speed, whatever it had");
    net_link_init();
    (void)net_link_on_report(&k_up_100fd, 10u);
    CHECK(net_link_on_report(&k_down, 99u));
    net_link_get(&st);
    CHECK(!st.link_up);
    /* Keeping the last negotiated speed here would read as a live link to
     * anything that only glances at the snapshot. */
    CHECK_EQI(st.speed, NET_SPEED_NONE);
    CHECK(!st.full_duplex);
    CHECK_EQI(st.up_count, 1);
    CHECK_EQI(st.down_count, 1);
    CHECK_EQI(st.last_change_ms, 99);
    TDONE();
}

static void test_speed_change_is_not_an_edge(void)
{
    net_link_status_t st;

    TCASE("renegotiating speed while up is not an edge");
    net_link_init();
    (void)net_link_on_report(&k_up_100fd, 10u);
    CHECK(!net_link_on_report(&k_up_10hd, 20u));
    net_link_get(&st);
    CHECK(st.link_up);
    CHECK_EQI(st.speed, NET_SPEED_10M);
    CHECK(!st.full_duplex);
    CHECK_EQI(st.up_count, 1);
    CHECK_EQI(st.down_count, 0);
    CHECK_EQI(st.last_change_ms, 10);
    TDONE();
}

static void test_flapping(void)
{
    net_link_status_t st;

    TCASE("a flapping link counts every transition");
    net_link_init();
    for (uint32_t i = 0; i < 7u; i++) {
        CHECK(net_link_on_report(&k_up_100fd, 100u + i * 10u));
        CHECK(net_link_on_report(&k_down,     105u + i * 10u));
    }
    net_link_get(&st);
    CHECK_EQI(st.up_count, 7);
    CHECK_EQI(st.down_count, 7);
    CHECK_EQI(st.last_change_ms, 105u + 6u * 10u);
    CHECK(!st.link_up);
    TDONE();
}

static void test_phy_init_record(void)
{
    net_link_status_t st;

    TCASE("the scanned PHY address is recorded, not assumed");
    net_link_init();
    /* The LAN8720A strap allows 0 or 1 and the repository's sources disagree
     * about which this module uses, so whatever the driver's scan found is
     * what gets reported. */
    net_link_set_phy_init(true, 1u);
    net_link_get(&st);
    CHECK(st.phy_init_ok);
    CHECK_EQI(st.phy_addr, 1);

    net_link_set_phy_init(false, 0u);
    net_link_get(&st);
    CHECK(!st.phy_init_ok);
    CHECK_EQI(st.phy_addr, 0);
    TDONE();
}

static void test_null_safety(void)
{
    net_link_status_t before, after;

    TCASE("a null report changes nothing and is not an edge");
    net_link_init();
    (void)net_link_on_report(&k_up_100fd, 42u);
    net_link_get(&before);
    CHECK(!net_link_on_report(NULL, 43u));
    net_link_get(&after);
    CHECK(memcmp(&before, &after, sizeof(before)) == 0);
    net_link_get(NULL);            /* must not fault */
    TDONE();
}

int main(void)
{
    printf("CNC5AX-ETH network (M12) host verification\n");
    printf("  static %u.%u.%u.%u/%u.%u.%u.%u  MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
           NET_IP_ADDR0, NET_IP_ADDR1, NET_IP_ADDR2, NET_IP_ADDR3,
           NET_NETMASK0, NET_NETMASK1, NET_NETMASK2, NET_NETMASK3,
           NET_MAC_0, NET_MAC_1, NET_MAC_2, NET_MAC_3, NET_MAC_4, NET_MAC_5);
    printf("  PHY reset %u us assert + %u us settle, ETH prio %u vs STEP-DMA %u\n\n",
           NET_PHY_RESET_ASSERT_US, NET_PHY_RESET_SETTLE_US,
           NET_PRIO_ETH, NET_PRIO_STEP_DMA);

    test_config_values();
    test_initial_state();
    test_up_transition();
    test_down_transition();
    test_speed_change_is_not_an_edge();
    test_flapping();
    test_phy_init_record();
    test_null_safety();

    printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
