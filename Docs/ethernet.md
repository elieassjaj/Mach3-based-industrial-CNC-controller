# CNC5AX-ETH: Ethernet & LwIP Network Architecture

## 1. Document Status

This document defines the Ethernet and network architecture of the CNC5AX-ETH controller.

The purpose of this document is to provide a reliable reference for firmware development and to clearly distinguish between:

- confirmed hardware characteristics
- confirmed project decisions
- proposed software architecture
- examples
- items that still require verification

### Status Tags

| Tag | Meaning |
|---|---|
| `[HW-CONFIRMED]` | Confirmed by the project hardware/design documentation |
| `[FW-CONFIRMED]` | Confirmed by the actual firmware/source code |
| `[PROJECT-DECISION]` | Explicit project requirement or architectural decision |
| `[DESIGN-RECOMMENDATION]` | Recommended architecture or implementation practice |
| `[EXAMPLE]` | Example only; not a project requirement |
| `[TBD]` | Not yet verified or finalized |

> **Important:** AI-assisted development must not treat `[EXAMPLE]`, `[DESIGN-RECOMMENDATION]`, or `[TBD]` information as an implemented project feature.

---

# 2. Hardware Ethernet Architecture

## 2.1 Hardware Components

[HW-CONFIRMED]

The Ethernet subsystem is based on:

| Component | Configuration |
|---|---|
| MCU | STM32F407VGT6 |
| Ethernet PHY | LAN8720A |
| MCU Ethernet interface | Integrated Ethernet MAC |
| MAC ↔ PHY interface | RMII |
| MCU external oscillator | 8 MHz HSE crystal |

The STM32F407VGT6 contains an integrated 10/100 Ethernet MAC.

The LAN8720A provides the physical Ethernet interface between the MCU and the Ethernet network.

The MAC-to-PHY connection uses RMII.

```text
Host PC
   │
   │ Ethernet
   ▼
RJ45 / Ethernet Interface
   │
   ▼
LAN8720A PHY
   │
   │ RMII
   ▼
STM32F407VGT6
   │
   └── Integrated Ethernet MAC
```

### Clock Architecture

[HW-CONFIRMED / TBD]

The MCU uses an external 8 MHz HSE crystal.

```text
8 MHz Crystal
      │
      ▼
STM32F407 HSE
      │
      ▼
MCU Clock System
```

The Ethernet RMII reference clock is a separate requirement and must not be confused with the 8 MHz HSE clock.

```text
MCU HSE Clock
      ≠
RMII 50 MHz Reference Clock
```

[TBD]

The exact LAN8720A RMII clock mode and the source/direction of the 50 MHz RMII reference clock must be verified against the final schematic and LAN8720A configuration.

---

# 3. Ethernet MAC and DMA

[HW-CONFIRMED]

The STM32F407 Ethernet peripheral includes an Ethernet MAC and DMA functionality.

Conceptually:

```text
LAN8720A
    │
    │ RMII
    ▼
STM32 Ethernet MAC
    │
    ▼
Ethernet DMA
    │
    ▼
RAM
```

DMA is used to transfer Ethernet frames between the Ethernet peripheral and memory with reduced CPU involvement.

[DESIGN-RECOMMENDATION]

Ethernet DMA processing must be configured so that it does not interfere with deterministic motion-control operations.

Important parameters include:

- DMA descriptors
- RX buffers
- TX buffers
- buffer ownership
- interrupt configuration
- memory placement
- cache/buffer considerations
- packet-processing time

[TBD]

The exact DMA descriptor and buffer architecture must be taken from the final STM32 Ethernet driver implementation.

---

# 4. LwIP Network Stack

[PROJECT-DECISION]

The project uses the LwIP TCP/IP stack.

The intended protocol stack is:

```text
Application / Motion Protocol
          │
          ▼
         UDP
          │
          ▼
          IP
          │
     ┌────┴────┐
     │         │
    ARP       ICMP
     │         │
     └────┬────┘
          ▼
      Ethernet
          │
          ▼
      STM32 MAC
          │
          ▼
         RMII
          │
          ▼
      LAN8720A
```

LwIP is responsible for the network protocol layers.

The motion-control subsystem remains separate from the network stack.

---

# 5. Transport Protocol

## 5.1 UDP

[PROJECT-DECISION]

UDP is selected as the intended transport protocol for the CNC motion communication layer.

UDP is suitable for applications where low protocol overhead and packet-oriented communication are important.

