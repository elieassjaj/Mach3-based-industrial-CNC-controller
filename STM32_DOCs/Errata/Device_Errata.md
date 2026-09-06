# STM32F405/407 Device Errata

## Purpose

This document contains the official **STMicroelectronics Errata Sheet** for the STM32F405/415/407/417 device family.

It describes known limitations, unexpected behaviors, silicon issues, and recommended workarounds affecting the STM32F405/407 family.

For this project, this document is an important reference when the actual STM32F407VGT6 hardware behaves differently from the expected behavior described in the Datasheet or Reference Manual.

---

# Target Device

The project uses:

```text
MCU:        STM32F407VGT6
Family:     STM32F405/407
Core:       Arm Cortex-M4
Manufacturer: STMicroelectronics
```

The Errata Sheet applies to multiple devices in the STM32F405/407 family.

Always verify that the reported issue applies to the **exact device, revision, and operating conditions** used by the project.

---

# What This Document Is For

Use the Errata Sheet when investigating:

- Unexpected peripheral behavior
- Hardware-dependent software failures
- Peripheral initialization problems
- DMA problems
- Timer anomalies
- Ethernet issues
- Clock-related anomalies
- Interrupt-related behavior
- Low-power mode problems
- Debugging inconsistencies
- Silicon revision-specific limitations
- Unexpected reset behavior
- Peripheral interaction problems

The document may provide:

- Description of the limitation
- Conditions required to reproduce it
- Affected devices or revisions
- Impact on the application
- Recommended workaround

---

# Why the Errata Is Important

The STM32 Reference Manual describes the **intended hardware behavior**.

However, real silicon can contain implementation limitations that are not obvious from the normal peripheral documentation.

Therefore, the documentation hierarchy should be considered as:

```text
Expected hardware behavior
        ↓
STM32F407 Reference Manual
        ↓
Actual silicon limitations
        ↓
STM32F405/407 Errata
```

The Errata Sheet must be consulted when an observed hardware behavior cannot be explained by the normal peripheral documentation.

---

# When to Check the Errata

Check this document especially when:

### 1. The peripheral behaves differently than expected

For example:

```text
Reference Manual:
Peripheral should operate normally

Actual hardware:
Peripheral behaves incorrectly

        ↓

Check Errata
```

---

### 2. A problem only occurs under specific conditions

For example:

- High clock frequency
- Specific peripheral combinations
- Specific bus configurations
- DMA activity
- Interrupt activity
- Low-power modes
- Specific reset sequences
- Specific operating conditions

The Errata may describe exactly these conditions.

---

### 3. A workaround is required

Some silicon limitations cannot be fixed by hardware changes.

In these cases, ST may specify a software workaround.

Do not invent a workaround when an official workaround exists.

---

# Important Areas for This Project

This project is an industrial CNC motion controller using the STM32F407VGT6.

The following areas deserve particular attention when debugging.

---

## Timers

Timers are critical because they may be used for:

- STEP generation
- PWM generation
- Motion timing
- Input capture
- Output compare
- Periodic interrupts

If timer behavior is inconsistent or dependent on a particular operating condition, check the Errata before changing the motion-control algorithm.

---

## DMA

DMA is important for deterministic and high-speed operations.

Potential project uses include:

- Timer-assisted STEP generation
- Ethernet data handling
- Peripheral transfers
- High-speed buffers

Unexpected DMA behavior should be checked against the Errata, especially when multiple peripherals are active simultaneously.

---

## Ethernet

The project uses:

```text
STM32F407 Ethernet MAC
          ↓
         RMII
          ↓
       LAN8720A
```

If Ethernet communication exhibits unexplained problems, check both:

```text
STM32F405/407 Errata
+
LAN8720A documentation
```

Possible symptoms that should trigger an Errata check include:

- Unexpected packet loss
- DMA anomalies
- Ethernet initialization problems
- Descriptor-related problems
- Link or PHY interaction issues
- Interrupt problems

For Ethernet register definitions and normal operation, use **RM0090**.

---

## Clock System

Clock-related problems can affect:

- CPU operation
- Timer frequency
- Peripheral clocks
- Communication baud rates
- Ethernet
- External oscillator operation

If behavior changes unexpectedly after clock configuration changes, check the Errata in addition to the RCC and clock sections of the Reference Manual.

---

## Reset and Startup

Check the Errata when investigating:

- Unexpected reset behavior
- Startup problems
- Boot behavior
- Peripheral state after reset
- Brown-out or power-related behavior
- Debugger-dependent startup differences

---

## Interrupts

If an interrupt:

- Does not occur
- Occurs unexpectedly
- Is lost
- Remains pending
- Behaves differently under specific conditions

check the Errata before assuming the NVIC or firmware logic is necessarily incorrect.

For Cortex-M4 core-level interrupt architecture, consult the Cortex-M4 Programming Manual.

---

# Device Revision Matters

Some errata items apply only to specific silicon revisions.

