/**
 * @file    io_spindle.c
 * @brief   Spindle PWM duty control (M10), portable core.
 *
 * All this module owns is one number and the arithmetic that turns it into
 * a compare value. The safety interlock that decides whether that number
 * reaches the timer at all lives in io_outputs.c, because the same
 * interlock governs the relay and the two must never disagree about
 * whether the machine is allowed to do work.
 */
#include "io_spindle.h"
#include "io_port.h"

static struct {
    uint16_t requested;      /**< per mille, as commanded  */
    uint16_t actual;         /**< per mille, as programmed */
} s;

/**
 * Per mille -> timer counts, rounded to nearest.
 *
 * The period is read back from the port rather than assumed, so a port
 * that had to settle for a different period still produces the duty the
 * host asked for. Rounding to nearest rather than truncating keeps the
 * worst-case error at half a count - 0.006 % duty at the 8400-count period
 * this project uses, against 0.1 % if it truncated.
 *
 * At 1000 per mille this returns exactly `period`, which is one more than
 * the largest counter value. That is deliberate and is what a real 100 %
 * duty requires: in PWM mode 1 the output is active while CNT < CCR, so a
 * compare equal to the counter's top value would still leave one inactive
 * count per period.
 *
 * The multiply is done in 64-bit because period * 1000 overflows 32 bits
 * for any period above ~4.29 million counts. This project's period is
 * 8400, but a module that silently breaks if someone lowers the PWM
 * frequency is a trap, and the cost here is one multiply per host packet.
 */
static uint32_t pmille_to_ccr(uint16_t pmille)
{
    const uint64_t period = (uint64_t)io_port_spindle_period();
    const uint64_t num    = (uint64_t)pmille * period + (SPINDLE_PMILLE_MAX / 2u);

    return (uint32_t)(num / SPINDLE_PMILLE_MAX);
}

void io_spindle_reset(void)
{
    s.requested = 0u;
    s.actual    = 0u;
}

bool io_spindle_set_pmille(uint16_t pmille)
{
    if (pmille > SPINDLE_PMILLE_MAX) {
        return false;                    /* refused, never clamped */
    }
    s.requested = pmille;
    return true;
}

uint16_t io_spindle_requested(void) { return s.requested; }
uint16_t io_spindle_actual(void)    { return s.actual;    }

void io_spindle_apply(bool inhibited)
{
    if (inhibited || s.requested == 0u) {
        /* Stop the generator outright rather than leaving it running at
         * 0 % duty. A stopped timer cannot produce a pulse if a compare
         * register is corrupted or a glitch reaches the peripheral; a
         * running one at 0 % is one bad write away from turning a spindle. */
        if (io_port_spindle_running()) {
            io_port_spindle_stop();
        }
        s.actual = 0u;
        return;
    }

    io_port_spindle_set_ccr(pmille_to_ccr(s.requested));
    s.actual = s.requested;
}

void io_spindle_force_off(void)
{
    io_port_spindle_stop();
    s.actual    = 0u;
    /* The request is dropped too, not merely withheld. Otherwise clearing
     * the fault would spin the tool back up on its own, and ADR-010 is
     * explicit that recovery is never automatic. */
    s.requested = 0u;
}
