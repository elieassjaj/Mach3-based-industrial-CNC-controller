# Hardware Pinout Definition

This document outlines the complete hardware pinout for the Mach3-Based 5-Axis Industrial CNC Controller.

---

## STEP Pins (GPIOA 8..12, via DMA → GPIO BSRR)

These pins are designated for high-speed pulse generation (up to 2 MHz) and are controlled exclusively via a single DMA stream writing to `GPIOA->BSRR`, triggered by a shared base timer's Update event (see ADR-005 in `Docs/FIRMWARE-ARCHITECTURE.md`). They are configured as standard GPIO Output (Push-Pull) — **not** Alternate Function. Even though PA8/PA9/PA10/PA11 have valid TIM1_CH1–CH4 alternate functions, PWM/Output-Compare generation is intentionally not used here, because all four TIM1 channels share a single ARR (period register), which would force axes X, Y, Z, and A onto one common step frequency. `PA12` has no TIM1 PWM-channel alternate function at all (only `TIM1_ETR`), so it introduces no equivalent conflict. DMA-to-BSRR keeps every axis's frequency fully independent.

All five STEP pins are on the **same GPIO port (GPIOA)**, in sequential order, so a single DMA stream and a single 32-bit `GPIOA->BSRR` write can update any subset of the five axes in one transfer, with zero timing skew between axes.

- **STEP_X_PIN:** `PA8` (`GPIO_PIN_8`), port `GPIOA`
- **STEP_Y_PIN:** `PA9` (`GPIO_PIN_9`), port `GPIOA`
- **STEP_Z_PIN:** `PA10` (`GPIO_PIN_10`), port `GPIOA`
- **STEP_A_PIN:** `PA11` (`GPIO_PIN_11`), port `GPIOA`
- **STEP_B_PIN:** `PA12` (`GPIO_PIN_12`), port `GPIOA`
- *Note:* Single combined mask: `STEP_PINS_MASK_GPIOA` = `(0x1F00)` (Bits 8–12 → X, Y, Z, A, B → `GPIOA->BSRR`)
- *Note:* PWM/Output-Compare (Alternate Function) generation is **not** used for these pins, regardless of what their native alternate functions support.
- *Note:* `PC9` (the former `STEP_X` location) is now unused/reserved; do not configure it as `STEP_X` in the `.ioc`.

> **Hardware note:** This pin assignment was changed from an earlier revision (`STEP_X` on `PC9`, split across `GPIOA`+`GPIOC`) to consolidate all five STEP pins on `GPIOA`. Per this document's own rule (below), verify this against the actual PCB/schematic before finalizing — if `PC9` is already routed to the X-axis driver on fabricated hardware, this reassignment requires a hardware change, not just a firmware/`.ioc` change.
---

## DIRECTION Pins (GPIOD 8..12)

* **DIR_X_PIN:** `PD8` (`GPIO_PIN_8`)
* **DIR_Y_PIN:** `PD9` (`GPIO_PIN_9`)
* **DIR_Z_PIN:** `PD10` (`GPIO_PIN_10`)
* **DIR_A_PIN:** `PD11` (`GPIO_PIN_11`)
* **DIR_B_PIN:** `PD12` (`GPIO_PIN_12`)

---

## EN pin (GPIOD)

* **EN_PIN:** `PD15` (`GPIO_PIN_15`)

---

## Peripherals & Outputs (GPIOB)

* **SPINDLE_PWM_PIN:** `PB4` (`GPIO_PIN_4`) — Must be configured as TIM Alternate Function (e.g., TIM3_CH1) with a 10 kHz frequency. `PB4` defaults to the `NJTRST` function on the STM32F407; using it as `TIM3_CH1` requires the debug interface to run in SWD-only mode (see `Docs/FIRMWARE-ARCHITECTURE.md` §29). This project uses SWD-only debug/programming, so this pin assignment is valid as-is.
* **RELAY_PIN:** `PB8` (`GPIO_PIN_8`)
* **LED_RUN_PIN:** `PB2` (`GPIO_PIN_2`)
* **LED_ERROR_PIN:** `PB1` (`GPIO_PIN_1`)

---
## Digital Inputs (External Interrupts)

15 active-low digital inputs are connected to `PE0–PE14`. **All inputs must be configured as EXTI (External Interrupt) inputs** and handled through hardware interrupts rather than continuous polling.

