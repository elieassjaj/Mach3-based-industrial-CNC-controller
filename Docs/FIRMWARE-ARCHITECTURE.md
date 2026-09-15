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

**Decision:** DIR (`PD8`–`PD12`, active-high) is generated by **CPU-timed GPIO writes with a guard interval of at least one base tick (250 ns)** around the relevant STEP edge, not by a dedicated DMA/timer path.

**Reason:**
- The fixed requirement is `DIR setup ≥ 200 ns` and `DIR hold ≥ 200 ns` (`Docs/MOTION-ENGINE.md` Section 7). One `TIM2` base tick (250 ns, ADR-004) already exceeds both minimums with margin, provided the motion engine enforces "no STEP edge for an axis within one tick of that axis's DIR change" as a scheduling rule — i.e. the margin comes from the tick grid itself, not from software timing loops.
- A DMA-hardware DIR path was the alternative (`DMA1_Stream7`/`Channel3`, the `TIM2_UP` slot freed by ADR-005) and remains architecturally available, but it is not needed to meet the fixed timing requirement, and Rule 7 (Section 42, "prefer the simplest architecture that meets all requirements") directs against adding it without a demonstrated need.
- CPU-timed writes are simpler: DIR changes happen only between motion segments for a given axis (far less frequently than STEP edges), so there is no risk of this becoming a hot path.

**Alternatives considered:**
- *Second DMA stream to `GPIOD->BSRR`, synchronized to `TIM2`* — not adopted now; kept as the documented fallback if hardware measurement (Section 39) ever shows the CPU-timed approach insufficient. Because `DMA1_Stream7`/`Channel3` is already reserved and unused (ADR-002/ADR-004), adopting this later does not require re-deriving a DMA resource.

**Rule enforced by the Motion Engine (M5/M7, `Docs/FIRMWARE-IMPLEMENTATION-PLAN.md`):** a direction change for an axis must be scheduled at least one tick before that axis's next STEP edge, and no STEP edge may be scheduled within one tick after a direction change for that axis. This is a scheduling-algorithm constraint (ADR-007), not a hardware one.

**Timing impact:** Meets the fixed 200 ns setup/hold requirement with 50 ns (25%) margin at the granularity of a single base tick; exact hold/setup margin must still be confirmed on real hardware (Section 39), since GPIO output slew and DMA/CPU bus arbitration jitter both apply.

**Memory impact:** None beyond existing per-axis state.

**CPU impact:** Negligible — a plain `GPIOD->BSRR` write per direction change, only at segment boundaries.

**Risks:** If a future motion profile requires direction reversals more frequently than the tick-guard scheduling rule can absorb (e.g. extremely high-frequency oscillatory motion), this decision should be revisited in favor of the DMA fallback above.

**Validation result:** TBD — pending real-hardware DIR setup/hold measurement (Section 39).

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

