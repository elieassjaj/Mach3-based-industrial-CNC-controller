/**
 * @file    stepgen_hw_map.h
 * @brief   STM32F407VGT6 peripheral allocation for the STEP/DIR generator.
 *
 * ====================== DEVIATION FROM ADR-004/ADR-005 ===================
 * The frozen ADRs route TIM2_UP -> DMA1_Stream1 -> GPIOA->BSRR, and the
 * .ioc is configured that way. Verified against the repository's own
 * RM0090 Rev 22, that path cannot work:
 *
 *   §2.1  "In STM32F405xx/07xx ... Eight masters: Cortex-M4 ... I-bus,
 *          D-bus and S-bus; DMA1 memory bus; DMA2 memory bus; DMA2
 *          peripheral bus; Ethernet DMA bus; USB OTG HS DMA bus"
 *         -> the DMA1 *peripheral* bus is not a bus-matrix master.
 *   Fig 33 note: "The DMA1 controller AHB peripheral port is not connected
 *          to the bus matrix like DMA2 controller."
 *   §2.1  slaves include "AHB1 peripherals" - where GPIOA lives.
 *   §10.3.16 / §10.3.17 step 2: in a memory-to-peripheral transfer the
 *          DMA_SxPAR side is driven by the AHB *peripheral* port.
 *
 * So DMA1 cannot address GPIOA->BSRR at all. Only DMA2's peripheral port
 * reaches AHB1, and RM0090 Table 44 gives DMA2 timer requests for TIM1 and
 * TIM8 only - so the base timer cannot be TIM2 either.
 *
 * This file therefore uses TIM8 + DMA2_Stream1 (Channel 7 = TIM8_UP,
 * RM0090 Table 44, verified). Everything else from the frozen ADRs is
 * kept: the 4 MHz tick, the single GPIOA port (ADR-005), CPU-timed DIR
 * (ADR-006), direct mode, and the NVIC priority ordering (ADR-004).
 *
 * This is recorded as ADR-012 and needs an owner decision plus an .ioc
 * change; it is not a silent substitution. See Docs/PHASE1-STATUS.md.
 * =========================================================================
 */
#ifndef STEPGEN_HW_MAP_H
#define STEPGEN_HW_MAP_H

#include "stm32f4xx.h"
#include "cnc_motion_config.h"

/* ------------------------------------------------------------- timer --- */
/* TIM8: APB2, 168 MHz timer clock (RCC.APB2TimFreq_Value in the .ioc),
 * PSC = 0, ARR = 41 -> exactly 4.000 MHz. TIM1 is left free; TIM3 is the
 * 10 kHz spindle PWM on PB4. TIM8's own interrupt must stay disabled - at
 * the 4 MHz tick it would fire 4,000,000 times a second (ADR-004).        */
#define STEPGEN_TIM                 TIM8
#define STEPGEN_TIM_RCC_APB2ENR_BIT RCC_APB2ENR_TIM8EN
#define STEPGEN_TIM_CLK_HZ          STEPGEN_TIMER_CLK_HZ

/* --------------------------------------------------------------- DMA --- */
/* TIM8_UP -> DMA2 Stream 1, Channel 7 (RM0090 Rev 22, Table 44, verified). */
#define STEPGEN_DMA                 DMA2
#define STEPGEN_DMA_RCC_AHB1ENR_BIT RCC_AHB1ENR_DMA2EN
#define STEPGEN_DMA_STREAM          DMA2_Stream1
#define STEPGEN_DMA_CHSEL           7u
#define STEPGEN_DMA_IRQn            DMA2_Stream1_IRQn
#define STEPGEN_DMA_IRQHandler      DMA2_Stream1_IRQHandler

/* Stream 1 lives in the low interrupt-status/clear register pair. */
#define STEPGEN_DMA_ISR             (STEPGEN_DMA->LISR)
#define STEPGEN_DMA_IFCR            (STEPGEN_DMA->LIFCR)
#define STEPGEN_DMA_HTIF            DMA_LISR_HTIF1
#define STEPGEN_DMA_TCIF            DMA_LISR_TCIF1
#define STEPGEN_DMA_TEIF            DMA_LISR_TEIF1
#define STEPGEN_DMA_DMEIF           DMA_LISR_DMEIF1
#define STEPGEN_DMA_CLR_ALL         (DMA_LIFCR_CHTIF1 | DMA_LIFCR_CTCIF1 | \
                                     DMA_LIFCR_CTEIF1 | DMA_LIFCR_CDMEIF1 | \
                                     DMA_LIFCR_CFEIF1)

/* -------------------------------------------------------------- GPIO --- */
#define STEPGEN_GPIO_STEP           GPIOA      /* PA8..PA12 */
#define STEPGEN_GPIO_DIR            GPIOD      /* PD8..PD12 */
#define STEPGEN_GPIO_EN             GPIOD      /* PD15      */

/* --------------------------------------------------------- priorities -- */
/* ADR-004's scheme, with the STEP-DMA vector following the deviation above.
 * This table IS the Ethernet timing-isolation requirement expressed in
 * hardware: nothing at or below Ethernet's priority can delay anything
 * above it. Requires NVIC_PRIORITYGROUP_4 (4 bits pre-emption).           */
#define STEPGEN_PRIO_ESTOP          0u   /* EXTI2 - PE2, dedicated vector  */
#define STEPGEN_PRIO_INPUTS         1u   /* PE0,PE1,PE3..PE14              */
#define STEPGEN_PRIO_DMA            2u   /* STEP ring boundary + refill    */
#define STEPGEN_PRIO_ETH            5u   /* Ethernet MAC (Phase 2)         */
/* 3-4 reserved as headroom; SysTick stays at the HAL default (15).        */

#define STEPGEN_TIM_ARR_FOR(tick_hz)  ((STEPGEN_TIM_CLK_HZ / (tick_hz)) - 1u)

#endif /* STEPGEN_HW_MAP_H */