* **Inputs:** `PE0–PE14`
* **E-STOP:** `PE2`
* **E-STOP** must have the highest appropriate interrupt priority and immediately trigger the emergency-stop handling.
* The interrupt trigger edge (`Rising`, `Falling`, or `Both`) must be selected according to the final hardware design.

---

## Ethernet / RMII Pins

The controller uses the integrated Ethernet MAC of the `STM32F407VGT6` together with the external `LAN8720A` PHY through the RMII interface.

The following Ethernet signals use fixed hardware routing between the STM32F407VGT6 and LAN8720A:

| Ethernet Signal | STM32F407VGT6 Pin | GPIO |
|---|---|---|
| **MDC** | `PC1` | `GPIO_PIN_1` |
| **REF_CLK** | `PA1` | `GPIO_PIN_1` |
| **MDIO** | `PA2` | `GPIO_PIN_2` |
| **CRS_DV** | `PA7` | `GPIO_PIN_7` |
| **RXD0** | `PC4` | `GPIO_PIN_4` |
| **RXD1** | `PC5` | `GPIO_PIN_5` |
| **PHY_NRST** | `PB0` | `GPIO_PIN_0` |
| **TX_EN** | `PB11` | `GPIO_PIN_11` |
| **TXD0** | `PB12` | `GPIO_PIN_12` |
| **TXD1** | `PB13` | `GPIO_PIN_13` |

### Ethernet Signal Description

* **MDC (`PC1`)** — Ethernet Management Data Clock.
* **MDIO (`PA2`)** — Ethernet Management Data I/O.
* **REF_CLK (`PA1`)** — RMII reference clock.
* **CRS_DV (`PA7`)** — RMII Carrier Sense / Receive Data Valid.
* **RXD0 (`PC4`)** — RMII Receive Data bit 0.
* **RXD1 (`PC5`)** — RMII Receive Data bit 1.
* **PHY_NRST (`PB0`)** — Hardware reset control for the LAN8720A PHY.
* **TX_EN (`PB11`)** — RMII transmit enable.
* **TXD0 (`PB12`)** — RMII transmit data bit 0.
* **TXD1 (`PB13`)** — RMII transmit data bit 1.

### Ethernet Hardware Notes

* The Ethernet PHY is **LAN8720A**.
* The STM32F407VGT6 provides the Ethernet MAC.
* Communication between the MCU and PHY uses **RMII**.
* The Ethernet pins listed above are **fixed hardware connections** and must not be reassigned in firmware unless the hardware design is changed. PA1 = ETH_RMII_REF_CLK (INPUT, sourced from onboard 50MHz oscillator on the PHY board, not MCU MCO). 

* The exact RMII reference-clock source/configuration is defined by the Ethernet hardware design and must be kept consistent with the LAN8720A configuration.

---

## Pin Assignment Summary

| Function | STM32F407VGT6 |
|---|---|
| STEP X | `PA8` |
| STEP Y | `PA9` |
| STEP Z | `PA10` |
| STEP A | `PA11` |
| STEP B | `PA12` |
| DIR X | `PD8` |
| DIR Y | `PD9` |
| DIR Z | `PD10` |
| DIR A | `PD11` |
| DIR B | `PD12` |
| Enable | `PD15` |
| Spindle PWM | `PB4` |
| Relay | `PB8` |
| Run LED | `PB2` |
| Error LED | `PB1` |
| Digital Inputs | `PE0–PE14` |
| Ethernet MDC | `PC1` |
| Ethernet REF_CLK | `PA1` |
| Ethernet MDIO | `PA2` |
| Ethernet CRS_DV | `PA7` |
| Ethernet RXD0 | `PC4` |
| Ethernet RXD1 | `PC5` |
| Ethernet PHY Reset | `PB0` |
| Ethernet TX_EN | `PB11` |
| Ethernet TXD0 | `PB12` |
| Ethernet TXD1 | `PB13` |

---

## Firmware Constraint

The firmware must treat the pin assignments in this document as the authoritative hardware pinout.

Any peripheral initialization, GPIO configuration, Ethernet MAC/RMII configuration, interrupt configuration, or timer configuration must respect these fixed assignments.

If a future firmware implementation requires a different pin assignment, the change must first be explicitly verified against the hardware schematic and PCB design.
