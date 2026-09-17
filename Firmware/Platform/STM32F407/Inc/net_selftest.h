/**
 * @file    net_selftest.h
 * @brief   On-target hardware validation for the Ethernet subsystem (M12).
 *
 * Same contract as stepgen_selftest.h: each entry corresponds to a numbered
 * test in Docs/HARDWARE-VALIDATION.md and is a measurement, not an
 * assertion. HV-0x/HV-1x belong to the motion engine; the network subsystem
 * takes HV-2x.
 *
 * Two of these exist because the repository asked for them by name:
 * Docs/PRE-IMPLEMENTATION-DECISIONS.md items 10 and 11 both end with a
 * recommendation for a runtime check "so a future regression fails a test
 * instead of failing silently on hardware". HV-21 and HV-22 are those
 * checks.
 */
#ifndef NET_SELFTEST_H
#define NET_SELFTEST_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    HV_20_PHY_RESET_PULSE = 0, /**< ADR-013 pulse ran, nRST released       */
    HV_21_BUFFERS_IN_SRAM1,    /**< ADR-012: no Ethernet buffer outside SRAM1 */
    HV_22_ETH_IRQ_PRIORITY,    /**< ADR-012: Ethernet below the STEP DMA   */
    HV_23_MAC_ADDRESS,         /**< ADR-011 address, not the placeholder   */
    HV_24_STATIC_IP,           /**< static 192.168.5.10/24, DHCP inactive  */
    HV_25_LINK_STATE,          /**< link up, speed and duplex (needs cable) */
    NET_HV_TEST_COUNT
} net_hv_test_t;

typedef struct {
    bool     run;
    bool     pass;
    uint32_t measured;
    uint32_t expected;
} net_hv_result_t;

/**
 * Run every firmware-checkable test.
 *
 * Call after MX_LWIP_Init(), which is what performs the PHY reset and brings
 * the interface up. Safe at any time: every test reads state, none of them
 * reconfigures the MAC, moves a buffer or touches the motion engine.
 *
 * @return true only if all of HV-20..HV-24 pass. HV-25 is excluded from the
 *         verdict on purpose - it depends on a cable being plugged in, and a
 *         bench run with no cable is not a firmware failure.
 */
bool net_selftest_run_all(void);

const net_hv_result_t *net_selftest_results(void);

#endif /* NET_SELFTEST_H */