UDP does not provide:

- guaranteed packet delivery
- guaranteed packet ordering
- duplicate protection
- application-specific synchronization

Therefore, if the CNC communication protocol requires these properties, they must be implemented at the application layer.

```text
Host
 │
 │ UDP
 ▼
Ethernet
 │
 ▼
CNC5AX-ETH
 │
 ▼
Motion Protocol
 │
 ▼
Motion Controller
```

---

# 6. Host Communication

## 6.1 Host Software

[TBD]

The exact host-side integration has not been defined by this document.

Potential host systems may include:

- Mach
- LinuxCNC
- custom host software

However, the presence of a UDP interface does **not** by itself establish compatibility with Mach or LinuxCNC.

The following items must be defined before claiming host compatibility:

- host-side driver/plugin
- packet format
- command protocol
- timing requirements
- synchronization mechanism
- feedback protocol
- error handling
- connection/watchdog behavior

Therefore:

```text
Mach / LinuxCNC UDP Compatibility = TBD
```

until the actual host integration is implemented and tested.

---

# 7. Communication Rate

[TBD]

The previous documentation referenced approximately 1 kHz as a possible target communication rate.

This value is **not a confirmed Ethernet specification**.

It should therefore be treated as:

```text
Initial design target: 1 kHz
Final communication rate: TBD
```

The final rate must be determined from:

- host-side implementation
- packet size
- Ethernet bandwidth
- LwIP processing time
- MCU CPU load
- motion-buffer architecture
- required motion look-ahead
- deterministic timing requirements

A 1 kHz network update rate must not be confused with the STEP pulse generation frequency.

```text
Network Update Rate
        ≠
STEP Pulse Frequency
```

---

# 8. LwIP API

## 8.1 RAW API

[PROJECT-DECISION / TBD]

The intended LwIP programming model is the RAW API because it provides a callback-based interface with low software overhead.

The UDP receive path can use an LwIP UDP receive callback such as:

```c
udp_recv();
```

Conceptually:

```text
Ethernet Frame
      │
      ▼
Ethernet DMA
      │
      ▼
     LwIP
      │
      ▼
     UDP
      │
      ▼
 UDP Receive Callback
      │
      ▼
Packet Validation
      │
      ▼
Protocol Parser
      │
      ▼
Motion Layer
```

[FW-CONFIRMED / TBD]

The exact implementation must be verified against the actual generated and application firmware.

The documentation must not assume a specific callback execution context until the final firmware architecture is established.

---

# 9. UDP Receive Processing

[DESIGN-RECOMMENDATION]

The UDP receive callback should remain lightweight.

It should preferably perform only:

1. packet validation
2. extraction of required information
3. hand-off to the motion/protocol layer

Avoid performing long-running or blocking operations inside the network callback.

Recommended architecture:

```text
UDP Receive Callback
        │
        ├── Validate packet
        │
        ├── Parse packet
        │
        └── Pass command to motion layer
                    │
                    ▼
              Motion Processing
```

The callback should not directly perform heavy:

- kinematic calculations
- motion planning
- blocking operations
- lengthy calculations
- operations that could compromise STEP timing

unless explicitly required by the final architecture.

---

# 10. Packet Validation

[PROJECT-DECISION]

All data received from Ethernet must be treated as untrusted input until validated.

The protocol parser should validate, where applicable:

- packet size
- protocol header
- magic/signature value
- command type
- payload length
- sequence number
- axis data range
- checksum/CRC

Not all of these fields are currently implemented.

[TBD]

The final packet format must define which validation fields are mandatory.

---

# 11. Application Protocol

[TBD]

The application-layer motion protocol has not yet been fully specified by this document.

The following information must eventually be defined:

```text
Packet Header
Packet Type
Sequence Number
Payload Length
Axis Data
Flags
Checksum / CRC
```

A possible conceptual structure is:

```text
┌────────────────────────────┐
│ Packet Header              │
├────────────────────────────┤
│ Packet Type                │
├────────────────────────────┤
│ Sequence Number            │
├────────────────────────────┤
│ Payload Length             │
├────────────────────────────┤
│ Motion Payload             │
├────────────────────────────┤
│ Optional CRC / Checksum    │
└────────────────────────────┘
```

[EXAMPLE]

This structure is illustrative only.

It is **not the final CNC5AX-ETH packet specification**.

---

# 12. Memory Management

[DESIGN-RECOMMENDATION]

