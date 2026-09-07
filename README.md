# CNC5AX-ETH

## 5-Axis Ethernet Mach3 Industrial Motion Controller

CNC5AX-ETH is an open hardware and firmware project for a **five-axis (X, Y, Z, A, B) Mach3 motion controller with Ethernet connectivity**, built around the **STM32F407VGT6** (ARM Cortex‑M4) and the **LAN8720A** Ethernet PHY.

The controller communicates with a host PC over **Ethernet/UDP** and is designed to generate deterministic STEP/DIR signals for CNC machine control, with **Mach3** as the primary host-side platform (via the Mach3 SDK included in this repository).

This README describes the **current repository structure**. It is an entry point into the project's documentation rather than a replacement for it — the authoritative technical detail for each subsystem lives in the linked documents under [`Docs/`](Docs), [`MACH3/`](MACH3), and [`STM32_DOCs/`](STM32_DOCs).

---

## Project Goals

- Five real motion axes: **X, Y, Z, A, B**
- Mach3-based motion control, using the official Mach3 SDK as the source of truth for host integration
- Ethernet connectivity via the LAN8720A PHY (RMII interface)
- UDP-based host communication using LwIP's RAW API
- Target STEP rate of up to **2 MHz**, with a guaranteed minimum of **3 axes simultaneously at 2 MHz**
- Low-latency, deterministic motion command transfer
- Hardware-timer/DMA-based STEP generation
- 15 active-low digital inputs (pull-up biased)
- One relay output and one dedicated spindle PWM output (10 kHz)
- STM32-based real-time control architecture, with safety-critical inputs and motion timing prioritized above networking

The motion-control subsystem is the highest-priority part of the firmware — Ethernet communication and other non-real-time functions must not compromise deterministic STEP/DIR generation.

---

## Repository Structure

```text
Mach3-based-industrial-CNC-controller/
│
├── Docs/
│   ├── FIRMWARE_ARCHITECTURE.md — CNC5AX-ETH Firmware Architecture.md
│   ├── MOTION_ENGINE.md — Motion Engine Requirements and Design Constraints.md
│   ├── PINOUT.md
│   ├── SYSTEM_ARCHITECTURE.md
│   └── ethernet.md
│
├── LAN8720A/
│   ├── LAN8720                              (short reference note)
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
│   ├── Cortex-M4_Programming_Manual/
│   │   ├── Cortex-M4_Programming_Manual.md
│   │   └── pm0214-stm32-cortexm4-mcus-and-mpus-programming-manual-stmicroelectronics.pdf
│   ├── Datasheet/
│   │   ├── STM32F407VG_Datasheet.md
│   │   └── datasheet_stm32f407vg.pdf
│   ├── Errata/
│   │   ├── Device_Errata.md
│   │   └── stm32f405407xx-device-errata-stmicroelectronics.pdf
│   ├── Reference_Manual/
│   │   ├── STM32F407_Reference_Manual___RM0090.md
│   │   └── ref_manual0090___stm32f407vgt6_advanced-armbased-32bit-mcus-stmicroelectronics.pdf
│   ├── application note/
│   │   ├── AN3966_readme.md
│   │   └── stm32_application note_AN3966.pdf
│   └── stm32_github_reference/
│       ├── STM32CubeF4.txt
│       └── stm32f4xx-hal-driver.txt
│
└── README.md
```

The repository currently contains **specification and reference documentation** (firmware architecture, motion engine, pinout, system architecture, Ethernet stack), plus the **Mach3 SDK**, **STM32F407 reference material**, and **LAN8720A hardware references**. Firmware source code has not been added yet — the documents above define what it must satisfy.

> **Note on file names:** two files under `Docs/` currently carry combined names (for example `FIRMWARE_ARCHITECTURE.md — CNC5AX-ETH Firmware Architecture.md`). The table below links to them using their exact current names; rename them to plain `.md` names if that was not intentional.

As the project grows, structural changes should be reflected here.

