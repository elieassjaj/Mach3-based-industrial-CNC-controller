# CNC5AX-ETH Firmware Architecture

## 1. Purpose

This document defines the **firmware architecture constraints, responsibilities, priorities, interfaces, and design principles** for the CNC5AX-ETH controller.

The purpose of this document is to define **what the firmware must guarantee and how major subsystems are separated**.

It intentionally does **not** prescribe every implementation detail.

The final implementation architecture must be selected by the implementing engineer or AI based on:

- Mach3 SDK behavior
- Project requirements
- STM32F407VGT6 capabilities
- STM32 official documentation
- Real-time timing requirements
- Code size and complexity
- CPU utilization
- Memory requirements
- Determinism
- Reliability
- Maintainability
- Measured hardware performance

The final implementation must provide substantial engineering margin rather than operating at the minimum theoretical limits.

---

# 2. Target Platform

```text
MCU:        STM32F407VGT6
Core:       ARM Cortex-M4 with FPU
Axes:       X, Y, Z, A, B
Ethernet:   LAN8720A via RMII
Network:    Ethernet / UDP / LwIP
IDE:        STM32CubeIDE
```

The firmware must remain fully developable, buildable, debuggable, and maintainable using **STM32CubeIDE**.

The final project should therefore maintain compatibility with the STM32CubeIDE project structure and toolchain.

---

# 3. High-Level Architecture

The firmware should follow the following logical structure:

```text
                         HOST PC
                           │
                           │ Ethernet / UDP
                           ▼
                  ┌───────────────────┐
                  │ Ethernet / LwIP   │
                  └─────────┬─────────┘
                            │
                            ▼
                  ┌───────────────────┐
                  │ Protocol Layer   │
                  └─────────┬─────────┘
                            │
                            ▼
                  ┌───────────────────┐
                  │   Motion Engine   │
                  └─────────┬─────────┘
                            │
              ┌─────────────┼─────────────┐
              ▼             ▼             ▼
        Motion Timing     Position      Safety
              │
              ▼
      STEP Event / BSRR Buffer
      ↓
     DMA
      ↓
GPIOx->BSRR
      ↓
     STEP
              │
       ┌──────┼──────┬──────┬──────┐
       ▼      ▼      ▼      ▼      ▼
       X      Y      Z      A      B
```

In parallel with the motion path:

```text
Digital Inputs
      │
      ▼
Input / Safety Manager
      │
      ├──────────────► Motion Engine
      │
      └──────────────► System State / Fault Handling


Other Outputs
      │
      ├── Relay
      ├── Spindle PWM
      └── Status LEDs
```

The exact software module names and internal implementation may differ.

The logical separation must remain.

---

# 4. Core Architectural Principle

The firmware must separate:

```text
Host Communication
        ↓
Network
        ↓
Protocol
        ↓
Motion Processing
        ↓
Real-Time Hardware Control
        ↓
Physical Outputs
```

No subsystem should silently take responsibility for functionality belonging to another subsystem.

For example:

```text
Ethernet callback
        X
        └── Must not directly become the STEP generator
```

Instead:

```text
Ethernet
   ↓
Protocol
   ↓
Motion command / buffer
   ↓
Motion Engine
   ↓
  DMA
   ↓
GPIOx->BSRR
   ↓
STEP       / DIR
```

---

# 5. Firmware Subsystems

The final firmware should be organized around clearly separated logical subsystems.

A suitable high-level decomposition is:

```text
Firmware
│
├── Hardware Abstraction
│   ├── GPIO
│   ├── Timers
│   ├── DMA
│   ├── Ethernet MAC
│   └── Other MCU peripherals
│
├── Safety / Input Manager
│   ├── Limit / End-stop inputs
│   ├── E-stop
│   ├── Probe
│   └── Other digital inputs
│
├── Ethernet / Network
│   ├── PHY interface
│   ├── Ethernet driver
│   ├── LwIP
│   └── UDP
│
├── Protocol Layer
│   ├── Packet validation
│   ├── Command decoding
│   └── Status generation
│
├── Motion Engine
│   ├── Motion state
│   ├── Axis handling
│   ├── Motion planning
│   ├── Interpolation
│   ├── Position tracking
│   └── STEP/DIR control
│
├── Output Management
│   ├── Relay
│   ├── Spindle PWM
│   └── Status indicators
│
├── Diagnostics
│   ├── Error reporting
│   ├── Debug support
│   └── Runtime diagnostics
│
└── System Management
    ├── Initialization
    ├── State management
    └── Fault management
```

This is a logical architecture.

The final source-file structure may be optimized during implementation.

---

# 6. Real-Time Priority Model

The firmware must prioritize operations according to their effect on machine safety and motion determinism.

The highest-priority subsystem is **safety-related input handling**, especially end-stop and emergency-related inputs.

The overall priority model should follow:

```text
1. Safety-critical inputs
   ├── Emergency stop
   ├── End-stop / limit signals
   └── Other safety-critical machine inputs

2. Hard real-time motion timing
   ├── STEP generation
   ├── DIR timing
   └── Motion synchronization

3. Motion-state processing
   ├── Motion buffers
   ├── Trajectory processing
   └── Position management

4. Communication
   ├── Ethernet
   ├── UDP
   └── Protocol processing

5. Non-real-time functions
   ├── Diagnostics
   ├── Logging
   └── Status / auxiliary processing
```

Safety-critical input events must not wait behind normal Ethernet or background processing.

The exact interrupt priorities and scheduling mechanism must be determined during implementation based on measured timing and the STM32F407 interrupt architecture.

---

# 7. Digital Input Architecture

The controller provides 15 active-low digital inputs:

```text
PE0
PE1
PE2
...
PE14
```

These pins are defined in:

```text
Docs/PINOUT.md
```

All 15 inputs are on GPIOE and are therefore suitable candidates for EXTI-based event detection.

The final implementation should evaluate **external interrupts for these inputs** because prompt response is important for machine control and safety.

The exact handling method must distinguish between:

- Safety-critical events
- Motion-control inputs
- Normal digital inputs

Not every input necessarily requires identical software treatment.

---

# 8. Safety Input Priority

E-Stop is handled via EXTI on PE2 (not polling).
End-stop and emergency-related inputs have priority over normal firmware functions.

The implementation must ensure that a safety-related input can be serviced with sufficiently low latency even when:

- Ethernet traffic is high
- Motion is running at high speed
- Multiple axes are active
- DMA is active
- Non-real-time tasks are executing

The exact safety response must be derived from the project requirements and Mach3 integration.

The firmware must not assume that a network command is sufficient to provide immediate machine safety.

Hardware and interrupt-level mechanisms should be used where appropriate.

---

# 9. STEP / DIR Motion Interface

The motion engine controls five real axes:

```text
X
Y
Z
A
B
```

The authoritative pin mapping is:

```text
Docs/PINOUT.md
```

The architecture must not duplicate the pin definitions here.

The known electrical requirements are:

```text
STEP = Active High
DIR  = Active High
```

Maximum reliable target STEP rate:

```text
2 MHz
```

Minimum guaranteed simultaneous performance:

```text
3 axes at 2 MHz
```

This is a minimum guaranteed capability, not an indication that the other two axes are optional.

The complete motion requirements are documented in:

```text
Docs/MOTION-ENGINE.md
```

---

# 10. STEP Generation Architecture

The STEP generator uses DMA-driven GPIO BSRR updates.

STEP pins are configured as standard GPIO outputs, not Timer Alternate
Function outputs. DMA transfers write precomputed BSRR values directly
to the corresponding GPIO port.

A base timer may be used as the DMA request/transfer trigger. The timer
does not generate the STEP waveform itself.

The implementation must select the architecture that provides the best balance of:

- Determinism
- Multi-axis synchronization
- Low jitter
- Low CPU utilization
- DMA efficiency
- DIR timing
- Scalability
- Reliability

PWM must not be selected merely because the physical pins support timer PWM.

---

# 11. Motion Engine Independence

The Motion Engine is the main hard real-time subsystem.

It must not depend directly on:

- LwIP callback execution time
- Ethernet packet arrival timing
- UART debugging
- Logging
- Blocking delays
- Non-deterministic background processing

The Motion Engine should consume already-validated motion information through a controlled interface.

Conceptually:

```text
Ethernet
   ↓
Packet
   ↓
Validation
   ↓
Motion Command
   ↓
Motion Buffer / Interface
   ↓
Motion Engine
   ↓
Hardware Timing
```

---

# 12. Mach3 SDK as Primary Host-Side Reference

The Mach3 SDK contained in:

```text
MACH3/
```

is the primary source for understanding the **host-side Mach3 integration and command semantics**.

The implementing AI must inspect the complete SDK material before defining firmware interfaces that depend on Mach3 behavior.

This includes:

- API functions
- Callbacks
- Structures
- Enumerations
- Constants
- Examples
- Initialization
- Shutdown behavior
- Timing behavior
- Threading requirements
- Memory ownership
- Communication mechanisms

The existing SDK guidance in:

```text
MACH3/SDK-README.MD
```

must be followed.

AI must never invent Mach3 SDK behavior.

---

# 13. Source-of-Truth Hierarchy

Different documents have authority over different parts of the project.

## Project Requirements

Project-specific requirements documented in:

```text
Docs/
```

take precedence over generic implementation assumptions.

## Mach3 Integration

For host-side Mach3 behavior:

```text
MACH3 SDK
```

is the primary source.

## STM32 Hardware

For device-specific hardware:

```text
STM32F407 Datasheet
STM32F407 Reference Manual
STM32F405/407 Errata
```

are authoritative.

## Cortex-M4 Core

For CPU-core behavior:

```text
Cortex-M4 Programming Manual
```

is authoritative.

## Ethernet PHY

For LAN8720A-specific behavior:

```text
LAN8720A Datasheet
and project Ethernet documentation
```

are authoritative.

## Software Drivers

For HAL/LL behavior:

```text
Official STM32F4 HAL/LL source and documentation
```

must be consulted.

---

# 14. RTOS vs Bare-Metal

**Decision made — see ADR-003 (Section 41).** The firmware uses a **Bare-Metal, interrupt-driven superloop**. No RTOS (FreeRTOS) is used. This section retains the original evaluation criteria for reference; ADR-003 records the reasoning, alternatives considered, and the conditions under which this decision should be revisited.

The evaluation was made by weighing:

- Real-time requirements
- STEP generation architecture
- Ethernet processing
- LwIP requirements
- Required concurrency
- Code size
- Memory usage
- Interrupt latency
- Maintenance
- Debugging complexity

Possible final architectures include:

```text
Bare Metal
```

or:

```text
FreeRTOS
```

or a hybrid architecture where hard real-time functions remain hardware/interrupt based while higher-level software uses RTOS tasks.

The chosen architecture must not compromise:

- Safety input latency
- STEP timing
- DIR timing
- Motion determinism

See ADR-003 (Section 41) for the final decision and its justification.

---

# 15. STM32CubeIDE Compatibility

The complete firmware must remain compatible with:

```text
STM32CubeIDE
```

The project should use standard STM32Cube project conventions where practical.

Generated configuration and source files must remain manageable within the CubeIDE workflow.

When STM32CubeMX/CubeIDE-generated code is used:

```text
USER CODE
```

sections must be respected.

The implementation should avoid modifying generated code in a way that makes future regeneration destructive or unreliable.

The `.ioc` configuration, generated initialization code, firmware source, and project settings should remain internally consistent.

---

# 16. HAL / LL / Register-Level Policy

The default abstraction level should be:

```text
STM32 HAL
```

Use:

```text
LL
```

or direct register-level access when there is a documented reason related to:

- Timing
- Performance
- Determinism
- Hardware control
- Missing HAL functionality
- Reduced overhead

Direct register manipulation should not be used merely because it is possible.

The decision should be based on actual requirements and measurements.

The preferred strategy is:

```text
HAL
  ↓
Use by default

LL
  ↓
Use where timing/performance requires it

Direct Register Access
  ↓
Use only where justified and documented
```

---

# 17. Ethernet Architecture

The Ethernet subsystem is:

```text
LAN8720A
    ↓
RMII
    ↓
STM32F407 Ethernet MAC
    ↓
Ethernet DMA
    ↓
Ethernet Driver
    ↓
LwIP
    ↓
UDP
    ↓
Protocol Layer
```

The detailed Ethernet architecture is documented in:

```text
Docs/ETHERNET.md
```

The firmware architecture must follow that document rather than defining a second independent Ethernet design.

Ethernet processing must not compromise motion timing.

---

# 18. Ethernet / Motion Boundary

The Ethernet layer is responsible for:

- Receiving packets
- Validating packets
- Passing valid commands to the appropriate subsystem
- Sending status/feedback data

The Motion Engine is responsible for:

- Motion interpretation
- Motion planning
- Multi-axis coordination
- Timing
- STEP generation
- DIR generation
- Position handling
- Motion safety behavior

Ethernet callbacks must remain short and must not contain large motion calculations or blocking operations.

The detailed packet semantics must be derived from the Mach3 SDK and project protocol design.

---

# 19. Protocol Layer

The protocol layer provides the boundary between the transport system and motion/control logic.

Conceptually:

```text
UDP
 ↓
Packet Validation
 ↓
Protocol Decode
 ↓
Command Object / Internal Representation
 ↓
Motion Engine
```

The protocol layer should be responsible for:

- Packet size validation
- Header validation
- Command validation
- Payload validation
- Sequence handling where required
- Data range checking
- Error detection
- Conversion into internal project-defined data structures

The Motion Engine should not need to understand raw network packet structure.

The exact protocol specification must be derived from the Mach3 integration and project requirements.

---

# 20. Input Manager

The digital input subsystem should be isolated into a dedicated module.

A suitable logical interface is:

```text
Digital Inputs
      ↓
Input Manager
      ↓
Debounce / filtering if required
      ↓
Event / state interface
      ↓
Motion / Safety / Application
```

The implementation must determine whether each input requires:

- EXTI interrupt
- Polling
- Software filtering
- Hardware filtering
- Event latching
- Immediate safety action
- Normal state monitoring

The approach must be selected according to the function and timing requirements of each input.

---

# 21. Output Manager

Non-motion outputs should be separated from the hard real-time STEP/DIR subsystem.

Relevant outputs include:

```text
Relay
Spindle PWM
RUN LED
ERROR LED
```

The exact mapping is defined by:

```text
Docs/PINOUT.md
```

The Output Manager should provide a clean interface to these outputs without allowing background software to interfere with STEP/DIR timing.

---

# 22. Spindle PWM

The spindle output is a dedicated hardware PWM output.

The architecture should treat spindle control as a separate subsystem from the motion pulse generator.

The exact:

- Frequency
- Duty-cycle range
- Scaling
- Command source
- Update mechanism

must be derived from the project requirements and Mach3 SDK.

---

# 23. Safety and Fault Management

A central safety/fault subsystem should be used if the final architecture determines that it improves clarity and reliability.

Potential conditions include:

```text
Emergency Stop
End-stop / Limit
Motion Fault
Ethernet Fault
Invalid Packet
Motion Buffer Underflow
DMA Fault
Timer Fault
System Initialization Failure
Other hardware/software faults
```

