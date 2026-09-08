# AN3966 — LwIP TCP/IP Stack Demonstration for STM32F4x7

## Purpose

This document describes an STMicroelectronics demonstration of the **LwIP TCP/IP networking stack** on STM32F4x7 microcontrollers.

It is primarily useful for understanding how:

```text
STM32F4 Ethernet MAC
        ↓
Ethernet DMA
        ↓
Low-level Ethernet driver
        ↓
LwIP network interface
        ↓
TCP/IP stack
        ↓
Application
```

can be implemented in an embedded system.

For this project, this document is especially relevant because the **STM32F407VGT6 uses its Ethernet MAC with an external LAN8720A PHY through RMII**.

---

# Scope

This Application Note applies to the STM32F4x7 family, including:

- STM32F407xx
- STM32F417xx
- STM32F427xx
- STM32F437xx

The project uses:

```text
MCU: STM32F407VGT6
PHY: LAN8720A
Interface: RMII
Network stack: LwIP
```

The document describes a demonstration environment rather than the exact hardware and software architecture of this project.

---

# What This Document Provides

AN3966 covers several important areas:

- LwIP architecture
- LwIP APIs
- Packet buffers (`pbuf`)
- STM32 Ethernet interface
- Ethernet MAC/DMA
- DMA descriptors
- PHY control
- Hardware checksum
- Standalone operation
- FreeRTOS operation
- TCP applications
- UDP applications
- HTTP server
- TFTP server
- LwIP memory configuration

These topics make this document particularly useful when designing or debugging the Ethernet communication layer.

---

# LwIP API Models

One of the most useful parts of this document is the explanation of the different LwIP application programming models.

## Raw API

The Raw API is a callback-based API intended for standalone operation.

The application registers callbacks that are invoked by the LwIP core when networking events occur.

Typical TCP operations include:

```text
tcp_new()
tcp_bind()
tcp_listen()
tcp_accept()
tcp_connect()
tcp_write()
tcp_recv()
tcp_sent()
tcp_poll()
tcp_close()
tcp_abort()
```

This model is useful when:

- No RTOS is required
- Low memory usage is important
- Callback-based networking is acceptable
- Maximum control over the network stack is desired

---

## Netconn API

The Netconn API provides a higher-level sequential interface to LwIP.

It is intended for a multi-threaded environment.

Typical operations include:

```text
netconn_new()
netconn_bind()
netconn_connect()
netconn_listen()
netconn_accept()
netconn_recv()
netconn_write()
netconn_close()
```

This API is associated with an RTOS-based architecture.

---

## Socket API

LwIP also provides a BSD-style socket interface.

Typical functions include:

```text
socket()
bind()
listen()
connect()
accept()
read()
write()
```

This API can make application-level networking code easier to structure, especially when the software architecture already uses socket-style interfaces.

---

# LwIP Buffer Management

AN3966 explains the use of LwIP packet buffers (`pbuf`).

This is important when working with:

- Ethernet frames
- Packet reception
- Packet transmission
- Memory allocation
- Network buffers

Important concepts include:

```text
Ethernet packet
      ↓
    pbuf
      ↓
   LwIP
      ↓
Application
```

When debugging packet corruption, dropped packets, or memory usage, the LwIP buffer model should be understood before modifying the Ethernet interface.

---

# STM32 Ethernet Low-Level Driver

This is one of the most important sections of AN3966 for this project.

The document explains the relationship between the STM32 Ethernet MAC/DMA and LwIP.

The low-level interface includes functions conceptually equivalent to:

```text
low_level_init()
low_level_output()
low_level_input()
ethernetif_init()
ethernet_input()
```

The important architecture is:

```text
                     STM32F407
                        │
                        ▼
                 Ethernet MAC
                        │
                        ▼
                      DMA
                  ┌─────┴─────┐
                  ▼           ▼
              RX Descriptor  TX Descriptor
                  │           │
                  ▼           ▼
               RX Buffer    TX Buffer
                  │           │
                  └─────┬─────┘
                        ▼
                     LwIP
```

This architecture is useful when implementing or debugging the Ethernet driver.

---

# Ethernet MAC and DMA

AN3966 describes configuration and operation of the STM32 Ethernet MAC/DMA.

Topics include:

- MAC configuration
- DMA configuration
- Ethernet speed
- Duplex mode
- Auto-negotiation
- MAC address filtering
- Checksum offload
- RX/TX operation
- DMA descriptors
- Buffer management
- Ethernet interrupts