---

## Documentation Map

| Document | Covers |
|---|---|
| [`Docs/SYSTEM_ARCHITECTURE.md`](Docs/SYSTEM_ARCHITECTURE.md) | High-level data-flow diagram: Mach3 → Ethernet → STM32F407 → STEP/DIR/I/O |
| [`Docs/PINOUT.md`](Docs/PINOUT.md) | Authoritative MCU pin assignments (STEP, DIR, EN, inputs, relay, spindle, LEDs) |
| [`Docs/FIRMWARE_ARCHITECTURE.md`](<Docs/FIRMWARE_ARCHITECTURE.md>) | Full firmware architecture: subsystem breakdown, real-time priority model, HAL/LL policy, recommended source tree, AI-assisted development rules |
| [`Docs/MOTION_ENGINE.md`](<Docs/MOTION_ENGINE.md>) | Motion-engine requirements: STEP/DIR timing, 5-axis synchronization, buffering, safety, acceptance criteria |
| [`Docs/ethernet.md`](Docs/ethernet.md) | LAN8720A/RMII hardware, LwIP RAW API, UDP communication model, real-time networking constraints |
| [`MACH3/SDK_README.MD`](MACH3/SDK_README.MD) | Rules for using the Mach3 SDK as the source of truth for host-side integration |
| [`STM32_DOCs/Datasheet/STM32F407VG_Datasheet.md`](STM32_DOCs/Datasheet/STM32F407VG_Datasheet.md) | Guide to the STM32F407VG datasheet (device pins, electrical characteristics) |
| [`STM32_DOCs/Reference_Manual/STM32F407_Reference_Manual___RM0090.md`](STM32_DOCs/Reference_Manual/STM32F407_Reference_Manual___RM0090.md) | Guide to RM0090 (peripheral/register-level reference) |
| [`STM32_DOCs/Cortex-M4_Programming_Manual/Cortex-M4_Programming_Manual.md`](STM32_DOCs/Cortex-M4_Programming_Manual/Cortex-M4_Programming_Manual.md) | Guide to the Cortex-M4 core programming manual |
| [`STM32_DOCs/Errata/Device_Errata.md`](STM32_DOCs/Errata/Device_Errata.md) | Guide to the STM32F405/407 silicon errata |
| [`STM32_DOCs/application note/AN3966_readme.md`](<STM32_DOCs/application note/AN3966_readme.md>) | Guide to AN3966 — ST's LwIP/TCP-IP demonstration for STM32F4x7 |

### Suggested reading order

1. This README, for the repository map.
2. `Docs/SYSTEM_ARCHITECTURE.md`, for the high-level data flow.
3. `Docs/PINOUT.md`, for the authoritative pin assignments.
4. `Docs/FIRMWARE_ARCHITECTURE.md`, for firmware-wide constraints and subsystem boundaries.
5. `Docs/MOTION_ENGINE.md`, for motion-specific requirements.
6. `Docs/ethernet.md`, for the networking stack.
7. `MACH3/SDK_README.MD`, before touching any Mach3-side code.
8. `STM32_DOCs/`, on demand, whenever a decision depends on exact hardware behavior.

---

## System Architecture

```text
Mach3
  │
  │ Mach3 Plugin
  ▼
PC Ethernet
  │
  │ UDP
  ▼
LAN8720A
  │ RMII
  ▼
STM32F407
  │
  ├── STEP/DIR → X
  ├── STEP/DIR → Y
  ├── STEP/DIR → Z
  ├── STEP/DIR → A
  ├── STEP/DIR → B
  │
  ├── Inputs
  ├── Outputs
  ├── E-Stop
  └── Spindle
```

---

## Hardware

### Main Components

