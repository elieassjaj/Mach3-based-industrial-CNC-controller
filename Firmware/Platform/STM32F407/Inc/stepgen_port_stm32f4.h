/**
 * @file    stepgen_port_stm32f4.h
 * @brief   STM32F407-specific extras of the STEP generator port.
 */
#ifndef STEPGEN_PORT_STM32F4_H
#define STEPGEN_PORT_STM32F4_H

#include <stdint.h>
#include "stepgen_hw_map.h"

/** Ring-boundary interrupt. The CubeIDE startup file must route the vector
 *  named by STEPGEN_DMA_IRQHandler here. */
void STEPGEN_DMA_IRQHandler(void);

/** Count of DMA transfer/FIFO errors seen. Must stay 0. */
uint32_t stepgen_port_dma_errors(void);

#endif /* STEPGEN_PORT_STM32F4_H */
