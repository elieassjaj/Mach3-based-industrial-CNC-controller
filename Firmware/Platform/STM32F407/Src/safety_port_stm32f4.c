/**
 * @file    safety_port_stm32f4.c
 * @brief   STM32F407VGT6 GPIOE + EXTI port for the digital-input manager.
 *
 *      PE0..PE14 ──► EXTI0,1,2,3,4,9_5,15_10 ──► safety_input_on_*_edge()
 *                         (PE2 alone at NVIC priority 0)
 *
 * The port configures the pins and the interrupt controller itself rather
 * than trusting MX_GPIO_Init() to have done it. The generated code does do
 * it today, and identically - but a dropped pin in a future .ioc
 * regeneration would otherwise turn into a silently dead input, and one of
 * these fifteen is the E-STOP. Every write here is idempotent and the cost
 * is a few dozen cycles once at boot.
 *
 * Register-level rather than HAL, per Docs/FIRMWARE-ARCHITECTURE.md §16:
 * the E-STOP path's duration is a safety property (§8, HV-05, HV-43), and
 * the HAL's per-pin scan and weak-callback dispatch sit directly in it.
 */
#include "safety_hw_map.h"
#include "safety_port_stm32f4.h"
#include "safety_port.h"
#include "safety_input.h"

/* ---------------------------------------------------------------------- */

bool safety_port_init(void)
{
    RCC->AHB1ENR |= SAFETY_GPIO_RCC_AHB1ENR_BIT;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;                       /* ensure the write landed */

    /* Inputs, no internal pull. Docs/PINOUT.md: external pull-ups are
     * fitted on all fifteen lines, so an internal pull-up is redundant and
     * an internal pull-down would fight the board. */
    for (unsigned pin = 0; pin < SAFETY_INPUT_COUNT; pin++) {
        const unsigned sh = pin * 2u;
        SAFETY_GPIO->MODER = (SAFETY_GPIO->MODER & ~(3u << sh));  /* input */
        SAFETY_GPIO->PUPDR = (SAFETY_GPIO->PUPDR & ~(3u << sh));  /* none  */
    }

    /* Mask first: no line may interrupt while its trigger configuration is
     * half-written, and the core wants to sample the initial state before
     * the first edge can arrive. */
    EXTI->IMR &= ~SAFETY_EXTI_MASK;

    /* Route EXTI0..EXTI14 to port E. Four lines per EXTICR word. */
    for (unsigned pin = 0; pin < SAFETY_INPUT_COUNT; pin++) {
        const unsigned reg = pin / 4u;
        const unsigned sh  = (pin % 4u) * 4u;
        SYSCFG->EXTICR[reg] = (SYSCFG->EXTICR[reg] & ~(0xFu << sh))
                            | (SAFETY_SYSCFG_PORT_E << sh);
    }

    /* Both edges (Docs/PINOUT.md). The inputs are active low, so the
     * falling edge is the assertion and the rising edge the release; both
     * are needed because this subsystem tracks STATE, not events. */
    EXTI->RTSR |= SAFETY_EXTI_MASK;
    EXTI->FTSR |= SAFETY_EXTI_MASK;

    /* Discard anything latched while the pins were being configured. */
    EXTI->PR = SAFETY_EXTI_MASK;

    /* ADR-004's priority scheme. Requires NVIC_PRIORITYGROUP_4, which the
     * .ioc sets and HAL_Init() applies before this runs. */
    NVIC_SetPriority(SAFETY_IRQ_ESTOP,    SAFETY_PRIO_ESTOP);
    NVIC_SetPriority(SAFETY_IRQ_LINE0,    SAFETY_PRIO_INPUTS);
    NVIC_SetPriority(SAFETY_IRQ_LINE1,    SAFETY_PRIO_INPUTS);
    NVIC_SetPriority(SAFETY_IRQ_LINE3,    SAFETY_PRIO_INPUTS);
    NVIC_SetPriority(SAFETY_IRQ_LINE4,    SAFETY_PRIO_INPUTS);
    NVIC_SetPriority(SAFETY_IRQ_LINE9_5,  SAFETY_PRIO_INPUTS);
    NVIC_SetPriority(SAFETY_IRQ_LINE15_10, SAFETY_PRIO_INPUTS);

    NVIC_EnableIRQ(SAFETY_IRQ_ESTOP);
    NVIC_EnableIRQ(SAFETY_IRQ_LINE0);
    NVIC_EnableIRQ(SAFETY_IRQ_LINE1);
    NVIC_EnableIRQ(SAFETY_IRQ_LINE3);
    NVIC_EnableIRQ(SAFETY_IRQ_LINE4);
    NVIC_EnableIRQ(SAFETY_IRQ_LINE9_5);
    NVIC_EnableIRQ(SAFETY_IRQ_LINE15_10);

    /* DWT cycle counter, used by HV-43 to measure the E-STOP path. Already
     * enabled by the motion port; enabling it twice is harmless. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;

    return true;
}

void safety_port_arm(void)
{
    EXTI->PR   = SAFETY_EXTI_MASK;      /* nothing stale gets to fire */
    EXTI->IMR |= SAFETY_EXTI_MASK;
}

void safety_port_disarm(void)
{
    EXTI->IMR &= ~SAFETY_EXTI_MASK;
}

uint16_t safety_port_read_raw(void)
{
    return (uint16_t)(SAFETY_GPIO->IDR & SAFETY_INPUT_MASK);
}

bool safety_port_estop_raw_asserted(void)
{
    /* Active low: LOW at the pin means asserted. */
    return (SAFETY_GPIO->IDR & SAFETY_ESTOP_MASK) == 0u;
}

uint32_t safety_port_cycle_count(void)
{
    return DWT->CYCCNT;
}

/* ---------------------------------------------------------------------- */
/* Vectors                                                                 */
/* ---------------------------------------------------------------------- */

void safety_estop_isr(void)
{
    /* Clear BEFORE reading the level, never after. If the contact moves
     * again between the read and the clear, clearing afterwards would
     * discard that second edge and leave the software holding a level that
     * is one transition out of date. Clearing first costs a re-entry into
     * this handler instead, which is the harmless failure. */
    EXTI->PR = SAFETY_PR_ESTOP;

    safety_input_on_estop_edge();
}

void safety_inputs_isr(uint32_t pr_mask)
{
    /* Only this vector's own lines - see safety_port_stm32f4.h. */
    EXTI->PR = (pr_mask & SAFETY_EXTI_OTHERS_MASK);

    safety_input_on_edge();
}