| Function | Component / Configuration |
|---|---|
| MCU | STM32F407VGT6 (ARM Cortex-M4 with FPU) |
| Ethernet PHY | LAN8720A |
| MAC–PHY Interface | RMII |
| Host Communication | Ethernet / UDP, LwIP RAW API |
| Motion Axes | 5 (X, Y, Z, A, B) |
| Target STEP Rate | Up to 2 MHz (≥ 3 axes guaranteed simultaneously) |
| STEP/DIR Interface | Direct GPIO (GPIOD), active-high STEP |
| Digital Inputs | 15, active-low, pull-up biased (GPIOE) |
| Relay Output | 1 |
| Spindle Output | 1 × PWM, 10 kHz |
| Status LEDs | Run + Error |
| ADC | None |
| DAC | None |
| IDE / Toolchain | STM32CubeIDE |
| Primary Host Platform | Mach3 (via included SDK) |

### Pinout Summary

Full authoritative detail is in [`Docs/PINOUT.md`](Docs/PINOUT.md); the current assignment is:

| Signal | Pin(s) | Notes |
|---|---|---|
| STEP X / Y / Z / A / B | PD0, PD1, PD2, PD3, PD4 | GPIOD, DMA/timer-driven, `STEP_PINS_MASK = 0x001F` |
| DIR X / Y / Z / A / B | PD8, PD9, PD10, PD11, PD12 | GPIOD |
| EN | PD15 | GPIOD |
| Spindle PWM | PB4 | e.g. TIM3_CH1, 10 kHz |
| Relay | PB8 | |
| LED (Run) | PB2 | |
| LED (Error) | PB1 | |
| Digital Inputs 0–14 | PE0…PE14 | Active-low, pull-up biased |
| Ethernet (RMII) | Fixed by LAN8720A hardware routing | Not itemized in `PINOUT.md` |

Do not infer pin behavior from naming conventions — `Docs/PINOUT.md` is the authoritative source.

### Digital Inputs

- **15 digital inputs**, active-low, pull-up biased, on `PE0`–`PE14`.
- Typical CNC uses include limit/home switches, probe, and E-stop, but the exact functional assignment of each input must follow the project's I/O map rather than being assumed.

### Outputs

- **Relay (`PB8`)** — general machine-control use; exact assignment is firmware/protocol dependent.
- **Spindle PWM (`PB4`)** — dedicated spindle control output, intended for a 10 kHz PWM frequency; duty-cycle scaling is a firmware detail.
- **Status LEDs (`PB2` run / `PB1` error)**.

---

## Motion Engine

Full requirements are defined in [`Docs/MOTION_ENGINE.md`](<Docs/MOTION_ENGINE.md — Motion Engine Requirements and Design Constraints.md>). Key fixed requirements:

- 5 real motion axes (X, Y, Z, A, B) — none of them optional.
- STEP is active-high; drivers are sensitive to the rising edge.
- Maximum target STEP rate: **2 MHz** (500 ns nominal period).
- At least **3 axes must run simultaneously at 2 MHz** with deterministic timing; the remaining 2 axes must stay fully functional, not architecturally deprioritized.
- Hardware-timer/DMA-based STEP generation is preferred over CPU bit-banging.
- The motion engine must remain independent of, and not blocked by, Ethernet/LwIP processing.
- Any 2 MHz (or multi-axis) performance claim must be validated on real hardware, not assumed from theoretical calculation.

---

## Ethernet Communication

Full architecture is defined in [`Docs/ethernet.md`](Docs/ethernet.md). Summary:

```text
Host PC → RJ45/Ethernet → LAN8720A PHY → RMII → STM32F407VGT6
    (Ethernet MAC + DMA) → LwIP (RAW API) → UDP → Motion Processing → STEP/DIR
```

- **Network stack:** LwIP, using the **RAW API** (callback-driven, low overhead) rather than sockets.
- **Transport:** UDP, static IPv4 configuration for direct controller–host communication.
- **Design rule:** the UDP receive callback must stay lightweight — it validates/parses a packet and hands it to the motion layer; it must never become the STEP generator itself.
- UDP guarantees neither delivery, ordering, nor duplicate protection — any such guarantee needed by the motion protocol must be implemented at the application layer.
- Ethernet/LwIP processing must never compromise STEP/DIR timing determinism.