LwIP memory configuration must be sized according to the expected network traffic and the available STM32F407 RAM.

Important parameters include:

- number of RX buffers
- number of TX buffers
- number of simultaneously allocated PBUFs
- packet size
- RX packet rate
- TX packet rate
- packet-processing time
- available RAM
- DMA descriptor requirements

---

# 13. PBUF

[DESIGN-RECOMMENDATION]

LwIP uses PBUF structures for packet storage and management.

Conceptually:

```text
Ethernet DMA
      │
      ▼
  RX Buffer
      │
      ▼
     LwIP
      │
      ▼
     PBUF
      │
      ▼
 UDP Callback
      │
      ▼
Motion Protocol
```

Unnecessary memory copies should be minimized where the selected driver architecture safely permits this.

> **Important:** Using PBUF does not automatically mean that the implementation is zero-copy.

[TBD]

The exact zero-copy behavior depends on:

- Ethernet driver
- DMA descriptor configuration
- RX buffer architecture
- PBUF configuration
- memory placement
- buffer ownership
- MCU memory/cache behavior
- LwIP integration

Therefore:

```text
Zero-Copy Status = TBD
```

until verified in the final firmware.

---

# 14. Network Initialization

## 14.1 LwIP Initialization

[FW-CONFIRMED / TBD]

STM32CubeMX/CubeIDE-generated projects commonly provide:

```c
MX_LWIP_Init();
```

The exact initialization sequence must follow the generated firmware of this project.

The function name must not be considered a project API unless it exists in the actual generated source code.

---

# 15. IP Configuration

[PROJECT-DECISION]

The intended controller network configuration uses a static IPv4 address.

Typical parameters are:

```text
MAC Address
IP Address
Subnet Mask
Gateway
UDP Port
```

[TBD]

The actual values must be taken from the final firmware configuration.

For a direct controller-to-PC connection, both devices must use compatible network addressing.

[EXAMPLE]

```text
Controller:
IP Address : 192.168.1.10
Subnet     : 255.255.255.0

Host PC:
IP Address : 192.168.1.100
Subnet     : 255.255.255.0
```

These values are examples only.

They are not protocol requirements.

---

# 16. Network Configuration Source

[TBD]

The final project must identify the authoritative source of network configuration.

Possible locations include:

```text
lwipopts.h
ethernetif.c
main.c
application configuration
CubeMX-generated configuration
project-specific network configuration
```

The final documentation should explicitly identify the file containing:

- controller IP
- subnet mask
- gateway
- MAC address
- UDP port

AI-generated firmware must use the actual project configuration rather than inventing new macro names.

---

# 17. Bare-Metal Architecture

[TBD]

If the final firmware uses a Bare-Metal architecture instead of an RTOS, LwIP processing must be integrated according to the selected STM32/LwIP driver architecture.

Conceptually:

```c
int main(void)
{
    System_Init();
    MX_LWIP_Init();

    while (1)
    {
        Network_Process();
        Motion_Process();
        IO_Process();
    }
}
```

[EXAMPLE]

`Network_Process()` in the example above is **not necessarily an actual project function**.

The actual implementation must use the functions provided by the selected STM32 Ethernet/LwIP integration.

---

# 18. LwIP Periodic Processing

[DESIGN-RECOMMENDATION]

The firmware must ensure that all required LwIP periodic processing continues to execute.

Depending on the enabled LwIP features, this can include mechanisms related to:

- ARP
- DHCP
- TCP
- other protocol timers

For a static-IP, UDP-only system, the exact required processing depends on the selected LwIP port and driver architecture.

[TBD]

The final firmware implementation must be checked to ensure all required LwIP timers and processing functions are serviced correctly.

---

# 19. Motion-System Interface

[PROJECT-DECISION]

The Ethernet subsystem should transport logical motion commands and controller status.

The network layer should not become responsible for low-level STEP pulse generation.

Recommended architecture:

```text
Ethernet
   │
   ▼
LwIP
   │
   ▼
UDP
   │
   ▼
Motion Protocol
   │
   ▼
Motion Controller
   │
   ├── Kinematics
   ├── Motion Planning
   ├── Axis Management
   ├── Acceleration / Deceleration
   └── Step Generation
           │
           ▼
       Hardware Timers
           │
           ▼
        STEP / DIR
```

---
# 20. Five-Axis Motion Architecture

[PROJECT-DECISION]