1. **STEP-DMA buffer (M4, low level):** a small, fixed-size circular buffer of precomputed `GPIOA->BSRR` words, refilled by the CPU on the DMA half-transfer and transfer-complete interrupts (already established in ADR-004's DMA configuration: circular mode, both interrupts enabled). Its size is an internal implementation parameter, not a protocol concern, and is not frozen by this ADR — it should be chosen to keep the refill-interrupt rate low (comfortably sub-10 kHz) without costing meaningful RAM (a few KB is more than sufficient at 4 MHz), and tuned once real interrupt-latency measurements exist (Section 39).
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

**Specific behaviors fixed by this ADR:**
- **Entering `EMERGENCY_STOP` or `FAULT`:** `EN` (`PD15`) is deasserted (LOW) immediately, disabling the drives; the STEP-DMA (M4) is halted. This is the same "motion must not proceed once a fault is recognized" principle already required by Section 21/23.
- **Communication timeout:** treated as a `FAULT` (not `EMERGENCY_STOP`) — the local E-STOP path must never depend on the network (Section 8, Section 28 of `Docs/ETHERNET.md`), and a lost network link is not itself a machine safety event. On timeout, the motion buffer is treated as if it underflowed (ADR-008): the interpolator halts new STEP output for the affected motion, `EN` deasserts, and a `COMM_TIMEOUT` fault is raised.
- **Limit-switch inputs** (the 14 non-E-STOP `PE0/1/3–14` inputs): per `Docs/FIRMWARE-ARCHITECTURE.md` Section 7, "not every input necessarily requires identical treatment" — but no per-input semantic assignment (which physical input is X+ limit, Y− limit, probe, etc.) exists anywhere in the repository yet. That mapping is host/Mach3-signal-table-driven (`Docs/MACH3-INTERFACE.md` Section 4, `Engine->InSigs[]`), so the firmware's own obligation is only to report all 14 raw states promptly and accurately; *deciding* that a given input means "stop now" is expected to happen on the host side unless a future revision of this document assigns specific safety semantics to specific pins in firmware.

**Reason:** the states and the FAULT/EMERGENCY_STOP split follow directly from Sections 7, 8, 21, 23 and 32 already cited; the recovery rule (explicit clear required, never automatic) is the standard conservative default for machine safety systems and is consistent with this project's own "preserve engineering margin," "do not hide uncertainty" principles (Section 42, Rules 8–9) — silently auto-clearing a fault would be exactly the kind of assumption those rules warn against.

**Alternatives considered:**
- *Auto-clear on condition resolution* (e.g. automatically returning to `READY` the instant communication resumes, or the buffer refills) — not adopted as the default. This is flagged explicitly for confirmation rather than decided unilaterally, since it is a genuine safety-policy judgment call, not something derivable from the documents alone. **This needs the project owner's decision — see the question below.**

**Timing impact:** State transitions themselves are not hard-real-time; the underlying actions they trigger (deasserting `EN`, halting DMA) must be, and already are, since they reuse the existing GPIO/DMA control paths.

**Memory impact:** One state variable plus a small fault-code/fault-flags value.

**CPU impact:** Negligible.

**Risks:** The exact set of conditions that escalate a `FAULT` versus that stay merely "held" (e.g. does a single missed packet count as a timeout, or only N consecutive misses / a fixed watchdog window?) is not fixed here — it depends on the still-undesigned communication protocol's timing (`Docs/MACH3-INTERFACE.md` Section 7) and is deferred to that design.

**Validation result:** N/A for the state structure; the fault-response latencies (E-STOP, comm-timeout) require hardware measurement once implemented (Section 39).

---

### ADR-011 — MAC Address: Bench-Test vs. Production Strategy

**Decision:** Two distinct, explicitly separated policies:

1. **Bench-test / development (now):** use a **locally-administered, unicast MAC address** in place of the current CubeMX placeholder (`00:80:E1:00:00:00`, which is a real vendor's OUI prefix and must not ship even on an isolated link, since it is not this project's to use). A locally-administered address sets the U/L bit (bit 1 of the first octet) to `1` and the multicast bit (bit 0) to `0` — e.g. `02:00:05:10:00:01`, chosen to echo this project's static IP octets (`192.168.5.10`, ADR/`Docs/ETHERNET.md` Section 15) purely as a memorable bench convention, with no other significance. This is safe on the isolated point-to-point link this project targets and requires no external registration.
2. **Production / final product:** **not decided here.** A shipped product should not rely on an ad-hoc locally-administered address chosen by firmware developers if the controller is ever expected to coexist on a shared network with other devices (even though the current architecture targets an isolated link) or if the project is ever produced in quantity (where address collisions across units of the *same* project become possible if every unit shares one hardcoded address). Real options — buying an IEEE OUI block, using a distributor-assigned block, deriving a per-unit address from the MCU's unique ID (`Docs/FIRMWARE-ARCHITECTURE.md`-adjacent: STM32F407 has a factory-programmed 96-bit unique device ID that could seed a locally-administered address to make every unit distinct) — are a product/business decision outside what this repository's documents establish. **This needs the project owner's decision — see the question below.**

**Reason:** distinguishing these two cases avoids two different failure modes: shipping a real vendor's OUI without authorization (the current placeholder's problem), and freezing a single hardcoded address as if it were a final answer when the actual constraint (will multiple units of this controller ever exist on the same network?) hasn't been decided.

**Alternatives considered:** keeping the CubeMX placeholder — rejected outright, it belongs to a real vendor (per the OUI byte pattern) and using it is not this project's to do even for bench testing.

**Timing impact:** None.

**Memory impact:** None.

**CPU impact:** None.

**Risks:** If multiple bench units ever end up on the same switch/network segment simultaneously, using the *same* bench MAC on more than one of them will cause ARP/switching conflicts — fine for a single isolated point-to-point link (this project's stated target, `Docs/ETHERNET.md` Section 15) but worth remembering if a second unit is ever brought up on the same network for comparison testing.

**Validation result:** N/A.

---

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
