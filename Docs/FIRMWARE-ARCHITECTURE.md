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

**Decision:** STEP pulses for all five axes (PC9, PA8, PA9, PA10, PA11) are generated via a shared base timer whose Update event triggers DMA transfers directly into each GPIO port's BSRR register (`GPIOA->BSRR` for Y/Z/A/B, `GPIOC->BSRR` for X). The pins are configured as standard GPIO Output (Push-Pull), **not** Alternate Function — native PWM/Output-Compare generation is intentionally not used.

**Reason:** PA8–PA11 correspond to TIM1_CH1–CH4, which share a single ARR (period) register. Native PWM/Output-Compare would therefore force axes Y, Z, A, and B onto one common STEP frequency, which conflicts with the project requirement that each axis run at an independently commanded step rate (e.g. during a coordinated multi-axis move). DMA-to-BSRR decouples each axis's effective frequency from any single timer's shared period.

**Alternatives considered:**
- *Native PWM/Output-Compare per channel* — rejected: shared-ARR constraint above.
- *Output-Compare toggle mode with independent per-channel CCR updates via DMA* — technically possible, but requires a separate DMA request/stream and state machine per channel, increasing complexity and DMA budget with no timing benefit over the BSRR method.

**Timing impact:** Each axis's STEP frequency becomes fully independent of the others. Jitter is bounded by base-timer resolution and DMA transfer latency — exact figures **TBD**, pending hardware measurement (Section 39).

**Memory impact:** One DMA buffer per port (GPIOA, GPIOC), sized to the interpolation tick depth — exact size **TBD**.

**CPU impact:** Near-zero during steady-state pulse generation; CPU only refills buffers at a lower, batched rate.

**Risks:** Requires two DMA streams/masks (one per port) instead of one; must be checked against Ethernet DMA stream usage for conflicts — **TBD**.

**Validation result:** TBD — pending real-hardware testing (≥3 axes simultaneously at 2 MHz, per Section 39).

---

### ADR-002 — STEP-Generation Base Timer & DMA Allocation

**Decision:** `TIM2` is the shared base timer whose Update event triggers the STEP-generation DMA transfers established in ADR-001. `TIM2`'s Update-event DMA request (`TIM2_UP`) is expected to be serviced through **DMA1** (request mapping places `TIM2_UP` on `Stream 1`/`Channel 3`, with `Stream 7`/`Channel 3` available as the alternate mapping) — one stream feeding `GPIOA->BSRR` (Y/Z/A/B) and the other feeding `GPIOC->BSRR` (X), both triggered from the same `TIM2_UP` event.

**Reason:**
- `Docs/MOTION-ENGINE.md` Section 33 originally named `TIM2` or `TIM3` as candidates. `TIM3_CH1` (`PB4`) is already committed to Spindle PWM at a fixed 10 kHz period (`Docs/PINOUT.md`). Sharing `TIM3` between Spindle PWM and the STEP-DMA base rate would recreate the same shared-ARR conflict ADR-001 already rejected for `TIM1` — the spindle's fixed 10 kHz period and the STEP base tick rate would be forced to share one period register. `TIM2` has no other confirmed use in this project and avoids the conflict entirely.
- `TIM2` is a 32-bit general-purpose timer, giving more prescaler/ARR range than the 16-bit alternatives for tuning the base tick rate.
- The STM32F407 Ethernet MAC uses its own dedicated internal DMA engine, entirely separate from the DMA1/DMA2 general-purpose controllers. `TIM2`'s use of DMA1 therefore cannot contend with Ethernet RX/TX DMA traffic.
- No other peripheral currently defined in `Docs/PINOUT.md` uses the remaining DMA1 clients (I2C1–3, SPI2–3, USART3, UART4/5), so `DMA1 Stream1`/`Stream7 Channel 3` are expected to be free.

**Alternatives considered:**
- *`TIM3`* — rejected: conflicts with Spindle PWM's fixed 10 kHz period (see Reason).
- *`TIM4`/`TIM5`* — not selected: no advantage over `TIM2` for this role, and `TIM2`'s 32-bit counter is preferable; may be revisited only if `TIM2` becomes needed elsewhere.

**Timing impact:** Unchanged from ADR-001 — base-tick jitter remains bounded by timer update-event timing and DMA arbitration latency; exact figures **TBD**, pending hardware measurement (Section 39).

**Memory impact:** Unchanged from ADR-001 (one DMA buffer per GPIO port).

**CPU impact:** None beyond ADR-001; `TIM2` configuration is a one-time initialization cost.

**Risks / open items:**
- The exact DMA1 stream/channel assignment (which of `Stream 1`/`Stream 7` feeds `GPIOA` vs `GPIOC`) must still be confirmed against RM0090's DMA1 request-mapping table before implementation. **The RM0090 PDF currently checked into this repository (`STM32_DOCs/Reference_Manual/`) is a 2-byte placeholder file, not the real document, and must be replaced with a valid copy before this mapping can be verified against the authoritative source**, per the project's source-of-truth rule (Section 13).
- `TIM2`'s clock source, prescaler, and the resulting base-tick rate are motion-engine implementation decisions (`Docs/MOTION-ENGINE.md` Section 27) and remain **TBD**.

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