The CNC5AX-ETH controller uses **five independent motion axes**.

The five axes are controlled independently, with each axis having its own motion command, direction control, and STEP pulse generation.

```text
                         Motion Command
                              │
                              ▼
                    ┌─────────────────────┐
                    │   Motion Controller │
                    └──────────┬──────────┘
                               │
             ┌─────────────────┼─────────────────┐
             │                 │                 │
             ▼                 ▼                 ▼
          Axis 1            Axis 2            Axis 3
          STEP/DIR          STEP/DIR          STEP/DIR
             │                 │                 │
             ▼                 ▼                 ▼
          Driver 1           Driver 2           Driver 3


             ┌─────────────────┼─────────────────┐
             │                                   │
             ▼                                   ▼
          Axis 4                              Axis 5
          STEP/DIR                            STEP/DIR
             │                                   │
             ▼                                   ▼
          Driver 4                            Driver 5
```

## 20.1 Independent Axis Control

[PROJECT-DECISION]

Each axis must have an independent control path.

The motion-control layer is responsible for:

- axis position
- target position
- direction
- step generation
- velocity
- acceleration/deceleration
- axis limits
- axis enable/disable state
- motion state

The Ethernet/network layer should transport motion commands and data to the motion-control layer.

It should not directly generate STEP pulses.

---

## 20.2 Network-to-Axis Data Flow

[DESIGN-RECOMMENDATION]

The intended software data flow is:

```text
Host Computer
      │
      │ Ethernet / UDP
      ▼
     LwIP
      │
      ▼
Motion Protocol Parser
      │
      ▼
Motion Controller
      │
      ├──────────► Axis 1 ──► STEP/DIR
      │
      ├──────────► Axis 2 ──► STEP/DIR
      │
      ├──────────► Axis 3 ──► STEP/DIR
      │
      ├──────────► Axis 4 ──► STEP/DIR
      │
      └──────────► Axis 5 ──► STEP/DIR
```

The motion protocol should therefore represent the five axes independently.

---

## 20.3 Axis Independence

[PROJECT-DECISION]

No kinematic transformation such as CoreXY conversion is performed by the controller for the five primary axes.

Each axis is treated as an independent motion channel.

For example:

```text
Axis 1 → Motor / Driver 1
Axis 2 → Motor / Driver 2
Axis 3 → Motor / Driver 3
Axis 4 → Motor / Driver 4
Axis 5 → Motor / Driver 5
```

A command affecting one axis must not implicitly modify another axis unless such behavior is explicitly defined by the final motion-planning implementation.

---

## 20.4 Motion Planning

[DESIGN-RECOMMENDATION]

Although the five axes are electrically and logically independent, coordinated multi-axis motion may be required.

The motion planner may therefore generate synchronized motion profiles for multiple axes.

Conceptually:

```text
                    Motion Planner
                         │
          ┌──────────────┼──────────────┐
          │              │              │
          ▼              ▼              ▼
       Axis 1         Axis 2         Axis 3
          │              │              │
          └───────┬──────┴──────┬───────┘
                  │             │
               Axis 4        Axis 5
                  │             │
                  └──────┬──────┘
                         ▼
                  Hardware Timers
                         │
                         ▼
                     STEP / DIR
```

Synchronization between axes should be handled by the motion-control subsystem rather than by the Ethernet transport layer.

---

## 20.5 Hardware Timer Interface

[PROJECT-DECISION / TBD]

Each axis requires a deterministic STEP pulse-generation mechanism.

The final firmware should map each axis to its assigned hardware timer/channel or other validated pulse-generation mechanism.

```text
Axis 1 ──► Timer / PWM Channel ──► STEP 1
Axis 2 ──► Timer / PWM Channel ──► STEP 2
Axis 3 ──► Timer / PWM Channel ──► STEP 3
Axis 4 ──► Timer / PWM Channel ──► STEP 4
Axis 5 ──► Timer / PWM Channel ──► STEP 5
```

[TBD]

The exact timer/channel assignment must be taken from the project's authoritative pinout and firmware configuration.

---

## 20.6 Direction Control

[PROJECT-DECISION / TBD]

Each axis has an independent DIR signal.

The direction state must be established according to the motion command before the corresponding STEP pulses are generated.

```text
Axis 1 ──► DIR 1 + STEP 1
Axis 2 ──► DIR 2 + STEP 2
Axis 3 ──► DIR 3 + STEP 3
Axis 4 ──► DIR 4 + STEP 4
Axis 5 ──► DIR 5 + STEP 5
```

