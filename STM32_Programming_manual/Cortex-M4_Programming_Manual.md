# Cortex-M4 Programming Manual

## Purpose

This document is the primary technical reference for the **ARM Cortex-M4 processor core** used inside the STM32F407VGT6.

Unlike the STM32F407 Reference Manual, which describes STM32-specific peripherals, this document focuses on the **Cortex-M4 CPU core, execution model, interrupts, exceptions, memory system, instruction set, and core-level features**.

Use this document when firmware behavior depends on the Cortex-M4 processor itself rather than on an STM32 peripheral.

---

## Use This Document For

- Cortex-M4 processor architecture
- CPU registers
- Program Status Registers
- Exception handling
- Interrupt behavior
- NVIC
- SysTick
- Fault handling
- Memory protection concepts
- Memory ordering
- Atomic operations
- Exclusive access instructions
- FPU
- Floating-point operations
- Instruction set
- Thumb-2 instructions
- Assembly-level programming
- CPU debugging
- Low-level startup code

---

## Important Areas for This Project

The STM32F407VGT6 uses an **ARM Cortex-M4** core.

The following sections are particularly relevant to this CNC motion-control project.

---

### NVIC and Interrupts

The Cortex-M4 NVIC is responsible for managing interrupts and exceptions.

This documentation should be consulted when working with:

- Interrupt priorities
- Interrupt preemption
- Interrupt pending/active states
- Nested interrupts
- Exception handling
- Interrupt enable/disable
- Critical sections

This is particularly important for time-sensitive operations such as:

- STEP generation
- Motion-control interrupts
- Timer interrupts
- Ethernet interrupts
- DMA interrupts
- High-speed input processing

STM32-specific interrupt sources and peripheral interrupt mapping should still be verified in the **STM32F407 Reference Manual** and device documentation.

---

### SysTick

SysTick is a Cortex-M4 core peripheral used for periodic system timing.

It can be used for:

- System tick generation
- Time bases
- Periodic software events
- RTOS tick generation

When using STM32 HAL, the SysTick configuration and HAL time base should be considered together with the STM32 HAL documentation.

---

### Exceptions and Fault Handling

Use this document when implementing or debugging:

- HardFault
- MemManage Fault
- BusFault
- UsageFault
- SVC
- PendSV
- SysTick exceptions

These mechanisms can be useful for diagnosing firmware failures and unexpected CPU behavior.

For example, a custom fault handler can inspect processor registers and determine where a firmware crash occurred.

---

### Memory Model

The Cortex-M4 memory model is important when developing:

- Interrupt-driven firmware
- DMA-based systems
- Shared data between interrupts and main code
- Concurrent software components
- High-performance peripheral handling

Special attention should be given to:

- Memory ordering
- Volatile accesses
- Exclusive accesses
- Synchronization instructions
- Data and instruction barriers

For STM32-specific memory addresses and memory regions, use the STM32F407 documentation.

---

### Data Synchronization Barriers

The Cortex-M4 provides instructions such as:

- `DMB`
- `DSB`
- `ISB`

These instructions control instruction and memory synchronization.

They may become important when dealing with:

- Interrupt control
- Peripheral registers
- DMA
- Memory-mapped hardware
- Low-level synchronization
- Critical timing operations

Do not add barriers arbitrarily. Their use should be based on the actual hardware/software synchronization requirement.

---

### Floating Point Unit

The Cortex-M4 in the STM32F407 includes a hardware floating-point unit.

Use this document when dealing with:

- Floating-point instructions
- FPU registers
- FPU configuration
- Floating-point exceptions
- Single-precision calculations

This can be relevant to CNC applications involving:

- Kinematic calculations
- Coordinate transformations
- Motion calculations
- Velocity calculations
- Acceleration calculations

However, floating-point calculations in timing-critical interrupt routines should be evaluated carefully because deterministic execution time is important for motion control.

---

### CPU Registers

The document describes the Cortex-M4 processor registers, including:

- General-purpose registers
- Program Counter (`PC`)
- Link Register (`LR`)
- Stack Pointer (`SP`)
- Program Status Registers
- Special-purpose registers
- FPU registers

This information is useful when writing:

- Startup code
- Context-switching code
- Fault handlers
- Assembly routines
- Low-level debugging tools

---

## Cortex-M4 vs STM32F407 Documentation

It is important to distinguish between the Cortex-M4 core and the STM32F407 microcontroller.

```text
STM32F407VGT6
│
├── Cortex-M4 CPU Core
│   ├── CPU registers
│   ├── NVIC
│   ├── SysTick
│   ├── Exceptions
│   ├── FPU
│   └── Instruction Set
│
└── STM32 Peripherals
    ├── GPIO
    ├── Timers
    ├── DMA
    ├── ADC
    ├── USART
    ├── SPI
    ├── CAN
    ├── Ethernet MAC
    └── RCC
```

### Use Cortex-M4 Programming Manual for:

```text
CPU
Interrupt architecture
Exceptions
NVIC
SysTick
FPU
Instruction set
Memory ordering
Core-level debugging
```

### Use STM32F407 Reference Manual for:

```text
GPIO
Timers
DMA
RCC
ADC
SPI
USART
CAN
Ethernet
STM32-specific peripheral registers
```

---

## Firmware Development Rule

When implementing core-level functionality:

1. Determine whether the problem concerns the Cortex-M4 core or an STM32 peripheral.
2. Use this document for Cortex-M4 architecture and behavior.
3. Use the STM32F407 Reference Manual for STM32-specific peripherals.
4. Use the STM32F407 Datasheet for pins, alternate functions, electrical specifications, and device-level information.
5. Check the STM32F405/407 Errata for known silicon limitations.

---

## Important Note for AI-Assisted Development

When generating STM32F407 firmware, AI should distinguish between:

**ARM Cortex-M4 behavior**

and

**STM32F407-specific behavior.**

Generic Cortex-M4 information must not be used to infer STM32-specific peripheral behavior.

For example:

- NVIC architecture → Cortex-M4 Programming Manual
- Timer configuration → STM32F407 Reference Manual
- GPIO alternate function → STM32F407 Datasheet
- Ethernet MAC → STM32F407 Reference Manual
- LAN8720A PHY behavior → LAN8720A documentation
- Known silicon limitations → STM32F405/407 Errata

---

## Documentation Priority

For core-level questions, use the following priority:

```text
ARM Cortex-M4 Programming Manual
            ↓
ARM Cortex-M4 Architecture Documentation
            ↓
STM32F407 Reference Manual
            ↓
STM32F407 Datasheet
            ↓
STM32F405/407 Errata
```

The official ARM and STMicroelectronics documentation should take priority over third-party tutorials, forum posts, and AI-generated explanations.

---

## Scope

This document describes the **Cortex-M4 processor core** and should not be considered a replacement for the STM32F407 Reference Manual or Datasheet.

For complete STM32F407 firmware development, this document should be used together with the other technical documents provided in this repository.
