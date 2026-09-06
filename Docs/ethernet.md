# CNC5AX-ETH: Ethernet & LwIP Network Stack

## 1. Overview

This document describes the Ethernet networking implementation of the **CNC5AX-ETH** controller using the **LwIP (Lightweight IP) network stack**.

The network subsystem is designed to provide a fast and deterministic communication channel between the CNC controller and a host computer while keeping the main processing resources focused on **motion pulse generation and axis control**.

### Main objectives

- Ethernet-based communication with the host computer
- Low-latency transfer of motion commands
- UDP-based communication
- Static IP configuration for direct host-to-controller communication
- DMA-based Ethernet frame transfer
- LwIP RAW API for low software overhead
- Direct transfer of received network data to the motion-processing layer
- Feedback of controller status to the host

---

## 2. Hardware Ethernet Architecture

The controller uses the following Ethernet architecture:

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
  ├── Ethernet MAC
  │
  ├── DMA
  │
  └── LwIP
       │
       └── UDP
            │
            ▼
       Motion Processing
            │
            ▼
       Axis / Timer Outputs
```

### MCU

- **Microcontroller:** STM32F407VGT6
- **Ethernet interface:** Integrated Ethernet MAC
- **MAC-to-PHY interface:** RMII
- **PHY:** LAN8720A
- **External clock source:** 8 MHz HSE crystal

The STM32F407VGT6 contains an integrated Ethernet MAC, while the LAN8720A provides the physical-layer interface.

---

## 3. LwIP Network Architecture

LwIP is responsible for the software networking layers required by the controller, including:

- Ethernet frame handling
- ARP
- IP
- ICMP
- UDP

The implementation uses UDP as the primary transport protocol for real-time CNC communication.

### Protocol Stack

```text
Application / Motion Protocol
          │
          ▼
         UDP
          │
          ▼
          IP
          │
          ├── ARP
          └── ICMP
          │
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

---

## 4. UDP Communication

### Why UDP?

The controller uses **UDP** because CNC motion communication is latency-sensitive and benefits from a lightweight transport protocol.

Compared with connection-oriented protocols such as TCP, UDP provides:

- Lower protocol overhead
- No connection-management overhead
- No retransmission mechanism inside the transport layer
- Low latency
- Simple packet-based communication

This makes UDP appropriate for transmitting time-sensitive motion commands where the application layer can define its own packet validation, sequencing, synchronization, and recovery mechanisms if required.

### Typical Use Case

A host system such as **LinuxCNC** or **Mach** can transmit motion-related packets to the controller at a high frequency.

Example:

```text
LinuxCNC / Mach
      │
      │ UDP motion packets
      │
      ▼
   Ethernet
      │
      ▼
  CNC5AX-ETH
      │
      ▼
Motion Processing
      │
      ▼
Hardware Timers
      │
      ▼
STEP / DIR Outputs
```

A target communication rate may reach approximately **1 kHz**, depending on the host-side motion protocol and implementation.

---

## 5. Memory Management

LwIP memory management must be configured carefully because the controller may receive high-frequency network traffic.

### Memory Pools

LwIP memory pools should be sized according to the expected traffic pattern to prevent packet-buffer exhaustion under sustained network load.

Important considerations include:

- Number of simultaneously allocated packet buffers
- RX packet rate
- TX packet rate
- Maximum packet size
- Processing time per packet
- Available MCU RAM
- Interaction between Ethernet DMA descriptors and LwIP buffers

### PBUF

LwIP uses **PBUF (Packet Buffer)** structures for packet storage and management.

The PBUF configuration should minimize unnecessary memory copies between:

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
UDP Callback
      │
      ▼