For detailed register definitions and hardware behavior, always use the **STM32F407 Reference Manual (RM0090)** as the primary authority.

AN3966 should be treated as an implementation example and architectural guide.

---

# DMA Descriptors

Ethernet DMA descriptors are particularly important for the STM32F407 Ethernet implementation.

They connect the Ethernet DMA engine to packet buffers in memory.

Conceptually:

```text
                    Ethernet DMA
                         │
              ┌──────────┴──────────┐
              ▼                     ▼
         RX descriptors        TX descriptors
              │                     │
              ▼                     ▼
         RX buffers            TX buffers
              │                     │
              └──────────┬──────────┘
                         ▼
                       LwIP
```

This section should be consulted when debugging:

- RX packet loss
- TX packet loss
- DMA errors
- Buffer ownership problems
- Descriptor configuration
- Ethernet initialization

---

# PHY Control

The STM32 Ethernet MAC requires an external PHY in the project's hardware.

The document discusses PHY access and control.

For this project:

```text
STM32F407 Ethernet MAC
          │
          │ RMII
          ▼
       LAN8720A
          │
          ▼
       RJ45 / Ethernet
```

AN3966 provides useful context for the MAC/PHY relationship.

However:

> **LAN8720A-specific behavior must be taken from the LAN8720A datasheet and related Microchip documentation.**

Do not use AN3966 as the authoritative source for LAN8720A registers, strap configuration, clock requirements, or PHY-specific behavior.

---

# MII and RMII

AN3966 demonstrates both:

- MII
- RMII

interface modes.

For this project, the relevant mode is:

```text
RMII
```

The application note includes configuration examples for selecting the PHY interface mode and discusses the clock requirements in the STM32 evaluation-board environment.

However, the clocking arrangement in the evaluation board should **not** be copied directly to the project's PCB.

The actual project configuration must be determined from:

```text
STM32F407 Datasheet
+
STM32F407 Reference Manual
+
LAN8720A Datasheet
+
Project Hardware Documentation
```

---

# TCP and UDP

AN3966 contains examples demonstrating both TCP and UDP communication.

Examples include:

- TCP echo client
- TCP echo server
- UDP echo client
- UDP echo server

These examples are useful for understanding basic application-level networking.

They can also serve as conceptual references for implementing a simple communication protocol between:

```text
PC / Mach3
       │
       │ Ethernet
       ▼
STM32F407 CNC Controller
```

---

# HTTP Server

The application note includes HTTP server demonstrations.

The examples demonstrate concepts such as:

- TCP server operation
- HTTP request handling
- URL parsing
- CGI
- SSI
- HTTP POST
- Dynamic web content

These examples are useful for understanding how LwIP can support an embedded web server.

They are **not required** for the core Mach3 motion-controller functionality.

---

# TFTP

AN3966 also includes a TFTP server demonstration.

TFTP can be useful as a reference for simple UDP-based file-transfer mechanisms.

It is not inherently required by this project and should not be added to the firmware unless the project architecture specifically requires it.

---

# FreeRTOS Integration

The document demonstrates LwIP operation in a FreeRTOS environment using:

- Netconn API
- Socket API

Conceptually:

```text
FreeRTOS
   │
   ├── LwIP thread
   │
   ├── Network application
   │
   └── Other project tasks
```

The project does not automatically inherit this architecture simply because AN3966 demonstrates it.

The final firmware architecture must follow the project's own firmware design.

---

# Memory Configuration

AN3966 contains examples of LwIP memory configuration, including:

- `MEM_SIZE`
- `PBUF_POOL_SIZE`
- `PBUF_POOL_BUFSIZE`
- Ethernet driver buffers

The document explicitly states that its example memory values are demonstration values and must be adjusted for the target application.

Therefore, **do not copy the example memory configuration directly into the CNC controller**.

Memory usage must be calculated according to:

- Number of simultaneous connections
- Packet rate
- Packet sizes
- LwIP configuration
- Ethernet buffer requirements
- Application requirements
- Available STM32F407 RAM

---

# Important Version Information

The version of LwIP described in the Rev.2 version of AN3966 is:

```text
LwIP 1.4.1
```

The document was revised in **July 2013**.

This is important because current STM32CubeF4/LwIP integrations may use different versions and software structures.

Therefore:

> Do not assume that source code, APIs, directory structures, configuration files, or integration methods shown in AN3966 are identical to the current LwIP or STM32Cube environment.

