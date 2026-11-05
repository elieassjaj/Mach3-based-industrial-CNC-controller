/**
 * @file    io_sim.h
 * @brief   Bench readouts for the simulated output port.
 *
 * Lets a host test look at what the pins and the timer are actually doing,
 * including the thing a bench makes hardest to see: whether the PWM
 * generator is stopped or merely sitting at zero duty.
 */
#ifndef IO_SIM_H
#define IO_SIM_H

#include <stdbool.h>
#include <stdint.h>

/** Back to power-on: everything off, port uninitialised. */
void sim_io_reset(void);

bool     sim_io_relay(void);       /**< true = energised (logical, not pin) */
bool     sim_io_led_run(void);
bool     sim_io_led_err(void);

bool     sim_io_pwm_running(void);
uint32_t sim_io_pwm_ccr(void);
uint32_t sim_io_pwm_period(void);

/** Counts every emergency-off the port has been asked to perform. */
uint32_t sim_io_emergency_offs(void);

/** Transitions of the relay's logical state, to catch a glitch. */
uint32_t sim_io_relay_changes(void);

#endif /* IO_SIM_H */
