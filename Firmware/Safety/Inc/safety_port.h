/**
 * @file    safety_port.h
 * @brief   Hardware abstraction required by the digital-input manager (M3).
 *
 * Exactly one implementation is linked per build:
 *   Platform/STM32F407/Src/safety_port_stm32f4.c   GPIOE + EXTI, register level
 *   Platform/Host/Src/safety_port_sim.c            host simulation
 *
 * The port owns the pins and the interrupt controller. It owns no policy:
 * debounce, the E-STOP state machine and everything the rest of the
 * firmware reads live in the portable core (safety_input.h).
 */
#ifndef SAFETY_PORT_H
#define SAFETY_PORT_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Configure PE0..PE14 as both-edge EXTI inputs with no internal pull
 * (external pull-ups are fitted, Docs/PINOUT.md), map EXTI0..EXTI14 to
 * port E, and set the ADR-004 NVIC priorities - E-STOP at 0, the rest at 1.
 *
 * Interrupts are left DISABLED at the EXTI mask. safety_port_arm() enables
 * them, so the core can establish the initial state of every input before
 * the first edge can arrive.
 */
bool     safety_port_init(void);

/** Unmask the EXTI lines. Call once the core has sampled the initial state. */
void     safety_port_arm(void);

/**
 * Mask the EXTI lines again.
 *
 * Nothing in the firmware calls this today. It exists because a
 * self-test or a bring-up procedure may need the lines quiet, and because
 * its counterpart matters: the core is level-based precisely so that
 * whatever is missed while the lines are masked costs nothing beyond an
 * edge count (safety_port_read_raw()).
 */
void     safety_port_disarm(void);

/**
 * Raw levels of PE0..PE14, bit n = PEn, 1 = HIGH.
 *
 * Always a fresh read of the port, never a cached value: an edge that was
 * missed (two transitions inside one ISR, or a line masked during a
 * self-test) cannot desynchronise a level read the way it would desynchronise
 * an edge count. That property is why the whole subsystem is level-based.
 */
uint16_t safety_port_read_raw(void);

/** True while the E-STOP pin (PE2) reads asserted, straight from the port. */
bool     safety_port_estop_raw_asserted(void);

/** Free-running cycle counter used to measure the E-STOP path (0 if absent). */
uint32_t safety_port_cycle_count(void);

#endif /* SAFETY_PORT_H */
