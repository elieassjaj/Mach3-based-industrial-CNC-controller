/**
 * @file    io_port.h
 * @brief   Hardware abstraction required by the output subsystem (M9/M10).
 *
 * Exactly one implementation is linked per build:
 *   Platform/STM32F407/Src/io_port_stm32f4.c   GPIOB + TIM3_CH1, register level
 *   Platform/Host/Src/io_port_sim.c            host simulation
 *
 * The port owns the pins and the timer. It owns no policy: the safety
 * interlocks, the LED patterns and the per-mille arithmetic live in the
 * portable core (io_outputs.h, io_spindle.h).
 */
#ifndef IO_PORT_H
#define IO_PORT_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Configure PB8 (relay) and PB1/PB2 (LEDs) as outputs, all driven to their
 * inactive level BEFORE they become outputs, and program TIM3_CH1 for the
 * configured PWM frequency with 0 % duty.
 *
 * Leaves the PWM generator stopped and the relay off. Nothing this
 * function does can energise anything.
 */
bool     io_port_init(void);

/** Relay, honouring CNC_RELAY_ACTIVE_HIGH. */
void     io_port_set_relay(bool on);
bool     io_port_get_relay(void);

/** LEDs, honouring CNC_LED_ACTIVE_HIGH. */
void     io_port_set_led_run(bool on);
void     io_port_set_led_err(bool on);

/**
 * Load a compare value, in timer counts, and start the generator if it is
 * not already running. @p ccr of 0 is a legitimate 0 % duty, not a stop.
 */
void     io_port_spindle_set_ccr(uint32_t ccr);

/** Stop the generator outright and leave the pin low. */
void     io_port_spindle_stop(void);

bool     io_port_spindle_running(void);
uint32_t io_port_spindle_ccr(void);

/** Counts per PWM period, as the timer is actually programmed. */
uint32_t io_port_spindle_period(void);

/**
 * Immediate, interrupt-safe kill: relay off and spindle duty to zero, with
 * register writes only. Callable from the PE2 E-STOP handler, which is
 * exactly what it exists for - waiting for the superloop to notice an
 * emergency stop before de-energising a spindle contactor is not a design.
 */
void     io_port_emergency_off(void);

#endif /* IO_PORT_H */
