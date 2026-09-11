# CNC5AX-ETH
## 5-Axis Ethernet Mach3 Industrial Motion Controller

CNC5AX-ETH is an open hardware and firmware project for a five-axis CNC motion controller built around the **STM32F407VGT6** and **LAN8720A** Ethernet PHY.

The controller is designed to interface with **Mach3** over Ethernet/UDP and generate deterministic hardware-assisted **STEP/DIR** motion for five independent axes: **X, Y, Z, A, and B**.

This repository is the project's central engineering reference. The documentation defines the system requirements and constraints; the `Firmware/` directory contains the STM32CubeIDE firmware project as implementation work progresses.

---

## Project Goals

- Five independent motion axes: X, Y, Z, A, B
- Mach3 as the primary host-control platform
- Ethernet connectivity through LAN8720A using RMII
- LwIP-based networking with UDP as the intended motion transport
- Deterministic hardware-assisted STEP/DIR generation
- Target STEP rate of up to **2 MHz per axis**
- Minimum guaranteed capability: **3 axes simultaneously at 2 MHz**, to be demonstrated on final hardware
- 15 active-low digital inputs on `PE0–PE14`
- Hardware interrupt / EXTI handling for digital inputs, with `PE2` assigned as E-STOP
- Dedicated 10 kHz spindle PWM output
- Relay output and Run/Error status indicators
- Real-time motion control isolated from Ethernet and other non-real-time workloads

The central architectural rule is:

> **Deterministic motion generation has priority over networking and background processing.**

---

## Hardware Platform

| Function | Configuration |
|---|---|
| MCU | STM32F407VGT6 |
| CPU | ARM Cortex-M4 with FPU |
| Ethernet PHY | LAN8720A |
| MCU Ethernet | Integrated STM32 Ethernet MAC |
| MAC ↔ PHY | RMII |
| External MCU clock | 8 MHz HSE crystal |
| Motion axes | X, Y, Z, A, B |
| Maximum target STEP rate | 2 MHz |
| Minimum simultaneous maximum-rate axes | 3 |
| Digital inputs | 15 × active-low, `PE0–PE14` |
| E-STOP | `PE2` |
| Spindle | Hardware PWM, 10 kHz target |
| Relay | 1 × digital output |
| Status | Run LED + Error LED |
| ADC | Not used |
| DAC | Not used |
| Firmware IDE | STM32CubeIDE |

The **authoritative MCU pin mapping is `Docs/PINOUT.md`**. The README intentionally does not duplicate the complete pin assignment so that the pinout has a single source of truth.

---

## System Architecture

```text
                         HOST PC
                            │
                            │ Mach3
                            ▼
                    Mach3 Integration
                            │
                            │ Ethernet / UDP
                            ▼
                        LAN8720A
                            │
                           RMII
                            ▼
                     STM32F407VGT6
                            │
                ┌───────────┴───────────┐
                │                       │
          Ethernet / LwIP          Motion System
                │                       │
           Protocol Layer        Motion Engine / Buffer
                │                       │
                └──────────────┬────────┘
                               ▼
                 DMA →  GPIOx->BSRR   ← Base Timer Update Event
                               │
                         STEP / DIR
                               │
              ┌────────┬───────┼───────┬────────┐
              ▼        ▼       ▼       ▼        ▼
              X        Y       Z       A        B

Digital Inputs ──► Safety / Input Manager ──► Motion / Fault State

Other Outputs:
  Relay
  Spindle PWM
  Run LED
  Error LED
```

The Ethernet path must deliver commands to the motion layer; it must **not** become the source of STEP pulse timing. STEP/DIR timing belongs to the motion subsystem and its hardware peripherals.

---

## Repository Structure

```text
Mach3-based-industrial-CNC-controller/
│
├── Docs/
│   ├── ETHERNET.md
│   ├── FIRMWARE-ARCHITECTURE.md
│   ├── MOTION-ENGINE.md
│   ├── PINOUT.md
│   └── SYSTEM-ARCHITECTURE.md
│
├── Firmware/
│   └── STM32CubeIDE firmware project
│
├── LAN8720A/
│   ├── LAN8720
│   ├── LAN8720-ETH-Board-Schematic.pdf
│   ├── LAN8720-ETH_example_Code(1).zip
│   └── LAN8720A_DataSheet-DS00002165.pdf
│
├── MACH3/
│   ├── Current Includes Files from Mach3 Software Develope Kit.zip
│   ├── Mach3 Software Development Kit(SDK).zip
│   └── SDK-README.MD
│
├── STM32_DOCs/
│   ├── Cortex-M4_Programming_Manual/
│   ├── Datasheet/
│   ├── Errata/
│   ├── Reference_Manual/
│   ├── application_note/
│   └── stm32_github_reference/
│
└── README.md
```

