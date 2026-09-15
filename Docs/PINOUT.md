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

* **EN_PIN:** `PD15` (`GPIO_PIN_15`) — drive enable for the axis drivers.
* **Polarity: ACTIVE HIGH.** `HIGH` = drivers enabled, `LOW` = drivers disabled.
* Consequence for start-up safety: the reset/initial state of `PD15` must be `LOW`, so the drivers stay disabled until the firmware has completed initialization and reached a known-safe state (`Docs/FIRMWARE-ARCHITECTURE.md` §32). The generated `MX_GPIO_Init()` already drives `PD15` LOW before configuring it as an output, which satisfies this.
* The same applies to any fault or emergency-stop response: de-asserting (`LOW`) disables the drivers.

---

## Peripherals & Outputs (GPIOB)

* **SPINDLE_PWM_PIN:** `PB4` (`GPIO_PIN_4`) — Must be configured as TIM Alternate Function (e.g., TIM3_CH1) with a 10 kHz frequency. `PB4` defaults to the `NJTRST` function on the STM32F407; using it as `TIM3_CH1` requires the debug interface to run in SWD-only mode (see `Docs/FIRMWARE-ARCHITECTURE.md` §29). This project uses SWD-only debug/programming, so this pin assignment is valid as-is.
* **RELAY_PIN:** `PB8` (`GPIO_PIN_8`)
  * **Polarity: ACTIVE HIGH** (`HIGH` = relay energized, `LOW` = relay de-energized). Confirmed by the project owner against the actual relay driver circuit — no schematic for this stage exists in this repository (`LAN8720A/LAN8720-ETH-Board-Schematic.pdf` covers only the Ethernet PHY module, not the relay stage), so this value is a verified hardware fact, not derived from any document here. `PB8` must be driven `LOW` at boot and in any `FAULT`/`EMERGENCY_STOP` state so the relay defaults off.
* **LED_RUN_PIN:** `PB2` (`GPIO_PIN_2`)
* **LED_ERROR_PIN:** `PB1` (`GPIO_PIN_1`)

---
## Digital Inputs (External Interrupts)

15 active-low digital inputs are connected to `PE0–PE14`. **All inputs must be configured as EXTI (External Interrupt) inputs** and handled through hardware interrupts rather than continuous polling.

* **Inputs:** `PE0–PE14`
* **E-STOP:** `PE2`
* **E-STOP** must have the highest appropriate interrupt priority and immediately trigger the emergency-stop handling.
* **Pull resistors: external pull-ups are fitted on the board for all 15 inputs.** Internal pull-ups must therefore **not** be enabled in firmware; `GPIO_NOPULL` is the correct and intended configuration. An input reads `HIGH` when idle and is pulled `LOW` when asserted.
* **Interrupt trigger edge:** both edges (`GPIO_MODE_IT_RISING_FALLING`). Because the inputs are active-low, the falling edge is the assertion of the signal and the rising edge is its release; both are captured so the firmware can track the current state of each input rather than only its assertion.

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

* **RMII reference clock — verified against `LAN8720A/LAN8720-ETH-Board-Schematic.pdf`:** the PHY board carries its own **50 MHz oscillator**, whose output feeds both the LAN8720A's `XTAL1/CLKIN` input and the module header pin that connects to the MCU's `PA1`. The MCU therefore **receives** the 50 MHz RMII reference clock and must **not** generate it. No `MCO`/`MCO2` output is required or configured, and `PA1` is an input. Any example code that configures MCO to drive the PHY (including the ST STM3210C-EVAL demo shipped as the LAN8720A example) does **not** apply to this hardware.

* **PHY reset:** the module's `nRST` line has a 4.7 kΩ pull-up on the PHY board and is brought out to the header; it is driven by `PB0` (`PHY_NRST`) on the MCU. `nRST` is **active low**, so the firmware must drive `PB0` HIGH to release the PHY from reset before any MDIO access or Ethernet initialization.

* **PHY SMI (MDIO) address:** the LAN8720A's address is hardware-strapped by the `RXER/PHYAD0` pin to either 0 or 1 (datasheet §3.7.1, strap default `0b`). The vendor-supplied example for this module uses address `1`. Because the two sources disagree, the firmware should not hard-code an address — it should scan the SMI address range and detect the PHY, which is also what ST's PHY driver does.

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