---

## Mach3 Integration

Mach3 is the primary host-control platform for CNC5AX-ETH. The repository includes the **Mach3 Software Development Kit (SDK)** and related include files as the authoritative reference for the host-side integration:

```text
MACH3/
├── Current Includes Files from Mach3 Software Develope Kit.zip
├── Mach3 Software Development Kit(SDK).zip
└── SDK_README.MD
```

Per [`MACH3/SDK_README.MD`](MACH3/SDK_README.MD), the SDK is treated as an **authoritative technical reference**, not a source of assumptions:

- Read the SDK documentation, headers, and examples before writing or reviewing any Mach3-interface code.
- Never invent SDK functions, structures, constants, or behavior that cannot be verified in the SDK itself.
- If the SDK is ambiguous or incomplete, state what is known and what is uncertain rather than guessing.
- Keep SDK-provided functionality, project application logic, MCU firmware, and hardware interfaces clearly separated.

---

## Firmware Architecture

The full specification is in [`Docs/FIRMWARE_ARCHITECTURE.md`](<Docs/FIRMWARE_ARCHITECTURE.md — CNC5AX-ETH Firmware Architecture.md>). It defines constraints and responsibilities rather than a fixed implementation. Highlights:

### Real-Time Priority Model

```text
1. Safety-critical inputs      (E-stop, end-stops, other machine-critical inputs)
2. Hard real-time motion       (STEP generation, DIR timing, synchronization)
3. Motion-state processing     (buffers, trajectory, position management)
4. Communication               (Ethernet, UDP, protocol processing)
5. Non-real-time functions     (diagnostics, logging, status)
```

### Suggested Firmware Subsystems

```text
Firmware
├── Hardware Abstraction   (GPIO, Timers, DMA, Ethernet MAC, ...)
├── Safety / Input Manager (limits, E-stop, probe, other inputs)
├── Ethernet / Network     (PHY, driver, LwIP, UDP)
├── Protocol Layer         (packet validation, command decoding, status)
├── Motion Engine          (state, axes, planning, interpolation, position, STEP/DIR)
├── Output Management      (relay, spindle PWM, status indicators)
├── Diagnostics            (error reporting, debug, runtime diagnostics)
└── System Management      (initialization, state, fault management)
```

### Suggested Source Tree (post-implementation)

```text
Firmware/
├── Core/            (Inc/, Src/)
├── Drivers/         (Hardware/, Timer/, DMA/, Ethernet/)
├── Safety/
├── Motion/          (Axis/, Planner/, Interpolation/, StepGen/)
├── Communication/   (LwIP/, UDP/, Protocol/)
├── IO/              (DigitalInput/, Outputs/, Spindle/)
├── System/          (State/, Fault/, Diagnostics/)
└── Middlewares/
```

Neither breakdown is mandatory — the final module and file structure is chosen during implementation and must stay consistent with `Docs/FIRMWARE_ARCHITECTURE.md`.

---

## STM32 Reference Material

`STM32_DOCs/` holds the official STMicroelectronics references for the target device, each with a short `.md` guide describing what it's for and when to consult it:

| Folder | Contents |
|---|---|
| `Cortex-M4_Programming_Manual/` | PM0214 — Cortex-M4 core, execution model, interrupts, instruction set |
| `Datasheet/` | STM32F407VG datasheet — device pins, electrical characteristics, memory map |
| `Errata/` | STM32F405/407 device errata — known silicon issues and workarounds |
| `Reference_Manual/` | RM0090 — peripheral/register-level reference (GPIO, timers, DMA, Ethernet MAC, etc.) |
| `application note/` | AN3966 — ST's LwIP/TCP-IP demonstration for STM32F4x7 |
| `stm32_github_reference/` | Pointers to ST's official `STM32CubeF4` and `stm32f4xx-hal-driver` GitHub repositories |

