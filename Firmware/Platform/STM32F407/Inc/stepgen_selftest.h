/**
 * @file    stepgen_selftest.h
 * @brief   On-target hardware validation for the STEP/DIR generator.
 *
 * These exist so the claims in Docs/MOTION-ENGINE.md §29-§30 can be
 * MEASURED rather than asserted. Each corresponds to a numbered test in
 * Docs/HARDWARE-VALIDATION.md and either self-checks in firmware or drives
 * a stimulus intended for a scope or logic analyser.
 *
 * Nothing in this project may claim 2 MHz or 3-axis compliance until the
 * relevant test has been run on the real board and its result recorded.
 */
#ifndef STEPGEN_SELFTEST_H
#define STEPGEN_SELFTEST_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    HV_00_DMA_PATH = 0,     /**< DMA2 really can write GPIOA->BSRR         */
    HV_01_BSRR_PRIORITY,    /**< BSRR set bits win over reset bits         */
    HV_02_TICK_FREQUENCY,   /**< the base tick is exactly 4 MHz            */
    HV_03_SRAM2_PLACEMENT,  /**< the ring really is in SRAM2               */
    HV_04_REFILL_COST,      /**< worst-case refill cycles (DWT)            */
    HV_05_ESTOP_LATENCY,    /**< emergency-stop path duration              */
    HV_06_EN_POLARITY,      /**< EN was never at its enabled level at boot */
    HV_TEST_COUNT
} stepgen_hv_test_t;

typedef struct {
    bool     run;
    bool     pass;
    uint32_t measured;
    uint32_t expected;
} stepgen_hv_result_t;

/** Run every firmware-checkable test. Engine must be initialised, stopped,
 *  and the drives disconnected or unpowered. True only if all pass. */
bool stepgen_selftest_run_all(void);

const stepgen_hv_result_t *stepgen_selftest_results(void);

/* Scope / logic-analyser stimulus. Each queues segments and starts the
 * engine; call stepgen_stop() when the capture is done. */

/** HV-10/11/12: n_axes (1..5) at exactly 2 MHz, continuously. */
bool stepgen_stim_max_rate(uint32_t n_axes, uint32_t seconds);

/** HV-14: one axis reversing, to capture DIR setup/hold. */
bool stepgen_stim_dir_reversal(uint32_t seconds);

/** HV-13: five axes at deliberately incommensurate rates. */
bool stepgen_stim_mixed_rates(uint32_t seconds);

#endif /* STEPGEN_SELFTEST_H */
