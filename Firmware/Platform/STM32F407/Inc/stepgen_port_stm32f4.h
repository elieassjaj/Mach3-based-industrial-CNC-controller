/**
 * @file    stepgen_port_stm32f4.h
 * @brief   STM32F407-specific extras of the STEP generator port.
 */
#ifndef STEPGEN_PORT_STM32F4_H
#define STEPGEN_PORT_STM32F4_H

#include <stdbool.h>
#include <stdint.h>
#include "stepgen_hw_map.h"

/**
 * Ring-boundary interrupt body.
 *
 * Must be called from the CubeMX-generated DMA2_Stream1_IRQHandler(), as
 * the first thing it does, and that stub must then return without calling
 * HAL_DMA_IRQHandler() - see Core/Src/stm32f4xx_it.c.
 */
void stepgen_dma_isr(void);

/** Count of DMA transfer/FIFO errors seen. Must stay 0. */
uint32_t stepgen_port_dma_errors(void);

/**
 * True if, when stepgen_init() took over, PD15 was already an output at
 * the ENABLED level - i.e. the drives had been live since MX_GPIO_Init().
 * Means CNC_EN_ACTIVE_HIGH and CubeMX's initial level for PD15 disagree.
 * HV-06 reports it.
 */
bool stepgen_port_en_enabled_at_boot(void);

#endif /* STEPGEN_PORT_STM32F4_H */