The exact DIR timing requirements must be implemented according to the selected stepper/servo driver specifications.

---

## 20.7 Real-Time Requirement

[PROJECT-DECISION]

The Ethernet subsystem must not directly determine the timing of STEP pulses.

Network packets should provide motion information to the motion subsystem, while hardware timers and the motion-control architecture are responsible for deterministic pulse generation.

```text
Ethernet / UDP
      │
      ▼
Motion Command
      │
      ▼
Motion Buffer / Planner
      │
      ▼
Axis Control
      │
      ▼
Hardware Timers
      │
      ▼
Deterministic STEP/DIR
```

This separation is required to prevent network latency, packet jitter, or temporary communication delays from directly affecting STEP pulse timing.

---

## 20.8 Important Rule

[PROJECT-DECISION]

The CNC5AX-ETH controller must be treated as a **five-axis independent motion controller**.

The firmware must **not introduce CoreXY, Cartesian-to-motor transformation, or any other multi-axis kinematic transformation unless explicitly required by a future project specification**.

Any required coordinated motion should be implemented by the motion-planning layer while preserving the independent hardware control of all five axes.
---

# 21. Feedback Communication

[TBD]

The controller may send status information back to the host.

Potential feedback information includes:

- current axis position
- controller state
- I/O state
- error flags
- communication status
- sequence information
- motion status

These are candidate fields and do not constitute the final feedback protocol.

Conceptually:

```text
Motion / Hardware State
          │
          ▼
   Feedback Structure
          │
          ▼
         LwIP
          │
          ▼
          UDP
          │
          ▼
       Ethernet
          │
          ▼
        Host PC
```

---

# 22. Real-Time Requirements

[PROJECT-DECISION]

The Ethernet subsystem must not compromise deterministic motion generation.

The motion subsystem has priority over non-critical network processing.

Important requirements include:

### 22.1 Network Processing

[DESIGN-RECOMMENDATION]

Network processing should not block the motion subsystem.

### 22.2 Interrupt Priorities

[TBD]

The final NVIC priority configuration must be verified against the actual motion architecture.

Motion-critical timer interrupts should be protected from excessive Ethernet processing latency.

### 22.3 Memory Access

[TBD]

DMA buffers and descriptors must be configured so that Ethernet memory access does not introduce corruption or unacceptable timing behavior.

### 22.4 Callback Execution Time

[DESIGN-RECOMMENDATION]

UDP callbacks should remain short and non-blocking.

---

# 23. Ethernet-to-Motion Data Flow

The intended logical data flow is:

```text
HOST COMPUTER
      │
      │ Ethernet / UDP
      ▼
LAN8720A PHY
      │
      │ RMII
      ▼
STM32F407 Ethernet MAC
      │
      ▼
Ethernet DMA
      │
      ▼
LwIP
      │
      ▼
UDP
      │
      ▼
Motion Protocol Parser
      │
      ▼
Packet Validation
      │
      ▼
Motion Controller
      │
      ├── Kinematics
      ├── Motion Planning
      └── Axis Management
              │
              ▼
        Hardware Timers
              │
              ▼
           STEP / DIR
```

---

# 24. Separation of Software Layers

[DESIGN-RECOMMENDATION]

The software should maintain clear separation between networking and motion control.

Recommended architecture:

```text
┌──────────────────────────────┐
│       Ethernet Driver        │
└──────────────┬───────────────┘
               ▼
┌──────────────────────────────┐
│            LwIP              │
└──────────────┬───────────────┘
               ▼
┌──────────────────────────────┐
│        UDP Transport         │
└──────────────┬───────────────┘
               ▼
┌──────────────────────────────┐
│     Motion Protocol Parser   │
└──────────────┬───────────────┘
               ▼
┌──────────────────────────────┐
│       Motion Controller      │
└──────────────┬───────────────┘
               ▼
┌──────────────────────────────┐
│      Axis / Timer Layer      │
└──────────────────────────────┘
```

This separation allows the network protocol to evolve without unnecessarily modifying the low-level Ethernet driver.

---

# 25. Debugging Strategy

[DESIGN-RECOMMENDATION]

Ethernet debugging should proceed from the physical layer toward the application layer.

Recommended sequence:

```text
1. PHY Link
       │
       ▼
2. RMII Clock / Signals
       │
       ▼
3. Ethernet MAC
       │
       ▼
4. DMA
       │
       ▼
5. LwIP Initialization
       │
       ▼
6. ARP
       │
       ▼
7. IP Connectivity
       │
       ▼
8. UDP Reception
       │
       ▼
9. Packet Validation
       │
       ▼
10. Motion Protocol
       │
       ▼
11. Motion Processing
       │
       ▼
12. STEP / DIR Output
```

Useful debugging tools and checks include:

- RJ45 link/activity indicators
- LAN8720A reset/configuration signals
- RMII reference clock
- RMII signal integrity
- MAC configuration
- DMA descriptor state
- RX/TX buffer state
- IP configuration
- ARP behavior
- UDP port
- packet length
- packet content
- sequence handling
- CPU load
- STEP timing

Ethernet traffic can also be inspected using a packet analyzer such as Wireshark.

---

# 26. Packet Timing and Motion Timing

[PROJECT-DECISION]

Network timing and motion timing must be treated as separate timing domains.

```text
Host Network Update
        │
        ▼
   UDP Packet
        │
        ▼
 Motion Command Buffer
        │
        ▼
 Motion Planner
        │
        ▼
 Hardware Timer
        │
        ▼
 STEP Pulse Generation
```

The Ethernet packet arrival time must not directly define the STEP pulse timing.

[DESIGN-RECOMMENDATION]

The motion subsystem should use buffered motion data whenever required to maintain deterministic pulse generation despite network timing variation.

[TBD]

The exact buffering and look-ahead strategy must be defined by the final motion-control architecture.

---

# 27. Application-Level Reliability

[TBD]

Because UDP does not provide delivery guarantees, the CNC application may require its own reliability mechanisms.

Possible mechanisms include:

```text
Sequence Number
Acknowledgement
Timeout
Watchdog
CRC / Checksum
Duplicate Detection
Lost-Packet Detection
Resynchronization
```

These mechanisms are not currently confirmed as implemented features.

They should be treated as protocol design items until implemented and verified.

---

# 28. Emergency Stop Communication

[TBD]

Emergency-stop behavior must be defined separately from normal motion UDP traffic.

The Ethernet protocol must not be assumed to be the sole safety mechanism for an emergency stop unless the complete safety architecture has been explicitly designed and validated for that purpose.

Any hardware emergency-stop mechanism must remain independent of assumptions made by the network software.

---

# 29. Firmware Update

[TBD]

Firmware update over Ethernet is not currently considered an implemented feature of this document.

A future firmware-update mechanism may require:

- bootloader support
- image validation
- CRC/signature verification
- transfer protocol
- rollback/recovery mechanism
- power-loss handling

Until implemented and tested:

```text
Ethernet Firmware Update = NOT IMPLEMENTED
```

---

# 30. Authoritative Information Sources

The following hierarchy should be used when developing or modifying the firmware.

```text
1. Final Project Schematic
          │
          ▼
2. MCU / PHY Datasheets
          │
          ▼
3. MCU Reference Manual
          │
          ▼
4. Final CubeMX Configuration
          │
          ▼
5. Actual Firmware Source Code
          │
          ▼
6. Project Architecture Documents
          │
          ▼
7. This Document
          │
          ▼
8. Design Recommendations / Examples
```

If information in this document conflicts with the actual schematic, generated configuration, firmware, or official component documentation, the higher-level authoritative source must take precedence.

---

# 31. Items Requiring Verification

The following items must be explicitly verified before being treated as implemented project behavior:

| Item | Status |
|---|---|
| LAN8720A RMII clock mode | `[TBD]` |
| RMII 50 MHz clock source | `[TBD]` |
| Exact Ethernet pin configuration | `[HW-CONFIRMED / TBD]` |
| LwIP version | `[TBD]` |
| RAW API usage in final firmware | `[TBD]` |
| Static IP values | `[TBD]` |
| UDP port | `[TBD]` |
| UDP packet format | `[TBD]` |
| Host-side Mach integration | `[TBD]` |
| Host-side LinuxCNC integration | `[TBD]` |
| Communication update rate | `[TBD]` |
| 1 kHz target | `[PROJECT-TARGET / TBD]` |
| RX/TX DMA buffer architecture | `[TBD]` |
| Zero-copy operation | `[TBD]` |
| LwIP memory configuration | `[TBD]` |
| Packet sequence mechanism | `[TBD]` |
| CRC/checksum | `[TBD]` |
| Feedback packet format | `[TBD]` |
| Watchdog | `[TBD]` |
| Ethernet emergency-stop protocol | `[TBD]` |
| Ethernet firmware update | `[TBD]` |

