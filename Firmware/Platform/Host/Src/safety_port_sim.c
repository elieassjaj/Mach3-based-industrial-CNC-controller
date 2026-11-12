/**
 * @file    safety_port_sim.c
 * @brief   Host-side simulation port for the digital-input manager.
 *
 * Replaces GPIOE and EXTI with a pin-level variable and a dispatcher that
 * calls the same two ISR entry points the real vectors call. The masking
 * rules are reproduced too: while the port is disarmed a level change
 * produces no interrupt at all, which is what lets a test check that the
 * poll's level-based resynchronisation really does recover a lost edge.
 */
#include "safety_port.h"
#include "safety_input.h"
#include "safety_sim.h"

#define IN_MASK    ((uint16_t)SAFETY_INPUT_MASK)
#define ESTOP_BIT  ((uint16_t)SAFETY_ESTOP_MASK)

static uint16_t s_levels = IN_MASK;      /* all idle (HIGH) */
static bool     s_init;
static bool     s_armed;
static uint32_t s_cycles;
static uint32_t s_estop_isrs;
static uint32_t s_input_isrs;

/* ------------------------------------------------------------- port ---- */

bool safety_port_init(void)
{
    s_init  = true;
    s_armed = false;
    return true;
}

void safety_port_arm(void)    { s_armed = true;  }
void safety_port_disarm(void) { s_armed = false; }

uint16_t safety_port_read_raw(void) { return (uint16_t)(s_levels & IN_MASK); }

bool safety_port_estop_raw_asserted(void)
{
    return (s_levels & ESTOP_BIT) == 0u;
}

uint32_t safety_port_cycle_count(void) { return ++s_cycles; }

/* -------------------------------------------------------------- sim ---- */

void sim_safety_reset(void)
{
    s_levels     = IN_MASK;
    s_init       = false;
    s_armed      = false;
    s_cycles     = 0u;
    s_estop_isrs = 0u;
    s_input_isrs = 0u;
}

static void dispatch(uint16_t changed)
{
    /* E-STOP first, and on its own vector, mirroring the hardware: EXTI2 is
     * PE2's alone and pre-empts the shared vectors (ADR-004). */
    if ((changed & ESTOP_BIT) != 0u) {
        s_estop_isrs++;
        safety_input_on_estop_edge();
    }
    if ((changed & (uint16_t)~ESTOP_BIT) != 0u) {
        s_input_isrs++;
        safety_input_on_edge();
    }
}

void sim_safety_set_levels(uint16_t levels)
{
    const uint16_t next    = (uint16_t)(levels & IN_MASK);
    const uint16_t changed = (uint16_t)(s_levels ^ next);

    s_levels = next;

    if (s_init && s_armed && changed != 0u) {
        dispatch(changed);
    }
}

void sim_safety_set_levels_silent(uint16_t levels)
{
    s_levels = (uint16_t)(levels & IN_MASK);
}

void sim_safety_assert(unsigned index)
{
    sim_safety_set_levels((uint16_t)(s_levels & ~(uint16_t)(1u << index)));
}

void sim_safety_release(unsigned index)
{
    sim_safety_set_levels((uint16_t)(s_levels | (uint16_t)(1u << index)));
}

uint32_t sim_safety_estop_isr_count(void) { return s_estop_isrs; }
uint32_t sim_safety_input_isr_count(void) { return s_input_isrs; }
bool     sim_safety_armed(void)           { return s_armed; }
