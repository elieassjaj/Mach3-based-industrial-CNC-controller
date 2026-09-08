# STM32F407VG Datasheet

## Purpose

This document is the primary device-level reference for the **STM32F407VGT6** microcontroller used in this project.

It describes the physical device, pinout, electrical characteristics, memory configuration, peripheral availability, alternate functions, operating conditions, and other device-specific specifications.

Use this document when firmware or hardware decisions depend on the **specific STM32F407VG device and its pins or electrical characteristics**.

---

# Target Device

```text
MCU:        STM32F407VGT6
Family:     STM32F405xx / STM32F407xx
Core:       Arm Cortex-M4 with FPU
Maximum CPU frequency: 168 MHz
Flash:      Up to 1 MB
SRAM:       Up to 192 KB + 4 KB Backup SRAM
Package:    LQFP-100
```

The STM32F407VGT6 is part of the STM32F405/407 family. Device capabilities, package options, and pin availability must always be checked against the exact part number used by the project.

---

# Use This Document For

Use this Datasheet when you need information about:

- Pinout
- Package
- GPIO availability
- Alternate functions
- Pin multiplexing
- GPIO electrical characteristics
- 5 V tolerance
- Input/output characteristics
- Maximum operating frequency
- Supply voltage
- Power supply requirements
- Reset and boot-related pins
- Oscillator requirements
- HSE / LSE characteristics
- Flash and SRAM size
- Peripheral availability
- ADC characteristics
- DAC characteristics
- Communication interface availability
- Ethernet capability
- Temperature range
- Absolute maximum ratings
- Operating conditions
- Unique device identification
- Mechanical package information

---

# Pinout and Alternate Functions

One of the most important uses of this Datasheet is determining which MCU pins can perform a particular function.

Before assigning a peripheral to a pin, verify:

1. The physical pin exists on the selected package.
2. The required peripheral is available on the selected device.
3. The desired Alternate Function is available on that pin.
4. The electrical characteristics of the pin are suitable for the intended signal.
5. There are no project-specific hardware conflicts.

For example:

```text
Peripheral signal
       ↓
Required Alternate Function
       ↓
Available MCU pin
       ↓
Package pin number
       ↓
Electrical characteristics
```

Do not infer pin assignments from examples designed for another STM32F4 device or another package.

---

# Important Areas for This Project

The STM32F407VGT6 is used as the main MCU in an industrial CNC controller.

The following Datasheet sections are therefore particularly important.

---

## GPIO

Use this document to verify:

- GPIO pin availability
- Input/output characteristics
- Output drive characteristics
- Pull-up / pull-down behavior
- 5 V tolerance
- Maximum GPIO speed
- Analog-capable pins
- Alternate-function availability

This is important for signals such as:

- STEP
- DIR
- Digital inputs
- Control outputs
- PWM outputs
- Communication interfaces

Detailed GPIO register configuration belongs to the **STM32F407 Reference Manual**.

---

## Timers and PWM

The Datasheet provides the device-level information needed to determine which timer peripherals and timer-capable pins are available.

This is especially important because the project uses hardware timers for time-critical operations such as:

- STEP generation
- PWM generation
- Motion timing
- Input capture
- Output compare

Use the Datasheet to determine:

```text
Which timer exists?
        ↓
Which channels are available?
        ↓
Which pins support the required Alternate Function?
```

Use the **Reference Manual** for the actual timer configuration, register behavior, clocking, PWM modes, and timing calculations.

---

## Ethernet

The STM32F407 includes a **10/100 Ethernet MAC with dedicated DMA and MII/RMII support**.

For this project, Ethernet is used together with the external **LAN8720A PHY**.

The Datasheet should be used to verify:

- Ethernet peripheral availability
- RMII-capable pins
- Alternate functions
- Electrical characteristics
- Relevant supply requirements

The complete Ethernet implementation requires additional documentation:

```text
STM32F407 Datasheet
        +
STM32F407 Reference Manual
        +
LAN8720A Datasheet
        +
Project Ethernet Documentation
```

---

## Clock and Oscillator

Use the Datasheet for device-level clock information such as:

- HSE frequency range
- LSE characteristics
- Internal oscillators
- Electrical oscillator requirements
- Crystal-related specifications

The Datasheet should be used to verify whether a selected external oscillator or crystal is electrically appropriate.

The detailed RCC clock-tree configuration belongs to the **STM32F407 Reference Manual**.

---

## Memory

Use this document to verify device memory specifications such as:

- Flash size
- SRAM size
- Backup SRAM
- CCM data RAM
- OTP memory

For this project, memory information can affect:

- Firmware size
- Motion-control buffers
- Network buffers
- DMA buffers
- Static data allocation
- Stack and heap planning

For exact memory addresses and memory-map details, use the **Reference Manual** and the device startup/linker configuration.

---

## ADC and DAC

Use the Datasheet for device-level ADC/DAC specifications such as:

- Number of ADCs
- ADC resolution
- Available channels
- Conversion characteristics
- Electrical limitations
- DAC availability

For this project, these peripherals may be used for monitoring or analog control functions.

Peripheral registers, configuration sequences, triggers, DMA operation, and detailed modes should be taken from the **Reference Manual**.

---

## Communication Peripherals

The Datasheet should be used to confirm which communication peripherals are present on the device and which pins support them.

Relevant interfaces include:

- USART
- UART
- SPI
- I2C
- CAN
- SDIO
- USB
- Ethernet

Use the Datasheet primarily for:

```text
Availability
Pin mapping
Alternate functions
Electrical characteristics
```

Use the Reference Manual for:

```text
Registers
Configuration
Interrupts
DMA
Peripheral behavior
Timing
```

---

# Electrical Characteristics

The Datasheet is the authoritative source for the MCU's electrical limits and operating conditions.

Use it when evaluating:

- GPIO voltage levels
- Input thresholds
- Output voltage
- Source/sink capability
- 5 V tolerance
- Supply voltage
- Operating temperature
- Current consumption
- ADC electrical characteristics
- Oscillator electrical characteristics

Software must not assume that a GPIO or peripheral is electrically compatible simply because its logic function exists.

---

# 5 V-Tolerant Pins

Some STM32F407 GPIOs support 5 V-tolerant operation under the conditions specified by ST.

When connecting external CNC control signals, always verify the exact pin characteristics before connecting a signal above the MCU supply voltage.

Do not assume:

```text
GPIO = 5 V tolerant
```

for every pin.

The exact pin characteristics must be checked in the Datasheet.

---

# Package Information

This document should also be used when PCB design depends on:

- LQFP package dimensions
- Pin numbering
- Pin positions
- Package mechanical dimensions
- Thermal characteristics
- Device marking

For the project hardware, the exact package must match the selected device:

```text
STM32F407VGT6
        ↓
LQFP-100
```

---

# Datasheet vs Reference Manual

These two documents serve different purposes.

## Use the Datasheet for

```text
Pinout
Alternate Functions
Electrical Characteristics
Absolute Maximum Ratings
Operating Conditions
Memory Size
Peripheral Availability
Package Information
Oscillator Requirements
Device-Level Specifications
```

## Use the Reference Manual for

```text
Peripheral Registers
GPIO Registers
Timer Configuration
DMA Configuration
RCC Configuration
USART/SPI/I2C/CAN Configuration
Ethernet MAC Configuration
Interrupt Sources
Peripheral Operating Modes
Register Bit Fields
Peripheral Timing
```

Example:

```text
Question:
"Can this pin be used as TIMx_CHy?"

        ↓
Datasheet

Question:
"How do I configure TIMx_CHy as PWM?"

        ↓
Reference Manual
```

---

# Datasheet vs Cortex-M4 Programming Manual

The Cortex-M4 Programming Manual describes the **CPU core**.

Use it for:

- CPU registers
- NVIC
- Exceptions
- SysTick
- FPU
- Cortex-M4 instruction set
- Memory ordering
- Core-level behavior

Use this Datasheet for:

- STM32F407 device specifications
- Pinout
- Electrical characteristics
- Peripheral availability
- Package information

---

# Firmware Development Workflow

When implementing a new hardware feature:

### Step 1 — Verify the device

Confirm that the required peripheral exists on the STM32F407VGT6.

### Step 2 — Select the pin

Use the Datasheet to verify:

- Pin number
- GPIO port
- Alternate Function
- Electrical characteristics

### Step 3 — Configure the peripheral

Use the STM32F407 Reference Manual for:

- Registers
- Clock configuration
- Peripheral modes
- Interrupts
- DMA
- Timing

### Step 4 — Check silicon limitations

Consult the STM32F405/407 Errata document.

### Step 5 — Verify project hardware

Check the project's own:

```text
docs/pinout.md
docs/system_architecture.md
docs/ethernet.md
```

The final implementation must match both the STM32 documentation and the actual PCB design.

---

# Important Note for AI-Assisted Development

This Datasheet should be treated as the primary source for **device-specific hardware facts**.

When generating firmware, AI should use this document to verify:

- Pin numbers
- GPIO ports
- Alternate Functions
- Peripheral availability
- Electrical characteristics
- 5 V tolerance
- Clock input requirements
- Memory sizes
- Device limitations
- Package-specific information

AI should **not** infer detailed peripheral behavior from the Datasheet when that information belongs to the Reference Manual.

For example:

```text
"Is PA1 capable of a specific Alternate Function?"
        → Datasheet

"What register configures that peripheral?"
        → Reference Manual

"What happens inside the Cortex-M4 when an interrupt occurs?"
        → Cortex-M4 Programming Manual

"Is there a known silicon problem?"
        → Errata

"How does the LAN8720A PHY behave?"
        → LAN8720A documentation
```

---

# Authority

For device-level STM32F407VGT6 specifications, this Datasheet takes priority over:

- Third-party websites
- Generic STM32 tutorials
- Forum posts
- Unverified examples
- AI-generated assumptions

When a third-party example conflicts with the official STMicroelectronics Datasheet, the official Datasheet takes priority.

---

# Relationship With Other Project Documents

```text
STM32F407VG Datasheet
        │
        ├── Device
        ├── Pins
        ├── Electrical limits
        ├── Alternate Functions
        └── Peripheral availability
                │
                ▼
STM32F407 Reference Manual
        │
        ├── Registers
        ├── Peripheral configuration
        ├── Timers
        ├── DMA
        ├── GPIO
        └── Ethernet MAC
                │
                ▼
Cortex-M4 Programming Manual
        │
        ├── CPU
        ├── NVIC
        ├── Exceptions
        └── FPU
                │
                ▼
STM32F405/407 Errata
        │
        └── Known silicon limitations
                │
                ▼
Project Documentation
        │
        ├── Pinout
        ├── System Architecture
        ├── Ethernet
        └── Firmware Architecture
```

---

# Document Scope

This file is a navigation and usage guide for:

`datasheet_stm32f407vg.pdf`

It does not replace the official Datasheet itself.

The official STMicroelectronics Datasheet remains the authoritative source for STM32F405/STM32F407 device-level specifications.
