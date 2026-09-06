# CNC5AX-ETH

## 5-Axis Ethernet Mach3 Motion Controller

CNC5AX-ETH is an open hardware and firmware project for building a **five-axis Mach3 motion controller with Ethernet connectivity**.

The primary goal is to provide a dedicated motion-control hardware platform that communicates with a host PC over **Ethernet/UDP** and generates deterministic STEP/DIR signals for CNC machine control.

The controller is primarily intended for **CNC applications**, while the hardware architecture is designed to remain suitable for other motion-control applications where the same interfaces are useful.

---

## Project Goals

- Five real motion axes
- Mach3-based motion control
- Ethernet connectivity
- UDP-based host communication
- Target step output capability up to **2 MHz**
- Low-latency motion command transfer
- Hardware-timer-based motion generation
- Support for both single-ended and differential STEP/DIR interfaces
- 15 active-low digital inputs with pull-ups
- One relay output
- One dedicated spindle PWM output
- STM32-based real-time control architecture

The motion-control subsystem is the highest-priority part of the firmware. Ethernet communication and other non-real-time functions must not compromise deterministic STEP/DIR generation.

---

## High-Level Architecture

```text
                         HOST PC
                           |
                           | Ethernet / UDP
                           v
                    +---------------+
                    |   LAN8720A    |
                    |     PHY       |
                    +-------+-------+
                            | RMII
                            v
                    +---------------+
                    | STM32F407VGT6 |
                    |               |
                    | Ethernet MAC  |
                    | DMA           |
                    | Motion Engine |
                    | Timers        |
                    +-------+-------+
                            |
                +-----------+-----------+
                |           |           |
                v           v           v
             STEP/DIR      I/O      Spindle PWM
                |
        +-------+--------+
        |                |
        v                v
 Single-Ended       Differential
    Drivers          Drivers
   (TMC etc.)       (Leadshine etc.)
```

---

## Hardware

### Main Components

| Function | Component / Configuration |
|---|---|
| MCU | STM32F407VGT6 |
| Ethernet PHY | LAN8720A |
| Host Communication | Ethernet / UDP |
| Motion Axes | 5 real axes |
| Target Step Rate | Up to 2 MHz |
| Single-Ended STEP/DIR | Supported |
| Differential STEP/DIR | Supported |
| Differential Line Driver | AM26LS31 |
| Digital Inputs | 15 |
| Input Logic | Active-Low |
| Input Bias | Pull-Up |
| Relay Output | 1 |
| Spindle Output | 1 × PWM |
| ADC | None |
| DAC | None |
| Power Rails | 24 V / 5 V / 3.3 V |

---

## Motion Outputs

The controller supports two electrical STEP/DIR output approaches.

### Single-Ended

Single-ended STEP/DIR outputs are intended for drivers and modules that accept logic-level signals directly, including applications using drivers such as TMC-series devices.

```text
STM32
  |
  +-- STEP ----------> Driver
  |
  +-- DIR -----------> Driver
```

### Differential

For industrial stepper/servo drives that require differential signaling, the controller uses an **AM26LS31 differential line driver**.

```text
STM32
  |
  v
AM26LS31
  |
  +-- STEP+
  +-- STEP-
  +-- DIR+
  +-- DIR-
          |
          v
     Differential Driver
     (e.g. Leadshine)
```

The exact MCU pin mapping and timer allocation are hardware/firmware implementation details and must be taken from the authoritative hardware and firmware documentation rather than assumed from this README.

---

## Digital Inputs

The controller provides:

- **15 digital inputs**
- **Active-Low logic**
- **Pull-up biased inputs**

The exact purpose and pin assignment of each input must be defined in the authoritative hardware/I/O mapping documentation.

Potential CNC uses include limit, home, probe, emergency-stop, cycle-control, or other machine signals, but the final functional assignment must follow the project I/O map.

---

## Outputs

### Relay

One relay output is provided for general machine-control use.

The exact assignment is firmware/protocol dependent.

### Spindle PWM

One dedicated PWM output is provided for spindle control.

The exact PWM frequency, duty-cycle range, scaling, and electrical interface are firmware/hardware specifications and must be defined separately.

---

## Ethernet Communication

The host computer communicates with CNC5AX-ETH through Ethernet.

The Ethernet subsystem uses:

- LAN8720A PHY
- STM32 integrated Ethernet MAC
- RMII interface
- DMA-based Ethernet frame transfer
- LwIP
- UDP
- LwIP RAW API

The Ethernet subsystem is documented separately in:

**[`ethernet.md`](ethernet.md)**

### Communication Model

```text
Mach3
  |
  | Ethernet
  v
 UDP
  |
  v
CNC5AX-ETH
  |
  v
Motion Processing
  |
  v
STEP/DIR Hardware
```

LinuxCNC is **not currently a target platform** for this project.

---

## Mach3 Integration

Mach3 is the primary host-control platform for CNC5AX-ETH.

The repository includes the **Mach3 Software Development Kit (SDK)** and related SDK material used as the reference for developing the Mach3-side integration.

Current Mach3 resources include:

```text
MACH3/
├── Current Includes Files from Mach3 Software Develope Kit.zip
├── Mach3 Software Development Kit(SDK).zip
└── SDK_README.MD
```

The Mach3 integration must be treated as a separate interface layer from the embedded motion engine.

```text
Mach3
  |
  v
Mach3 Plugin / Integration Layer
  |
  | Ethernet / UDP
  v
CNC5AX-ETH
  |
  v
Motion Controller
```

The exact plugin architecture, packet protocol, synchronization mechanism, and host-side callback model must be derived from the actual SDK and project protocol documentation.

---

## Firmware Architecture

The final firmware architecture has **not been fixed in advance**.

The architecture is intentionally left open so that it can be designed during implementation based on:

- Real-time motion requirements
- Ethernet processing requirements
- Timer availability
- Interrupt latency
- DMA usage
- Memory constraints
- Mach3 communication requirements
- Maintainability

The implementation should maintain a clear separation between:

```text
Host Communication
        |
        v
Network / UDP
        |
        v
Protocol Parsing
        |
        v
Motion Control
        |
        v
Timer / STEP Generation
        |
        v
Physical Outputs
```

The final module structure must come from the implementation rather than being assumed by this README.

---

## Real-Time Motion Requirements

The main engineering requirement is deterministic motion generation.

Priority should be given to:

1. STEP pulse timing
2. DIR timing and synchronization
3. Multi-axis operation
4. Motion state handling
5. Safety-related inputs

Network communication, diagnostics, and non-critical processing must not introduce unacceptable timing jitter into the motion subsystem.

The target design supports five real axes with a target STEP rate up to **2 MHz**.

Any implementation claiming to satisfy the target 2 MHz rate must be validated on the final hardware with the relevant real-time and communication loads active.

---

## Software / Hardware Separation

### Hardware Layer

Responsible for:

- GPIO
- Timers
- Ethernet peripheral
- DMA
- Physical interfaces
- External line drivers
- I/O electrical behavior

### Communication Layer

Responsible for:

- Ethernet
- LwIP
- UDP
- Packet reception
- Packet transmission
- Protocol framing
- Packet validation

### Motion Layer

Responsible for:

- Motion commands
- Axis state
- STEP generation
- DIR generation
- Kinematic calculations
- Motion timing
- Safety handling

### Mach3 Integration Layer

Responsible for:

- Mach3-side communication
- Plugin/interface integration
- Command translation
- Feedback/status communication

No layer should silently take responsibility for functionality belonging to another layer.

---

## Repository Structure

The current GitHub repository structure is:

```text
Mach3-based-industrial-CNC-controller/
│
├── LAN8720A/
│   ├── LAN8720/
│   ├── LAN8720-ETH-Board-Schematic.pdf
│   ├── LAN8720-ETH_example_Code(1).zip
│   └── LAN8720A_DataSheet-DS00002165.pdf
│
├── MACH3/
│   ├── Current Includes Files from Mach3 Software Develope Kit.zip
│   ├── Mach3 Software Development Kit(SDK).zip
│   └── SDK_README.MD
│
├── STM32_DOCs/
│   ├── datasheet_stm32f407vg.pdf
│   └── ref_manual0090___stm32f407vgt6_advanced-armbased-32bit-mcus-stmicroelectronics.pdf
│
├── docs/
│   ├── pinout.md
│   ├── system architecture.md
│   └── ethernet.md
│
└── README.md
```

The repository currently contains Ethernet/LAN8720A resources, Mach3 SDK resources, and STM32F407 reference documents. The firmware source and additional project documentation can be added as implementation progresses.

As the project grows, structural changes must be documented and reflected in this README.

---

## Documentation