The final state machine and fault semantics are intentionally not predefined here.

The implementing AI must derive them from:

- Mach3 SDK
- Project requirements
- Motion requirements
- Safety behavior
- Hardware architecture

No undocumented safety behavior should be invented.

---

# 24. Global System State

A centralized system state model may be used if it improves consistency.

Possible logical states include:

```text
BOOT
INITIALIZING
READY
CONNECTED
IDLE
RUNNING
STOPPING
FAULT
EMERGENCY_STOP
```

These are examples, not mandatory names.

The final state model must be selected based on the actual requirements and subsystem interactions.

Subsystems may also maintain internal states where necessary.

---

# 25. Module Coupling

The firmware should use a modular and loosely coupled architecture.

Preferred relationship:

```text
Module A
   ↓
Public Interface
   ↓
Module B
```

Avoid:

```text
Module A
   ↓
Direct access to Module B internals
```

Modules should communicate through clearly defined interfaces.

Examples include:

- Function interfaces
- Events
- Queues
- Ring buffers
- Shared state
- Callbacks

The final mechanism is intentionally not fixed.

The selected mechanism must minimize:

- Race conditions
- Hidden dependencies
- Timing unpredictability
- Circular dependencies

---

# 26. Memory Management

Memory strategy is not fixed in advance.

The implementing AI must evaluate:

- Static allocation
- Stack usage
- Heap usage
- DMA buffer requirements
- LwIP buffers
- Motion buffers
- Network buffers
- Long-term fragmentation risk

However, the hard real-time motion path must not depend on operations with unbounded or unpredictable execution time.

Dynamic memory allocation inside timing-critical paths should be avoided unless a deterministic strategy is demonstrated.

The final memory strategy should be documented after architecture selection.

---

# 27. Interrupt Architecture

Interrupts must be designed according to actual real-time requirements.

The implementation should distinguish between:

```text
Safety-critical interrupts
Hard real-time motion interrupts
Peripheral interrupts
Communication interrupts
Background processing
```

High-priority interrupts must be short and deterministic.

Long calculations must not be performed inside safety or timing-critical interrupt handlers unless their worst-case execution time is demonstrated to be acceptable.

Interrupt priorities must be based on measured timing requirements, not arbitrary priority values.

---

# 28. DMA Usage

DMA should be used where it provides measurable benefit to:

- Reduce CPU load
- Improve determinism
- Maintain timing accuracy
- Increase data throughput

The final DMA architecture must be verified against STM32F407 DMA stream/channel/request constraints.

DMA ownership, buffer lifetime, alignment, cache behavior where applicable, and synchronization must be explicitly understood before implementation.

The exact DMA usage is an implementation decision.

---

# 29. Debugging

The firmware must remain fully debuggable through:

```text
SWD
```

SWD access must remain available throughout development. **Debug/program access is SWD-only — full JTAG is not used.** This is not only a tooling preference: `PB4` (`Docs/PINOUT.md`, Spindle PWM / `TIM3_CH1`) defaults to the `NJTRST` function on the STM32F407. Leaving the debug port in SWD-only mode (2-pin: `SWDIO`/`SWCLK` on `PA13`/`PA14`) is what allows `PB4` to be freely reconfigured as `TIM3_CH1` for Spindle PWM without contention from the JTAG function. If a future change ever required full JTAG, the Spindle PWM pin assignment on `PB4` would need to be revisited first.

The implementation should support debugging of:

- Motion state
- Axis position
- Input state
- Ethernet state
- Protocol state
- DMA state
- Timer state
- Fault state
- CPU exceptions

Debugging instrumentation must not interfere with hard real-time timing.

For example, high-frequency logging from a STEP interrupt should not be used in the production timing path.

---

# 30. Diagnostics

Diagnostics should be isolated from real-time motion.

Possible diagnostic mechanisms include:

```text
SWD
UART
Memory inspection
GPIO diagnostic signals
Counters
Status structures
```

The final implementation should prefer low-overhead diagnostics that can be disabled or reduced without changing the real-time architecture.

---

# 31. Initialization Order

The final startup sequence should respect hardware dependencies.

A suitable logical sequence is:

```text
Reset
  ↓
Clock / MCU initialization
  ↓
GPIO initialization
  ↓
Safety input initialization
  ↓
Timer / DMA initialization
  ↓
Ethernet / PHY / LwIP initialization
  ↓
Protocol initialization
  ↓
Motion subsystem initialization
  ↓
System self-check
  ↓
READY
```

The exact order may change depending on the final architecture.

Safety-related inputs should be made operational as early as safely possible.

Motion outputs should not be enabled before the system has established a valid and safe initial state.

---

# 32. Startup Safety

During boot and initialization:

- STEP outputs must not generate unintended pulses.
- DIR outputs must enter a known safe state.
- Safety inputs must be initialized correctly.
- Motion must remain disabled until initialization is complete.
- Ethernet initialization must not cause unintended motion commands.
- Invalid or stale network data must not start motion.

The final initialization sequence must guarantee a deterministic transition into the operational state.

---

# 33. Error Handling Principles

Errors should propagate through defined interfaces rather than being silently ignored.

The final design should distinguish between:

```text
Recoverable error
    ↓
Continue / Retry / Reinitialize

Motion-affecting fault
    ↓
Controlled motion response

Safety-critical fault
    ↓
Immediate safety response

Fatal system fault
    ↓
Safe fault state
```

The exact categories and responses must be determined during implementation.

---

# 34. Non-Blocking Design

The firmware should avoid blocking operations in:

- Safety-critical paths
- Motion timing paths
- Ethernet callbacks
- High-priority interrupt handlers

Examples of operations that require special caution include:

```text
HAL_Delay()
Large loops
Blocking communication
Dynamic memory allocation
Long calculations
Heavy logging
Waiting for network events
```

A blocking operation must only be used where its timing impact is understood and acceptable.

---

# 35. Host-to-Motion Data Flow

The intended logical flow is:

```text
Mach3
  ↓
Mach3 Plugin / Integration
  ↓
Ethernet / UDP
  ↓
LwIP
  ↓
Packet Validation
  ↓
Protocol Decode
  ↓
Motion Command Interface
  ↓
Motion Engine
     ↓
STEP Event / BSRR Buffer
     ↓
DMA
     ↓
GPIOx->BSRR
     ↓
STEP / DIR
```

The exact format and semantics of the motion command are not defined here.

They must be derived from the Mach3 SDK and final protocol design.

---

# 36. Motion-to-Host Data Flow

The reverse data path should be:

```text
Motion / System State
      ↓
Status / Feedback Generation
      ↓
Protocol Encoding
      ↓
UDP
      ↓
Ethernet
      ↓
Mach3
```

The exact feedback contents and update rates must be derived from the Mach3 SDK and project requirements.

---

# 37. Real-Time Resource Isolation

The architecture must ensure that real-time resources are protected from non-real-time workloads.

Resources requiring particular care include:

- Timer peripherals
- DMA streams
- Interrupt priorities
- CPU execution time
- RAM
- Ethernet buffers
- Motion buffers

The final architecture must identify and document resource conflicts before implementation.

---

# 38. Timing Budget

The implementing AI must calculate a timing budget for the final architecture.

The analysis should include:

```text
STEP timing
DIR timing
Safety input latency
Timer resolution
DMA transfer timing
Interrupt latency
Worst-case motion processing
Ethernet processing
Protocol processing
Buffer refill time
CPU utilization
```

The design must retain meaningful headroom.

A design that only works when every timing value is close to its theoretical limit is not considered sufficiently robust.

---

# 39. Performance Validation

Architecture decisions must be validated on actual hardware.

The following should be measured:

### Motion

- Maximum STEP rate
- Multi-axis STEP rate
- STEP jitter
- DIR setup timing
- DIR hold timing
- Synchronization

### Safety

- Input interrupt latency
- End-stop response latency
- Emergency-stop response latency

### Ethernet

- Maximum practical packet rate
- CPU usage under network load
- Buffer behavior
- Motion behavior under high network traffic

### CPU

- Idle load
- Typical motion load
- Maximum motion load
- Worst-case communication load

The theoretical design must not be treated as proof of compliance.

---

# 40. Architecture Selection Process

Before finalizing the architecture, the implementing AI should:

```text
1. Read project documentation
        ↓
2. Read Mach3 SDK
        ↓
3. Map required host behavior
        ↓
4. Analyze STM32F407 resources
        ↓
5. Map timers and DMA
        ↓
6. Analyze safety/input timing
        ↓
7. Analyze Ethernet workload
        ↓
8. Compare architecture options
        ↓
9. Select implementation
        ↓
10. Document the decision
        ↓
11. Implement
        ↓
12. Measure on hardware
        ↓
13. Validate requirements
```

Architecture should be selected **after analysis**, not before.

---

# 41. Architecture Decision Records

When an implementation decision is made that significantly affects the firmware architecture, it should be documented.

Examples:

```text
RTOS vs Bare-Metal
Timer allocation
DMA allocation
STEP generation method
Motion buffering
Protocol buffering
Interrupt priority scheme
Memory strategy
State machine design
Error handling strategy
```

For each significant decision, document:

```text
Decision
Reason
Alternatives considered
Timing impact
Memory impact
CPU impact
Risks
Validation result
```
### ADR-001 — STEP Pulse Generation Method

**Decision:** STEP pulses for all five axes (`PA8`–`PA12`, per ADR-005) are generated via a shared base timer whose Update event triggers DMA transfers directly into `GPIOA->BSRR`. The pins are configured as standard GPIO Output (Push-Pull), **not** Alternate Function — native PWM/Output-Compare generation is intentionally not used.

*(Historical note: the original version of this decision placed STEP_X on `PC9`, split across `GPIOA`+`GPIOC` with two DMA streams. ADR-005 consolidated all five STEP pins onto `GPIOA`/a single stream; the paragraphs below are updated to match, but the core decision — DMA-to-BSRR instead of PWM/Output-Compare — is unchanged.)*

**Reason:** PA8–PA11 correspond to TIM1_CH1–CH4, which share a single ARR (period) register. Native PWM/Output-Compare would therefore force axes X, Y, Z, and A onto one common STEP frequency, which conflicts with the project requirement that each axis run at an independently commanded step rate (e.g. during a coordinated multi-axis move). DMA-to-BSRR decouples each axis's effective frequency from any single timer's shared period.

**Alternatives considered:**
- *Native PWM/Output-Compare per channel* — rejected: shared-ARR constraint above.
- *Output-Compare toggle mode with independent per-channel CCR updates via DMA* — technically possible, but requires a separate DMA request/stream and state machine per channel, increasing complexity and DMA budget with no timing benefit over the BSRR method.

**Timing impact:** Each axis's STEP frequency becomes fully independent of the others. Jitter is bounded by base-timer resolution and DMA transfer latency — exact figures **TBD**, pending hardware measurement (Section 39).

**Memory impact:** One DMA buffer (single `GPIOA` port, per ADR-005), sized to the interpolation tick depth — exact size **TBD**.

**CPU impact:** Near-zero during steady-state pulse generation; CPU only refills buffers at a lower, batched rate.

**Risks:** None specific to DMA stream count after ADR-005 (single stream); Ethernet DMA independence confirmed in ADR-002/ADR-004 (separate DMA engine entirely).

**Validation result:** TBD — pending real-hardware testing (≥3 axes simultaneously at 2 MHz, per Section 39).

---

### ADR-002 — STEP-Generation Base Timer & DMA Allocation

**Decision:** `TIM2` is the shared base timer whose Update event triggers the STEP-generation DMA transfer established in ADR-001. `TIM2`'s Update-event DMA request (`TIM2_UP`) is serviced through **DMA1**: per RM0090 Rev 22, Table 43 ("DMA1 request mapping"), `TIM2_UP` is available on both `DMA1 Stream1/Channel3` and `DMA1 Stream7/Channel3`.

Since ADR-005 consolidated all five STEP pins onto `GPIOA`, only **one** of these two slots is needed for STEP:
  - `DMA1_Stream1`, `Channel 3` → `GPIOA->BSRR` (X/Y/Z/A/B, all five axes in one transfer)

`DMA1_Stream7`/`Channel 3` (the alternate `TIM2_UP` slot) is **not used by STEP** and is reserved as the natural candidate for a future DIR-DMA path (`GPIOD->BSRR`), should the DIR generation method (`Docs/MOTION-ENGINE.md` Section 27) later choose one. `TIM2->DIER.UDE` is enabled to source the request; `CC3DE` is left disabled so the slot is sourced purely by the Update event, not the coincident `TIM2_CH3` request that shares it.

*(This confirms and simplifies the two-stream plan in an earlier revision of this ADR — see the historical note in ADR-001/ADR-005.)*

**Reason:**
- `Docs/MOTION-ENGINE.md` Section 33 originally named `TIM2` or `TIM3` as candidates. `TIM3_CH1` (`PB4`) is already committed to Spindle PWM at a fixed 10 kHz period (`Docs/PINOUT.md`). Sharing `TIM3` between Spindle PWM and the STEP-DMA base rate would recreate the same shared-ARR conflict ADR-001 already rejected for `TIM1` — the spindle's fixed 10 kHz period and the STEP base tick rate would be forced to share one period register. `TIM2` has no other confirmed use in this project and avoids the conflict entirely.
- `TIM2` is a 32-bit general-purpose timer, giving more prescaler/ARR range than the 16-bit alternatives for tuning the base tick rate.
- The STM32F407 Ethernet MAC uses its own dedicated internal DMA engine, entirely separate from the DMA1/DMA2 general-purpose controllers. `TIM2`'s use of DMA1 therefore cannot contend with Ethernet RX/TX DMA traffic.
- No other peripheral currently defined in `Docs/PINOUT.md` uses the remaining DMA1 clients (I2C1–3, SPI2–3, USART3, UART4/5), so `DMA1 Stream1`/`Stream7 Channel 3` are expected to be free.

**Alternatives considered:**
- *`TIM3`* — rejected: conflicts with Spindle PWM's fixed 10 kHz period (see Reason).
- *`TIM4`/`TIM5`* — not selected: no advantage over `TIM2` for this role, and `TIM2`'s 32-bit counter is preferable; may be revisited only if `TIM2` becomes needed elsewhere.

**Timing impact:** Unchanged from ADR-001 — base-tick jitter remains bounded by timer update-event timing and DMA arbitration latency; exact figures **TBD**, pending hardware measurement (Section 39).

**Memory impact:** Unchanged from ADR-001 (one DMA buffer, single `GPIOA` port).

**CPU impact:** None beyond ADR-001; `TIM2` configuration is a one-time initialization cost.

**Risks / open items:**
- ~~DMA1 stream/channel assignment~~ — resolved above against the real RM0090 (Table 43).
- Motion-engine-level details (interpolation/DDA algorithm, DIR-pin DMA/synchronization strategy, motion buffer depth) remain open per `Docs/MOTION-ENGINE.md` Section 27; see ADR-004 for the base-tick rate and NVIC configuration that were fixed alongside this timer/DMA selection.

**Validation result:** TBD — pending real-hardware testing.

