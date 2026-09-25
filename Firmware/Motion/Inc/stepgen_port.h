/**
 * @file    stepgen_port.h
 * @brief   Hardware abstraction required by the STEP generator.
 *
 * Exactly one implementation is linked per build:
 *   Platform/STM32F407/Src/stepgen_port_stm32f4.c   TIM8 + DMA2 + GPIO BSRR
 *   Platform/Host/Src/stepgen_port_sim.c            host simulation
 */
#ifndef STEPGEN_PORT_H
#define STEPGEN_PORT_H

#include "motion_types.h"

/** Ring-boundary callback: the DMA has just finished physical half @p phalf. */
typedef void (*stepgen_boundary_cb_t)(uint32_t phalf);

bool     stepgen_port_init(stepgen_boundary_cb_t cb, uint32_t *step_buf,
                           uint32_t ring_ticks);

/** Set the base tick frequency. Only legal while stopped. */
bool     stepgen_port_set_tick_hz(uint32_t tick_hz);
uint32_t stepgen_port_get_tick_hz(void);

void     stepgen_port_start(void);
void     stepgen_port_stop(void);

/**
 * Immediate, interrupt-safe emergency stop. Register writes only: stop the
 * timer, force every STEP pin low, deassert ENABLE. Callable from the PE2
 * E-STOP handler.
 */
void     stepgen_port_emergency_stop(void);

/** Halt STEP output but leave the drives energised (ADR-010 hold-position). */
void     stepgen_port_halt_hold(void);

/** Driver ENABLE output (PD15). Polarity: CNC_EN_ACTIVE_HIGH. */
void     stepgen_port_set_enable(bool enable);
bool     stepgen_port_get_enable(void);

/**
 * The EN polarity rule, in one place.
 *
 * Every port computes the PD15 level through this, so the STM32 port and
 * the host test suite apply the same rule and the suite can check it for
 * both polarities without rebuilding. Pure, and header-only so an
 * interrupt-context caller (the emergency stop) pays no call.
 *
 * @return true if PD15 must be HIGH for the requested state.
 */
static inline bool stepgen_en_pin_level(bool enable, bool active_high)
{
    return enable == active_high;
}

/** Inverse: given the pin level, are the drives enabled? */
static inline bool stepgen_en_enabled_from_pin(bool pin_high, bool active_high)
{
    return pin_high == active_high;
}

/** Write a GPIOD BSRR word. This is the ADR-006 play-time DIR write. */
void     stepgen_port_write_dir(uint32_t bsrr_word);

/** Remaining transfers on the STEP stream. */
uint32_t stepgen_port_ndtr(void);

/** Free-running cycle counter used to measure refill cost (0 if absent). */
uint32_t stepgen_port_cycle_count(void);

#endif /* STEPGEN_PORT_H */
