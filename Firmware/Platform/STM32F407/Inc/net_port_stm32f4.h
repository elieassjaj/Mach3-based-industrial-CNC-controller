/**
 * @file    net_port_stm32f4.h
 * @brief   STM32F407VGT6 hardware port for the Ethernet subsystem (M12).
 *
 * Deliberately small. The MAC, the RMII pins and the lwIP plumbing are the
 * generated project's; what is genuinely this project's are the PHY reset
 * pulse ADR-013 requires and the introspection the self-tests need.
 *
 * Register-level rather than HAL for the reset pulse, per
 * Docs/FIRMWARE-ARCHITECTURE.md Section 16: HAL_Delay() has 1 ms SysTick
 * resolution and cannot express a 150 us assertion at all.
 */
#ifndef NET_PORT_STM32F4_H
#define NET_PORT_STM32F4_H

#include <stdbool.h>
#include <stdint.h>

#include "net_hw_map.h"

/**
 * Busy-wait for at least @p us microseconds.
 *
 * Uses the DWT cycle counter, enabling it first if nothing else has - this
 * runs during MX_LWIP_Init(), which is before stepgen_init() enables it for
 * HV-04, and enabling it twice is harmless.
 *
 * Interrupts are not disabled: an ISR during the wait only makes the delay
 * longer, and every user here has a minimum, not a maximum.
 */
void net_port_delay_us(uint32_t us);

/**
 * ADR-013: pulse PHY_NRST (PB0) low, then release it.
 *
 * Drives the line low for NET_PHY_RESET_ASSERT_US (150 us, against the
 * LAN8720A's 100 us trstia minimum), releases it, then waits
 * NET_PHY_RESET_SETTLE_US before returning so the caller may go straight to
 * MAC init and MDIO.
 *
 * Must be called before HAL_ETH_Init() - not between it and LAN8742_Init() -
 * so the PHY is already out of reset and settled when the MAC's DMA soft
 * reset runs.
 *
 * @return false if PB0 does not read back high after release, which means
 *         something external is holding nRST asserted (a short, or a missing
 *         module). The PHY cannot work in that state and MDIO would fail
 *         later with a less obvious symptom.
 */
bool net_port_phy_hw_reset(void);

/** True if the pulse has been executed at least once this boot (HV-20). */
bool net_port_phy_reset_done(void);

/** Measured duration of the last reset assertion, in CPU cycles (HV-20). */
uint32_t net_port_phy_reset_assert_cycles(void);

/** Current NVIC preempt priority of @p irq, in the .ioc's numbering. */
uint32_t net_port_irq_priority(IRQn_Type irq);

/** True if the whole [p, p+len) range lies inside SRAM1 (HV-21). */
bool net_port_addr_in_sram1(const void *p, uint32_t len);

#endif /* NET_PORT_STM32F4_H */
