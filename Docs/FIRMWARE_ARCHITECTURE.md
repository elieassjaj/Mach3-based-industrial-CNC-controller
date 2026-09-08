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
        Timer / DMA / HW
              │
              ▼
          STEP / DIR
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
Timer / DMA
   ↓
STEP / DIR
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

E-Stop is handled via EXTI on PE2 (not polling)
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
Docs/MOTION_ENGINE.md
```

---

# 10. STEP Generation Architecture

The hardware PCB places the STEP outputs on timer Alternate Function capable pins.

The initial implementation direction is:

```text
Timer + DMA
```

However, the final implementation must determine whether the most suitable method is:

- PWM
- Output Compare
- Toggle mode
- One-pulse generation
- Timer + DMA
- DMA-driven GPIO updates
- Another hardware-assisted mechanism
- A combination of these approaches

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
MACH3/SDK_README.MD
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

The firmware architecture must **not** assume FreeRTOS or Bare-Metal operation in advance.

The final decision must be made after evaluating:

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

The implementation decision should be documented after analysis.

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
Docs/ethernet.md
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

SWD access must remain available throughout development.

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
Trajectory / Interpolation
  ↓
Timer / DMA
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

This prevents future AI-assisted development from unintentionally reversing important architectural decisions.

---

# 42. AI-Assisted Development Rules

This repository is intended to support AI-assisted firmware development.

The AI must follow these rules.

### Rule 1 — Read before modifying

Before modifying firmware, inspect:

```text
README.md
Docs/SYSTEM_ARCHITECTURE.md
Docs/PINOUT.md
Docs/ethernet.md
Docs/MOTION_ENGINE.md
MACH3/SDK_README.MD
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
├── SYSTEM_ARCHITECTURE.md
├── ethernet.md
├── MOTION_ENGINE.md
└── FIRMWARE_ARCHITECTURE.md
```

and:

```text
MACH3/
└── SDK_README.MD
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
Docs/MOTION_ENGINE.md
```

for motion-specific requirements,

```text
Docs/PINOUT.md
```

for hardware pin assignments,

```text
Docs/ethernet.md
```

for Ethernet architecture,

or:

```text
MACH3/SDK_README.MD
```

for Mach3 SDK behavior.

The final implementation must remain consistent with all of these documents.