Motion Processing
```

Where the selected architecture permits it, the implementation should avoid unnecessary data copies to reduce CPU load and packet-processing latency.

> **Important:** Zero-copy operation is dependent on the exact STM32 Ethernet driver, DMA descriptor configuration, buffer placement, cache behavior, and LwIP integration. It should not be assumed solely from the use of PBUF.

---

## 6. LwIP Initialization

The initial network configuration is generated and configured in the STM32 development environment, such as **STM32CubeIDE / STM32CubeMX**.

The primary initialization function is:

```c
MX_LWIP_Init();
```

This initialization is responsible for preparing the LwIP network interface and associated Ethernet resources.

### Network Configuration

The controller uses a **static IP address** for direct communication with the host computer.

The configuration includes:

- MAC address
- IPv4 address
- Subnet mask
- Gateway configuration, if required

For a direct controller-to-PC connection, the IP addresses of both devices must belong to the same subnet.

Example:

```text
Controller:
IP Address : 192.168.1.10
Subnet     : 255.255.255.0

Host PC:
IP Address : 192.168.1.100
Subnet     : 255.255.255.0
```

The exact values are project configuration parameters and should not be hard-coded in documentation as protocol requirements.

---

## 7. LwIP RAW API

The networking layer uses the **LwIP RAW API** rather than higher-level socket APIs.

The RAW API provides a callback-driven programming model with low software overhead.

### UDP Receive Callback

The UDP receive path is based on callbacks such as:

```c
udp_recv();
```

Conceptually:

```text
Ethernet Packet
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
 udp_recv() callback
      │
      ▼
Packet Validation / Parsing
      │
      ▼
Motion Command Structure
      │
      ▼
Motion Processing
```

When a valid UDP packet is received, the callback should perform only the necessary packet handling and hand the motion data to the appropriate processing layer.

### Real-Time Consideration

The UDP callback should remain lightweight.

Avoid performing long-running operations directly inside the network callback, especially operations that could block or significantly delay:

- Timer processing
- STEP pulse generation
- Axis state updates
- Other time-critical interrupt routines

A suitable architecture is:

```text
UDP Callback
     │
     ├── Validate packet
     ├── Parse / copy required data
     └── Signal motion layer
              │
              ▼
       Motion Processing
```

---

## 8. Ethernet DMA

Ethernet frame transfers between the STM32 Ethernet MAC and RAM are handled using **DMA**.

Conceptually:

```text
              RX
LAN8720A ──► MAC ──► DMA ──► RAM ──► LwIP
                                  │
                                  ▼
                             UDP Callback


              TX
LwIP ──► RAM ──► DMA ──► MAC ──► LAN8720A
```

The purpose of DMA is to reduce CPU involvement in the movement of Ethernet frame data.

This allows the CPU to dedicate more processing time to the motion-control subsystem.

### Real-Time Requirement

Ethernet processing must not interfere with time-critical motion operations.

The implementation should therefore carefully manage:

- Ethernet interrupts
- DMA descriptors
- DMA buffers
- Interrupt priorities
- Packet-processing time
- Timer interrupt priorities
- Memory access and buffer ownership

The exact interrupt-priority configuration must be verified against the final firmware architecture.

---

## 9. Packet Processing

Received packets are parsed by the networking layer and converted into firmware data structures.

Conceptually:

```text
UDP Packet
    │
    ▼
Packet Validation
    │
    ▼
Packet Parsing
    │
    ▼
Firmware struct
    │
    ▼
Motion Processing
```

The packet-processing layer should be separated from the low-level Ethernet driver.

Recommended software separation:

```text
Ethernet Driver
      │
      ▼
     LwIP
      │
      ▼
 UDP Transport
      │
      ▼
Motion Protocol Parser
      │
      ▼
Motion Controller
      │
      ▼
Axis / Timer Hardware
```

This separation makes the networking implementation easier to debug and allows the motion protocol to evolve without modifying the Ethernet driver.

---

## 10. Axis / Kinematic Mapping

The CNC controller uses a **CoreXY** motion architecture for the relevant X/Y axes.

When a packet contains target X/Y motion data, the motion-processing layer converts the requested Cartesian motion into the corresponding motor-axis commands.

The CoreXY relationship is:

```text
A = X + Y
B = X - Y
```

Depending on the sign convention and mechanical wiring, the exact equations may be:

```text
A = X + Y
B = X - Y
```

or an equivalent sign-inverted form.

Therefore, the firmware must define the authoritative axis convention in the motion-control module rather than relying on the network layer to perform mechanical transformations.

### Recommended Data Flow

```text
Network Packet
     │
     ▼
