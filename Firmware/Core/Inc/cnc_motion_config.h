/**
 * @file    cnc_motion_config.h
 * @brief   CNC5AX-ETH motion subsystem compile-time configuration.
 *
 * Values are tagged:
 *   FIXED    - mandated by Docs/MOTION-ENGINE.md or Docs/PINOUT.md
 *   ADR      - set by a frozen Architecture Decision Record
 *   OPEN     - an ADR deliberately left this open; the value here is a
 *              documented default, not a decision
 *   DEVIATES - differs from a frozen ADR; see the note and ADR-012
 */
#ifndef CNC_MOTION_CONFIG_H
#define CNC_MOTION_CONFIG_H

/* ---------------------------------------------------------------- axes -- */
#define MOTION_AXIS_COUNT           5u                  /* FIXED            */

#define MOTION_AXIS_X               0u
#define MOTION_AXIS_Y               1u
#define MOTION_AXIS_Z               2u
#define MOTION_AXIS_A               3u
#define MOTION_AXIS_B               4u

/* --------------------------------------------------- STEP/DIR limits ---- */
#define MOTION_STEP_RATE_MAX_HZ     2000000u            /* FIXED §4.2       */
#define MOTION_STEP_WIDTH_MIN_NS    100u                /* FIXED §5         */
#define MOTION_DIR_SETUP_MIN_NS     200u                /* FIXED §7         */
#define MOTION_DIR_HOLD_MIN_NS      200u                /* FIXED §7         */

/* ------------------------------------------------------------ timebase -- */
/* ADR-004: 4 MHz base tick / 250 ns. Each STEP period is built from a SET
 * tick and a RESET tick, so the tick must be twice the 2 MHz ceiling. This
 * also yields a 250 ns pulse, 150 ns above the §5 minimum.                 */
#define STEPGEN_TICK_HZ             (2u * MOTION_STEP_RATE_MAX_HZ)

#define STEPGEN_TICK_PS             (1000000000000ull / STEPGEN_TICK_HZ)

/* DEVIATES from ADR-004's TIM2 (84 MHz, ARR=20).
 *
 * ADR-004/ADR-005 route TIM2_UP -> DMA1_Stream1 -> GPIOA->BSRR. That cannot
 * work on this MCU: RM0090 Rev 22 §2.1 lists the STM32F405xx/07xx bus-matrix
 * masters as Cortex I/D/S-bus, DMA1 *memory* bus, DMA2 memory bus, DMA2
 * *peripheral* bus, Ethernet DMA and USB OTG HS DMA - the DMA1 *peripheral*
 * bus is not among them, and Figure 33's note says so explicitly. GPIOA is
 * an AHB1 peripheral, i.e. a bus-matrix slave, and §10.3.16/§10.3.17 put the
 * DMA_SxPAR side of a memory-to-peripheral transfer on the peripheral port.
 * DMA1 therefore cannot address GPIOA->BSRR at all.
 *
 * Only DMA2 can, and RM0090 Table 44 gives DMA2 timer requests for TIM1 and
 * TIM8 only. TIM8 is used here (TIM1 left free, TIM3 is the spindle PWM).
 * TIM8 is on APB2: 168 MHz timer clock, ARR = 41 -> exactly 4 MHz.
 *
 * See ADR-012. This needs an owner decision, not a silent substitution.    */
#ifndef STEPGEN_TIMER_CLK_HZ
#define STEPGEN_TIMER_CLK_HZ        168000000u   /* APB2TimFreq from the .ioc */
#endif

/* ------------------------------------------------------------ DIR guard - */
/* ADR-006: g leading ticks of the target half are reserved idle for a
 * reversing axis. The ADR proves g=2 sufficient against a worst-case
 * software latency L ~ 502 ns and adopts g=3 (498 ns margin).              */
#ifndef STEPGEN_DIR_GUARD_TICKS
#define STEPGEN_DIR_GUARD_TICKS     3u
#endif

/* -------------------------------------------------------- STEP DMA ring - */
/* OPEN (ADR-008): the STEP-DMA refill buffer depth N is explicitly left to
 * be tuned after real interrupt-latency measurement. 1024 ticks is the
 * default used here:
 *   ring      = 1024 ticks = 256 us
 *   half      =  512 ticks = 128 us refill deadline
 *   ISR rate  = 7.8 kHz
 *   RAM       = 4 KB
 *   reversal latency (ADR-006) = [N/2, N] x 250 ns = [128, 256] us
 * Tracked in Docs/PHASE1-STATUS.md; HV-05 and HV-14 inform the final value. */
#ifndef STEPGEN_RING_TICKS
#define STEPGEN_RING_TICKS          1024u
#endif
#define STEPGEN_HALF_TICKS          (STEPGEN_RING_TICKS / 2u)

/* Motion command queue. ADR-008 targets >=128 ms of buffered motion; the
 * byte size depends on the Phase 3 protocol, so this is a slot count.      */
#ifndef MOTION_SEGMENT_QUEUE_DEPTH
#define MOTION_SEGMENT_QUEUE_DEPTH  64u
#endif

/* ------------------------------------------------- pin map (PINOUT.md) -- */
/* ADR-005: all five STEP pins on GPIOA, sequential, one DMA stream, one
 * 32-bit BSRR write per tick, zero cross-axis skew.                        */
#define STEP_X_PIN                  8u        /* PA8  */
#define STEP_Y_PIN                  9u        /* PA9  */
#define STEP_Z_PIN                  10u       /* PA10 */
#define STEP_A_PIN                  11u       /* PA11 */
#define STEP_B_PIN                  12u       /* PA12 */
#define STEP_PINS_MASK_GPIOA        0x1F00u   /* PA8..PA12 */

#define DIR_X_PIN                   8u        /* PD8  */
#define DIR_Y_PIN                   9u        /* PD9  */
#define DIR_Z_PIN                   10u       /* PD10 */
#define DIR_A_PIN                   11u       /* PD11 */
#define DIR_B_PIN                   12u       /* PD12 */
#define DIR_PINS_MASK_GPIOD         0x1F00u   /* PD8..PD12 */

#define EN_PIN                      15u       /* PD15 */
#define EN_PIN_MASK_GPIOD           0x8000u

/* PINOUT.md, verified by the project owner: EN is ACTIVE HIGH.
 * HIGH = drivers enabled. Reset state must be LOW.                         */
#define CNC_EN_ACTIVE_HIGH          1

/* ------------------------------------------------------- bring-up ------ */
/* Run the on-target hardware self-tests (HV-00..HV-05) once at boot.
 * Off by default; turn it on during hardware bring-up. Safe either way -
 * the drives stay disabled and the timebase is stopped between tests. */
#ifndef CNC_RUN_SELFTEST_AT_BOOT
#define CNC_RUN_SELFTEST_AT_BOOT    0
#endif

/* STEP polarity is FIXED active-high (§4.1) and is not configurable.
 * DIR is active-high at the pin (§6), but the mapping from "positive axis
 * motion" to DIR level is machine configuration that §25 forbids assuming,
 * so it is a per-axis runtime parameter defaulting to positive => HIGH.    */

#endif /* CNC_MOTION_CONFIG_H */
