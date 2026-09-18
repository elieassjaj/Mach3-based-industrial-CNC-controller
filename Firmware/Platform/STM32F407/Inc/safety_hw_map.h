/**
 * @file    safety_hw_map.h
 * @brief   STM32F407VGT6 peripheral allocation for the digital inputs (M3).
 *
 * All 15 inputs are on GPIOE (Docs/PINOUT.md), which is what makes the
 * whole subsystem cheap: one IDR read is a complete, skew-free sample of
 * every input, and one SYSCFG port selection covers all of them.
 *
 * Nothing here deviates from a frozen ADR. Unlike the STEP path (ADR-012),
 * the EXTI allocation is forced by the pin numbers alone - EXTIn is wired
 * to pin n of whichever port SYSCFG selects - so there is no allocation
 * decision left to make.
 */
#ifndef SAFETY_HW_MAP_H
#define SAFETY_HW_MAP_H

#include "stm32f4xx.h"

#include "cnc_safety_config.h"
#include "safety_port_stm32f4.h"
#include "stepgen_hw_map.h"     /* ADR-004's single NVIC priority table */

/* -------------------------------------------------------------- GPIO --- */
#define SAFETY_GPIO                 GPIOE          /* PE0..PE14           */
#define SAFETY_GPIO_RCC_AHB1ENR_BIT RCC_AHB1ENR_GPIOEEN
#define SAFETY_SYSCFG_PORT_E        4u             /* EXTICR nibble value */

/* -------------------------------------------------------------- EXTI --- */
/* EXTI lines 0..14, i.e. exactly SAFETY_INPUT_MASK. Line n serves pin n of
 * one selected port, so the mask and the pin mask are the same number. */
#define SAFETY_EXTI_MASK            ((uint32_t)SAFETY_INPUT_MASK)
#define SAFETY_EXTI_ESTOP_MASK      ((uint32_t)SAFETY_ESTOP_MASK)
#define SAFETY_EXTI_OTHERS_MASK     (SAFETY_EXTI_MASK & ~SAFETY_EXTI_ESTOP_MASK)

/* The six vectors that serve those lines. EXTI2 is E-STOP's alone, which
 * is a hardware property and the reason ADR-004 can give it priority 0
 * without starving the other inputs. */
#define SAFETY_IRQ_LINE0            EXTI0_IRQn        /* PE0              */
#define SAFETY_IRQ_LINE1            EXTI1_IRQn        /* PE1              */
#define SAFETY_IRQ_ESTOP            EXTI2_IRQn        /* PE2 - E-STOP     */
#define SAFETY_IRQ_LINE3            EXTI3_IRQn        /* PE3              */
#define SAFETY_IRQ_LINE4            EXTI4_IRQn        /* PE4              */
#define SAFETY_IRQ_LINE9_5          EXTI9_5_IRQn      /* PE5..PE9         */
#define SAFETY_IRQ_LINE15_10        EXTI15_10_IRQn    /* PE10..PE14       */

/* The per-vector EXTI pending masks live in safety_port_stm32f4.h, with
 * the ISR entry points they are arguments to. */

/* --------------------------------------------------------- priorities -- */
/* ADR-004, taken from the motion subsystem's copy of the table rather than
 * restated, so the two can never drift apart. */
#define SAFETY_PRIO_ESTOP           STEPGEN_PRIO_ESTOP    /* 0 */
#define SAFETY_PRIO_INPUTS          STEPGEN_PRIO_INPUTS   /* 1 */

_Static_assert(SAFETY_PRIO_ESTOP < SAFETY_PRIO_INPUTS,
               "ADR-004: E-STOP must pre-empt the other inputs");
_Static_assert(SAFETY_PRIO_INPUTS < STEPGEN_PRIO_DMA,
               "ADR-004: inputs must pre-empt the STEP ring refill");
_Static_assert((SAFETY_PR_LINE9_5 | SAFETY_PR_LINE15_10 | SAFETY_PR_LINE0 |
                SAFETY_PR_LINE1 | SAFETY_PR_ESTOP | SAFETY_PR_LINE3 |
                SAFETY_PR_LINE4) == SAFETY_EXTI_MASK,
               "the per-vector line masks do not cover PE0..PE14 exactly");

#endif /* SAFETY_HW_MAP_H */
