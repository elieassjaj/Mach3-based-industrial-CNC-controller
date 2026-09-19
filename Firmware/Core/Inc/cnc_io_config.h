/**
 * @file    cnc_io_config.h
 * @brief   CNC5AX-ETH output subsystem configuration (M9 relay/LEDs, M10 spindle).
 *
 * Values are tagged the same way as cnc_motion_config.h:
 *   FIXED    - mandated by Docs/PINOUT.md or Docs/FIRMWARE-ARCHITECTURE.md
 *   ADR      - set by a frozen Architecture Decision Record
 *   OPEN     - a documented default, not a decision
 */
#ifndef CNC_IO_CONFIG_H
#define CNC_IO_CONFIG_H

/* --------------------------------------------------- M9: digital out --- */
/* FIXED, Docs/PINOUT.md. All three are on GPIOB.
 *
 * The relay is ACTIVE HIGH - verified against the actual relay driver
 * circuit by the project owner, not derived from any document in this
 * repository, since no schematic for that stage exists here. PINOUT.md
 * also requires it to be driven LOW at boot and in any FAULT or
 * EMERGENCY_STOP, so the relay defaults off.                              */
#define RELAY_PIN                   8u        /* PB8  */
#define RELAY_PIN_MASK_GPIOB        (1u << RELAY_PIN)
#define CNC_RELAY_ACTIVE_HIGH       1

#define LED_RUN_PIN                 2u        /* PB2  */
#define LED_ERR_PIN                 1u        /* PB1  */
#define LED_RUN_MASK_GPIOB          (1u << LED_RUN_PIN)
#define LED_ERR_MASK_GPIOB          (1u << LED_ERR_PIN)

/* OPEN. No LED drive circuit is documented in this repository, so this is
 * the CubeMX-generated reset state's assumption (both LEDs driven LOW at
 * init) read as "LOW = off". If the board sinks through the LED instead,
 * flip this one line rather than editing the module.                      */
#define CNC_LED_ACTIVE_HIGH         1

/* How many output bits the protocol's out_mask/out_value pair addresses.
 * Docs/PROTOCOL.md §9: bit 0 is the relay, the rest are reserved. The
 * reserved bits are REJECTED rather than ignored - a host that sets a bit
 * this firmware does not implement must be told, not silently obeyed in
 * part. */
#define CNC_OUT_BIT_RELAY           (1u << 0)
#define CNC_OUT_MASK_SUPPORTED      CNC_OUT_BIT_RELAY

/* ------------------------------------------------------- M10: spindle -- */
/* FIXED, Docs/PINOUT.md: TIM3_CH1 on PB4, 10 kHz. PB4 defaults to NJTRST
 * on the STM32F407; this project uses SWD-only debug, which is what frees
 * the pin (FIRMWARE-ARCHITECTURE §29).                                    */
#define SPINDLE_PWM_HZ              10000u

/* TIM3 is on APB1: PCLK1 = 42 MHz with APB1 prescaler /4, so the timer
 * clock is 84 MHz by the RCC "x2 when APBx prescaler != 1" rule (RM0090
 * Figure 16). Same clock-tree assumption the spindle values in the .ioc
 * already encode.                                                        */
#ifndef SPINDLE_TIMER_CLK_HZ
#define SPINDLE_TIMER_CLK_HZ        84000000u
#endif

/* Counts per PWM period. 84 MHz / 10 kHz = 8400 exactly.
 *
 * REFINES the .ioc's PSC=83/ARR=99, which also gives exactly 10 kHz but
 * only 100 duty steps - coarser than the 1000 the wire format carries, so
 * a host asking for 12.3% would silently get 12%. Same prescaler-free
 * period, same frequency, 84x the resolution. The frequency is the thing
 * PINOUT.md fixes and it is unchanged; see ADR-016.                       */
#define SPINDLE_PWM_PERIOD_COUNTS   (SPINDLE_TIMER_CLK_HZ / SPINDLE_PWM_HZ)

/* Duty is commanded in per mille, 0..1000, which is the protocol's
 * spindle_pmille and a direct quantisation of the Mach3 SDK's own
 * MainPlanner->Spindle.ratio (0..1) - SDK/ncPod/MachDevImplementation.cpp.
 * Nothing here rescales it into RPM: the host owns the RPM<->ratio map
 * (SpindleFM::SetSpindleSpeed), and this device has no idea what spindle
 * is fitted.                                                             */
#define SPINDLE_PMILLE_MAX          1000u

/* ---------------------------------------------------------- LED blink -- */
/* OPEN. Blink periods in milliseconds, driven from the superloop's
 * millisecond timebase. Chosen to be distinguishable across a workshop,
 * not measured.                                                          */
#ifndef LED_BLINK_SLOW_MS
#define LED_BLINK_SLOW_MS           500u      /* READY: "armed, idle"     */
#endif
#ifndef LED_BLINK_FAST_MS
#define LED_BLINK_FAST_MS           125u      /* FAULT: "recoverable"     */
#endif

#endif /* CNC_IO_CONFIG_H */