## LAN8720A Reference Material

`LAN8720A/` holds the PHY datasheet, board schematic, and an example project for the Ethernet PHY used on this controller:

| File | Contents |
|---|---|
| `LAN8720A_DataSheet-DS00002165.pdf` | LAN8720A PHY datasheet |
| `LAN8720-ETH-Board-Schematic.pdf` | Ethernet board schematic |
| `LAN8720-ETH_example_Code(1).zip` | Example Ethernet code |
| `LAN8720` | Short note pointing to the above as the reference set for the PHY |

---

## Development Rules for AI-Assisted Development

This project is intended to be developed with AI assistance. The full, authoritative rule sets are in [`Docs/FIRMWARE_ARCHITECTURE.md`](<Docs/FIRMWARE_ARCHITECTURE.md — CNC5AX-ETH Firmware Architecture.md>) (§42) and [`Docs/MOTION_ENGINE.md`](<Docs/MOTION_ENGINE.md — Motion Engine Requirements and Design Constraints.md>) (§32); the summary below is not a substitute for reading them.

1. **Read before modifying.** Check existing documentation and verified implementation before making a change, and identify impact on hardware, firmware, timing, Ethernet, Mach3 integration, and I/O mapping.
2. **Mach3 behavior comes from the SDK** (`MACH3/SDK_README.MD`), not from assumptions or general programming knowledge.
3. **Hardware behavior comes from official documentation** (`STM32_DOCs/`, `Docs/PINOUT.md`) — never inferred from naming conventions alone.
4. **Safety has priority.** Safety-critical inputs must never wait behind Ethernet or background processing.
5. **Real-time motion must remain deterministic.** Networking and other background functions are designed around the motion engine, not the other way around.
6. **Do not invent missing specifications** — IP addresses, ports, packet layouts, timer/DMA/GPIO assignments, PWM frequency, etc. Unknown values are marked **TBD**, proposed explicitly, or derived from an authoritative source.
7. **Check structural changes against the rest of the system** (timers, GPIO, module structure, protocol, interrupt priorities, DMA) before treating them as local.
8. **Use STM32 HAL where its overhead is acceptable**; use LL drivers or direct register access for timing-critical or performance-sensitive code, based on measured requirements rather than preference.
9. **Do not claim hardware capability without validation.** A theoretical calculation (e.g. "3 axes × 2 MHz") is not sufficient — real hardware testing is required.

---

## Current Project Status

| Feature | Status |
|---|---|
| Five-axis controller (X, Y, Z, A, B) | Defined |
| STM32F407VGT6 target MCU | Defined |
| LAN8720A Ethernet PHY | Defined |
| Ethernet / UDP / LwIP (RAW API) | Defined |
| Mach3 as primary host platform | Defined |
| Mach3 SDK in repository | Available |
| Full MCU pinout (STEP/DIR/EN/inputs/outputs) | Defined — `Docs/PINOUT.md` |
| 15 digital inputs (active-low, `PE0`–`PE14`) | Defined |
| Relay output | Defined |
| Spindle PWM output (10 kHz) | Defined |
| STEP/DIR electrical interface | Single-ended GPIO (as currently documented) |
| Differential STEP/DIR line driver | Not currently documented |
| ADC / DAC | Not used |
| Firmware architecture specification | Documented — `Docs/FIRMWARE_ARCHITECTURE.md` |
| Motion engine specification | Documented — `Docs/MOTION_ENGINE.md` |
| Ethernet/LwIP architecture specification | Documented — `Docs/ethernet.md` |
| Firmware source code | Not yet implemented |
| Final timer/DMA allocation | TBD |
| Final UDP application protocol | TBD |
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

## License

License information has not yet been defined in this README.

The licensing status of the hardware design, firmware, Mach3 integration/plugin code, documentation, and included third-party SDK/reference files must be reviewed and documented separately before project distribution.