| Document | Description |
|---|---|
| [`docs/pinout.md`](docs/pinout.md) | MCU pin assignments, peripheral mapping, and hardware I/O |
| [`docs/system_architecture.md`](docs/system_architecture.md) | Overall hardware and firmware system architecture |
| [`docs/ethernet.md`](docs/ethernet.md) | LAN8720A, RMII, LwIP, UDP, DMA, and Ethernet architecture |
| [`MACH3/SDK_README.md`](MACH3/SDK_README.md) | Mach3 SDK Reference — API documentation and integration details for developing custom Mach3 hardware interfaces and plugins. |

Documentation files should describe the authoritative behavior of their corresponding subsystem.

---

## Development Rules for AI-Assisted Development

This project is intended to be developed with AI assistance. The AI must treat repository documentation, hardware definitions, and verified implementation details as authoritative project constraints.

### 1. Do not make major design decisions autonomously

Major architectural decisions must not be introduced without checking existing project constraints and identifying the impact on:

- Hardware
- Firmware
- Timing
- Ethernet
- Mach3 integration
- I/O mapping
- Compatibility

Any decision that can materially change the architecture must be explicitly identified before implementation.

### 2. Check structural changes before applying them

Any structural change must be checked against the rest of the system.

Examples include:

- Timer assignments
- GPIO assignments
- Firmware module structure
- Communication protocol
- Packet formats
- Interrupt priorities
- DMA usage
- Hardware abstraction

A change must not be treated as local until its dependencies and side effects have been checked.

### 3. Use HAL when performance is adequate

STM32 HAL is acceptable where its abstraction overhead does not negatively affect required speed or deterministic timing.

For timing-critical or performance-sensitive code, use:

- STM32 LL drivers
- Direct peripheral register access
- CMSIS-level access where appropriate

The choice must be based on the actual performance requirement rather than personal preference.

### 4. Protect the real-time motion path

The AI must avoid introducing code that unnecessarily delays or blocks:

- STEP generation
- DIR updates
- Motion timers
- Critical interrupts

Networking and other background functions must be designed around the motion engine.

### 5. Verify before changing hardware-dependent code

Before changing hardware-dependent firmware, check:

- MCU datasheet
- STM32 reference manual
- Schematic
- Pin mapping
- Peripheral availability
- Timer/channel mapping
- Electrical requirements

Do not infer hardware connections from naming conventions alone.

### 6. Do not invent missing specifications

If the repository does not define a value, the AI must not silently invent it.

Examples:

- IP addresses
- UDP port numbers
- Packet layouts
- Timer assignments
- GPIO assignments
- PWM frequency
- Axis mapping
- Safety logic
- Protocol fields

Unknown values must be marked **TBD**, proposed explicitly, or derived from an authoritative source.

---

## Current Project Status

This table represents the current project direction and defined requirements. It does not claim that every subsystem is already implemented or hardware-validated.

| Feature | Status |
|---|---|
| Five-axis controller concept | Defined |
| STM32F407VGT6 | Defined |
| LAN8720A Ethernet PHY | Defined |
| Ethernet / UDP communication | Defined |
| Mach3 as primary host platform | Defined |
| Mach3 SDK in repository | Available |
| 15 digital inputs | Defined |
| Single relay output | Defined |
| Spindle PWM output | Defined |
| Single-ended STEP/DIR | Defined |
| Differential STEP/DIR | Defined |
| AM26LS31 differential driver | Defined |
| ADC | Not used |
| DAC | Not used |
| LinuxCNC support | Not currently targeted |
| Final firmware architecture | TBD |
| Final UDP application protocol | TBD |
| Final timer allocation | TBD |
| Final GPIO/pin mapping documentation | TBD |
| Verified 2 MHz motion performance | TBD |

---

## Design Philosophy

CNC5AX-ETH should be developed around the following principle:

> **Deterministic motion generation comes first; networking and higher-level software exist to serve the motion engine.**

The project should favor:

- Deterministic timing
- Hardware peripherals
- Minimal unnecessary CPU overhead
- Clearly separated software layers
- Explicit interfaces
- Verifiable assumptions
- Hardware/software traceability
- Conservative architectural changes

The project should remain understandable both to engineers and to AI coding agents working on future firmware revisions.

---

## Reference Material

The repository currently contains reference material for:

- STM32F407VGT6
- STM32F407 reference manual
- LAN8720A
- Ethernet example code
- Ethernet board schematic
- Mach3 SDK

These reference materials should be consulted before implementing hardware-dependent functionality.

---

## License

License information has not yet been defined in this README.

The licensing status of:

- Hardware design
- Firmware
- Mach3 integration/plugin code
- Documentation
- Included third-party SDK/reference files

must be reviewed and documented separately before project distribution.
