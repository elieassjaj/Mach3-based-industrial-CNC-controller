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

### Prototype board vs. final PCB

Firmware bring-up happens on a **bench prototype** first:
[`prototype_test/prototype_sch.pdf`](prototype_test/prototype_sch.pdf). It
is not the product, and it differs from the final PCB where it matters:

| | Prototype | Final PCB |
|---|---|---|
| Ethernet PHY | Waveshare LAN8720 ETH Board **module** on a 2×7 header | LAN8720A **IC** on the board |
| PHY reset | The module resets itself (on-board RC); `PB0` reaches nothing | `nRST` on `PB0` |
| Motor drive | One StepStick-style driver socket, axis chosen with jumpers | External industrial drives |
| Inputs | `PE2` (E-STOP), `PE3`, `PE4` on switches; the rest tied inactive | All 15 conditioned |

The prototype schematic has been checked pin by pin against `Docs/PINOUT.md`
and the datasheets: **[`Docs/PROTOTYPE-BOARD.md`](Docs/PROTOTYPE-BOARD.md)**.
Every MCU pin number is correct and the Ethernet header matches the module
exactly. Four findings stop it working as drawn:

- **PR-1:** the crystal is 16 MHz, but the firmware is configured for
  8 MHz. The PLL would run out of spec, so **do not flash the current
  firmware as it is**.
- **PR-2:** the driver socket's `EN` is active low; the project's is
  active high.
