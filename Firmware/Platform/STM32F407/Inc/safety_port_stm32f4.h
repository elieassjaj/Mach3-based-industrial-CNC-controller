/**
 * @file    safety_port_stm32f4.h
 * @brief   EXTI vector entry points for the digital-input manager (M3).
 *
 * The vectors themselves stay owned by the CubeMX-generated
 * Core/Src/stm32f4xx_it.c, so regenerating the project never produces a
 * duplicate-symbol clash. Each generated stub calls the matching function
 * here from its USER CODE block and returns before HAL_GPIO_EXTI_IRQHandler(),
 * exactly as the STEP-DMA vector already does.
 *
 * Why the HAL path is not used, for the E-STOP vector above all:
 * HAL_GPIO_EXTI_IRQHandler() tests one pin, clears it, then dispatches
 * through a weak callback, and a shared vector needs one call per line.
 * That is avoidable work and avoidable indirection in front of the one
 * path Docs/FIRMWARE-ARCHITECTURE.md §8 requires to be the fastest in the
 * system, and §16 permits register-level access exactly where timing
 * justifies it.
 */
#ifndef SAFETY_PORT_STM32F4_H
#define SAFETY_PORT_STM32F4_H

#include <stdint.h>

/* Per-vector EXTI pending masks. PE0..PE14 are served by six vectors; each
 * handler passes its own mask so it clears its own lines and no others. */
#define SAFETY_PR_LINE0             (1u << 0)
#define SAFETY_PR_LINE1             (1u << 1)
#define SAFETY_PR_ESTOP             (1u << 2)
#define SAFETY_PR_LINE3             (1u << 3)
#define SAFETY_PR_LINE4             (1u << 4)
#define SAFETY_PR_LINE9_5           (0x03E0u)         /* lines 5..9       */
#define SAFETY_PR_LINE15_10         (0x7C00u)         /* lines 10..14     */

/** EXTI2 - PE2, E-STOP. Clears the pending bit, then acts. */
void safety_estop_isr(void);

/**
 * Any other input vector. @p pr_mask is that vector's own set of EXTI
 * pending bits (SAFETY_PR_* in safety_hw_map.h) and nothing else: EXTI->PR
 * is write-1-to-clear and shared with every line, so a handler that clears
 * more than it owns can discard an E-STOP that has not been serviced yet.
 */
void safety_inputs_isr(uint32_t pr_mask);

#endif /* SAFETY_PORT_STM32F4_H */
