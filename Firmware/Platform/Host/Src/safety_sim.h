/**
 * @file    safety_sim.h
 * @brief   Bench controls for the simulated digital-input port.
 *
 * Lets a host test move a contact and observe what the firmware does about
 * it, including the cases a real bench makes awkward: a switch that bounces
 * for a precise number of milliseconds, an E-STOP that is already down at
 * power-on, and an edge that never gets delivered at all.
 */
#ifndef SAFETY_SIM_H
#define SAFETY_SIM_H

#include <stdbool.h>
#include <stdint.h>

/** Back to power-on: every input idle (HIGH), port uninitialised, disarmed. */
void sim_safety_reset(void);

/**
 * Drive the fifteen pin levels, bit n = PEn, 1 = HIGH = idle.
 * Delivers the matching interrupts if the port has been armed, exactly as
 * EXTI would.
 */
void sim_safety_set_levels(uint16_t levels);

/** Same, but deliver no interrupt - a missed or masked edge. */
void sim_safety_set_levels_silent(uint16_t levels);

/** Convenience: pull one input low (assert) or let it go high (release). */
void sim_safety_assert(unsigned index);
void sim_safety_release(unsigned index);

/** How many times the simulated EXTI has dispatched, split by vector. */
uint32_t sim_safety_estop_isr_count(void);
uint32_t sim_safety_input_isr_count(void);

bool sim_safety_armed(void);

#endif /* SAFETY_SIM_H */