- **PR-3:** the driver socket's RESET and SLEEP pins float.
- **PR-4:** the 250 ns STEP pulse is too short for a StepStick driver.
  `stepgen_configure_max_rate()` fixes it without a hardware change.

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
│   ├── FIRMWARE-IMPLEMENTATION-PLAN.md
│   ├── HARDWARE-VALIDATION.md
│   ├── MACH3-INTERFACE.md
│   ├── MOTION-ENGINE.md
│   ├── PHASE1-STATUS.md
│   ├── PHASE2-STATUS.md
│   ├── PHASE3-STATUS.md
│   ├── PHASE4-STATUS.md
│   ├── PHASE5-STATUS.md
│   ├── PINOUT.md
│   ├── PROTOCOL.md
│   ├── PROTOTYPE-BOARD.md          bench prototype: pin check and findings
│   ├── PRE-IMPLEMENTATION-DECISIONS.md
│   └── SYSTEM-ARCHITECTURE.md
│
├── Firmware/                       STM32CubeIDE project (CNC5AX-ETH)
│   ├── CNC5AX-ETH.ioc              CubeMX configuration
│   ├── Core/
│   │   ├── Inc/                    main.h, gpio.h, dma.h, tim.h, HAL/IT headers
│   │   ├── Src/                    main.c, gpio.c, dma.c, tim.c, stm32f4xx_it.c, ...
│   │   └── Startup/                startup_stm32f407vgtx.s
│   ├── Drivers/
│   │   ├── CMSIS/
│   │   └── STM32F4xx_HAL_Driver/
│   ├── LWIP/
│   │   ├── App/                    lwip.c, lwip.h
│   │   └── Target/                 ethernetif.c, lwipopts.h
│   ├── Middlewares/Third_Party/LwIP/
│   ├── Motion/                     portable STEP/DIR engine (Phase 1)
│   ├── Safety/                     portable input manager + E-STOP (Phase 4)
│   ├── IO/                         portable relay, LEDs, spindle PWM (Phase 5)
│   ├── Net/                        portable network config, link observer,
│   │                               C5P1 protocol (Phases 2-3)
│   ├── Platform/
│   │   ├── STM32F407/              hardware ports and on-target self-tests
│   │   └── Host/                   simulation port for the host test suite
│   ├── Tests/                      host verification
│   ├── Makefile                    test | arm | firmware
│   ├── MOTION-README.md
│   ├── NET-README.md
│   ├── SAFETY-README.md
│   ├── IO-README.md
│   ├── STM32F407VGTX_FLASH.ld
│   ├── STM32F407VGTX_RAM.ld
│   └── .project, .cproject, .mxproject, .settings/
│
├── Tools/
│   └── c5p1.py                     PC-side protocol client (no Mach3 needed)
│
├── prototype_test/
│   ├── prototype_sch.pdf           bench prototype schematic (not the product)
│   └── README.md
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
├── .gitignore
└── README.md
```

`Firmware/` is the implementation area for the STM32CubeIDE project. The final source/module structure must remain consistent with the architecture defined in `Docs/FIRMWARE-ARCHITECTURE.md`. Its current state is described under [Firmware Project State](#firmware-project-state).

---

## Documentation Map

| Document | Purpose |
|---|---|
| [`Docs/SYSTEM-ARCHITECTURE.md`](Docs/SYSTEM-ARCHITECTURE.md) | High-level system and data-flow architecture |
| [`Docs/PINOUT.md`](Docs/PINOUT.md) | Authoritative STM32F407VGT6 pin assignments and I/O constraints |
| [`Docs/FIRMWARE-ARCHITECTURE.md`](Docs/FIRMWARE-ARCHITECTURE.md) | Firmware subsystem boundaries, priorities, real-time rules, safety and AI-development constraints |
| [`Docs/MOTION-ENGINE.md`](Docs/MOTION-ENGINE.md) | STEP/DIR timing requirements, multi-axis behavior, buffering and acceptance criteria |
| [`Docs/ETHERNET.md`](Docs/ETHERNET.md) | LAN8720A/RMII, Ethernet MAC/DMA, LwIP and UDP architecture |
| [`Docs/HARDWARE-VALIDATION.md`](Docs/HARDWARE-VALIDATION.md) | Procedure for measuring the motion requirements on real hardware |
| [`Docs/PHASE1-STATUS.md`](Docs/PHASE1-STATUS.md) | Phase 1 (motion engine) results, assumptions, blockers and open risks |
| [`Docs/PHASE2-STATUS.md`](Docs/PHASE2-STATUS.md) | Phase 2 (Ethernet/LwIP) results, the reverted-static-IP finding, assumptions and risks |
| [`Docs/PHASE3-STATUS.md`](Docs/PHASE3-STATUS.md) | Phase 3 (UDP + protocol) results, decisions, risks and what Phase 4 must know |
| [`Docs/PHASE4-STATUS.md`](Docs/PHASE4-STATUS.md) | Phase 4 (digital inputs + E-STOP, M3) results, decisions, risks and what the plugin must know |
| [`Docs/PHASE5-STATUS.md`](Docs/PHASE5-STATUS.md) | Phase 5 (relay, LEDs, spindle PWM, M9/M10) results, decisions and risks |
| [`Docs/PROTOTYPE-BOARD.md`](Docs/PROTOTYPE-BOARD.md) | The bench prototype (`prototype_test/`): pin-by-pin check against `PINOUT.md`, findings PR-1..PR-10, the firmware settings it needs, and what carries forward to the final PCB |
| [`Docs/PROTOCOL.md`](Docs/PROTOCOL.md) | **Authoritative C5P1 wire format**: framing, opcodes, motion encoding, sequencing, flow control, status |
| [`Docs/MACH3-INTERFACE.md`](Docs/MACH3-INTERFACE.md) | How Mach3 drives an external motion device, established from the SDK |
| [`Docs/FIRMWARE-IMPLEMENTATION-PLAN.md`](Docs/FIRMWARE-IMPLEMENTATION-PLAN.md) | Firmware module breakdown, protocol dependencies, frozen-assumption list, implementation/verification order |
| [`Docs/PRE-IMPLEMENTATION-DECISIONS.md`](Docs/PRE-IMPLEMENTATION-DECISIONS.md) | Per-item decision list: requirement, what was undecided, dependents, resolution or explicit open question |
| [`MACH3/SDK-README.MD`](MACH3/SDK-README.MD) | Rules and guidance for Mach3 SDK integration |

### Recommended Reading Order

1. `README.md`
2. `Docs/SYSTEM-ARCHITECTURE.md`
3. `Docs/PINOUT.md`
4. `Docs/FIRMWARE-ARCHITECTURE.md`
5. `Docs/MOTION-ENGINE.md`
6. `Docs/ETHERNET.md`
7. `Docs/MACH3-INTERFACE.md`
8. `Docs/FIRMWARE-IMPLEMENTATION-PLAN.md`
9. `Docs/PRE-IMPLEMENTATION-DECISIONS.md`
10. `MACH3/SDK-README.MD`
11. `STM32_DOCs/` whenever a decision depends on exact STM32 behavior

---

## Firmware Project State

`Firmware/` contains an STM32CubeIDE project (`CNC5AX-ETH.ioc`, CubeMX 6.15.0, STM32Cube FW_F4 V1.28.0, target `STM32F407VGT6`/LQFP100) carrying the CubeMX-generated startup, clock, GPIO, DMA, timer, EXTI/NVIC and LwIP scaffolding, plus five implemented subsystems:

| Phase | Subsystem | State |
|---|---|---|
| 1 | Motion engine — STEP/DIR generation (`Motion/`, `Platform/STM32F407/`) | Implemented, 1150 host checks, integrated and linking. **Not hardware-validated** — see [`Docs/PHASE1-STATUS.md`](Docs/PHASE1-STATUS.md) |
| 2 | Ethernet/LwIP bring-up, M12 (`Net/`, `Platform/STM32F407/`) | Implemented, 130 host checks, whole firmware links. **No link has been established on real hardware** — see [`Docs/PHASE2-STATUS.md`](Docs/PHASE2-STATUS.md) |
| 3 | UDP + motion protocol, M13/M14 (`Net/`, `Tools/c5p1.py`) | Implemented, 316 host checks. **No datagram has crossed real Ethernet** — see [`Docs/PHASE3-STATUS.md`](Docs/PHASE3-STATUS.md) |
| 4 | Digital inputs + E-STOP, M3/M11 (`Safety/`, `Platform/STM32F407/`) | Implemented, 151 host checks. `STATUS.inputs` is live and ADR-010's E-STOP release interlock is enforced. **No button has been pressed on a board** — see [`Docs/PHASE4-STATUS.md`](Docs/PHASE4-STATUS.md) |
| 5 | Relay, status LEDs + spindle PWM, M9/M10 (`IO/`, `Platform/STM32F407/`) | Implemented, 1160 host checks. `OUTPUTS` is acted on, `flags.6` is set, and the outputs drop from the E-STOP interrupt. **No relay has switched on a board** — see [`Docs/PHASE5-STATUS.md`](Docs/PHASE5-STATUS.md) |

No phase may be reported as compliant with any requirement it has not measured on the board; all five are gated on the prototype PCB. `Firmware/MOTION-README.md`, `Firmware/NET-README.md`, `Firmware/SAFETY-README.md` and `Firmware/IO-README.md` cover building each subsystem. **Importing `Firmware/` into STM32CubeIDE needs no manual project setup**: `.cproject` carries the include paths and source folders for all four subsystems, in both the Debug and Release configurations.

**Every firmware module in the implementation plan now exists.** Still unwritten: the Mach3 host plugin. The protocol it needs is specified and implemented — [`Docs/PROTOCOL.md`](Docs/PROTOCOL.md) — and `Tools/c5p1.py` drives the controller from a PC without Mach3.

### What the generated configuration establishes

| Area | Configured state |
|---|---|
| Clock | 8 MHz HSE crystal → PLL (M=4, N=168, P=2) → 168 MHz SYSCLK; APB1 42 MHz (84 MHz timer clock), APB2 84 MHz (168 MHz timer clock); `FLASH_LATENCY_5`, voltage scale 1 |
| Debug | SWD only (`PA13`/`PA14`); JTAG not used, which is what frees `PB4` for spindle PWM |
| STEP | `PA8`–`PA12` = X, Y, Z, A, B — GPIO output push-pull, very-high speed, driven LOW at init |
| DIR / enable | `PD8`–`PD12` (DIR X–B), `PD15` (`MOTOR_EN`) — GPIO output push-pull, very-high speed, driven LOW at init. `MOTOR_EN` is **active high**, so LOW at init means the drives are disabled until firmware enables them |
| STEP base timer | TIM8, PSC = 0, ARR = 41 → 4.000 MHz update rate (250 ns tick); TIM8 global interrupt deliberately not enabled. (TIM2 per ADR-004, superseded by **ADR-012**: DMA1's peripheral port is not a bus-matrix master and cannot reach GPIO at all, and DMA2 takes timer requests only from TIM1/TIM8) |
| STEP DMA | `DMA2_Stream1` / Channel 7 on the `TIM8_UP` request — memory-to-peripheral, 32-bit both sides, circular, very-high priority, peripheral increment disabled |
| Spindle PWM | TIM3 CH1 on `PB4` (AF2), PSC = 83, ARR = 99 → 10.000 kHz, 0 % duty at init |
| Digital inputs | `PE0`–`PE14` (15 pins) as EXTI, rising **and** falling edge, `GPIO_NOPULL` — correct, the board provides external pull-ups |
| E-STOP | `PE2`, dedicated `EXTI2_IRQn` vector, pre-emption priority 0 |
| NVIC | Priority group 4; EXTI2 = 0, other EXTI = 1, `DMA2_Stream1` = 2, ETH = 5, SysTick = 15. The ETH-below-STEP-DMA ordering is a binding rule (ADR-012) checked at runtime by HV-22 |
| Ethernet | ETH peripheral in RMII mode on the nine pins listed in `Docs/PINOUT.md`; `PB0` (`PHY_NRST`) pulsed LOW for 150 µs then released before MAC init (**ADR-013**, Phase 2), internal pull-up enabled as a backup to the board's own 4.7 kΩ pull-up |
| PHY driver | LAN8742 (CubeMX's closest available option — see `Docs/ETHERNET.md` §2.2), auto-scans SMI address 0–31, decodes link/speed/duplex from register `0x1F` and applies it to the MAC via `HAL_ETH_SetMACConfig()` |
| LwIP | v2.1.2, `NO_SYS = 1`, RAW API only (`LWIP_NETCONN` / `LWIP_SOCKET` = 0), hardware checksum offload; ETH DMA descriptors placed in normal RAM (not CCM); `MX_LWIP_Process()` now called each superloop iteration |
| IP configuration | Static — controller `192.168.5.10`, PC `192.168.5.100`, mask `255.255.255.0`, no gateway (`LWIP_DHCP = 0`) — see `Docs/ETHERNET.md` §15 |
| Outputs | `PB8` relay, `PB2` run LED, `PB1` error LED — GPIO output push-pull |

These values match ADR-004 and ADR-005 in `Docs/FIRMWARE-ARCHITECTURE.md`, as corrected by ADR-012, and ADR-011/ADR-013 for the Ethernet entries.

### Known open items

Listed so they are not mistaken for working functionality:

- **Nothing above has been confirmed on hardware.** Every claim is from source, the linker map and host tests. The 2 MHz three-axis requirement (HV-11) and the Ethernet timing-isolation requirement (HV-15) are both unmeasured, and `Docs/MOTION-ENGINE.md` Rule 8 forbids claiming either until they are.
- The spindle PWM period is `PSC=0`/`ARR=8399` at runtime, not the `.ioc`'s `PSC=83`/`ARR=99`: identical 10.000 kHz, 8400 duty steps instead of 100 (ADR-016). The port re-applies it at boot, so a regeneration cannot revert it, and HV-51 reads it back.
- The LED drive polarity (`CNC_LED_ACTIVE_HIGH` = 1) is **confirmed active high for the prototype** by `prototype_test/prototype_sch.pdf`. The final PCB must keep it, or HV-55 will show otherwise.
- No Mach3 integration: the UDP and protocol layers exist (Phase 3) but nothing on the PC side speaks to them except `Tools/c5p1.py`.
- The digital-input debounce windows (3 ms, 50 ms) are documented defaults, not measurements — HV-45 is the test that replaces them (ADR-015).
- Probing has no capture path: `STATUS.inputs` is a 50 Hz state report, and a probe needs the position latched at the edge.
- Watchdog, heap/stack sizes and LwIP memory sizing are still at CubeMX defaults. MAC and IP are no longer: both come from `Firmware/Net/Inc/net_config.h` as of Phase 2.
- PHY SMI address is auto-scanned by the LAN8742 driver rather than assumed. On the prototype's Waveshare module the `PHYAD0` strap is tied high, so the address is **1** (`Docs/PROTOTYPE-BOARD.md` §5). The final PCB's strap is its own design choice, and the scan covers either.
- **PHY reset timing on the final PCB:** ADR-013's pulse on `PB0` meets `trstia` (100 µs) but is released a few milliseconds into boot. The LAN8720A also requires `nRST` held until ≥ 25 ms after power-up (`tpurstd`). Needs a firmware change and an `nRST` pull-down before the final board (`Docs/PROTOTYPE-BOARD.md` §7). The prototype is unaffected, because the module resets itself.
- **EN polarity is a build switch:** `CNC_EN_ACTIVE_HIGH` in `cnc_motion_config.h` (`1` active high, the default; `0` active low). It controls every write to `PD15`. It is open for the prototype, whose StepStick-style drivers are active low, and will be settled on the bench. The board's EN pull resistor and PD15's CubeMX initial level must match it; HV-06 checks the latter.
- A CubeMX regeneration remains the main hazard to both subsystems: it silently reverted the static IP configuration once already (`Docs/PHASE2-STATUS.md` §2). Run the self-tests after every regeneration.

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
The base timer used as the DMA request source is **TIM2** (see ADR-002 in
`Docs/FIRMWARE-ARCHITECTURE.md`). The remaining implementation details are the
exact DMA stream/channel allocation and BSRR buffering.

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

The STM32F407VG datasheet, RM0090 (Rev 22), and the STM32F405/407 errata are now valid, complete copies. The DMA1 request-mapping table (RM0090 Table 43) has been consulted directly and confirms the `TIM2_UP` stream/channel assignment recorded in ADR-002/ADR-004.

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
| `Firmware/` STM32CubeIDE project | Present — motion engine (Phase 1) and Ethernet bring-up (Phase 2) implemented (see [Firmware Project State](#firmware-project-state)) |
| STEP-DMA base timer & DMA allocation | **TIM8 + DMA2 Stream1/Channel7** (ADR-012, accepted). The original `TIM2`+`DMA1_Stream1` path could not work — RM0090 §2.1 and Figure 33 show DMA1's peripheral port is not a bus-matrix master, so it cannot reach `GPIOA->BSRR` at all. `.ioc` updated and verified |
| All 5 STEP axes on one GPIO port (`PA8`–`PA12`) | As decided (ADR-005) — one BSRR word per tick, zero cross-axis skew |
| Firmware execution model | Bare-metal, interrupt-driven superloop (ADR-003) — superloop body not yet written |
| STM32 Datasheet / RM0090 / Errata PDFs | Uploaded and verified |
| CubeMX `.ioc` peripheral configuration | Clock, GPIO, EXTI/NVIC, TIM8 + DMA2, TIM3 spindle PWM, Ethernet RMII and LwIP (static IP) configured |
| Ethernet bring-up code (M12, Phase 2) | **Implemented** — ADR-013 PHY reset pulse, ADR-011 MAC, static IP restored and made regeneration-proof, portable link observer. 130 host checks passing. **No link established on hardware** — see [`Docs/PHASE2-STATUS.md`](Docs/PHASE2-STATUS.md) |
| Motion engine / STEP generation code | **Implemented and integrated (Phase 1)** — DDA step generator, DMA→BSRR ring, ADR-006 CPU-timed DIR, ADR-010 state model. 1150 host checks passing |
| Full firmware build | **Links clean**, motion + network + inputs + outputs. `make firmware` verifies the whole image, including the generated files; Ethernet buffers confirmed in SRAM1 and the STEP ring in SRAM2 |
| Motion hardware validation plan | **Written** — [`Docs/HARDWARE-VALIDATION.md`](Docs/HARDWARE-VALIDATION.md), plus on-target self-tests HV-00..HV-05 |
| On-target self-tests | Implemented — HV-00..HV-05 (motion), HV-20..HV-25 (network), HV-40..HV-43 (inputs) and HV-50..HV-53 (outputs); **not yet run** (no hardware available) |
| Measured CPU headroom | **Not measured** — estimated ~60-70% duty at the 2 MHz ceiling; scales down with `stepgen_configure_max_rate()` (1 MHz ceiling ≈ half). HV-04 is the gate. See RISK-1 in [`Docs/PHASE1-STATUS.md`](Docs/PHASE1-STATUS.md) |
| Digital inputs / E-STOP path (M3, Phase 4) | **Implemented** — register-level EXTI, assert-immediately/release-filtered E-STOP, ADR-010's physical-release interlock enforced, `STATUS.inputs` live. 151 host checks passing. **Not hardware-validated** — HV-18 and HV-40..HV-45 — see [`Docs/PHASE4-STATUS.md`](Docs/PHASE4-STATUS.md) |
| Relay, status LEDs and spindle PWM (M9/M10, Phase 5) | **Implemented** — one interlock over both, every fault class drops them, E-STOP de-energises from the interrupt, LEDs report the ADR-010 state model. 1160 host checks passing. **Not hardware-validated** — HV-50..HV-55 — see [`Docs/PHASE5-STATUS.md`](Docs/PHASE5-STATUS.md) |
| Mach3 host-side plugin | Not started — and now the only thing left. Its obligations are recorded in [`Docs/MACH3-INTERFACE.md`](Docs/MACH3-INTERFACE.md) §8 and §9 |
| Final UDP application protocol | **C5P1 v1 on UDP 55010** — specified in [`Docs/PROTOCOL.md`](Docs/PROTOCOL.md), implemented and host-tested (ADR-014) |
| PC-side test client | `Tools/c5p1.py` — drives the controller without Mach3; verified byte-for-byte against the firmware codec |
| Verified 3-axis @ 2 MHz performance | **Not validated** — requires HV-11 on real hardware; explicitly not claimed |
| Motion timing under Ethernet load | **Not validated** — HV-15, the empirical form of the project's central rule. Runnable now that Phase 2 exists; needs the board |
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
