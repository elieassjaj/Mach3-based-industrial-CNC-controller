# STM32F407 Reference Manual — RM0090

## Purpose

This document is the primary technical reference for the **STM32F407VGT6 peripheral architecture and register-level behavior** used in this project.

It describes the internal bus architecture, memory organization, clocks, reset logic, GPIO, timers, DMA, communication interfaces, Ethernet MAC, ADC, DAC, and other STM32-specific peripherals.

Use this document whenever firmware implementation depends on the **configuration, registers, operating modes, timing, or behavior of an STM32F407 peripheral**.

> **Reference Manual:** RM0090  
> **Target device:** STM32F407VGT6  
> **Manufacturer:** STMicroelectronics

---

# What This Document Is For

The Reference Manual should be used for detailed STM32-specific firmware development.

Typical information found here includes:

- Peripheral architecture
- Memory and bus architecture
- Peripheral registers
- Register bit fields
- Reset values
- Clocking requirements
- Peripheral configuration sequences
- Interrupt sources
- DMA behavior
- Timer operation
- Communication peripheral behavior
- Ethernet MAC operation
- ADC/DAC operation
- Power and reset control
- Hardware timing information

---

# Most Important Sections for This Project

This project is an Ethernet-connected **industrial CNC motion controller** based on the STM32F407VGT6.

The following areas of RM0090 are therefore especially important.

---

## 1. Memory and Bus Architecture

Use this section when firmware performance or memory access behavior is important.

Topics include:

- System architecture
- AHB/APB buses
- Peripheral bus structure
- DMA bus access
- Memory regions
- Bus interconnection
- Peripheral memory mapping

This is useful for understanding how the CPU, DMA, timers, peripherals, SRAM, and Flash interact.

For high-performance motion control, DMA and timer behavior should be considered together with the bus architecture.

---

## 2. RCC — Reset and Clock Control

The RCC section is essential for configuring the STM32 clock tree.

Use it for:

- HSI
- HSE
- PLL
- System clock
- AHB clocks
- APB1 clocks
- APB2 clocks
- Peripheral clock enable
- Peripheral reset
- Clock prescalers

This is particularly important for:

- Timer frequency calculations
- STEP pulse generation
- PWM
- UART baud-rate generation
- SPI clock configuration
- Ethernet
- CPU performance

### Important

Do not calculate timer frequency from APB frequency alone.

STM32F4 timer clock behavior must be verified using the RCC and Timer sections of this Reference Manual.

---

# 3. GPIO

Use the GPIO section for:

- GPIO modes
- Input/output configuration
- Output type
- Pull-up / pull-down
- Output speed
- Alternate-function selection
- GPIO registers
- Port configuration

This is important for:

- STEP outputs
- DIR outputs
- Digital inputs
- PWM outputs
- Peripheral interfaces
- Control signals

For the physical pin number, package information, and alternate-function availability, also consult the **STM32F407 Datasheet**.

---

# 4. EXTI

Use the EXTI section when implementing:

- External interrupts
- Edge-triggered inputs
- Rising-edge detection
- Falling-edge detection
- GPIO-triggered interrupts
- Interrupt event routing

This may be relevant for CNC input signals, limit switches, fault inputs, or external timing signals.

The CPU-level interrupt architecture itself should be checked in the **Cortex-M4 Programming Manual**.

---

# 5. Timers

The timer chapters are among the most important parts of this Reference Manual for this project.

Use them for:

- Timer configuration
- Prescaler
- Auto-reload register
- Counter operation
- Output Compare
- Input Capture
- PWM
- One-pulse mode
- Encoder interface
- Timer interrupts
- Timer DMA requests
- Advanced timers
- General-purpose timers

### CNC-specific usage

Timers may be used for:

```text
Motion control
    ↓
STEP pulse generation
    ↓
Timer
    ├── Counter
    ├── ARR
    ├── CCR
    ├── Output Compare / PWM
    └── DMA or Interrupt
```

For any timing-critical STEP-generation implementation, verify the exact timer behavior from RM0090 rather than relying on generic STM32 examples.

---

# 6. DMA

DMA is particularly important for high-speed and deterministic firmware.

Use the DMA section for:

- DMA controller architecture
- Streams
- Channels
- Peripheral requests
- Memory-to-peripheral transfers
- Peripheral-to-memory transfers
- Memory-to-memory transfers
- Circular mode
- Data width
- Address increment
- Interrupt behavior
- FIFO configuration

Potential project applications include:

- Timer-assisted waveform generation
- Ethernet data movement
- High-speed peripheral transfers
- Motion-control data handling

When implementing DMA, verify the exact **stream/channel/request mapping** for the STM32F407.

---

# 7. NVIC and Interrupt-Related Peripherals

Peripheral interrupt sources and interrupt flags are documented throughout RM0090.

Use this Reference Manual to determine:

- Which peripheral generates an interrupt
- Which interrupt flag is involved
- Which interrupt-enable bit must be configured
- How a peripheral interrupt is cleared
- Which hardware event causes an interrupt

For the Cortex-M4 NVIC architecture, exception handling, priority behavior, and core-level interrupt behavior, use the **Cortex-M4 Programming Manual (PM0214)**.

---

# 8. ADC

Use the ADC sections for:

- ADC architecture
- Regular conversions
- Injected conversions
- Trigger sources
- Sampling time
- Conversion sequence
- Interrupts
- DMA
- Analog watchdog
- ADC registers

For electrical characteristics and ADC accuracy specifications, use the **STM32F407 Datasheet**.

---

# 9. DAC

Use the DAC section for:

- DAC channels
- Trigger configuration
- Output behavior
- Waveform generation
- DMA interaction
- DAC registers

Device-level electrical specifications must be taken from the Datasheet.

---

# 10. USART / UART

Use the USART sections for:

- Baud-rate configuration
- Word length
- Stop bits
- Parity
- Interrupts
- DMA
- Transmission
- Reception
- Status flags
- Hardware flow control

This is useful for debugging, MCU communication, configuration interfaces, and auxiliary communication.

---

# 11. SPI

Use the SPI section for:

- Master/slave configuration
- Clock polarity
- Clock phase
- Data size
- Baud-rate configuration
- Full-duplex operation
- Interrupts
- DMA
- Status flags

The actual GPIO pin and alternate-function selection should be verified with the Datasheet.

---

# 12. I2C

Use the I2C section for:

- Master operation
- Slave operation
- Addressing
- START/STOP conditions
- ACK/NACK
- Interrupts
- DMA
- Bus events
- Error handling

---

# 13. CAN

Use the CAN section for:

- CAN controller configuration
- Bit timing
- Filters
- Mailboxes
- Transmission
- Reception
- Interrupts
- Error handling
- Bus states

This may be useful for future or auxiliary control interfaces.

---

# 14. Ethernet MAC

Ethernet is one of the most important peripheral sections for this project.

The STM32F407 provides an Ethernet MAC that interfaces with an external PHY through **MII or RMII**.

Use RM0090 for:

- Ethernet MAC configuration
- MAC registers
- DMA
- RX descriptors
- TX descriptors
- Buffer management
- Interrupts
- Status registers
- RMII/MII configuration

For the external PHY, use the separate **LAN8720A documentation**.

The complete implementation should therefore be based on:

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

# 15. CRC

The CRC peripheral may be used for:

- Hardware CRC calculation
- Data integrity checking
- Communication protocols
- Packet validation

Use RM0090 for register behavior and peripheral configuration.

---

# 16. PWR

Use the PWR section for:

- Voltage-related control
- Power management
- Low-power modes
- Wake-up behavior
- Power-control registers
- Backup domain interaction

For an industrial CNC controller, this is also useful when implementing reliable reset and power-fault behavior.

---

# How to Use RM0090

When implementing a new STM32 peripheral feature, follow this workflow:

```text
1. Identify the required peripheral
            ↓
2. Check STM32F407 Datasheet
   for pin / Alternate Function
            ↓
3. Open the corresponding RM0090 chapter
            ↓
4. Read peripheral configuration and registers
            ↓
5. Verify clock requirements
            ↓
6. Verify interrupt / DMA behavior
            ↓
7. Check STM32F405/407 Errata
            ↓
8. Implement and test firmware
```

---

# Datasheet vs Reference Manual

These documents have different roles.