Target X/Y
     │
     ▼
Motion Planner / Kinematics
     │
     ├──► Motor A
     │
     └──► Motor B
             │
             ▼
       Hardware Timers
             │
             ▼
        STEP / DIR
```

The networking layer should preferably transport logical motion coordinates, while the motion-control layer performs the CoreXY kinematic transformation.

---

## 11. Feedback Packets

After processing the received commands and updating the relevant controller state, the firmware can send status information back to the host.

A feedback packet may contain information such as:

- Current axis position
- Current timer counters
- I/O states
- Controller status
- Error/status flags
- Communication sequence information

Conceptually:

```text
Hardware / Motion State
          │
          ▼
   Feedback Structure
          │
          ▼
      LwIP PBUF
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

A new PBUF can be allocated for a feedback packet when required by the selected LwIP buffer-management architecture.

---

## 12. Bare-Metal LwIP Processing

If the firmware uses a **Bare-Metal** architecture rather than an RTOS, network-stack processing must be integrated into the main application loop according to the selected STM32/LwIP Ethernet driver architecture.

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

The exact function name depends on the Ethernet/LwIP integration generated by STM32CubeMX and the firmware architecture.

### Important

The main loop must ensure that required LwIP processing and timers continue to execute.

This is particularly important for protocol mechanisms such as:

- ARP table maintenance
- TCP timers, if TCP is enabled
- DHCP timers, if DHCP is enabled
- Other LwIP timeout-based mechanisms

For a static-IP UDP-only system, the exact required periodic processing depends on the LwIP port and driver implementation.

---

## 13. IP Configuration

The default network configuration should be defined in the project's network configuration files.

Typical parameters include:

```text
MAC Address
IP Address
Subnet Mask
Gateway
UDP Port
```

For example:

```c
#define CONTROLLER_IP
#define CONTROLLER_NETMASK
#define CONTROLLER_GATEWAY
#define MOTION_UDP_PORT
```

The actual macro names depend on the project's generated LwIP configuration.

### Changing the IP Address

To change the controller's default IP configuration:

1. Locate the network configuration source/header file.
2. Modify the IP address and, if necessary, subnet mask.
3. Verify that the host PC uses a compatible subnet.
4. Rebuild the firmware.
5. Flash the updated firmware to the controller.
6. Verify communication using the configured UDP port.

---

## 14. Development and Debugging

When debugging the Ethernet subsystem, verify the system in layers.

### Recommended Debugging Order

```text
1. PHY Link
      │
      ▼
2. RMII Communication
      │
      ▼
3. Ethernet MAC / DMA
      │
      ▼
4. LwIP Initialization
      │
      ▼
5. ARP
      │
      ▼
6. IP Connectivity
      │
      ▼
7. UDP Reception
      │
      ▼
8. Motion Packet Parsing
      │
      ▼
9. Motion Processing
      │
      ▼
10. STEP / DIR Output
```

This approach helps isolate whether a problem originates in hardware, the Ethernet driver, LwIP, the UDP protocol layer, or the motion-control firmware.

### Useful Checks

- Verify RJ45 link/activity LEDs.
- Verify LAN8720A reset and configuration.
- Verify RMII clock availability.
- Verify MAC address configuration.
- Verify static IP configuration.
- Verify subnet configuration.
- Check ARP table behavior.
- Capture Ethernet traffic with Wireshark.
- Verify UDP destination port.
- Verify packet length and packet structure.
- Verify packet sequence handling.
- Monitor CPU load during high-rate traffic.
- Verify that network processing does not disrupt STEP pulse timing.

---

## 15. Real-Time Design Requirements

The Ethernet subsystem is not allowed to compromise the deterministic behavior of the motion-control subsystem.

The following principles should be maintained:

### 15.1 Keep network callbacks short

Do not perform heavy calculations or blocking operations in `udp_recv()`.

### 15.2 Protect motion timing

Timer interrupts and other hard real-time operations must have appropriate priority relative to Ethernet processing.

### 15.3 Avoid unnecessary memory copies

Where safe and supported by the selected LwIP/STM32 Ethernet driver architecture, minimize copying of packet data.

### 15.4 Validate all received packets