`Firmware/` is the implementation area for the STM32CubeIDE project. The final source/module structure must remain consistent with the architecture defined in `Docs/FIRMWARE-ARCHITECTURE.md`.

---

## Documentation Map

| Document | Purpose |
|---|---|
| [`Docs/SYSTEM-ARCHITECTURE.md`](Docs/SYSTEM-ARCHITECTURE.md) | High-level system and data-flow architecture |
| [`Docs/PINOUT.md`](Docs/PINOUT.md) | Authoritative STM32F407VGT6 pin assignments and I/O constraints |
| [`Docs/FIRMWARE-ARCHITECTURE.md`](Docs/FIRMWARE-ARCHITECTURE.md) | Firmware subsystem boundaries, priorities, real-time rules, safety and AI-development constraints |
| [`Docs/MOTION-ENGINE.md`](Docs/MOTION-ENGINE.md) | STEP/DIR timing requirements, multi-axis behavior, buffering and acceptance criteria |
| [`Docs/ETHERNET.md`](Docs/ETHERNET.md) | LAN8720A/RMII, Ethernet MAC/DMA, LwIP and UDP architecture |
| [`MACH3/SDK-README.MD`](MACH3/SDK-README.MD) | Rules and guidance for Mach3 SDK integration |

### Recommended Reading Order

1. `README.md`
2. `Docs/SYSTEM-ARCHITECTURE.md`
3. `Docs/PINOUT.md`
4. `Docs/FIRMWARE-ARCHITECTURE.md`
5. `Docs/MOTION-ENGINE.md`
6. `Docs/ETHERNET.md`
7. `MACH3/SDK-README.MD`
8. `STM32_DOCs/` whenever a decision depends on exact STM32 behavior

---

## Firmware Architecture

The firmware is divided logically into the following responsibilities:

```text
Firmware
├── STM32CubeIDE / CubeMX generated base
├── Hardware Abstraction
│   ├── GPIO
│   ├── Timers
│   ├── DMA
│   └── Ethernet MAC
├── Safety / Input Manager
├── Ethernet / Network
│   ├── PHY interface
│   ├── LwIP
│   └── UDP
├── Protocol Layer
├── Motion Engine
│   ├── Axis handling
│   ├── Motion state
│   ├── Planning / interpolation
│   └── STEP/DIR generation
├── Output Management
│   ├── Spindle PWM
│   ├── Relay
│   └── Status LEDs
├── Diagnostics
└── System / Fault Management
```

The exact source-file and module layout may evolve during implementation, but subsystem responsibilities must remain separated.

### Real-Time Priority

```text
1. Safety-critical inputs
2. Hard real-time motion / STEP-DIR timing
3. Motion-state processing
4. Ethernet / UDP / protocol processing
5. Diagnostics and other background work
```

Safety and motion timing must not be blocked by Ethernet callbacks, logging, large loops, blocking communication, or other non-real-time work.

---

## Motion Requirements

The motion engine has five required independent axes: **X, Y, Z, A, B**.

Fixed project-level requirements include:

- STEP is active-high and rising-edge sensitive.
- Target maximum STEP rate: **2 MHz** (`500 ns` nominal period).
- At least **3 axes simultaneously at 2 MHz** must be supported deterministically.
- The other two axes remain fully functional and are not optional.
- STEP generation must be hardware-assisted.
- DIR signals are active-high.
- Minimum DIR setup time: **200 ns**.
- Minimum DIR hold time: **200 ns**.
- STEP pulse width should maintain a practical margin above **100 ns** and must satisfy the connected driver's requirements.
- The 2 MHz requirement is not considered verified until it is measured on the final hardware.

The STEP generation architecture is fixed as DMA-driven GPIO BSRR updates.
The remaining implementation details include DMA stream allocation, BSRR buffering,
and the base timer used as the DMA request source.

---

## Ethernet Architecture

The intended communication path is:

```text
Mach3
  ↓
Ethernet / UDP
  ↓
LAN8720A
  ↓ RMII
STM32F407 Ethernet MAC / DMA
  ↓
LwIP
  ↓
Protocol Layer
  ↓
Motion Command Interface
  ↓
Motion Engine
```

Current project decisions include:

- LAN8720A PHY
- STM32 integrated Ethernet MAC
- RMII interface
- LwIP network stack
- UDP transport
- Static IPv4 configuration is intended for direct controller-host communication
- Ethernet processing must not determine the timing of STEP pulses

The final UDP packet format, port numbers, sequence/reliability mechanism, feedback format and some low-level Ethernet/DMA details remain implementation/TBD items unless explicitly verified elsewhere in the repository.

---

## Mach3 Integration

Mach3 is the primary host platform for CNC5AX-ETH.

The repository contains the Mach3 Software Development Kit and related include files. The SDK is treated as the authoritative technical reference for host-side integration.

Do not invent Mach3 SDK APIs, structures, constants or behavior. Any host-controller behavior that is not directly verified from the SDK or the implemented project should be treated as unverified rather than assumed.

---

## Hardware and Reference Sources

`STM32_DOCs/` contains the project's STM32 reference material, including:

- STM32F407VG datasheet
- STM32F407 reference manual (RM0090)
- Cortex-M4 Programming Manual (PM0214)
- STM32F405/407 device errata
- Relevant STM32 application notes
- References to ST's STM32CubeF4 and HAL driver repositories

`LAN8720A/` contains the Ethernet PHY datasheet, board schematic and example code used as project reference material.

These sources should be consulted before making hardware-dependent implementation decisions.

---

## AI-Assisted Development Rules

This repository is intended to be developed with AI assistance. The following rules are mandatory project principles:

1. Read the relevant project documentation before modifying firmware.
2. Treat `Docs/PINOUT.md` as the authoritative MCU pin mapping.
3. Treat the Mach3 SDK as the authority for Mach3-specific behavior.
4. Verify the DMA, GPIO/BSRR, DMA-request source, timer configuration , and interrupt decisions... .
5. Do not invent unspecified project values or APIs.
6. Keep safety and real-time motion independent of Ethernet/background processing.
7. Prefer deterministic hardware peripherals for timing-critical tasks.
8. Use HAL where overhead is acceptable; use LL/direct register access only where measured requirements justify it.
9. Preserve engineering margin rather than designing exactly at the theoretical limit.
10. Do not claim compliance with the 2 MHz multi-axis requirement without real-hardware validation.
11. When documentation, generated CubeMX configuration, firmware source, hardware schematic and official silicon documentation disagree, resolve the conflict using the project's documented source-of-truth hierarchy rather than guessing.

---

## Project Status

| Area | Status |
|---|---|
| Five-axis hardware concept | Defined |
| STM32F407VGT6 target | Defined |
| LAN8720A + RMII Ethernet hardware | Defined |
| Mach3 as primary host platform | Defined |
| Mach3 SDK reference set | Available |
| MCU pinout | Defined in `Docs/PINOUT.md` |
| 15 digital inputs | Defined |
| E-STOP input (`PE2`) | Defined |
| Spindle PWM | Defined — 10 kHz target |
| Relay / status outputs | Defined |
| Firmware architecture | Documented |
| Motion engine requirements | Documented |
| Ethernet/LwIP architecture | Documented |
| `Firmware/` STM32CubeIDE project | In preparation / implementation in progress |
| Final timer/DMA allocation | TBD / implementation decision |
| Final UDP application protocol | TBD |
| Verified 3-axis @ 2 MHz performance | Not yet validated |
| Production-ready firmware | Not yet complete |

---

## Design Philosophy

CNC5AX-ETH is designed around a traceable engineering workflow:

```text
Hardware / Official Documentation
              ↓
       Project Requirements
              ↓
      Firmware Architecture
              ↓
      CubeMX / STM32 Project
              ↓
       Firmware Implementation
              ↓
      Instrumented Measurement
              ↓
        Hardware Validation
```

The project should prefer measurable, verifiable behavior over assumptions or theoretical capability.

---

## License

Licensing for the hardware, firmware, Mach3 integration/plugin code, documentation, and included third-party reference material has not yet been fully defined in this README and should be reviewed before distribution.