---

### ADR-003 — Firmware Execution Model: Bare-Metal vs RTOS

**Decision:** **Bare-metal, interrupt-driven superloop.** No RTOS (FreeRTOS) is used.

**Reason:**
- The hardest real-time constraint in this project — STEP/DIR pulse timing — is already fully offloaded to hardware by ADR-001/ADR-002 (`TIM2` Update event → DMA → `GPIOx->BSRR`). Once a STEP buffer is committed to DMA, its timing depends on neither a bare-metal loop nor an RTOS scheduler. This removes the traditional strongest argument for an RTOS in a motion controller — protecting hard-real-time timing from scheduling jitter — because that protection already comes from the hardware/DMA path.
- Safety-critical input handling (E-STOP and the other `PE0`–`PE14` EXTI inputs) is serviced by NVIC hardware interrupts, which preempt a bare-metal main loop and RTOS tasks identically, provided ISR priority is configured above the kernel's own priority (e.g. above `PendSV`/`SysTick` in a FreeRTOS build). An RTOS does not improve E-STOP latency in this architecture.
- The project's chosen LwIP programming model (`Docs/ETHERNET.md`, RAW API) is a callback-based, single-context API designed for exactly this kind of bare-metal superloop (or one dedicated RTOS task); it is not thread-safe and gains nothing from a preemptive scheduler. Running it under an RTOS would require additional locking/mailbox discipline (LwIP's `tcpip.c` model) with no offsetting benefit at this project's scope.
- The remaining software responsibilities (Ethernet/LwIP polling, protocol parsing, motion-buffer refill, digital I/O bookkeeping, diagnostics) form a small, bounded, well-understood set of periodic/event-driven work — not an open-ended set of independently-scheduled services. This is exactly the case Rule 7 (Section 42, "prefer the simplest architecture that meets all requirements") describes as not warranting an RTOS.
- Bare-metal avoids RTOS-associated RAM/flash overhead (kernel, per-task stacks, heap) and an added class of concurrency failure modes (priority inversion, mutex/queue misuse), keeping the system easier to reason about and debug via SWD (Section 29).

**Alternatives considered:**
- *FreeRTOS (fully RTOS-based)* — rejected for this project's scope: adds scheduler/RAM overhead and new concurrency-bug classes without improving the two things that actually need protection (STEP timing — already hardware-guaranteed; E-STOP latency — already NVIC-guaranteed).
- *Hybrid (hardware/interrupt-driven hard-real-time + RTOS tasks for everything else)* — the fallback if the bare-metal superloop is later measured to introduce unacceptable worst-case latency into Ethernet processing or motion-buffer refill. Not adopted now because no such measurement exists yet, and Rule 7/Rule 8 (Section 42) direct against introducing RTOS complexity pre-emptively.

**Resulting logical structure** (not a mandated source layout):

```text
main()
 └─ System / Clock / GPIO / Timer / DMA / EXTI init
 └─ Ethernet / LwIP init
 └─ while (1):
      Ethernet / LwIP processing   (RAW-API callbacks + required LwIP periodic calls)
      Motion command / buffer processing
      Digital I/O + Output Manager housekeeping
      Diagnostics
```

Running above and independent of this loop, purely via NVIC hardware interrupt:

```text
EXTI (PE0–PE14, E-STOP on PE2)        → Safety / Input Manager   (highest priority)
TIM2-triggered DMA (STEP/DIR)          → no CPU involvement in steady state
DMA Transfer-Complete/Half-Complete    → Motion buffer refill      (next-highest priority)
Ethernet MAC/DMA IRQ                   → hand-off into LwIP        (lower priority)
```

**Timing impact:** No change to the STEP/DIR timing guarantees already established by ADR-001/ADR-002 (hardware/DMA-timed). Safety-input latency remains bounded by NVIC interrupt latency, not by loop or scheduler timing.

**Memory impact:** No RTOS kernel, task stacks, or heap required; only static/DMA buffers and normal call-stack usage.

**CPU impact:** Superloop iterations must remain non-blocking (Section 34: no `HAL_Delay()` or blocking waits in the loop). Worst-case loop latency must still be measured (Section 39) to size the STEP-DMA and motion-command buffers with adequate margin — a requirement that exists regardless of the RTOS/bare-metal choice.

**Risks:** If a future requirement introduces genuinely independent, long-running, or blocking background work (e.g. a firmware-update-over-Ethernet mechanism — currently **NOT IMPLEMENTED** per `Docs/ETHERNET.md` Section 29), that work must not be added to the same superloop without re-evaluating this decision; this is exactly the case the hybrid alternative above exists for.

**Validation result:** TBD — pending measurement of worst-case superloop iteration time under maximum Ethernet + motion load (Section 39; `Docs/MOTION-ENGINE.md` Section 29).

---

### ADR-004 — TIM2 Base-Tick Configuration, DMA Transfer Parameters, and NVIC Priority Scheme

**Decision:**

*TIM2 configuration (assuming the standard 168 MHz clock tree from the 8 MHz HSE — `APB1` prescaler `/4` → `PCLK1` = 42 MHz → `TIM2` clock = 84 MHz via the RCC "×2 when APBx prescaler ≠ 1" rule, RM0090 Figure 16):*

```text
Prescaler (PSC)      = 0
Counter Period (ARR) = 20
Counter Mode         = Up
Auto-reload preload  = Enable
```

`(PSC+1)×(ARR+1) = 1×21 = 21` → Update-event rate = 84 MHz / 21 = **4.000 MHz exactly** (250 ns base tick).

Reasoning for a 4 MHz / 250 ns base tick: the STEP generator (ADR-001) produces each edge as two BSRR phases — a SET tick and a RESET tick. To reach the fixed 2 MHz / 500 ns STEP-period requirement, a period must contain at least one SET tick and one RESET tick, i.e. tick period ≤ 250 ns → base rate ≥ 4 MHz. 4 MHz is the coarsest (lowest DMA-bandwidth) base rate that still meets the 2 MHz requirement, and at that rate it produces a 250 ns STEP pulse width — 150 ns above the 100 ns minimum in Section 5/Acceptance Criteria of `Docs/MOTION-ENGINE.md`. A faster base tick would not widen the pulse further (it would still be built from the same number of phases) and would only increase DMA transaction count for no benefit — so 4 MHz is the margin-preserving choice, not a corner-cutting one.

Note: this fixes the **base tick rate**, not the interpolation/DDA algorithm that decides, per tick, which axis bits get set in each port's BSRR word for axes running below 2 MHz — that remains an open motion-engine decision (`Docs/MOTION-ENGINE.md` Section 27).

*DMA configuration (single stream, per ADR-002/ADR-005):*

```text
Stream direction        = Memory to Peripheral
Peripheral address      = &GPIOA->BSRR — fixed, no increment
Memory address          = STEP event buffer (all five axes) — incrementing
Data width (both sides) = Word (32-bit) — BSRR is a 32-bit register
Mode                    = Circular, with Half-Transfer and Transfer-Complete interrupts enabled, so the CPU refills the half of the buffer that DMA just finished with while DMA continues through the other half
Stream priority         = Very High (this is the hard-real-time path)
```

`STM32CubeMX`'s built-in `HAL_TIM_Base_Start_DMA()` helper targets the timer's own `ARR` register and is **not** used here; the DMA handle CubeMX generates for the `TIM2_UP` request (`hdma_tim2_up`, on `DMA1_Stream1`) is instead started manually (`HAL_DMA_Start_IT()`) with the `GPIOA->BSRR` address, in application code — this is firmware-implementation work, not an `.ioc` setting, and comes later. In the `.ioc`, only one `TIM2_UP` DMA request needs to be added under `TIM2`'s DMA Settings tab; CubeMX does not support adding the same request twice to two different streams from that tab, which is a tooling limitation, not a hardware one — ADR-005's single-port consolidation sidesteps it entirely for STEP.

**DIR pins (`GPIOD`, per `Docs/PINOUT.md`) are not part of this DMA scheme.** `DMA1_Stream7`/`Channel3` (the alternate `TIM2_UP` slot, unused by STEP after ADR-005) is the natural candidate for a future DIR-DMA path to `GPIOD->BSRR` — configuring it would require manually adding a second, independent DMA handle/stream outside `TIM2`'s own CubeMX DMA-settings linkage (since that tab only wires one stream per request type), the same kind of manual step already needed for the STEP stream's `GPIOA->BSRR` redirection above. Whether DIR needs this DMA-hardware treatment at all, or can instead be managed by CPU-timed GPIO writes with a guard interval (one base tick ≥ 200 ns satisfies the DIR setup/hold requirement on its own), is still an open motion-engine decision (`Docs/MOTION-ENGINE.md` Section 27, "DIR generation implementation") and is **not** required to configure the `.ioc` at this stage.

*NVIC priority scheme* (requires Priority Grouping set to 4 bits pre-emption / 0 bits sub-priority, i.e. `NVIC_PRIORITYGROUP_4`):

| Priority | Vector(s) | Role |
|---|---|---|
| 0 (highest) | `EXTI2_IRQn` | E-STOP (`PE2`) — dedicated vector, not shared with any other input |
| 1 | `EXTI0_IRQn`, `EXTI1_IRQn`, `EXTI3_IRQn`, `EXTI4_IRQn`, `EXTI9_5_IRQn`, `EXTI15_10_IRQn` | Remaining digital inputs (`PE0`,`PE1`,`PE3`–`PE14`) |
| 2 | `DMA1_Stream1_IRQn` | STEP buffer half/full-transfer refill |
| 5 | `ETH_IRQn` | Ethernet MAC/DMA |
| default (lowest, 15) | `SysTick_Handler` | HAL tick / LwIP timing — leave at the CubeMX/HAL default, do not raise it |
| disabled | `TIM2_IRQn` | **Must not be enabled.** `TIM2`'s Update event drives DMA directly with zero CPU involvement per tick (Section 10); enabling its global interrupt would mean an ISR firing 4,000,000 times/second, defeating the entire point of the DMA-driven design. |
| reserved, not yet enabled | `DMA1_Stream7_IRQn` | Reserved for a possible future DIR-DMA refill (see DIR note above); would sit at the same priority (2) as `DMA1_Stream1_IRQn` if implemented. |

Priorities 3–4 are left unassigned as headroom.

`EXTI2_IRQn`'s dedicated vector (distinct from the shared `EXTI9_5_IRQn`/`EXTI15_10_IRQn` vectors that service the other inputs) means the E-STOP handler never has to share an ISR entry or scan multiple pending bits before reaching `PE2` — this is a hardware property, not a software design choice, and it directly supports Section 8's requirement that E-STOP be serviced with the lowest possible latency.

**Alternatives considered:** A finer base tick (e.g. 8 MHz) was considered and rejected — see Reasoning above, it does not improve STEP pulse width at the 2 MHz ceiling and only adds DMA overhead.

**Timing impact:** Establishes the concrete numbers behind ADR-001/ADR-002's timing claims; exact jitter and worst-case latency remain **TBD** pending hardware measurement (Section 39).

**Memory impact:** No change from ADR-001/ADR-002.

**CPU impact:** Buffer refill now runs at the half-buffer/full-buffer DMA interrupt rate rather than per-tick — the exact rate depends on the motion-engine buffer depth (still TBD, Section 26/`Docs/MOTION-ENGINE.md` Section 16).

**Risks:** The 84 MHz `TIM2` clock assumption must match the project's actual RCC configuration (`APB1` prescaler `/4`). This is self-consistent with the Spindle PWM values already configured (`TIM3` `PSC=83`, `ARR=99` → exactly 10.000 kHz only if `TIM3`'s clock, also `APB1`-derived, is 84 MHz), so both timers' numbers corroborate the same clock-tree assumption — but the `.ioc`'s Clock Configuration tab should still be checked to confirm `APB1 Timer clocks = 84 MHz` before relying on this.

**Validation result:** TBD — pending real-hardware testing.

---

### ADR-005 — STEP Pin Consolidation onto a Single GPIO Port

**Decision:** All five STEP pins are moved onto `GPIOA`, in sequential order:

```text
STEP_X = PA8
STEP_Y = PA9
STEP_Z = PA10
STEP_A = PA11
STEP_B = PA12
```

This replaces the earlier assignment (`STEP_X` on `PC9`, `STEP_Y..STEP_B` on `PA8..PA11`, split across `GPIOA`+`GPIOC` with two DMA streams). `PC9` is now unused/reserved. `Docs/PINOUT.md` is the authoritative record of this change.

**Reason:**
- **CubeMX tooling constraint (the immediate trigger for this decision):** a peripheral's DMA Settings tab (e.g. `TIM2`'s) can link a given request type (`TIM2_UP`) to only one DMA stream; there is no GUI path to add the same request a second time for a second stream. The two-port design needed exactly that (two streams, both sourced by `TIM2_UP`) and could only have been realized by manually configuring the second stream outside CubeMX's normal peripheral-DMA linkage. Consolidating onto one port needs only one stream, which CubeMX supports natively.
- **Perfect cross-axis synchronization:** with all five STEP pins in one 32-bit word, a single `GPIOA->BSRR` DMA write updates any subset of the five axes in exactly the same bus cycle — there is no possibility of the kind of cross-port DMA arbitration skew that a two-stream/two-port design could exhibit between `GPIOA` and `GPIOC` writes, even though both were triggered by the same `TIM2_UP` event.
- **Frees a DMA resource for DIR.** `TIM2_UP` has exactly two DMA1 slots (`Stream1`, `Stream7`, per RM0090 Table 43). The two-port STEP design consumed both, leaving none for DIR. The single-port design uses only `Stream1`, leaving `Stream7` available as the natural candidate for a future DIR-DMA path (see ADR-004's DIR note).
- **No new pin conflicts.** `PA12` is free in `Docs/PINOUT.md` (no Ethernet, SWD, or other assigned function there — SWD uses `PA13`/`PA14`, Ethernet RMII uses `PA1`/`PA2`/`PA7`). `PA12`'s only alternate functions are `TIM1_ETR`/`USART1_RTS`/`OTG_FS_DP`, none of which are used by this project, and it is used here purely as a plain GPIO output.

**Alternatives considered:**
- *Keep the two-port split (original ADR-001/ADR-002 design)* — rejected: works electrically, but requires manually configuring a second DMA stream outside CubeMX's supported peripheral-DMA workflow for no compensating benefit, and does not free a slot for DIR.
- *Split some other way (e.g. 3+2 across two ports)* — not evaluated; the single-port option strictly dominates any split option once all five pins fit on one port.

**Timing impact:** Improves determinism relative to the two-port design (no cross-port DMA skew). No other change to ADR-001/ADR-002/ADR-004's timing analysis.

**Memory impact:** Reduces from two DMA buffers to one.

**CPU impact:** None beyond ADR-001/ADR-004.

**Risks:** **This is a pin reassignment and must be verified against the actual PCB/schematic before being treated as final**, per this document's own hardware-change rule (Section 15/`Docs/PINOUT.md`'s closing note) — if `PC9` is already routed to the X-axis driver on fabricated hardware, this change requires a hardware/wiring change, not just a firmware/`.ioc` update. If the board is still at the schematic/prototyping stage, this is a zero-cost change.