Network data must never be assumed to be valid.

The parser should validate:

- Packet size
- Header / magic value
- Command type
- Payload length
- Sequence number, if implemented
- Axis data range
- Checksum/CRC, if implemented

### 15.5 Separate networking from motion control

The Ethernet layer should transport commands and status data. The motion layer should remain responsible for:

- Kinematics
- Motion planning
- Step generation
- Direction control
- Acceleration/deceleration
- Axis limits
- Real-time timing

---

## 16. High-Level Firmware Architecture

The complete architecture can be represented as:

```text
                         HOST COMPUTER
                    LinuxCNC / Mach / Other
                              │
                              │ Ethernet / UDP
                              ▼
                    ┌─────────────────────┐
                    │     LAN8720A PHY    │
                    └──────────┬──────────┘
                               RMII
                                │
                                ▼
                    ┌─────────────────────┐
                    │ STM32F407 Ethernet  │
                    │        MAC + DMA    │
                    └──────────┬──────────┘
                               │
                               ▼
                    ┌─────────────────────┐
                    │        LwIP         │
                    │  IP / ARP / ICMP    │
                    │        UDP          │
                    └──────────┬──────────┘
                               │
                               ▼
                    ┌─────────────────────┐
                    │   UDP RAW API       │
                    │   Receive Callback  │
                    └──────────┬──────────┘
                               │
                               ▼
                    ┌─────────────────────┐
                    │ Motion Protocol     │
                    │ Parser / Validation │
                    └──────────┬──────────┘
                               │
                               ▼
                    ┌─────────────────────┐
                    │  Motion Controller  │
                    │                     │
                    │  Kinematics         │
                    │  Planning           │
                    │  Axis Management    │
                    └──────────┬──────────┘
                               │
                               ▼
                    ┌─────────────────────┐
                    │ Hardware Timers     │
                    │ PWM / STEP Generate │
                    └──────────┬──────────┘
                               │
                               ▼
                         CNC AXES / DRIVERS
```

---

## 17. Important Implementation Notes

The following points are considered architectural requirements rather than assumptions about a specific generated STM32CubeMX project:

1. **LwIP must not be treated as the real-time motion engine.** It provides network transport; deterministic motion generation belongs to the motion-control subsystem.

2. **UDP does not guarantee delivery, ordering, or duplicate protection.** If the motion protocol requires these properties, they must be implemented at the application/protocol layer.

3. **DMA does not automatically guarantee zero CPU overhead.** Descriptor management, interrupts, cache/buffer handling, and LwIP integration still require CPU processing.

4. **PBUF configuration must match the actual Ethernet DMA buffer architecture.** Incorrect buffer ownership or lifetime management can cause packet corruption or memory exhaustion.

5. **CoreXY kinematic conversion should have one authoritative implementation.** Avoid applying the transformation both in the network parser and motion controller.

6. **Network packet processing must not block time-critical motion operations.**

7. **All packet data originating from Ethernet must be validated before being used by the motion subsystem.**

---

## 18. Configuration Summary

| Component | Configuration |
|---|---|
| MCU | STM32F407VGT6 |
| Ethernet MAC | Integrated STM32 Ethernet MAC |
| PHY | LAN8720A |
| MAC-PHY Interface | RMII |
| Network Stack | LwIP |
| Transport Protocol | UDP |
| LwIP API | RAW API |
| IP Configuration | Static IPv4 |
| Ethernet Frame Transfer | DMA |
| Motion Protocol | UDP-based application protocol |
| Primary Use | CNC motion command and status communication |
| Host Examples | LinuxCNC / Mach |
| Real-Time Priority | Motion timing takes priority over network processing |

---

## 19. Future Extensions

The networking architecture can be extended with:

- Packet sequence numbers
- Application-level acknowledgements
- CRC/checksum
- Connection/session identification
- Watchdog / communication timeout
- Emergency-stop communication
- Controller status packets
- Axis position feedback
- Error reporting
- Configuration packets
- Firmware update protocol
- Multiple UDP ports for separating command and diagnostic traffic

Any such extension should preserve the primary requirement: **network communication must not compromise deterministic motion generation.**