Therefore, never assume:

```text
Issue exists on all STM32F407 devices
```

Instead verify:

```text
Device
   ↓
Exact MCU
   ↓
Silicon revision
   ↓
Errata applicability
```

When debugging a hardware-specific issue, identify the actual silicon revision of the MCU and compare it with the affected revisions listed in the Errata Sheet.

---

# Recommended Debugging Workflow

When unexpected hardware behavior is observed:

```text
Observed problem
       ↓
Check project firmware
       ↓
Check project hardware
       ↓
Check STM32F407 Datasheet
       ↓
Check STM32F407 Reference Manual
       ↓
Check STM32F405/407 Errata
       ↓
Check related external IC documentation
       ↓
Apply official workaround if applicable
       ↓
Test again
```

---

# Relationship With Other STM32 Documents

The Errata Sheet should be used together with the other documentation in this repository.

```text
                    STM32F407VGT6
                          │
          ┌───────────────┼────────────────┐
          ▼               ▼                ▼
      Datasheet      Reference Manual   Errata
          │               │                │
          │               │                └── Known silicon limitations
          │               │
          │               └── Intended peripheral behavior
          │
          └── Device / pin / electrical information
                          │
                          ▼
                  Cortex-M4 Manual
                          │
                          └── CPU core behavior
```

---

# Datasheet vs Reference Manual vs Errata

These documents have different roles.

## Datasheet

Use for:

```text
Pinout
Alternate Functions
Electrical Characteristics
Operating Conditions
Memory Size
Peripheral Availability
Package Information
Device Specifications
```

## Reference Manual

Use for:

```text
Peripheral Architecture
Registers
Bit Fields
Reset Values
Configuration
Timers
DMA
GPIO
RCC
Ethernet MAC
Peripheral Behavior
```

## Errata

Use for:

```text
Known Silicon Limitations
Unexpected Hardware Behavior
Affected Revisions
Conditions of Occurrence
Impact
Official Workarounds
```

### Example

```text
Question:
Can PA1 be used for a specific Alternate Function?

→ Datasheet

Question:
How is TIM2 configured?

→ Reference Manual

Question:
TIM2 behaves incorrectly under a specific condition.
Is this a known silicon issue?

→ Errata
```

---

# AI-Assisted Firmware Development

This document is particularly important for AI-assisted development because an AI system may otherwise assume that the Reference Manual completely describes every behavior of the physical MCU.

It does not.

When firmware produces unexplained hardware behavior, AI should check this Errata Sheet before introducing complex software workarounds.

AI should verify:

- Whether the issue applies to STM32F407
- Whether the issue applies to the exact silicon revision
- Required conditions for the issue
- Impact of the issue
- Official workaround
- Whether the workaround affects timing or performance

---

# Important Rule for AI

Do **not** use an Errata item as a general statement about the MCU.

An errata entry normally applies only under specific conditions.

The correct reasoning pattern is:

```text
Observed behavior
        ↓
Find matching Errata item
        ↓
Check affected device
        ↓
Check affected revision
        ↓
Check conditions
        ↓
Check whether project conditions match
        ↓
Apply official workaround if required
```

Do not assume that an errata item is relevant merely because the same peripheral is being used.

---

# Do Not Replace Normal Documentation With Errata

The Errata Sheet is not a replacement for:

- Datasheet
- Reference Manual
- Cortex-M4 Programming Manual
- HAL/LL documentation

It only describes known deviations or limitations.

For example:

```text
How does TIMx normally work?
        → Reference Manual

What are the electrical limits of a pin?
        → Datasheet

How does Cortex-M4 NVIC work?
        → Cortex-M4 Programming Manual

Is there a known silicon limitation affecting TIMx?
        → Errata
```

---

# Project-Specific Verification

Before applying an Errata workaround, verify the actual project configuration.

Relevant project documentation includes:

```text
docs/
├── pinout.md
├── system_architecture.md
├── ethernet.md
└── firmware_architecture.md
```

A workaround should not be added to the firmware simply because it exists in the Errata.

It should be applied only when:

1. The documented silicon limitation applies to the actual device.
2. The project configuration satisfies the conditions of the issue.
3. The workaround is relevant to the affected functionality.

---

# Priority of Information

For normal STM32F407 peripheral behavior:

```text
Datasheet
    ↓
Reference Manual
    ↓
HAL/LL Documentation / Source
```

For known silicon limitations:

```text
Official STM32F405/407 Errata
        ↓
Official Workaround
        ↓
Project Implementation
```

When the normal Reference Manual behavior and a specific applicable Errata item appear to conflict, the applicable silicon erratum must be taken into account.

---

# Scope

This README is a navigation and usage guide for:

`stm32f405407xx-device-errata-stmicroelectronics.pdf`

The original STMicroelectronics Errata Sheet remains the authoritative source for known STM32F405/407 silicon limitations and official workarounds.

This README does not replace the original Errata document.
