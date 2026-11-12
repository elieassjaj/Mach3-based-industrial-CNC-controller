/**
 * @file    io_hw_map.h
 * @brief   STM32F407VGT6 peripheral allocation for the output subsystem.
 *
 * Relay and both LEDs are on GPIOB, and so is the spindle PWM pin, so the
 * whole subsystem needs one GPIO clock and one timer.
 *
 * TIM3 is the spindle timer and is not shared with anything: TIM8 is the
 * STEP base tick (ADR-012) and TIM1 is left free. Nothing here contends
 * with the motion path - different timer, different DMA controller, and no
 * DMA at all on this side.
 */
#ifndef IO_HW_MAP_H
#define IO_HW_MAP_H

#include "stm32f4xx.h"

#include "cnc_io_config.h"

/* -------------------------------------------------------------- GPIO --- */
#define IO_GPIO                     GPIOB          /* PB1, PB2, PB4, PB8  */
#define IO_GPIO_RCC_AHB1ENR_BIT     RCC_AHB1ENR_GPIOBEN

#define IO_GPIO_OUT_MASK            (RELAY_PIN_MASK_GPIOB | \
                                     LED_RUN_MASK_GPIOB   | \
                                     LED_ERR_MASK_GPIOB)

/* ------------------------------------------------------------- timer --- */
/* TIM3_CH1 on PB4, alternate function 2 (RM0090 Table 9). PB4 is NJTRST
 * at reset; this project debugs over SWD only, which is what frees it
 * (Docs/FIRMWARE-ARCHITECTURE.md §29). */
#define IO_SPINDLE_TIM              TIM3
#define IO_SPINDLE_TIM_RCC_APB1_BIT RCC_APB1ENR_TIM3EN
#define IO_SPINDLE_PIN              4u             /* PB4                 */
#define IO_SPINDLE_PIN_MASK         (1u << IO_SPINDLE_PIN)
#define IO_SPINDLE_AF               2u             /* AF2 = TIM3_CH1      */

/* Channel 1 output-compare mode bits, RM0090 §18.4.7. */
#define IO_OC1M_PWM1                (6u << 4)      /* 110: PWM mode 1     */
#define IO_OC1M_FORCE_INACTIVE      (4u << 4)      /* 100: force low      */
#define IO_OC1M_MASK                (7u << 4)

/* The .ioc carries PSC=83/ARR=99. This port programs PSC=0/ARR=8399
 * instead: identical 10.000 kHz, 84x the duty resolution. See ADR-016 and
 * cnc_io_config.h. The frequency - the only spindle number Docs/PINOUT.md
 * actually fixes - is unchanged. */
#define IO_SPINDLE_PSC              0u
#define IO_SPINDLE_ARR              (SPINDLE_PWM_PERIOD_COUNTS - 1u)

_Static_assert(SPINDLE_TIMER_CLK_HZ % SPINDLE_PWM_HZ == 0u,
               "the PWM frequency must divide the timer clock exactly");
_Static_assert(SPINDLE_PWM_PERIOD_COUNTS >= SPINDLE_PMILLE_MAX,
               "period below 1000 counts cannot resolve per-mille duty");
_Static_assert(IO_SPINDLE_ARR <= 0xFFFFu,
               "TIM3 is a 16-bit timer; ARR does not fit");
_Static_assert((IO_GPIO_OUT_MASK & IO_SPINDLE_PIN_MASK) == 0u,
               "the spindle pin must not also be a GPIO output");

#endif /* IO_HW_MAP_H */
