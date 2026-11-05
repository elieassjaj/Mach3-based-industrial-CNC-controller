/**
 * @file    io_selftest.h
 * @brief   On-target validation for the output subsystem (M9 / M10).
 *
 * Same contract as the motion, network and input self-tests: each entry is
 * a numbered test in Docs/HARDWARE-VALIDATION.md.
 *
 * **Nothing here turns a spindle or closes a relay.** A boot-time test that
 * span a tool to prove it could would be a test nobody dares enable on a
 * machine. These check the configuration and the interlock; HV-54 and
 * HV-55 exercise the outputs themselves, on a bench, deliberately.
 */
#ifndef IO_SELFTEST_H
#define IO_SELFTEST_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    HV_50_OUTPUTS_SAFE = 0,  /**< relay off, PWM stopped, pins inactive   */
    HV_51_PWM_CONFIG,        /**< exactly 10 kHz, preloaded, PB4 in AF2   */
    HV_52_ESTOP_KILL,        /**< the interrupt-time kill is registered   */
    HV_53_INTERLOCK_HOLDS,   /**< a duty request at boot moves nothing    */
    HV_IO_TEST_COUNT
} io_hv_test_t;

typedef struct {
    bool     run;
    bool     pass;
    uint32_t measured;
    uint32_t expected;
} io_hv_result_t;

/**
 * Run every firmware-checkable test. io_init() must have run, and the
 * engine must still be in SAFE_IDLE - which at boot it is.
 * True only if all pass.
 */
bool io_selftest_run_all(void);

const io_hv_result_t *io_selftest_results(void);

#endif /* IO_SELFTEST_H */
