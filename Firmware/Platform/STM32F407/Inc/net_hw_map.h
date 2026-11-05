/**
 * @file    net_hw_map.h
 * @brief   STM32F407VGT6 hardware allocation for the Ethernet subsystem (M12).
 *
 * Same convention as stepgen_hw_map.h: every peripheral and pin the network
 * port touches is named here and nowhere else, so a reader can see the whole
 * allocation in one place and a CubeMX regeneration cannot quietly move one.
 *
 * The RMII data pins (PA1/PA2/PA7, PC1/PC4/PC5, PB11/PB12/PB13) are owned by
 * the generated HAL_ETH_MspInit() and are deliberately absent here - this
 * port does not reconfigure them. Only PHY_NRST is ours, because ADR-013
 * gives the reset pulse to the firmware.
 */
#ifndef NET_HW_MAP_H
#define NET_HW_MAP_H

#include "stm32f4xx.h"
#include "net_config.h"

/* ---------------------------------------------------------- PHY_NRST ---- */
/* Docs/PINOUT.md: PHY_NRST = PB0, active low. On the final PCB this drives
 * the LAN8720A's nRST directly (ADR-013). On the prototype's Waveshare
 * module nRST is not on the header, so PB0 reaches nothing and the module's
 * own 4.7k + 100nF RC resets the PHY (Docs/PROTOTYPE-BOARD.md section 5). */
#define NET_PHY_NRST_GPIO           GPIOB
#define NET_PHY_NRST_RCC_AHB1ENR    RCC_AHB1ENR_GPIOBEN
#define NET_PHY_NRST_PIN            0u
#define NET_PHY_NRST_MASK           (1u << NET_PHY_NRST_PIN)

/* ------------------------------------------------------------- NVIC ----- */
/* ADR-012's binding rule, as concrete vectors. The STEP-DMA vector is the
 * one that must never be preempted by Ethernet; it is named here only so
 * HV-22 can compare the two without including the motion port's headers.   */
#define NET_ETH_IRQn                ETH_IRQn
#define NET_ETH_WKUP_IRQn           ETH_WKUP_IRQn
#define NET_STEP_DMA_IRQn           DMA2_Stream1_IRQn

/* -------------------------------------------------------- SRAM regions -- */
/* RM0090 Section 2.1 memory map, matching STM32F407VGTX_FLASH.ld:
 *   SRAM1  0x20000000 + 112K   default RAM region - Ethernet and lwIP live
 *                              here, and ADR-012 requires that they do
 *   SRAM2  0x2001C000 + 16K    carved out for the STEP-DMA ring only
 *   CCM    0x10000000 + 64K    not on the bus matrix; NO DMA master can
 *                              reach it, including the Ethernet MAC's own
 * HV-21 exists because a buffer landing in either of the last two is a
 * silent failure on hardware rather than a build error.                    */
#define NET_SRAM1_BASE              0x20000000u
#define NET_SRAM1_SIZE              (112u * 1024u)
#define NET_SRAM2_BASE              0x2001C000u
#define NET_SRAM2_SIZE              (16u * 1024u)
#define NET_CCMRAM_BASE             0x10000000u
#define NET_CCMRAM_SIZE             (64u * 1024u)

#endif /* NET_HW_MAP_H */
