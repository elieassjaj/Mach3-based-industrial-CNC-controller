/**
 * @file    safety_selftest.h
 * @brief   On-target validation for the digital-input manager (M3).
 *
 * The same contract as stepgen_selftest.h and net_selftest.h: each entry
 * corresponds to a numbered test in Docs/HARDWARE-VALIDATION.md and either
 * self-checks in firmware or states what a bench must do.
 *
 * These are configuration and cost checks. They cannot prove an E-STOP
 * stops a machine - HV-18 on a scope does that - but they do catch the
 * failures that would make HV-18 meaningless: a line routed to the wrong
 * port, a priority inversion against the STEP refill, an input that never
 * reaches idle, or an interlock nobody registered.
 */
#ifndef SAFETY_SELFTEST_H
#define SAFETY_SELFTEST_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    HV_40_ESTOP_INTERLOCK = 0, /**< the release gate is registered         */
    HV_41_EXTI_CONFIG,         /**< lines, edges, port select, priorities  */
    HV_42_INPUTS_IDLE,         /**< all fifteen read idle with nothing on  */
    HV_43_ISR_COST,            /**< input ISR duration, CPU cycles         */
    HV_SAFETY_TEST_COUNT
} safety_hv_test_t;

typedef struct {
    bool     run;
    bool     pass;
    uint32_t measured;
    uint32_t expected;
} safety_hv_result_t;

/**
 * Run every firmware-checkable test. safety_input_init() must have run.
 * HV-42 assumes nothing is holding an input down, so run it with the
 * machine's switches in their rest position. True only if all pass.
 */
bool safety_selftest_run_all(void);

const safety_hv_result_t *safety_selftest_results(void);

#endif /* SAFETY_SELFTEST_H */
