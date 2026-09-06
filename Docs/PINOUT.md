# Hardware Pinout Definition

This document outlines the complete hardware pinout for the Mach3-Based 5-Axis Industrial CNC Controller. 

## STEP Pins (GPIOD 0..4 via DMA/Timer)
These pins are designated for high-speed pulse generation (up to 2 MHz) and must be controlled using DMA and hardware timers.
*   **STEP_X_PIN:** `PD0` (`GPIO_PIN_0`)
*   **STEP_Y_PIN:** `PD1` (`GPIO_PIN_1`)
*   **STEP_Z_PIN:** `PD2` (`GPIO_PIN_2`)
*   **STEP_A_PIN:** `PD3` (`GPIO_PIN_3`)
*   **STEP_B_PIN:** `PD4` (`GPIO_PIN_4`)
*   *Note:* The `STEP_PINS_MASK` is `(0x001F)` (Bits 0 to 4 for DMA BSRR transfers).
*   *Note : **All STEP pins (PD0–PD4) are connected to one of the PWM generation outputs of the timers.**

## DIRECTION Pins (GPIOD 8..12)
*   **DIR_X_PIN:** `PD8` (`GPIO_PIN_8`)
*   **DIR_Y_PIN:** `PD9` (`GPIO_PIN_9`)
*   **DIR_Z_PIN:** `PD10` (`GPIO_PIN_10`)
*   **DIR_A_PIN:** `PD11` (`GPIO_PIN_11`)
*   **DIR_B_PIN:** `PD12` (`GPIO_PIN_12`)
   
## EN pin (GPIOD)
* **EN_PIN:** `PD15` (`GPIO_PIN_15`)

## Peripherals & Outputs (GPIOB)
*   **SPINDLE_PWM_PIN:** `PB4` (`GPIO_PIN_4`) — *Must be configured as TIM Alternate Function (e.g., TIM3_CH1) with a 10 kHz frequency.*
*   **RELAY_PIN:** `PB8` (`GPIO_PIN_8`) 
*   **LED_RUN_PIN:** `PB2` (`GPIO_PIN_2`) 
*   **LED_ERROR_PIN:** `PB1` (`GPIO_PIN_1`) 

## Digital Inputs (Active-Low)
15 active-low digital inputs are utilized for microswitches, limit switches, e-stop, and probes. They are mapped consecutively on GPIOE:
*   **Inputs 0 to 14:** `PE0` through `PE14` (`GPIO_PIN_0` to `GPIO_PIN_14`).

## Ethernet Pins (Fixed Hardware Routing)
The pins related to Ethernet communication are not explicitly listed in this document. The system uses the LAN8720A PHY IC, and its connections to the STM32F407VGT6 microcontroller are fixed according to the standard hardware RMII interface layout.