## Datasheet

Use the Datasheet for:

```text
Pinout
Package
Alternate Functions
Electrical Characteristics
5 V Tolerance
Operating Conditions
Memory Size
Peripheral Availability
Oscillator Requirements
Device-Level Specifications
```

## Reference Manual

Use RM0090 for:

```text
Peripheral Architecture
Registers
Register Bit Fields
Reset Values
Peripheral Configuration
Timer Modes
DMA
Interrupt Sources
Clock Configuration
Ethernet MAC
GPIO Registers
Peripheral Timing
Peripheral Operating Modes
```

### Example

```text
Question:
Can PA1 be used for TIM2_CH2?

→ Datasheet

Question:
How should TIM2_CH2 be configured as PWM?

→ RM0090

Question:
How does Cortex-M4 interrupt priority work?

→ Cortex-M4 Programming Manual

Question:
Is there a known silicon limitation?

→ STM32F405/407 Errata
```

---

# Relationship With HAL / LL

When using STM32Cube HAL or LL drivers, this Reference Manual should remain the **hardware-level authority**.

The software stack can be considered as:

```text
Application
    ↓
Project Firmware
    ↓
STM32 HAL / LL
    ↓
STM32 Peripheral Registers
    ↓
STM32F407 Hardware
```

If HAL/LL behavior is unclear, inspect the official HAL/LL source and then verify the resulting hardware behavior against RM0090.

Do not assume that a HAL function guarantees a particular hardware behavior without verifying the underlying peripheral documentation when the implementation is timing-critical.

---

# AI-Assisted Firmware Development

This document is intended to be one of the primary technical references available to AI systems working on the project firmware.

When generating code, AI should use RM0090 to verify:

- Peripheral availability
- Register names
- Register addresses
- Register bit fields
- Reset values
- Configuration sequences
- Clock dependencies
- Interrupt sources
- DMA mappings
- Timer behavior
- Peripheral timing
- Hardware operating modes

AI should not invent register names, bit fields, DMA mappings, interrupt mappings, or timer behavior from memory.

When a low-level implementation is uncertain, the relevant RM0090 section must be checked.

---

# Important Project-Specific Rules

### Rule 1 — Verify hardware before code

Firmware must match the actual PCB design.

The project-specific pin assignments are documented separately in:

```text
docs/pinout.md
```

---

### Rule 2 — Verify timers carefully

Because this is a CNC motion controller, timing-sensitive code must be based on the actual timer architecture and clock tree described in RM0090.

Do not use software delays for functions that require deterministic high-frequency timing when a suitable hardware peripheral can perform the task.

---

### Rule 3 — Verify DMA carefully

DMA configuration depends on the actual STM32F407 stream/channel/request mapping.

Do not copy DMA configuration from another STM32 family without verifying it against RM0090.

---

### Rule 4 — Ethernet requires multiple documents

Do not treat the STM32 Ethernet MAC and LAN8720A PHY as the same device.

```text
STM32F407
    ↓
Ethernet MAC
    ↓ RMII
LAN8720A
    ↓
RJ45 / Ethernet network
```

Each part has its own documentation.

---

### Rule 5 — Check Errata

If hardware behavior differs from the expected behavior described in RM0090, check the official **STM32F405/407 Errata Sheet** before assuming that the firmware is incorrect.

---

# Documentation Priority

For STM32F407-specific peripheral questions, use the following hierarchy:

```text
Official STMicroelectronics Documentation
                ↓
        STM32F407 Datasheet
                ↓
          RM0090 Reference Manual
                ↓
       STM32F405/407 Errata
                ↓
       Official HAL/LL Source
                ↓
       Official Application Notes
                ↓
      Third-Party Examples / Tutorials
                ↓
          AI-Generated Code
```

For Cortex-M4 core behavior, also consult the **Cortex-M4 / STM32 Cortex-M4 Programming Manual**.

---

# Scope

This README is a navigation and usage guide for:

`ref_manual0090___stm32f407vgt6_advanced-armbased-32bit-mcus-stmicroelectronics.pdf`

The PDF itself remains the authoritative source for the STM32F405/407 family peripheral architecture and register-level behavior.

This README does not replace the official Reference Manual.