---

# 32. Configuration Summary

| Component | Status / Configuration |
|---|---|
| MCU | `[HW-CONFIRMED]` STM32F407VGT6 |
| Ethernet MAC | `[HW-CONFIRMED]` Integrated STM32 Ethernet MAC |
| PHY | `[HW-CONFIRMED]` LAN8720A |
| MAC ↔ PHY | `[HW-CONFIRMED]` RMII |
| MCU HSE | `[HW-CONFIRMED]` 8 MHz external crystal |
| Network Stack | `[PROJECT-DECISION]` LwIP |
| Transport | `[PROJECT-DECISION]` UDP |
| LwIP API | `[TBD]` RAW API intended |
| IP Configuration | `[PROJECT-DECISION]` Static IPv4 intended |
| Ethernet Transfer | `[HW-CONFIRMED]` DMA capable |
| Motion Protocol | `[TBD]` UDP-based application protocol |
| Host Integration | `[TBD]` Mach / LinuxCNC / custom host |
| Network Update Rate | `[TBD]` |
| Motion Timing | `[PROJECT-DECISION]` Must remain deterministic |
| CoreXY | `[PROJECT-DECISION]` |
| Feedback Protocol | `[TBD]` |

---

# 33. Final Architectural Rules

The following rules should be considered mandatory during firmware development.

### Rule 1 — Ethernet is not the motion engine

LwIP and Ethernet provide communication.

Deterministic motion generation belongs to the motion-control subsystem.

### Rule 2 — UDP is not guaranteed delivery

The application protocol must implement any required reliability mechanism.

### Rule 3 — Do not assume zero-copy

Zero-copy behavior must be verified from the actual Ethernet driver, DMA configuration, buffers, and LwIP integration.

### Rule 4 — Do not invent project APIs

Functions such as:

```c
Network_Process();
```

are examples unless they exist in the actual firmware.

### Rule 5 — Do not invent configuration macros

Names such as:

```c
CONTROLLER_IP
CONTROLLER_NETMASK
CONTROLLER_GATEWAY
MOTION_UDP_PORT
```

are examples unless they exist in the actual project.

### Rule 6 — Do not assume Mach/LinuxCNC compatibility

A UDP interface alone does not establish compatibility with either system.

### Rule 7 — Keep one authoritative CoreXY implementation

The CoreXY transformation must not accidentally be applied twice.

### Rule 8 — Validate all Ethernet input

Network-originated data must be validated before entering the motion-control subsystem.

### Rule 9 — Protect motion timing

Network processing must not introduce unacceptable latency or jitter into the time-critical motion subsystem.

### Rule 10 — Treat TBD items as unknown

AI-assisted firmware development must not fill `[TBD]` items using assumptions.

---

# 34. Current Architecture

Based only on the currently established project information, the confirmed high-level architecture is:

```text
                         HOST COMPUTER
                              │
                              │ Ethernet
                              ▼
                    ┌───────────────────┐
                    │     LAN8720A      │
                    │       PHY         │
                    └─────────┬─────────┘
                              │
                             RMII
                              │
                              ▼
                    ┌───────────────────┐
                    │   STM32F407VGT6   │
                    │                   │
                    │ Ethernet MAC      │
                    │ Ethernet DMA       │
                    └─────────┬─────────┘
                              │
                              ▼
                    ┌───────────────────┐
                    │       LwIP        │
                    │                   │
                    │ Ethernet / IP     │
                    │ ARP / UDP         │
                    └─────────┬─────────┘
                              │
                              ▼
                    ┌───────────────────┐
                    │  Motion Protocol  │
                    │      Parser       │
                    └─────────┬─────────┘
                              │
                              ▼
                    ┌───────────────────┐
                    │ Motion Controller │
                    │                   │
                    │ Kinematics        │
                    │ Planning          │
                    │ Axis Management   │
                    └─────────┬─────────┘
                              │
                              ▼
                    ┌───────────────────┐
                    │  Hardware Timers  │
                    │   STEP / DIR      │
                    └─────────┬─────────┘
                              │
                              ▼
                         CNC AXES
```

This diagram describes the intended architecture. Details marked `[TBD]` must be resolved from the final hardware, CubeMX configuration, firmware implementation, and protocol specification.