Use the document primarily for **architecture, concepts, and implementation principles** unless the exact software version matches the project.

---

# Standard Peripheral Library vs HAL/LL

The demonstration described in AN3966 uses the STM32F4 Ethernet driver and **STM32F4xx Standard Peripheral Library** in its example software environment.

This project may use a different software stack, such as:

```text
STM32Cube HAL
STM32Cube LL
CMSIS
LwIP
```

Therefore:

```text
AN3966 source code
        ≠
Current project source code
```

The code should be treated as a reference implementation, not copied blindly.

For HAL/LL-specific behavior, consult the official STM32F4 HAL/LL source and documentation.

---

# Relation to the Project Ethernet Architecture

The project's Ethernet stack should conceptually follow:

```text
Mach3 / PC
     │
     │ Ethernet
     ▼
RJ45
     │
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
Low-Level Ethernet Driver
     │
     ▼
LwIP
     │
     ▼
Project Network Protocol
     │
     ▼
CNC Motion Controller
```

AN3966 is primarily useful for the section between:

```text
STM32F407 Ethernet MAC
        ↓
Ethernet Driver
        ↓
LwIP
```

---

# What This Document Does NOT Define

AN3966 does not define the complete architecture of this CNC controller.

It does not define:

- Mach3 communication protocol
- Project packet format
- Motion planner
- STEP/DIR generation strategy
- Axis control architecture
- Project-specific Ethernet protocol
- LAN8720A hardware implementation
- PCB routing
- Project-specific pin assignments
- Current STM32Cube configuration

Those items must be taken from the project's own documentation.

---

# Recommended Usage for AI-Assisted Development

When AI is implementing Ethernet functionality, use AN3966 as a **conceptual and architectural reference**.

A recommended documentation workflow is:

```text
Project Ethernet Documentation
             ↓
STM32F407 Datasheet
             ↓
STM32F407 Reference Manual
             ↓
LAN8720A Documentation
             ↓
AN3966
             ↓
Current LwIP Documentation / Source
             ↓
Project Implementation
```

### Use AN3966 for:

- Understanding LwIP architecture
- Understanding Raw API
- Understanding Netconn API
- Understanding Socket API
- Understanding `pbuf`
- Understanding Ethernet interface structure
- Understanding MAC/DMA descriptors
- Understanding PHY interaction
- Reviewing example networking applications

### Do not use AN3966 alone for:

- Current LwIP API/version details
- Exact STM32F407 register configuration
- LAN8720A-specific configuration
- Project-specific Ethernet protocol
- Current STM32Cube HAL implementation

---

# AI Verification Rules

When generating Ethernet firmware for this project:

### Rule 1

Do not copy old AN3966 code directly without checking its software version and dependencies.

### Rule 2

Verify STM32 peripheral behavior against **RM0090**.

### Rule 3

Verify pin and electrical information against the **STM32F407 Datasheet**.

### Rule 4

Verify PHY-specific behavior against **LAN8720A documentation**.

### Rule 5

Verify the current LwIP API and configuration against the version actually included in the project.

### Rule 6

Do not introduce FreeRTOS solely because AN3966 contains FreeRTOS examples.

The operating-system architecture must follow the project's firmware architecture.

---

# Primary References

For Ethernet development, use these documents together:

```text
1. STM32F407 Datasheet
   ↓
   Device, pins, electrical characteristics

2. STM32F407 Reference Manual (RM0090)
   ↓
   Ethernet MAC, DMA, registers and hardware behavior

3. LAN8720A Datasheet
   ↓
   PHY-specific behavior

4. AN3966
   ↓
   LwIP architecture and implementation examples

5. Current LwIP source/documentation
   ↓
   Actual networking stack version and APIs used by the project

6. Project Ethernet Documentation
   ↓
   Project-specific implementation
```

---

# Document Information

```text
Document:       AN3966
Title:          LwIP TCP/IP stack demonstration for STM32F4x7 microcontrollers
Revision:       Rev. 2
Date:           July 2013
LwIP version:   1.4.1
Applicable MCU: STM32F407xx, STM32F417xx,
                STM32F427xx, STM32F437xx
```

---

# Scope

This README is a navigation and usage guide for:

`stm32_application note_AN3966.pdf`

The original STMicroelectronics Application Note remains the authoritative source for the information contained in the document.

For implementation in this repository, the project's actual hardware, firmware architecture, current LwIP version, and current STM32 software stack take precedence over the old demonstration environment described in AN3966.