**Validation result:** TBD — pending confirmation that the change is compatible with the actual board, and pending real-hardware timing testing (Section 39).

---

### ADR-006 — DIR Generation Method

**Decision:** DIR (`PD8`–`PD12`, active-high) is generated by **CPU-timed GPIO writes**, not a dedicated DMA/timer path. This section defines the exact synchronization mechanism precisely, correcting an earlier, imprecise "one tick" framing (superseded below) that did not distinguish a **DMA hardware boundary** from a **host-protocol motion segment** — these are unrelated concepts and must never be conflated. A further cycle-precise verification of the play-time guard interval `g` (below, "Cycle-precise setup-margin verification") confirms the mechanism holds with real margin; nothing here changes the CPU-timed decision itself or the fixed 200 ns setup/hold requirement — both stand, unrevised, because no contradiction was found.

**Terminology, fixed by this section:**
- **DMA half-buffer ("a half")** — one of the exactly two halves of the circular STEP-DMA buffer (ADR-001/002/004), each `N/2` ticks deep, where `N` is the buffer's total depth in ticks (words) — a firmware-internal implementation parameter, still open per ADR-008.
- **Half-boundary** — a `DMA1_Stream1` Half-Transfer-Complete (`HTIF`) or Transfer-Complete/circular-wrap (`TCIF`) interrupt. These are the **only two hardware-synchronized instants available to CPU-timed code** for anything tied to DMA playback position — there is no mechanism (absent a second DMA/timer, which ADR-006 deliberately avoids) to synchronize a plain GPIO write to an arbitrary mid-half tick.
- **Motion segment** — a host-protocol-level unit of commanded motion (ADR-007; still undesigned, `Docs/MACH3-INTERFACE.md` §7). Its duration (illustratively a few ms, per the SDK's `ncPod` evidence) is unrelated to and, in the general case, spans many DMA half-boundaries. **A motion segment boundary and a DMA half-boundary are not the same thing and must not be assumed to coincide.**

**Mechanism (two-stage, because fill time and play time are one half-period apart):**

Label consecutive halves `H(i)`, each played for duration `τ = (N/2) × 250 ns`. Steady-state DMA double-buffering means `H(i)`'s content is filled by the CPU during the half-boundary ISR that fires at `t = (i−1)τ` (exactly when `H(i−2)` finishes and `H(i−1)` begins playing, freeing the physical region `H(i)` will reuse), and `H(i)` itself plays during `[iτ, (i+1)τ)`. **Fill always precedes play by exactly one half-period `τ` — not a full buffer period `N×250 ns` as an earlier draft of this ADR stated.**

A direction reversal for an axis is therefore committed in two separate ISR invocations, one half-period apart:

1. **Fill-time** (ISR at `t=(i−1)τ`, filling `H(i)`): if the DDA (ADR-007) determines axis X must reverse, the interpolator records `armed_direction[X] = new_direction`, targeted at `H(i)`, **and reserves the first `g` ticks of `H(i)` as idle for axis X** (no STEP edge) — see the `g` derivation below. This ISR does **not** touch `GPIOD` — writing it here would flip the pin while `H(i−1)` (old direction) is still autonomously playing, which is exactly the hazard this whole analysis exists to avoid.
2. **Play-time** (ISR nominally associated with `H(i)` starting to play, while it fills `H(i+1)`): as the **first action of this ISR, before any other work**, the code checks every axis's `armed_direction`; for any axis armed for `H(i)`, it writes the new value to `GPIOD->BSRR` now — synchronized to the real DMA event — then clears the armed entry. **Precisely when this IRQ fires relative to `t=iτ` is refined below** ("Cycle-precise setup-margin verification"): RM0090's own wording for `HTIF`/`TCIF` ("once half the data have been transferred... the flag is set") means the flag is asserted on completion of `H(i−1)`'s *last* transfer, which — because this DMA stream is single-transfer-per-`TIM2_UP`-request (direct mode, FIFO disabled, ADR-004) rather than back-to-back — occurs a full tick period (`250 ns`) *before* `H(i)`'s own first transfer at `t=iτ`, not simultaneously with it. This is a small, favorable correction to the "fires exactly when `H(i)` starts playing" shorthand used above; it does not change the `τ`/`N`-based reversal-latency figures below (`250 ns` is negligible against `[N/2, N]×250 ns` for any realistic `N`), but it materially affects the setup-margin arithmetic and is accounted for precisely there.

**Worst-case and best-case reversal latency, as an explicit function of `N`:**
- **Best case ≈ `τ = (N/2) × 250 ns`:** the DDA determines the reversal early enough (at, or very close to, the start of filling `H(i)`) that `H(i)`'s own leading ticks can be reserved for it. Latency from decision to physical effect = one half-period.
- **Worst case = `2τ = N × 250 ns`:** the DDA determines the reversal too late in the fill pass to still reserve `H(i)`'s leading `g` ticks (e.g., discovered only after `H(i)`'s early ticks are already committed with old-direction content). It must then defer to `H(i+1)`, which is filled one half-period later and played one half-period after that — two half-periods total from the original decision point.
- **This replaces the earlier "one buffer-refill period" statement with a precise range: `[N/2, N] × 250 ns`, a direct function of the buffer depth `N` (ADR-008), not a fixed or assumed single value.** At any plausible `N` this is tens of microseconds — still 100–1,000× the 200 ns requirement and negligible next to real mechanical deceleration times (milliseconds) — but it is now stated as a range tied to `N`, not asserted as "one period" without saying which.

**Setup margin — why a leading guard `g` (in ticks, within the target half) is still required beyond the half-boundary itself, verified cycle-precisely:**

*Corrected timeline (see the play-time refinement above).* The boundary IRQ that services `H(i)` is asserted at `t = iτ − 250 ns` (upon completion of `H(i−1)`'s final transfer), not at `t = iτ`. The play-time ISR's `GPIOD->BSRR` write therefore completes at `t = (iτ − 250 ns) + L`, where `L` is the real, end-to-end software latency from IRQ assertion to the write's effect on the AHB1 bus. `H(i)`'s first `g` ticks are reserved idle for the axis (fill-time, above), so the first new-direction edge occurs at `t = iτ + g×250 ns`. The setup window is therefore:

```text
setup_gap = (iτ + g×250 ns) − [(iτ − 250 ns) + L]  =  (g + 1)×250 ns − L
```

Requiring `setup_gap ≥ 200 ns` gives `g ≥ (L − 50 ns) / 250 ns` — one full tick more forgiving than the earlier `g×250 ns − L ≥ 200 ns` estimate, because that estimate did not yet account for the natural head start the DMA's own completion semantics provide.

*Itemized worst-case `L` budget*, each term sourced explicitly:

| # | Term | Value | Source |
|---|---|---|---|
| 1 | Cortex-M4 NVIC exception entry (vector fetch + 8-word hardware register stacking, `R0–R3,R12,LR,PC,xPSR`) | ~12 cycles ≈ 71 ns @ 168 MHz | **External, not repo-sourced.** `pm0214-...pdf` (`Docs/../STM32_DOCs/Cortex-M4_Programming_Manual`) confirms the 8-word stack-frame mechanism and tail-chaining/late-arrival optimizations exist (§2.3.7) but states **no cycle count** anywhere in the document. The commonly-cited "12 cycles" is a Cortex-M4/ARMv7-M architecture figure from ARM's own documentation, not this repo's PM0214 excerpt — treat as an estimate pending Section 39 real-hardware measurement, not a repo-confirmed number. |
| 2 | Possible NVIC priority-preemption block: a higher-priority interrupt (`EXTI2`/E-Stop, priority 0, or another `EXTI`, priority 1 — ADR-004) already executing when the `DMA1_Stream1` IRQ (priority 2) becomes pending must finish first | Bounded qualitatively only: Section 27 requires "high-priority interrupts must be short and deterministic" but **fixes no numeric worst-case-execution-time (WCET).** For this budget, an explicit interpretation of `~50 cycles (≈300 ns)` is assumed as the WCET ceiling for any priority-0/1 handler — generous for a debounce/latch/flag-set-style E-Stop or limit-input ISR. **This is a proposed numeric reading of Section 27's existing qualitative rule, offered here for this derivation; it is not yet a separately adopted spec** and should be confirmed (or tightened) once those ISRs are implemented and measured (Section 39). | 
| 3 | ISR body up to and including the `armed_direction` check + `GPIOD->BSRR` write (≈15–20 Thumb-2 instructions, warm I-cache/ART "0-wait-state-equivalent" sequential execution — this ISR runs at `4 MHz/(N/2)`, e.g. tens of kHz for realistic `N`, so the code is resident and warm in steady state; only the very first invocation after boot is cold, and that occurs before motion starts, outside the reversal-latency-critical path) | ~20 cycles ≈ 119 ns | RM0090 §3 (Table 11: 5 wait states / 6 CPU cycles per 128-bit Flash line at `150 < HCLK ≤ 168 MHz`) and the ART accelerator description ("0 wait state equivalent... for sequential code already prefetched/cached") — confirmed against this project's own `stm32f4xx_hal_conf.h` (`PREFETCH_ENABLE`, `INSTRUCTION_CACHE_ENABLE`, `DATA_CACHE_ENABLE` all set). Instruction count itself is an implementation estimate, not yet measured. |
| 4 | `GPIOD->BSRR` store completion | ~2 cycles ≈ 12 ns | **Corrects a possible false assumption from earlier analysis:** `GPIOD` (like `GPIOA`, already used for STEP) is an **AHB1 peripheral**, not an APB2 peripheral — confirmed by its address (`0x4002 0C00`, inside the AHB1 range) and `RCC_AHB1ENR` (not `RCC_APB2ENR`) gating its clock (RM0090 §2.2/§6.3.10/§7.3.10), and by the GPIO section's own statement that the input register "captures the data present on the I/O pin at every AHB1 clock cycle." **There is no AHB-to-APB2 bridge crossing for this write at all** — it is a direct, full-`HCLK`-rate (168 MHz) AHB1 bus-matrix access, the same class of access STEP's own `GPIOA->BSRR` DMA writes already use. RM0090 does not tabulate an exact cycle count for a bare AHB1 peripheral register write (unlike its Flash wait-state table); 1–2 cycles is the standard assumption for a non-wait-stated AHB1 access and is treated as an estimate pending Section 39 confirmation. A theoretical contention case — the CPU's `GPIOD` write (via the D-bus, through the multilayer bus matrix's "AHB1 peripherals" slave port) coinciding with `DMA1_Stream1`'s own `GPIOA` write — is structurally unlikely to serialize at the bus-matrix level: RM0090 §19 notes explicitly that "the DMA1 controller AHB peripheral port is not connected to the bus matrix like DMA2 controller," i.e. DMA1's peripheral-port path to `GPIOA` is architecturally separate from the CPU D-bus's bus-matrix path to `GPIOD`. RM0090 does not detail cycle-level arbitration below this, so this remains a structurally-favorable but not cycle-exact finding, flagged for Section 39. |

**Worst-case `L ≈ 71 + 300 + 119 + 12 ≈ 502 ns`.** Applying `g ≥ (L − 50)/250`: `g ≥ 1.8`, so **`g = 2` is proven sufficient under this worst-case budget** (`setup_gap = 3×250 − 502 = 248 ns ≥ 200 ns`); **`g = 3` (the value already adopted above for comfortable margin) gives `setup_gap = 4×250 − 502 = 498 ns`**, roughly double the required floor, and is kept as the adopted value precisely to absorb the budget's two explicitly-external/unconfirmed line items (row 1, row 4) coming in worse than estimated on real hardware, without needing a documentation change if they do. **No architecture change is needed: this is a refinement of the existing `g = 2–3` derivation, not a contradiction of it** — if anything, the DMA completion-semantics correction (natural `250 ns` head start) makes the mechanism's margin better than the earlier, simpler estimate showed, which is why `g = 2` now proves sufficient outright rather than merely "minimum viable."

**Hold margin — no extra guard needed, by construction, now with an explicit floor independent of `L`:** the last old-direction edge occurs no later than `H(i−1)`'s final tick at `t = iτ − 250 ns`; the `GPIOD` write happens at `t = (iτ − 250 ns) + L`. The hold gap is therefore `= L` measured from that edge to the write's own trigger, but because the *value itself* does not change until the write completes and `GPIOD` is never touched in between, the true stability floor is `hold_gap = 250 ns + L`, evaluated at `L → 0`: even a hypothetical zero-latency write would still land a full tick period (`250 ns`) after the last old-direction edge, which already exceeds the `200 ns` requirement on its own. **Hold is therefore satisfied unconditionally, for every value of `L` from zero to the ~502 ns worst case above — it does not depend on interrupt latency at all**, confirming (with an explicit floor, not just a qualitative "free by construction" claim) that no axis "parking" or extra trailing reserved ticks are needed on the hold side.

**How a pending change is represented, and why no stale-direction STEP event can occur after `GPIOD` changes:** each axis carries a single `armed_direction` slot (direction value + target half) — a queue depth of one is sufficient, since realistic reversal cadence is far slower than one buffer cycle; a new reversal request while one is already armed simply waits for the armed one to be realized first (`Docs/FIRMWARE-ARCHITECTURE.md` Section 42 Rule 7, simplest adequate mechanism). The "no stale STEP after the DIR change" guarantee is **structural, not a runtime check**: the fill algorithm generates each half's ticks in strict logical order and, by construction, never emits a new-direction edge before the reserved `g`-tick gap at the start of whichever half the reversal targets; the corresponding `GPIOD` write happens only once, in that half's play-time ISR, before that ISR does anything else. There is no code path that can emit a tick out of order or apply the write early or late relative to this sequencing.

**Reason DIR remains CPU-timed (unchanged from the original decision):**
- The fixed requirement (`DIR setup ≥ 200 ns`, `DIR hold ≥ 200 ns`, `Docs/MOTION-ENGINE.md` Section 7) is met with the margins derived above.
- A DMA-hardware DIR path (`DMA1_Stream7`/`Channel3`, freed by ADR-005) remains architecturally available and is not subject to this pipeline-depth latency at all (it would be tick-synchronous, like STEP itself), but is not needed to meet the fixed requirement, and Rule 7 (Section 42) directs against adding it without demonstrated need.
- CPU-timed writes remain simple: the mechanism above is two small, deterministic pieces of bookkeeping in the existing refill ISR, not a new hot path.

**Alternatives considered:**
- *Second DMA stream to `GPIOD->BSRR`, synchronized to `TIM2`* — not adopted now; kept as the documented fallback if a future motion profile needs reversals faster than the `[N/2, N]` latency range allows, or if Section 39 measurement shows the CPU-timed mechanism's margins are inadequate in practice. `DMA1_Stream7`/`Channel3` remains reserved and unused for exactly this purpose (ADR-002/ADR-004).

