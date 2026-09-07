# Hardware Pinout Definition

This document outlines the complete hardware pinout for the Mach3-Based 5-Axis Industrial CNC Controller.

---

## STEP Pins (GPIOD 0..4 via DMA/Timer)

These pins are designated for high-speed pulse generation (up to 2 MHz) and must be controlled using DMA and hardware timers.

* **STEP_X_PIN:** `PD0` (`GPIO_PIN_0`)
* **STEP_Y_PIN:** `PD1` (`GPIO_PIN_1`)
* **STEP_Z_PIN:** `PD2` (`GPIO_PIN_2`)
* **STEP_A_PIN:** `PD3` (`GPIO_PIN_3`)
* **STEP_B_PIN:** `PD4` (`GPIO_PIN_4`)
* **STEP_PINS_MASK:** `(0x001F)` — Bits 0 to 4 for DMA BSRR transfers.
* **Note:** All STEP pins (`PD0–PD4`) are connected to one of the PWM generation outputs of the timers.

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

* **SPINDLE_PWM_PIN:** `PB4` (`GPIO_PIN_4`) — Must be configured as TIM Alternate Function (e.g., TIM3_CH1) with a 10 kHz frequency.
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
* The Ethernet pins listed above are **fixed hardware connections** and must not be reassigned in firmware unless the hardware design is changed.
* `REF_CLK` is the RMII reference clock and is distinct from the MCU's `8 MHz HSE` clock.
* The exact RMII reference-clock source/configuration is defined by the Ethernet hardware design and must be kept consistent with the LAN8720A configuration.

---

## Pin Assignment Summary

| Function | STM32F407VGT6 |
|---|---|
| STEP X | `PD0` |
| STEP Y | `PD1` |
| STEP Z | `PD2` |
| STEP A | `PD3` |
| STEP B | `PD4` |
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