**Timing impact:** Meets the fixed 200 ns setup/hold requirement, with the precise mechanism and margins (`g = 3` ticks setup, `250 ns + L` hold floor, `[N/2, N]×250 ns` reversal latency) derived above. The cycle-precise verification above shows `g = 2` already sufficient (`248 ns` setup margin) and `g = 3` (adopted) giving `498 ns` — against a worst-case software latency budget `L ≈ 502 ns` built from repo-sourced Flash/ART/DMA/bus-matrix facts plus two explicitly-flagged external estimates (Cortex-M4 exception-entry cycle count; NVIC priority-preemption WCET ceiling). Those two external line items, and the exact AHB1 GPIO-write cycle count, are the only remaining items needing real-hardware confirmation (Section 39); none of them, even at plausible worse-than-estimated values, threaten the `g = 3` margin without growing several-fold.

**Memory impact:** One `armed_direction` slot (direction + target-half tag) per axis — negligible.

**CPU impact:** Negligible — one conditional check and, when armed, one `GPIOD->BSRR` write, executed once per half-boundary ISR (not per tick), as the first action of that ISR.

**Risks:** If a future motion profile requires reversals faster than the worst-case `N×250 ns` latency allows, either `N` must be reduced (ADR-008's existing size/latency/CPU-overhead trade-off, now with reversal latency as an explicit third factor) or this decision should be revisited in favor of the DMA fallback above. Separately: the priority-preemption WCET ceiling (`≈50 cycles`/`300 ns`) used in the setup-margin budget above is this analysis's own proposed numeric reading of Section 27's qualitative "short and deterministic" rule for priority-0/1 handlers, not yet a separately adopted spec — if a future priority-0/1 ISR (E-Stop, limit inputs, or any future addition) is implemented with a materially larger worst-case execution time, this budget's row 2 must be revisited and the `g = 3` margin re-checked against it.

**Validation result:** Quantitative model complete and self-consistent (this section); **`g = 2` is proven sufficient and `g = 3` (adopted) carries roughly 2× the required setup margin** under the worst-case budget derived above. Two line items in that budget are explicitly external-knowledge estimates, not figures found in this repo's PM0214/RM0090 excerpts (Cortex-M4 exception-entry cycle count; AHB1 GPIO-write exact cycle count), and one is this analysis's own proposed numeric reading of an existing qualitative rule (priority-preemption WCET ceiling) — all three, plus the DIR setup/hold timing itself, still require real-hardware confirmation (Section 39), but none of them is a repo-documented unknown of the "we don't know the architecture" kind; they are ordinary hardware-measurement confirmations of an otherwise-complete derivation.

---

### ADR-007 — Interpolation Algorithm and Motion Command Domain

**Decision:**
1. The Motion Engine's interpolation (M5) uses a **per-axis digital differential analyzer (DDA/Bresenham accumulator)** running once per `TIM2` base tick (4 MHz, ADR-004): each axis holds a fixed-point rate register; each tick, an accumulator is incremented by that rate, and a STEP bit is emitted for that axis whenever the accumulator crosses a fixed threshold (carrying the remainder forward, never truncating it away).
2. **All motion data the firmware operates on is in step-domain (step counts / step rates), never in host engineering units (mm, inches).** Any mm↔step conversion (using Mach3's configured steps-per-unit) happens on the **PC side**, before a command ever reaches the firmware.

**Reason:**
- This is not a new invention: it is the same technique confirmed in `Docs/MACH3-INTERFACE.md` Section 4 from the SDK's own `SDK/ncPod/` reference device — a per-axis signed velocity value computed each fixed time slice, with a fractional accumulator (`Pod->fractions[axis]`) carried between slices so nothing is lost to rounding. Running that same accumulator logic at the firmware's own 4 MHz tick, rather than at the host's slower slice rate, is the natural way to turn a per-slice velocity into individual STEP edges without CPU involvement per edge (ADR-001).
- Point 2 follows from `Docs/MOTION-ENGINE.md` Section 13 ("the firmware must implement the smallest and most deterministic data path necessary") and from the SDK evidence itself: `ncPod`'s `Send512Block()` (`Docs/MACH3-INTERFACE.md` Section 4) computes its velocity values from `MainPlanner->Movements[index].ex` (a `GMoves` engineering-unit position, `Docs/MACH3-INTERFACE.md` Section 3) **on the PC side**, and sends the result already in the device's native units. The firmware never needs to know a machine's steps-per-unit configuration, which keeps it independent of machine-specific configuration Mach3 already owns.
- This also directly resolves how Mach3's `GMoves` maps onto this firmware: `GMoves` (6-axis, engineering units, arc/cubic segments) is consumed and converted **entirely on the PC-side plugin**; the firmware's input is the already-time-sliced, already-unit-converted, already-5-axis-mapped stream that crosses the still-undesigned UDP protocol (`Docs/MACH3-INTERFACE.md` Section 7). The firmware side of this boundary is fixed by this ADR; the wire format that carries it across the network is not (Section "UDP protocol dependencies" below).

**Alternatives considered:**
- *Send raw `GMoves` segments (arcs/cubics, engineering units) to the firmware and interpolate machine kinematics on-device* — rejected: duplicates work Mach3's planner already does, requires floating-point machine-unit math in the hard-real-time path, and contradicts the "smallest deterministic data path" principle.

**Timing impact:** DDA update is a fixed, small number of integer operations per axis per tick — bounded and independent of motion complexity, consistent with ADR-004's tick budget.

**Memory impact:** One rate register + one accumulator per axis (10 machine words for 5 axes) — negligible.

**CPU impact:** Bounded per-tick cost as above; still zero CPU cost for the STEP edge itself (DMA-driven, ADR-001).

**Risks:** The exact fixed-point width for the rate/accumulator registers is an implementation detail, not fixed by this ADR — it must be wide enough that the accumulator does not wrap during the longest expected single motion segment at the slowest supported rate. To be sized when M5 is implemented.

**Validation result:** TBD — pending implementation and the multi-axis timing tests in Section 39.

---

### ADR-008 — Motion Buffering: Depth Target and Overflow/Underflow Behavior

**Decision:**

*Two independent buffers exist, at two different levels:*

1. **STEP-DMA buffer (M4, low level):** a small, fixed-size circular buffer of `N` precomputed `GPIOA->BSRR` words (two halves of `N/2` ticks each), refilled by the CPU on the DMA half-transfer and transfer-complete interrupts (already established in ADR-004's DMA configuration: circular mode, both interrupts enabled). `N` is an internal implementation parameter, not a protocol concern, and is not frozen by this ADR. Three factors trade off against each other when `N` is chosen (Section 39): keeping the refill-interrupt rate (`4 MHz / (N/2)`) and per-refill CPU cost bounded, not costing meaningful RAM (`4×N` bytes — a few KB is more than sufficient at 4 MHz), and — per ADR-006's precise timing derivation — **DIR reversal latency, which ranges from `(N/2)×250 ns` (best case) to `N×250 ns` (worst case)**, so a smaller `N` directly buys lower worst-case reversal latency at the cost of a higher refill-interrupt rate.
2. **Motion command buffer (M8, high level):** buffers incoming time-sliced motion commands (ADR-007) ahead of the interpolator. **Target depth: at least the ~128 ms of buffered motion evidenced by the SDK's `ncPod` reference device** (`Docs/MACH3-INTERFACE.md` Section 4), expressed as a time target rather than a fixed byte size, because the exact command/segment encoding depends on the still-undesigned UDP protocol. This buffer is **statically allocated** (fixed-size array, no dynamic allocation), consistent with `Docs/FIRMWARE-ARCHITECTURE.md` Section 26.

*Overflow and underflow behavior (M8/M2):*

- **Underflow** (the command buffer empties while the system is in the `RUNNING` state, ADR-010): this is one of the fault categories the project's own architecture already names (`Docs/FIRMWARE-ARCHITECTURE.md` Section 23, "Motion Buffer Underflow"). On underflow, the interpolator (M5) stops emitting new nonzero-rate STEP ticks for the starved axis — i.e., it decelerates to and holds at zero commanded rate rather than continuing to guess — and M8 raises a `MOTION_BUFFER_UNDERFLOW` fault to M2. This does not by itself de-energize the drives (`EN`/`PD15`) or declare `EMERGENCY_STOP` (ADR-010); it is a `FAULT`-level event, recoverable once fresh commands arrive, unless it persists long enough to also trip the communication-timeout behavior (ADR-010).
- **Overflow** (commands arrive faster than the buffer can absorb): the firmware must **never silently drop or overwrite an unconsumed command**. This principle is evidenced directly by the SDK: `ncPod`'s flow-control mechanism (`Pod->BufferHolding`, `Docs/MACH3-INTERFACE.md` Section 4) has the device explicitly report "buffer full" back to the host rather than accept and discard data. The exact backpressure mechanism (a status flag in the still-undesigned feedback packet, most likely) is a UDP protocol dependency and cannot be finalized yet, but the principle — reject/backpressure, never silently overwrite — is fixed now so M8 and M14 are designed consistently once the protocol exists.

**Reason:** see inline above; this ADR exists to record the *principles* (buffer independence, static allocation, no silent data loss, underflow ≠ emergency stop) that are decidable now, while explicitly deferring the *exact sizes and wire mechanism* that are not.

**Alternatives considered:**
- *Single combined buffer for both DMA words and motion commands* — rejected: conflates a fixed-rate hardware consumer (DMA at 4 MHz) with a variable-rate network producer; keeping them separate (with M5's interpolation as the boundary) is simpler to reason about and matches the layering already established by ADR-001/ADR-007.

**Timing impact:** None beyond what ADR-001/ADR-004/ADR-007 already establish.

**Memory impact:** STEP-DMA buffer: a few KB. Motion command buffer: sized once the protocol's per-command encoding exists; targeting ~128 ms of motion is not expected to be RAM-constrained (total main SRAM is 128 KB; the Ethernet stack's zero-copy RX pool and lwIP heap together use on the order of 30 KB per the current `lwipopts.h`/`ethernetif.c` configuration, leaving ample headroom).

**CPU impact:** Refill-interrupt overhead for the STEP-DMA buffer, to be measured (Section 39).

**Risks:** The 128 ms target is evidence-based, not a project requirement — MOTION-ENGINE.md Section 16 still correctly lists the final depth as depending on "Mach3 command rate, network packet rate, worst-case network latency, packet loss" once those are known.

**Validation result:** TBD.

---

### ADR-009 — Internal Position Representation

**Decision:** All position and step-count state inside the firmware (commanded position, generated/actual position, per-axis step counters) uses **signed 64-bit integers (`int64_t`), in raw step-count units**, never floating-point and never host engineering units.

**Reason:**
- `Docs/MOTION-ENGINE.md` Section 23 requires "sufficiently wide integer representations... to prevent overflow during long machine operation" without fixing the width. 64-bit removes the question entirely: at the fixed 2 MHz maximum STEP rate (`Docs/MOTION-ENGINE.md` Section 4.2), a 64-bit signed counter would take well over 100,000 years of continuous full-speed stepping in one direction to overflow — the practical machine lifetime is not a constraint.
- Step-count units (not mm/inches) follow directly from ADR-007: the firmware operates entirely in step-domain, so its position state must be in the same domain as everything else it computes.
- This matches the SDK's own `GMoves.DDA1/DDA2/DDA3` fields (`Docs/MACH3-INTERFACE.md` Section 3), which are declared `__int64` in the Mach3 SDK itself for exactly this kind of accumulator/position state — using the same width on the firmware side is consistent with the host-side precedent, not an arbitrary choice.

**Alternatives considered:**
- *32-bit step counters* — rejected: while unlikely to overflow in most practical scenarios, "sufficiently wide... to prevent overflow" is trivially and permanently satisfied by 64-bit at zero meaningful cost on a 32-bit Cortex-M4 (register-pair arithmetic), so there is no reason to accept even a theoretical risk.
- *Floating-point (mm/inches) position state on-device* — rejected: reintroduces the unit-conversion and rounding questions ADR-007 already resolved by keeping conversion host-side.

**Timing impact:** None — 64-bit integer arithmetic on Cortex-M4 is a few extra instructions versus 32-bit, negligible against the tick budget (ADR-004).

**Memory impact:** 8 bytes per tracked position value per axis, versus 4 for 32-bit — negligible at 5 axes.

**CPU impact:** Negligible, per above.

**Risks:** None identified.

**Validation result:** N/A — this is a representation choice, not something requiring hardware measurement.

---

### ADR-010 — System State and Fault Model

**Decision:** The centralized system state model (`Docs/FIRMWARE-ARCHITECTURE.md` Section 24, which left the exact states as non-mandatory examples) is fixed as:

```text
BOOT
  → INIT                (peripheral/clock/DMA/NVIC init — Section 31)
    → SAFE_IDLE          (initialized; EN=LOW, drives disabled; awaiting a safe start condition)
      → READY            (EN=HIGH, drives enabled; motion buffer empty; awaiting commands)
        ⇄ RUNNING        (motion buffer non-empty; STEP DMA actively producing edges)

From any state except EMERGENCY_STOP:
  → FAULT               (recoverable: comm timeout, buffer underflow persisting, invalid
                          packet stream, non-fatal peripheral fault)
  → EMERGENCY_STOP       (E-STOP asserted, PE2 EXTI — latching, see below)

FAULT → SAFE_IDLE         (only via an explicit clear/reset action, never automatically)
EMERGENCY_STOP → SAFE_IDLE (only via an explicit clear/reset action, AND the physical E-STOP
                            input must already be released — never while PE2 still reads asserted)
```

`EMERGENCY_STOP` is a distinct state from `FAULT`, not a special case of it, because Section 8 requires E-STOP to be serviced independently of everything else in the system (it already is, at the hardware/NVIC level — `EXTI2_IRQn` priority 0, ADR-004) and because conflating it with general faults would let a generic fault-clear path accidentally clear an E-STOP condition.

**Specific behaviors fixed by this ADR (items marked ✅ were confirmed by the project owner; this ADR is no longer open on those points):**

- ✅ **Fault recovery policy — always explicit, never automatic.** No `FAULT` or `EMERGENCY_STOP` condition clears itself. Every recovery — including communication resuming after a `COMM_TIMEOUT`, or the motion buffer refilling after an underflow — requires an explicit clear/reset action. This was the conservative default proposed and is now the confirmed policy, not merely a placeholder.
- ✅ **`EMERGENCY_STOP` and hardware-integrity faults** (DMA fault, timer fault, invalid/corrupt packet stream, init failure, non-fatal peripheral fault): `EN` (`PD15`) is deasserted (LOW) immediately, fully disabling the drives; the STEP-DMA (M4) is halted. These are conditions where the system cannot trust its own step generation or where a physical safety input is asserted, so removing drive power is the safe default.
- ✅ **`COMM_TIMEOUT` and buffer-underflow FAULTs are the one exception: `EN` stays asserted (drives remain enabled/energized), and only new STEP output is halted.** The interpolator (M5) stops emitting new nonzero-rate ticks and holds the last commanded position under power; it does not deassert `EN`. This was confirmed by the project owner specifically to avoid a stepper (or step/dir-command servo) losing holding torque and drifting or dropping under load (e.g. a Z-axis under gravity) merely because the network link or the command stream paused — a strictly worse outcome than holding position and waiting for either fresh commands or an explicit reset. If motion needs to resume, it does so from `FAULT` via the same explicit-reset path as any other fault, not automatically.
- **Communication timeout is a `FAULT`, never `EMERGENCY_STOP`** — the local E-STOP path must never depend on the network (Section 8, Section 28 of `Docs/ETHERNET.md`), and a lost network link is not itself a machine safety event.
- **Limit-switch inputs** (the 14 non-E-STOP `PE0/1/3–14` inputs): per `Docs/FIRMWARE-ARCHITECTURE.md` Section 7, "not every input necessarily requires identical treatment" — but no per-input semantic assignment (which physical input is X+ limit, Y− limit, probe, etc.) exists anywhere in the repository yet. That mapping is host/Mach3-signal-table-driven (`Docs/MACH3-INTERFACE.md` Section 4, `Engine->InSigs[]`), so the firmware's own obligation is only to report all 14 raw states promptly and accurately; *deciding* that a given input means "stop now" is expected to happen on the host side unless a future revision of this document assigns specific safety semantics to specific pins in firmware.

**Reason:** the states and the FAULT/EMERGENCY_STOP split follow directly from Sections 7, 8, 21, 23 and 32 already cited; the explicit-only recovery rule is consistent with this project's own "preserve engineering margin," "do not hide uncertainty" principles (Section 42, Rules 8–9) — silently auto-clearing a fault would be exactly the kind of assumption those rules warn against. The `COMM_TIMEOUT`/underflow hold-position exception was a specific project-owner decision, not derived from the documents, made to avoid uncommanded drive de-energization under a merely-paused (not unsafe) condition.

**Alternatives considered:**
- *Auto-clear on condition resolution* — rejected (see above; now a confirmed policy, not a default).
- *Deasserting `EN` on every `FAULT` including comm-timeout/underflow* — this was this ADR's original default; superseded by the project owner's decision above, which distinguishes "the system doesn't trust itself" faults (disable drives) from "motion is merely paused" faults (hold position, keep drives enabled).

**Timing impact:** State transitions themselves are not hard-real-time; the underlying actions they trigger (deasserting `EN`, halting DMA) must be, and already are, since they reuse the existing GPIO/DMA control paths.

**Memory impact:** One state variable plus a small fault-code/fault-flags value.

**CPU impact:** Negligible.

**Risks:** The exact set of conditions that escalate a `FAULT` versus that stay merely "held" (e.g. does a single missed packet count as a timeout, or only N consecutive misses / a fixed watchdog window?) is not fixed here — it depends on the still-undesigned communication protocol's timing (`Docs/MACH3-INTERFACE.md` Section 7) and is deferred to that design.

**Validation result:** N/A for the state structure; the fault-response latencies (E-STOP, comm-timeout) require hardware measurement once implemented (Section 39).

---

### ADR-011 — MAC Address: Bench-Test vs. Production Strategy

**Decision:** Two distinct, explicitly separated policies — both now confirmed:

1. **Bench-test / development (now):** use a **locally-administered, unicast MAC address** in place of the current CubeMX placeholder (`00:80:E1:00:00:00`, which is a real vendor's OUI prefix and must not ship even on an isolated link, since it is not this project's to use). A locally-administered address sets the U/L bit (bit 1 of the first octet) to `1` and the multicast bit (bit 0) to `0` — e.g. `02:00:05:10:00:01`, chosen to echo this project's static IP octets (`192.168.5.10`, `Docs/ETHERNET.md` Section 15) purely as a memorable bench convention, with no other significance. This is safe on the isolated point-to-point link this project targets and requires no external registration.
2. ✅ **Production / final product: per-unit, derived from the STM32F407's factory-programmed 96-bit unique device ID.** Confirmed by the project owner. Each unit gets a distinct, automatically-generated address — no purchase, no per-unit provisioning step, and no collision risk if multiple units ever end up on the same network. The address remains locally-administered (U/L bit set, multicast bit clear), not a "real" registered OUI; that tradeoff was accepted explicitly in favor of avoiding both a purchase and a fixed shared address. The exact derivation (which bytes of the 96-bit ID feed which octets, and how U/L·multicast bits are forced) is an implementation detail for M12, not fixed by this ADR — it only fixes the *strategy* (per-unit, ID-derived, locally-administered).

**Reason:** distinguishing these two cases avoids two different failure modes: shipping a real vendor's OUI without authorization (the current placeholder's problem), and freezing a single hardcoded address as if it were a final answer once more than one unit can exist. Per-unit derivation was chosen over a purchased OUI block because it has no cost and no volume threshold to be worthwhile, and over a single fixed address because it removes the collision risk entirely rather than accepting it.

**Alternatives considered:**
- *Keeping the CubeMX placeholder* — rejected outright, it belongs to a real vendor (per the OUI byte pattern) and using it is not this project's to do even for bench testing.
- *Single fixed production address* — rejected: simplest, but reintroduces a collision risk the moment a second unit exists on one network.
- *Purchased IEEE OUI block* — not rejected outright, but not adopted now: only worth its cost at a production volume this project hasn't reached, and per-unit ID-derivation already solves the uniqueness problem without it.

**Timing impact:** None.

**Memory impact:** None.

**CPU impact:** None.

**Risks:** If multiple bench units ever end up on the same switch/network segment simultaneously, using the *same* bench MAC on more than one of them will cause ARP/switching conflicts — fine for a single isolated point-to-point link (this project's stated target, `Docs/ETHERNET.md` Section 15) but worth remembering if a second unit is ever brought up on the same network for comparison testing.

**Validation result:** N/A.

---

---

### ADR-012 — STEP-DMA path correction: DMA1 cannot reach GPIO

**Status:** ACCEPTED by the project owner. The `.ioc` has been
reconfigured to `TIM8_UP → DMA2_Stream1` and verified against this ADR;
the full firmware builds and links. Supersedes the DMA/timer allocation in
ADR-002 and ADR-004, and the `DMA1_Stream7` remark in ADR-005.

**Problem found.** ADR-002/ADR-004/ADR-005 route
`TIM2_UP → DMA1_Stream1 → GPIOA->BSRR`, and `CNC5AX-ETH.ioc` is configured
that way (`Dma.Request0=TIM2_UP/CH3`, `Instance=DMA1_Stream1`,
`Direction=DMA_MEMORY_TO_PERIPH`). Verified against this repository's own
RM0090 Rev 22, **that transfer cannot happen on this MCU.**

Four independent statements in RM0090 combine to settle it:

1. **§2.1, STM32F405xx/07xx.** The bus-matrix masters are: Cortex-M4
   I-bus, D-bus and S-bus; **DMA1 memory bus**; DMA2 memory bus;
   **DMA2 peripheral bus**; Ethernet DMA bus; USB OTG HS DMA bus. The
   **DMA1 *peripheral* bus is not in the list.**
2. **§2.1, same list.** The bus-matrix *slaves* include "AHB1 peripherals
   including AHB to APB bridges and APB peripherals". GPIOA is an AHB1
   peripheral (`0x4002 0000`, clock-gated by `RCC_AHB1ENR`), so it is only
   reachable *through* the bus matrix.
3. **Figure 33, note 1.** "The DMA1 controller AHB peripheral port is not
   connected to the bus matrix like DMA2 controller." ADR-006 already
   quotes this line, but takes from it only the memory-to-memory
   consequence the note happens to mention; the note is about the port,
   not about that one transfer type.
4. **§10.3.16 table and §10.3.17 step 2.** For a memory-to-peripheral
   transfer the source is the AHB *memory* port and the destination is the
   AHB *peripheral* port, and `DMA_SxPAR` is explicitly "the peripheral
   port register address... moved to this address *to the peripheral
   port*".

Putting `&GPIOA->BSRR` in `DMA_SxPAR` on DMA1 therefore asks the one port
that is not on the bus matrix to reach a slave that only exists on it.
Inverting the transfer does not help: DMA1's peripheral port cannot reach
SRAM either, for the same reason.

**Consequence.** Only DMA2's peripheral port can write a GPIO BSRR, and
**RM0090 Table 44 gives DMA2 timer requests for TIM1 and TIM8 only** — so
the base timer cannot be TIM2 either. This is not a tuning question; the
configured path would produce no STEP pulses at all.

**Correction implemented in Phase 1 firmware:**

```text
TIM8 UP  ──►  DMA2 Stream 1, Channel 7  ──►  GPIOA->BSRR
```

- `TIM8_UP → DMA2 Stream1 Ch7` is RM0090 Table 44, verified directly.
- TIM8 is on APB2, whose timer clock is 168 MHz in this project's own
  `.ioc` (`RCC.APB2TimFreq_Value=168000000`). `PSC = 0, ARR = 41` gives
  exactly 4.000 MHz, preserving ADR-004's base tick with no rounding.
- TIM1 is the equally valid alternative (`TIM1_UP → DMA2 Stream5 Ch6`).
  TIM8 was chosen so TIM1 stays free; TIM3 remains the spindle PWM.

**Everything else in the frozen decisions is preserved:** the 4 MHz base
tick and its reasoning (ADR-004), the single-GPIOA-port consolidation and
its zero-skew property (ADR-005), CPU-timed DIR with the two-stage
arm/play mechanism and `g = 3` (ADR-006), direct mode with one transfer
per request, circular with HT/TC interrupts, very-high stream priority,
and the NVIC ordering (E-STOP 0, other inputs 1, STEP-DMA 2, Ethernet 5).
Two consequences of this correction are elevated below from incidental
facts into binding rules, since both are load-bearing for the "Ethernet
must not affect STEP timing" requirement (`Docs/MOTION-ENGINE.md` §8) and
neither survives a careless future edit (e.g. a CubeMX regeneration, or a
new buffer declared without checking where it lands) without being stated
explicitly:

**Binding rule — Ethernet/`lwIP` buffers confined to SRAM1.** The STEP-DMA
ring now lives in SRAM2 (`Platform/STM32F407/linker/stepgen_sram2.ld`), on
its own bus-matrix slave port, precisely so Ethernet traffic can never
contend with it for that port. That isolation only holds if the *other*
side is also true: every Ethernet MAC DMA descriptor/buffer
(`DMARxDscrTab`, `DMATxDscrTab`) and the entire `lwIP` buffer pool
(zero-copy RX pbuf pool, TX path, `lwIP`'s own heap/pools) **must reside in
SRAM1 only — never SRAM2, and never CCM RAM.** The CCM case is not a
performance nuance but an outright functional impossibility: RM0090 §2.1
states the 64 KB CCM data RAM "is not part of the bus matrix and can be
accessed only through the CPU" — **no DMA master on this part, including
the Ethernet MAC's own dedicated DMA engine, can reach CCM at all.**
Verified against the current Phase 1 firmware: `DMARxDscrTab`,
`DMATxDscrTab`, and the `lwIP` `RX_POOL` pool
(`Firmware/LWIP/Target/ethernetif.c`) are plain static/global declarations
with no section attribute, so they land in the linker's default `RAM`
region, which `STM32F407VGTX_FLASH.ld` defines as exactly SRAM1
(`ORIGIN = 0x20000000, LENGTH = 112K`) — SRAM2 and CCMRAM are separate,
explicitly-carved-out regions that no Ethernet/`lwIP` code references
anywhere in the repository today. The rule holds by construction, not
merely by intent; see `Docs/PRE-IMPLEMENTATION-DECISIONS.md` item 10 for
the recommended (not yet implemented) HV-03-style runtime check that would
catch a future regression.

**Binding rule — Ethernet interrupt priority strictly below `DMA2_Stream1`.**
The isolation the NVIC ordering above is meant to provide only actually
holds for the *current* STEP-DMA interrupt, `DMA2_Stream1_IRQn` — not the
old, no-longer-used `DMA1_Stream1_IRQn` this ADR replaces. **`ETH_IRQn` and
`ETH_WKUP_IRQn` must always be configured at a strictly lower NVIC
preempt-priority (a numerically higher value) than `DMA2_Stream1_IRQn`,**
so that Ethernet RX/TX servicing can never preempt a STEP-DMA
refill/DIR-write in progress. Verified against the current Phase 1
firmware: `Firmware/Core/Src/dma.c` sets
`HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 2, 0)`, and
`Firmware/CNC5AX-ETH.ioc` configures both `NVIC.ETH_IRQn` and
`NVIC.ETH_WKUP_IRQn` at preempt-priority `5` (`5 > 2`) — the requirement
already holds. See `Docs/PRE-IMPLEMENTATION-DECISIONS.md` item 11; any
future change to either priority (including an unreviewed CubeMX
regeneration) must preserve this ordering before being accepted.

**What ADR-005's reasoning loses, and what it does not.** ADR-005's
"frees `DMA1_Stream7` for a future DIR-DMA path" no longer applies, since
STEP is not on DMA1 at all. The DIR-DMA fallback is still available and
is in fact better placed: DMA2 Streams 2, 3 and 4 carry `TIM8_CH1/CH2/CH3`
(Table 44), any of which can drive `GPIOD->BSRR` from the same timer.
ADR-005's other reasons — one BSRR word for all five axes, no cross-port
arbitration skew, one stream instead of two — are unaffected and are
exactly why the corrected design is still single-stream.

**Alternatives considered:**

- *Keep TIM2/DMA1 and accept it* — not viable; it produces no output.
- *TIM2 + DMA1 into an APB1 peripheral that mirrors to GPIO* — no such
  path exists on this part.
- *CPU-written STEP from a TIM2 interrupt* — forbidden by ADR-001 and by
  `Docs/MOTION-ENGINE.md` §27 ("CPU-generated STEP edges: Not allowed"),
  and a 4 MHz interrupt is exactly what ADR-004 rules out.
- *TIM1 instead of TIM8* — equivalent; see above.

**Timing impact:** None relative to ADR-004's intent. The base tick, pulse
width, DIR margins and jitter analysis are unchanged; only the peripheral
instances differ.

**Memory impact:** None.

**CPU impact:** None.

**Risks:**

1. ~~The `.ioc` still specifies TIM2/DMA1_Stream1~~ — **closed.** The
   `.ioc` now carries `TIM8_UP` on `DMA2_Stream1` with direct mode, word
   width, MINC on, PINC off, circular, very-high priority, and
   `NVIC.DMA2_Stream1_IRQn` at preempt priority 2. CubeMX's own generated
   code independently assigns `DMA_CHANNEL_7`, matching Table 44.
2. This ADR contradicts three frozen ADRs. It is raised rather than
   applied silently, per `Docs/FIRMWARE-ARCHITECTURE.md` §42 Rule 9 and
   the source-of-truth hierarchy of §13, which puts official silicon
   documentation above prior project decisions on hardware questions.
3. The reading above is a documentation analysis. Test **HV-00** settles
   it empirically on silicon: it runs the timebase and confirms the
   stream's `NDTR` actually advances at the base-tick rate. The same test
   built against DMA1 should fail.

**Validation result:** Documentation analysis complete and corroborated by
CubeMX's own channel assignment; the integrated firmware builds and links.
On-silicon confirmation NOT RUN — pending hardware (HV-00).

---

### ADR-013 — PHY Reset Pulse Width (`PB0`/`PHY_NRST`)

**Decision:** `PB0` (`PHY_NRST`, active-low, per `Docs/PINOUT.md`) must be
explicitly driven **LOW for at least 150 µs**, then released HIGH, at the
start of the PHY reset sequence — before `low_level_init()`
(`Firmware/LWIP/Target/ethernetif.c`) performs any MDIO access. This
resolves `Docs/FIRMWARE-IMPLEMENTATION-PLAN.md` §4 item 8, the one item of
the original eight left open pending a project-owner call
(`Docs/PRE-IMPLEMENTATION-DECISIONS.md` item 8).

**Reason:**
- The current firmware only ever drives `PB0` HIGH (`MX_GPIO_Init()`) and
  never actively pulses it LOW, per `Docs/ETHERNET.md` §2.2's own
  power-on-timing note. The board has no RC delay or reset supervisor on
  `nRST` — only a plain pull-up (per the schematic) — so a deterministic
  reset today depends entirely on the LAN8720A's internal power-on reset
  plus `LAN8742_Init()`'s MDIO soft-reset (`BCR` bit 15). That is
  sufficient at cold power-up in practice, but is not a controlled,
  firmware-owned reset on every reset path (e.g. a warm/software MCU
  reset that does not re-power the PHY).
- **Correction (prototype review, `Docs/PROTOTYPE-BOARD.md` §5).** Both
  premises above describe the Waveshare module wrongly. Its `nRST` is
  **not on its header**, and it has an RC network (4.7 kΩ + 100 nF), not a
  plain pull-up. On the prototype this ADR's pulse therefore reaches
  nothing. The decision still stands for the **final PCB**, where the
  LAN8720A's `nRST` is wired to `PB0`, but one requirement was missed
  there: this ADR sized the pulse against `trstia` (100 µs) only. The
  datasheet also requires `nRST` to stay asserted until ≥ 25 ms after the
  supplies reach 80 % (`tpurstd`, Table 5-9), and the current
  implementation releases it a few milliseconds into boot. That must be
  fixed before the final board runs, and the board should carry a
  pull-down on `nRST` (`Docs/PROTOTYPE-BOARD.md` §7).
- **150 µs is the project owner's confirmed value**, chosen with explicit
  margin over the LAN8720A datasheet's general `trstia` reset-assertion
  minimum (~100 µs) — the same figure `Docs/ETHERNET.md` §2.2 had already
  flagged as a "small, contained firmware change, not an architectural
  one." This ADR adopts the confirmed number; it does not change the
  underlying mechanism §2.2 already proposed.

**Implementation note — DONE (Phase 2).** `HAL_Delay()`'s 1 ms `SysTick`
resolution cannot express a 150 µs pulse. This project already
has a `DWT->CYCCNT`-based cycle-accurate busy-wait pattern
(`Firmware/Platform/STM32F407/Src/stepgen_selftest_stm32f4.c`,
`while ((DWT->CYCCNT - t0) < n) { __NOP(); }`), but its DWT enablement
currently happens inside the stepgen port's own init, which runs later in
`main()` than `MX_GPIO_Init()`/`MX_LWIP_Init()`. The first of the two
options this ADR left open was taken: `net_port_delay_us()`
(`Firmware/Platform/STM32F407/Src/net_port_stm32f4.c`) enables the DWT
cycle counter itself if nothing else has, which is idempotent with respect
to `stepgen_port_init()`'s later enablement and deliberately does not zero
`CYCCNT` (that counter belongs to HV-04). The pulse itself is
`net_port_phy_hw_reset()`, called from `low_level_init()`'s
`USER CODE BEGIN MACADDRESS` block — that is, **before** `HAL_ETH_Init()`,
not merely before the first MDIO access, so the MAC's DMA soft reset never
runs against a PHY still held in reset.

Two facts established from the repository's own sources while implementing
it, neither of which changes the decision:

- The datasheet minimum this ADR cites as "~100 µs" is exact:
  `LAN8720A/LAN8720A_DataSheet-DS00002165.pdf` Table 5-9 gives
  `trstia` = 100 µs min. 150 µs stands at 1.5×.
- Datasheet §3.8.5.1 requires a clock on `XTAL1/CLKIN` *during* the reset.
  `Docs/ETHERNET.md` §2.2 confirms against the schematic that the module's
  own 50 MHz oscillator supplies both that pin and the MCU's `PA1` RMII
  reference clock, independently of `nRST` — so asserting the reset stops
  neither the PHY's clock nor the MAC's.

A settle time between release and first MAC/MDIO access
(`NET_PHY_RESET_SETTLE_US`, 1 ms) is an **implementation choice, not a
datasheet minimum**, and is flagged as such in `Net/Inc/net_config.h`: the
datasheet's only post-deassertion figures are `todad` (≤ 800 ns) and
§3.8.5's note that the RMII interface runs at 2.5 MHz for the first 16 µs
after reset. 1 ms is ~60× that, and costs nothing on a boot path.

**Alternatives considered:**
- *Leave `PB0` permanently HIGH (status quo)* — not adopted: works today
  only because the LAN8720A's internal POR happens to cover cold
  power-up; not a firmware-owned guarantee, and silently relies on PHY
  behavior this project does not control.
- *Use the general `trstia` minimum (~100 µs) with no added margin* — not
  adopted: the project owner confirmed 150 µs specifically for margin
  beyond the datasheet floor, consistent with this project's general
  preference for margin over operating at a spec's exact minimum
  (`Docs/MOTION-ENGINE.md` §7's DIR setup/hold margin note makes the same
  choice).

**Timing impact:** A one-time, boot-path-only 150 µs busy-wait before
Ethernet init; no impact on STEP/DIR timing (unrelated subsystem, and
occurs before the motion engine's timebase starts).

**Memory impact:** None.

**CPU impact:** Negligible — a single blocking delay executed once per
boot/reset, not in any real-time path.

**Risks:** None identified beyond the open implementation detail above
(which delay primitive to use before the DWT counter's normal
enablement point).

**Validation result:** Firmware change done and compile-verified; hardware
result still TBD. `HV-20` (`Docs/HARDWARE-VALIDATION.md`) now measures the
assertion in CPU cycles on target and checks that `nRST` reads back
released, so a delay loop that is silently too short fails a test rather
than violating `trstia` unnoticed. What still needs the board: an
oscilloscope capture of `PB0` confirming the pulse width, and PHY link-up
on every reset path — warm/software MCU reset included, which is the case
the status quo never covered.

### ADR-014 — Motion Protocol v1 (`C5P1`) over UDP

**Status:** ACCEPTED for Phase 3. The full wire format is
`Docs/PROTOCOL.md`, which is authoritative; this ADR records only the
decisions and the alternatives rejected, so the reasoning is not buried in
a field table.

**Context.** `Docs/FIRMWARE-IMPLEMENTATION-PLAN.md` §3 listed M13 (UDP) and
M14 (protocol) as blocked on six items: port number, packet format, motion
wire encoding, feedback format, backpressure mechanism and comm-timeout
threshold. `Docs/MACH3-INTERFACE.md` §7 listed the same gaps from the host
side. This ADR closes them.

**Evidence used.** `SDK/ncPod/ExternalMovement.cpp` and `ncPODDriver.h` for
the motion model (time-sliced per-axis velocity, 32 records ≈ 128 ms at
≈50 Hz, host-side fractional carry, host sequence echoed in status,
buffer-full backpressure with host retry, input/output bitfields and
per-axis step positions in status). `SDK/Galil PlugIn/G100-Structs.h` for
independent confirmation of the same shape from a second vendor, including
a queue sequence kept separate from the message sequence, and a window
field for flow control.

**Decisions:**

1. **UDP port 55010.** Arbitrary, in IANA's dynamic range, digits echoing
   the device address `192.168.5.10`. No SDK evidence exists for a port —
   `ncPod` is a USB device, and Galil's `13887` is that vendor's. Defined
   once in `Firmware/Net/Inc/net_config.h`.
2. **Little-endian wire.** `ncPod` byte-swaps because its PIC was
   big-endian; a Cortex-M4 talking to an x86 PC has no such reason, and
   copying the swap would cost work at both ends for nothing.
3. **Motion carried as signed 1/65536-step-per-slice `int32` per axis**,
   five axes, with the slice duration explicit in the packet. Keeps the
   host free of the device's step scaling, and the device free of the
   host's millimetre domain — the ADR-007 split. The device converts to
   `motion_segment_t` with its own `tick_hz`; both the slice conversion and
   the resulting rate are checked exactly, never clamped.
4. **A motion sequence (`block_seq`) separate from the message sequence
   (`seq`).** They answer different questions: a lost status poll is
   harmless, a lost motion block is a missing piece of the toolpath. Galil
   makes the same split.
5. **A `block_seq` gap stops motion and latches a fault.** Skipping forward
   would cut the wrong shape. Recovery is explicit, per ADR-010.
6. **A motion block is accepted atomically.** Partial acceptance would make
   a retry ambiguous, which is exactly how a half-applied block becomes a
   wrong toolpath.
7. **No NACK packet.** `STATUS` carries the rejection reason, so there is
   one reply type and one state machine per side instead of two.
8. **Comm timeout 200 ms, supervised only while motion is active.** Ten
   host cycles at 50 Hz, and deliberately shorter than the ~256 ms a full
   queue takes to drain, so a dead host is diagnosed as a timeout rather
   than surfacing later as starvation. A quiet host between programs is not
   a fault.
9. **Corrupt packets get no reply.** If the CRC failed, the sequence number
   in that packet is not trustworthy either.

**Alternatives considered:**

- *Forward `GMoves` verbatim* — rejected by ADR-007 already, and by the
  evidence: the reference device does not do it either.
- *Send the engine's Q32 rate directly on the wire* — rejected. It is the
  cheapest possible decode, but it hard-codes the device's base tick into
  the host's encoder, so a device running `stepgen_configure_max_rate()` at
  1 MHz would execute every feed at double speed with nothing to detect it.
- *Reuse `ncPod`'s opcode numbers* — rejected: they are that device's USB
  API, and matching numbers would imply a compatibility this device does
  not have.
- *Treat a motion gap as recoverable and continue* — rejected; see 5.
- *Per-record acknowledgement instead of per-block* — rejected as
  bandwidth and state for no safety gain over atomic blocks.
- *A TCP transport* — not considered seriously: `Docs/ETHERNET.md` §5 fixes
  UDP, and the SDK's own socket wrapper shows UDP is what Mach3 plugins
  use. Head-of-line blocking would also be the wrong failure mode for
  motion.

**Timing impact:** None on STEP generation. Decoding a full 32-record block
is bounded work — 160 multiply/divide pairs, no allocation, no loops beyond
`record_count` — and runs in the lwIP `NO_SYS` receive path, which executes
in the `MX_LWIP_Process()` superloop, **not** in the ETH interrupt. It is
preemptible by the STEP-DMA vector at NVIC priority 2 throughout (ADR-012).

**Memory impact:** One UDP PCB, a 96-byte status packet built on the stack,
and no new buffers. The motion queue is Phase 1's existing 64 slots.

**Risks:**

1. The whole protocol is exercised only against the host test suite and a
   Python tool. Nothing has round-tripped over real Ethernet — that needs
   the board (HV-30, `Docs/HARDWARE-VALIDATION.md`).
2. `OUTPUTS` has a fixed wire format but no implementation behind it (M9 /
   M10 do not exist). It is answered with `NOT_IMPLEMENTED` rather than
   silently accepted.
3. Rounding in the rate conversion leaves a bounded residual (under
   4 × 10⁻⁶ steps per 4 ms slice). The host is expected to close the loop
   from `pos_output[]`, as `ncPod`'s plugin does from its own `vtotal[]`.

**Validation result:** Host tests pass; hardware TBD.

---

### ADR-015 — Digital Input Manager and the E-STOP Path

**Status:** ACCEPTED for Phase 4 (M3). Implemented in `Firmware/Safety/`
and `Firmware/Platform/*/Src/safety_port_*.c`.

**Context.** Section 7 fixes the hardware (15 active-low inputs on
`PE0`–`PE14`, external pull-ups, both-edge EXTI, `PE2` = E-STOP) and
Section 20 requires a dedicated Input Manager, but both deliberately leave
the handling method open: "the implementation must determine whether each
input requires ... software filtering ... event latching ... immediate
safety action". ADR-004 already fixed the interrupt priorities; ADR-010
already fixed what an E-STOP does to the state machine, and added one
clause the firmware could not yet honour — that `EMERGENCY_STOP` may only
clear once the physical input has been released. This ADR decides the five
things that were left open.

**Decision.**

1. **Level-based, never edge-count-based.** Every ISR re-reads `GPIOE->IDR`
   and the superloop re-reads it again on each poll. Five of the six input
   vectors are shared between lines, an EXTI pending bit can coalesce two
   transitions, and a line can be masked by a self-test — under all three,
   an edge *count* desynchronises and a level *read* does not. The one case
   this also covers is the one that matters most: an E-STOP already held
   down at power-on has no edge left to give, and only its level says so.

2. **Assertion is immediate; release is filtered.** The 14 ordinary inputs
   get a symmetric stable-for-`SAFETY_DEBOUNCE_MS` filter in both
   directions. `PE2` does not: an assertion is acted on inside the EXTI2
   ISR with no filtering at all, and only the *release* is filtered, over a
   longer window (`SAFETY_ESTOP_RELEASE_MS`). The asymmetry is the safety
   property. Filtering an assertion would add latency to the one path
   Section 8 requires to be the fastest in the system; not filtering the
   release would let a bouncing or intermittent contact present itself as
   "released" and unlock a machine. Nothing about unlocking a machine is
   time-critical, so the two directions are not allowed to share a number.

3. **Published as logical assertion, not as pin level.** Every bitfield the
   subsystem exposes — including `Docs/PROTOCOL.md` §6's `inputs` — uses
   bit *n* = `PE`*n*, **1 = asserted**, i.e. the pin reads LOW. The
   alternative, publishing raw levels, makes an all-zero word mean "every
   input asserted", which is precisely the reading §6.2's `INPUTS_PRESENT`
   flag exists to make impossible to confuse with "no data". It also keeps
   the board's electrical polarity out of the host: the plugin applies
   Mach3's own per-signal `Negated` flags to a logical word, exactly as
   `ncPod` does with `PodStatus.inio` (`Docs/MACH3-INTERFACE.md` §4).

4. **The E-STOP release interlock is a predicate the engine holds, not a
   check at a call site.** ADR-010 requires that a clear be refused while
   `PE2` still reads asserted. The motion engine owns the STEP/DIR pins and
   cannot see `PE2`, so `stepgen_set_estop_gate()` takes a predicate and
   `safety_input_init()` registers `safety_estop_released()` into it. The
   interlock therefore exists as soon as the subsystem does and cannot be
   forgotten by a caller — the protocol layer's `CONTROL:CLEAR_ESTOP` gets
   it for free, and so does any future path.

5. **No per-input meaning is assigned in firmware.** ADR-010 already
   settled that the mapping from a pin to "X+ limit" or "probe" is
   host-side (`Engine->InSigs[]`). M3 therefore reports all 15 states
   accurately and promptly and decides nothing about them. The single
   exception is `PE2`, whose meaning is fixed by `Docs/PINOUT.md` and
   Section 8, not chosen here.

**Chosen values (OPEN, not measured):** `SAFETY_DEBOUNCE_MS` = 3,
`SAFETY_ESTOP_RELEASE_MS` = 50, `SAFETY_CHATTER_EDGES_PER_S` = 200. No
switch datasheet exists in this repository, so these are documented
defaults above typical mechanical bounce, not derived values. Rule 9
applies: HV-45 replaces them with measured ones, and until it has run they
must not be described as validated.

**Alternatives considered:**

- *Polling instead of EXTI* — rejected: Section 7 and `Docs/PINOUT.md` both
  require interrupt handling, and a poll fast enough for E-STOP would cost
  more than the interrupt it replaces.
- *Debouncing the E-STOP assertion too* — rejected, see decision 2.
- *Hardware RC filtering* — not rejected, out of scope: this is a firmware
  document and the board is what it is. Software filtering is what M3 can
  provide; if HV-45 finds a contact that needs hardware, that is a hardware
  change.
- *Publishing raw pin levels* — rejected, see decision 3.
- *Checking the E-STOP input inside the protocol layer's `CLEAR_ESTOP`
  handler* — rejected: it works, and it is one forgotten call site away
  from not working. A registered predicate cannot be bypassed by adding a
  second caller.
- *Letting a released E-STOP clear itself* — rejected outright by ADR-010's
  confirmed "recovery is never automatic". The release grants permission
  for an explicit clear and does nothing else.

**Timing impact:** The EXTI vectors sit at NVIC priority 0 (`PE2`) and 1
(the rest), both **above** the STEP ring refill at 2 (ADR-004) — which is
the point, and also means input ISR time is subtracted from the refill's
128 µs deadline whenever an input moves. HV-43 measures that cost and HV-04
holds the other half of the budget. The E-STOP path itself is HV-05 plus
HV-18.

**Memory impact:** ~250 bytes of static state (per-input filter timestamps
and edge counters), no dynamic allocation, no new buffers.

**CPU impact:** One `IDR` read plus a few instructions per edge; one pass
over 15 bits per superloop iteration. A chattering contact is the only way
this becomes significant, which is why it is counted and reported.

**Risks:**

1. The three timing constants are defaults, not measurements (see above).
2. The debounce runs on the 1 ms `HAL_GetTick()` timebase, so the effective
   window is `[T, T+1]` ms. Adequate at 3 ms; it would not be at 1 ms.
3. `s.estop_asserted` is written by both the ISR (assert) and the release
   timer (clear). The clear re-reads the pin immediately afterwards to
   close that window rather than masking EXTI2, because a briefly deaf
   E-STOP line is not an acceptable trade at any price. The residual window
   is one port read wide.
4. Everything above is verified on a host simulation of EXTI. Nothing has
   been measured on silicon — HV-40..HV-43 and HV-18/HV-45 are the tests
   that change that, and Rule 10 forbids claiming otherwise until they run.

**Validation result:** Host tests pass (151 checks); hardware TBD.

---

### ADR-016 — Output Manager and Spindle PWM

**Status:** ACCEPTED for Phase 5 (M9, M10). Implemented in `Firmware/IO/`
and `Firmware/Platform/*/Src/io_port_*.c`.

**Context.** Section 21 requires the non-motion outputs to be separated
from the hard real-time STEP/DIR subsystem and reachable "without allowing
background software to interfere with STEP/DIR timing". Section 22 requires
the spindle to be a separate subsystem again, and explicitly leaves its
frequency, duty-cycle range, scaling, command source and update mechanism
to be derived from the project requirements and the Mach3 SDK. ADR-010
fixed what faults do to the drives but said nothing about what they do to a
relay or a spinning tool. This ADR decides the five things that were open.

**Decision.**

1. **One interlock governs both, and it is recomputed from the engine state
   every superloop iteration.** Outputs are permitted only in `READY` and
   `RUNNING`. The subsystem never caches a decision: `io_poll()` reads
   `stepgen_get_status()` and applies the answer, so an output cannot
   outlive the condition that permitted it, whatever raced with what.

2. **Every fault class inhibits the outputs — including the two ADR-010
   exempts from dropping `EN`.** That exemption exists so an axis does not
   drift or drop under gravity when the command stream pauses; it is about
   *holding force*. A spinning tool is not holding force. When the stream
   stops the axes stop, and leaving the spindle turning then burns a
   stationary tool into the work and leaves a machine doing work with
   nobody driving it. `SAFE_IDLE` inhibits too: reaching `READY` takes an
   explicit `CONTROL:ENABLE_DRIVES`, and that deliberate act is the point
   at which the machine is live (§32).
   *This extends an ADR-010 clause the project owner confirmed for `EN`
   only, and is flagged for confirmation on that basis.*

3. **Off is reached from the interrupt, not from the superloop.**
   `io_emergency_off()` is register-writes-only and is registered with the
   E-STOP path by `io_init()`, so the relay drops on the `PE2` edge. On a
   machine whose relay is the spindle contactor, the difference between
   that and one main-loop iteration later is the difference between a safe
   stop and a dangerous one. An emergency stop also *discards* the pending
   requests rather than withholding them, so clearing the stop cannot
   re-energise anything by itself (ADR-010: recovery is never automatic).

4. **Spindle scaling: none, and the SDK settles it.** `spindle_pmille`
   (0…1000) is a direct quantisation of the SDK's own
   `MainPlanner->Spindle.ratio`, which
   `SDK/ncPod/MachDevImplementation.cpp` reads as "speed from 0 - 1" and
   clamps at 1 `[SDK-CONFIRMED]`. RPM never reaches this device: the host
   owns the RPM↔ratio map (`SpindleFM::SetSpindleSpeed`) because it is the
   only side that knows what spindle is fitted. Out-of-range is **refused,
   not clamped** — a host asking for 150 % has a bug, and quietly giving it
   100 % hides that bug behind a spinning tool. Note that `ncPod` never
   sends a zero duty while the spindle is on (`if (vel == 0) vel = 1;`);
   that is host policy and is deliberately *not* replicated, because when
   this device is told zero it means zero.

5. **The PWM period is refined from the `.ioc`'s, the frequency is not.**
   The `.ioc` carries `PSC=83`/`ARR=99`: exactly 10 kHz, but only 100 duty
   steps, so a host asking for 12.3 % would silently get 12 %. This port
   programs `PSC=0`/`ARR=8399` instead — identical 10.000 kHz, 8400 steps,
   84× the resolution, no prescaler. The frequency is the only spindle
   number `Docs/PINOUT.md` fixes and it is unchanged; HV-51 reads the
   divider back from the hardware and reports the actual frequency in Hz.
   The compare register is preloaded, so a duty change lands at a period
   boundary and never produces a short or stretched pulse.

**Also fixed here, because nothing else owned it:** the two status LEDs
report the engine state and are **not** host-commandable. A host-controlled
"error" light can be made to lie, and on a board with no display this pair
is frequently the only thing that says anything at all. `FAULT` blinks and
`EMERGENCY_STOP` is solid, deliberately — one is cleared with a command,
the other needs somebody to release a physical button first, and telling
them apart from across a workshop is worth a distinct pattern.

**Chosen values (OPEN, not measured):** LED blink periods 500 ms (`READY`)
and 125 ms (`FAULT`). `CNC_LED_ACTIVE_HIGH` = 1 is an assumption read from
the CubeMX-generated reset state (both LEDs driven LOW at init); no LED
drive circuit is documented in this repository. HV-55 confirms it on a
board, and until then Rule 9 applies.

**Alternatives considered:**

- *Letting the ADR-010 hold-position exception cover the spindle* —
  rejected, see decision 2. The exception's own stated reason does not
  transfer.
- *Permitting outputs in `SAFE_IDLE`* — rejected as the conservative
  default. A host wanting coolant before arming the drives is a plausible
  workflow that nothing in this repository actually asks for; if it turns
  out to be needed, this is a one-line change to `compute_inhibit()` and
  belongs in a revision of this ADR, not in a call site.
- *Clamping an out-of-range duty* — rejected, see decision 4.
- *Keeping the `.ioc`'s `ARR=99`* — rejected: it silently discards the
  bottom digit of every duty the wire format can carry, for no benefit.
- *Host-commandable LEDs* — rejected, see above.
- *Applying an `OUTPUTS` packet partially when one field is bad* —
  rejected: a host that asked for two things and got one has no way to
  discover which.

**Timing impact:** None on the motion path. The subsystem touches three
GPIO bits and one compare register, none of which the STEP DMA path reads,
and it runs entirely in the superloop except the E-STOP kill, which is a
handful of register writes inside an interrupt that already exists.

**Memory impact:** ~40 bytes of static state. No buffers, no allocation.

**CPU impact:** One `stepgen_get_status()` copy and a few stores per
superloop iteration.

**Risks:**

1. Decision 2 extends an owner-confirmed ADR-010 clause; it is the safe
   direction, but it is this ADR's inference and not the owner's words.
2. `CNC_LED_ACTIVE_HIGH` is an assumption (see above).
3. The blink periods are defaults, not measurements.
4. `PB4` is `NJTRST` at reset. This works only because the project debugs
   over SWD (§29). Re-enabling JTAG would silently take the spindle pin.
5. Everything is verified on a host simulation of GPIO and a timer.
   Nothing has been measured on silicon — HV-50..HV-55 are the tests that
   change that, and Rule 10 forbids claiming otherwise until they run.

**Validation result:** Host tests pass (1160 checks); hardware TBD.

# 42. AI-Assisted Development Rules

This repository is intended to support AI-assisted firmware development.

The AI must follow these rules.

### Rule 1 — Read before modifying

Before modifying firmware, inspect:

```text
README.md
Docs/SYSTEM-ARCHITECTURE.md
Docs/PINOUT.md
Docs/ETHERNET.md
Docs/MOTION-ENGINE.md
MACH3/SDK-README.MD
```

and the relevant STM32 documentation.

---

### Rule 2 — Mach3 behavior must come from the SDK

Never invent:

- Mach3 API behavior
- Motion data semantics
- Callback behavior
- Threading assumptions
- Timing assumptions
- Communication behavior

Verify them from the SDK.

---

### Rule 3 — Hardware behavior must come from official documentation

Verify:

- Pin assignments
- Alternate Functions
- Timer capabilities
- DMA mapping
- Register behavior
- Interrupt mapping
- Ethernet MAC behavior

against the STM32 documentation.

---

### Rule 4 — Safety has priority

Safety-critical inputs and end-stop handling must not be delayed by:

- Ethernet
- Logging
- Diagnostics
- Non-critical application code

---

### Rule 5 — Real-time motion must remain deterministic

Networking and background processing must not compromise:

```text
STEP timing
DIR timing
Axis synchronization
```

---

### Rule 6 — Do not arbitrarily lock implementation choices

The following must remain design decisions until analyzed:

```text
FreeRTOS vs Bare-Metal
PWM vs Output Compare
DMA architecture
Buffer architecture
State machine
Memory strategy
Interrupt priority values
```

The AI must choose based on evidence.

---

### Rule 7 — Prefer the simplest architecture that meets all requirements

Do not introduce an RTOS, complex queue system, excessive abstraction layers, or unnecessary middleware merely because they are common patterns.

Every additional architectural component introduces:

- Complexity
- RAM usage
- CPU overhead
- Debugging cost
- Failure modes

The selected architecture should be as simple as possible while maintaining the required timing, safety, scalability, and maintainability.

---

### Rule 8 — Optimize for margin

The objective is not:

```text
Barely passes specification
```

The objective is:

```text
Requirement
    ↓
Measured worst-case performance
    ↓
Significant engineering margin
```

---

### Rule 9 — Do not hide uncertainty

If the required behavior cannot yet be established, explicitly mark it as:

```text
TBD
```

or:

```text
Requires verification
```

Do not silently replace an unknown requirement with an assumption.

---

### Rule 10 — Do not claim hardware capability without validation

A theoretical calculation is not enough to claim:

```text
3 axes × 2 MHz
```

or any other real-time performance requirement.

Actual hardware testing is required.

---

# 43. Recommended Source Tree

The final firmware source tree may evolve during implementation.

A possible logical structure is:

```text
Firmware/
│
├── Core/
│   ├── Inc/
│   └── Src/
│
├── Drivers/
│   ├── Hardware/
│   ├── Timer/
│   ├── DMA/
│   └── Ethernet/
│
├── Safety/
│
├── Motion/
│   ├── Axis/
│   ├── Planner/
│   ├── Interpolation/
│   └── StepGen/
│
├── Communication/
│   ├── LwIP/
│   ├── UDP/
│   └── Protocol/
│
├── IO/
│   ├── DigitalInput/
│   ├── Outputs/
│   └── Spindle/
│
├── System/
│   ├── State/
│   ├── Fault/
│   └── Diagnostics/
│
└── Middlewares/
```

This is a suggested logical organization, not a mandatory directory structure.

The final source tree should be selected after the implementation architecture is known.

---

# 44. Project Documentation Relationships

The firmware architecture must be consistent with the following project documents:

```text
Docs/
├── PINOUT.md
├── SYSTEM-ARCHITECTURE.md
├── ETHERNET.md
├── MOTION-ENGINE.md
└── FIRMWARE-ARCHITECTURE.md
```

and:

```text
MACH3/
└── SDK-README.MD
```

The STM32 hardware references are located in:

```text
STM32_DOCs/
```

LAN8720A hardware references are located in:

```text
LAN8720A/
```

---

# 45. Final Architectural Principle

The firmware should be built around the following principle:

```text
                    MACH3
                      │
                      ▼
                Host Integration
                      │
                      ▼
                 Ethernet / UDP
                      │
                      ▼
                 Protocol Layer
                      │
                      ▼
                 Motion Engine
                      │
              ┌───────┴────────┐
              ▼                ▼
       Motion Timing       Safety State
              │                ▲
              ▼                │
          Timer / DMA      Digital Inputs
              │                │
              ▼                │
          STEP / DIR ◄─────────┘
              │
       ┌──────┼──────┬──────┬──────┐
       ▼      ▼      ▼      ▼      ▼
       X      Y      Z      A      B
```

The central architectural priorities are:

```text
Safety
  ↓
Deterministic Motion
  ↓
Correct Mach3 Integration
  ↓
Reliable Ethernet Communication
  ↓
Maintainability
  ↓
Diagnostics / Auxiliary Functions
```

The implementation must preserve these priorities even when software architecture, RTOS selection, networking load, or future features evolve.

---

# 46. Scope

This document defines the **firmware architecture constraints and design principles** of CNC5AX-ETH.

It does not replace:

```text
Docs/MOTION-ENGINE.md
```

for motion-specific requirements,

```text
Docs/PINOUT.md
```

for hardware pin assignments,

```text
Docs/ETHERNET.md
```

for Ethernet architecture,

or:

```text
MACH3/SDK-README.MD
```

for Mach3 SDK behavior.

The final implementation must remain consistent with all of these documents.
