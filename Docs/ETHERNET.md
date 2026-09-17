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

## 2.2 PHY Driver Selection (LAN8720A vs LAN8742)

[DESIGN-RECOMMENDATION]

STM32CubeMX's LwIP *Platform Settings* does not offer a LAN8720 driver; within the same Microchip family only **LAN8742** is available. The LAN8742 driver can be used for the LAN8720A, because the registers ST's driver actually touches are the standard IEEE 802.3 MII management registers plus one vendor register whose relevant fields are defined identically on both parts:

| Register | Used for | LAN8720A |
|---|---|---|
| `0x00` BCR | soft reset, auto-negotiation enable/restart | Standard |
| `0x01` BSR | link status, auto-negotiation complete | Standard |
| `0x02`/`0x03` PHY ID | device identification | Standard |
| `0x12` Special Modes | holds the strapped `PHYAD` address bits | Present |
| `0x1F` PHY Special Control/Status | speed/duplex result after auto-negotiation | Present — bits `[4:2]` *Speed Indication*: `001` = 10 half, `101` = 10 full, `010` = 100 half, `110` = 100 full; bit `12` = *Autodone*. Verified against `LAN8720A/LAN8720A_DataSheet-DS00002165.pdf` §"PHY Special Control/Status Register". |

The LAN8742 driver reads exactly these fields, so selecting LAN8742 in CubeMX and using it against the LAN8720A is functionally correct for link-up, speed and duplex detection.

Two points must still be handled explicitly:

1. **PHY address.** The LAN8720A strap allows only 0 or 1 and the available sources disagree on which this module uses, so the address must be detected by scanning rather than hard-coded.
2. **Reset release.** The driver performs MDIO reads; these fail while the PHY is held in reset, so `PB0` (`PHY_NRST`, active-low) must be driven HIGH and the PHY given its reset-release time **before** the driver is initialized. `[FW-CONFIRMED]` `MX_GPIO_Init()` now drives `PB0` HIGH (with its internal pull-up also enabled, backing up the board's own 4.7 kΩ pull-up on `nRST`), and this runs before `MX_LWIP_Init()`.

   `[DECIDED — ADR-013]` `PB0` is currently only ever held HIGH — the firmware never actively pulses it LOW. The LAN8720A datasheet's power-on timing (§5.6.3, `tpurstd`) specifies external `nRST` should remain asserted for at least 25 ms after supplies reach 80% of nominal before being released. This board has no RC delay or reset supervisor on `nRST` (a plain pull-up per the schematic), so meeting that spec at cold power-up currently depends entirely on the LAN8720A's own internal power-on reset. `LAN8742_Init()` does also perform an MDIO soft-reset (`BCR` bit 15) independent of the pin, which is what makes this work in practice. **The project owner has confirmed the fix: `PB0` must be explicitly driven LOW for at least 150 µs** (deliberate margin over `trstia`, whose exact datasheet minimum is 100 µs — `LAN8720A_DataSheet-DS00002165.pdf` Table 5-9) **at the start of `low_level_init()`, before releasing it** — a small, contained firmware change, not an architectural one. See ADR-013 (`Docs/FIRMWARE-ARCHITECTURE.md` §41) and `Docs/PRE-IMPLEMENTATION-DECISIONS.md` item 8.

`[FW-CONFIRMED]` **Implemented in Phase 2.** `net_port_phy_hw_reset()` (`Firmware/Platform/STM32F407/Src/net_port_stm32f4.c`) performs the pulse with a `DWT->CYCCNT` microsecond busy-wait — `HAL_Delay()`'s 1 ms resolution cannot express 150 µs — and is called from `low_level_init()`'s `USER CODE BEGIN MACADDRESS` block, i.e. **before `HAL_ETH_Init()`**, so the MAC's own DMA soft reset never runs while the PHY is still held in reset. After releasing `nRST` it waits `NET_PHY_RESET_SETTLE_US` (1 ms, an implementation choice rather than a datasheet minimum — see §3.8.5's note that the RMII interface runs at 2.5 MHz for the first 16 µs out of reset) before returning. Datasheet §3.8.5.1 requires a clock on `XTAL1/CLKIN` *during* the reset; the module's own 50 MHz oscillator provides it independently of `nRST` (see the Clock Architecture subsection above), so that requirement is met. Hardware confirmation is HV-20 in `Docs/HARDWARE-VALIDATION.md`.

If the LAN8742 driver is ever found to diverge from the LAN8720A in a way that matters, the fallback is a small project-owned PHY driver using the same five registers above; this is a contained piece of work, not an architectural change.

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

[HW-CONFIRMED]

The source and direction of the 50 MHz RMII reference clock have been verified against `LAN8720A/LAN8720-ETH-Board-Schematic.pdf`: the PHY module carries its own **50 MHz oscillator**, whose output drives both the LAN8720A's `XTAL1/CLKIN` pin and the header pin wired to the MCU's `PA1`.

```text
PHY board 50 MHz oscillator
        │
        ├──────────────► LAN8720A  XTAL1/CLKIN
        │
        └──────────────► STM32F407  PA1 (ETH_RMII_REF_CLK, input)
```

The MCU therefore consumes the RMII reference clock and does not produce it: `PA1` is an input and no `MCO`/`MCO2` output is configured. This is independent of the 8 MHz HSE crystal, which only feeds the MCU's own PLL.

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

[FW-CONFIRMED]

The controller network configuration is a **static IPv4 address**, no DHCP, no AUTOIP. This is implemented in `Firmware/LWIP/App/lwip.h` (address octet `#define`s) and `Firmware/LWIP/App/lwip.c` (`MX_LWIP_Init()` calls `IP4_ADDR()` directly instead of `dhcp_start()`), with `LWIP_DHCP` set to `0` in `Firmware/LWIP/Target/lwipopts.h`.

> **This configuration was silently lost once and has been restored with a guard.** All three of those edits sat in CubeMX-*generated* regions, so the regeneration in commit `b409ba5` reverted every one of them — `LWIP_DHCP` back to `1`, `dhcp_start()` back in `MX_LWIP_Init()`, the address `#define`s gone — while this section still described the lost state as `[FW-CONFIRMED]`. On this link there is no DHCP server, so the controller would have come up on `0.0.0.0` with nothing in the build to say why.
>
> Phase 2 restores the configuration **and** adds two things that survive the next regeneration:
> - `net_glue_apply_static_ip()`, called from `MX_LWIP_Init()`'s `USER CODE BEGIN 3` block, applies the address from `Firmware/Net/Inc/net_config.h` unconditionally — and stops DHCP first if a regeneration has re-enabled it.
> - `HV-24` fails if the running interface is not on the configured address, or if DHCP is active.
>
> The `.ioc` now also carries `LWIP.LWIP_DHCP=0` with the address fields, so CubeMX itself regenerates the correct code rather than being overridden after the fact.

**Final values:**

```text
Controller (CNC5AX-ETH):
  IP Address : 192.168.5.10
  Subnet     : 255.255.255.0
  Gateway    : 0.0.0.0   (none — isolated point-to-point link, no router)

Host PC (Mach3):
  IP Address : 192.168.5.100
  Subnet     : 255.255.255.0
```

**Reasoning for the `192.168.5.0/24` subnet:** this is a dedicated, isolated point-to-point link between the controller and one PC NIC — not a shared LAN — so any private (RFC1918) subnet works electrically. `192.168.5.0/24` was chosen specifically to avoid the two most common home/office router defaults, `192.168.0.0/24` and `192.168.1.0/24`; if the Mach3 PC's other network adapter (e.g. for internet access) happens to sit on one of those, a shared subnet on the wrong interface could cause routing ambiguity. A distinct third octet avoids that regardless of how the PC's other NICs are configured.

**Gateway = `0.0.0.0`:** there is no router on this link and nothing outside the `/24` needs to be reached, so no default gateway is configured. This is standard for an isolated point-to-point industrial link.

**MAC address:** fully resolved — see ADR-011 in `Docs/FIRMWARE-ARCHITECTURE.md` §41. Bench testing uses a locally-administered address, replacing the CubeMX placeholder (`00:80:E1:00:00:00` in `ethernetif.c`, a real vendor's OUI that was never this project's to use). Production units derive their address per-unit from the STM32F407's factory-programmed 96-bit unique device ID (confirmed by the project owner) — no purchase, no fixed shared address, no collision risk across units.

**UDP port:** still `[TBD]` — depends on the application protocol design, not on the IP layer. Tracked in `Docs/MACH3-INTERFACE.md` §7.

The PC-side IP address (`192.168.5.100`) must be configured in Windows' network adapter settings for whichever NIC is physically connected to the controller; this is a host-side/plugin concern, not something the firmware can set.

---

# 16. Network Configuration Source

[FW-CONFIRMED]

The authoritative source of network configuration is:

```text
Firmware/Net/Inc/net_config.h  — AUTHORITATIVE: IP, netmask, gateway, MAC,
                                 PHY reset timing, Ethernet NVIC priority
Firmware/LWIP/App/lwip.h       — IP_ADDR0..3, NETMASK_ADDR0..3, GW_ADDR0..3
Firmware/LWIP/App/lwip.c       — MX_LWIP_Init(), applies the above via IP4_ADDR(),
                                 then net_glue_apply_static_ip() in USER CODE 3
Firmware/LWIP/Target/lwipopts.h — LWIP_DHCP (0)
Firmware/LWIP/Target/ethernetif.c — MACAddr[] in low_level_init(), overwritten
                                 from net_config.h in USER CODE MACADDRESS
Firmware/CNC5AX-ETH.ioc        — LWIP.LWIP_DHCP / IP_ADDRESS / NET_MASK /
                                 GATEWAY_ADDRESS, so CubeMX regenerates it right
```

`Firmware/Net/Inc/net_config.h` is the single source of truth as of Phase 2. The values in the generated files are CubeMX's own template output and are kept consistent with it, but they are not what the firmware ultimately relies on — the `USER CODE` calls above apply `net_config.h`'s values regardless, because the generated copies are exactly what a regeneration rewrites.

UDP port and any application-protocol configuration will live in the protocol layer once it exists (not yet implemented — see `Docs/MACH3-INTERFACE.md`).

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
                  Hardware Timer + DMA + BSRR
                         │
                         ▼
                     STEP / DIR
```

Synchronization between axes should be handled by the motion-control subsystem rather than by the Ethernet transport layer.

---

## 20.5 Hardware Timer Interface

[PROJECT-DECISION]

All five axes share **one** deterministic STEP pulse-generation mechanism. There is no per-axis timer or per-axis DMA stream: a single base timer (`TIM2`) Update event triggers a single DMA stream (`DMA1_Stream1`) that writes one 32-bit word to `GPIOA->BSRR`, and that one word carries the STEP bits of all five axes at once.

```text
                         Motion Engine
                              │
                              ▼
                   STEP event / BSRR buffer
                              │
         TIM2 Update ───► DMA1_Stream1 ───► GPIOA->BSRR
                              │
        ┌───────┬─────────────┼─────────────┬───────┐
        ▼       ▼             ▼             ▼       ▼
      PA8     PA9           PA10          PA11    PA12
     STEP X  STEP Y        STEP Z        STEP A  STEP B
```

Per-axis step rates are produced by which bits the motion engine sets in each successive BSRR word, not by giving each axis its own timer or channel. Because all five axes are updated by the same write, they change state with no timing skew relative to one another.

The authoritative definitions are `Docs/PINOUT.md` (pins), `Docs/MOTION-ENGINE.md` Sections 9–11 (architecture), and ADR-001/ADR-002/ADR-004/ADR-005 in `Docs/FIRMWARE-ARCHITECTURE.md` (timer, DMA, base tick, NVIC, pin consolidation).

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
Hardware Timer + DMA + BSRR
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
| LAN8720A RMII clock mode | `[HW-CONFIRMED]` RMII; PHY clocked from the module's own 50 MHz oscillator |
| RMII 50 MHz clock source | `[HW-CONFIRMED]` On-board 50 MHz oscillator on the PHY module, feeding both the PHY's `XTAL1/CLKIN` and the MCU's `PA1`. The MCU does not generate it; no MCO is configured. Verified against `LAN8720A/LAN8720-ETH-Board-Schematic.pdf`. |
| Exact Ethernet pin configuration | `[HW-CONFIRMED]` Nine RMII signals per `Docs/PINOUT.md`, matched by the generated `.ioc` |
| PHY SMI (MDIO) address | `[FW-CONFIRMED]` Strapped to 0 or 1 in hardware by `RXER/PHYAD0`; the LAN8742 driver's `LAN8742_Init()` scans addresses 0–31 and does not assume a fixed value, so the hardware ambiguity does not matter |
| PHY driver | `[FW-CONFIRMED]` LAN8742 (CubeMX offers no LAN8720 driver); register usage verified compatible — see §2.2 |
| PHY reset release (`PB0`) | `[FW-CONFIRMED]` `PB0` is driven HIGH in `MX_GPIO_Init()`, before `MX_LWIP_Init()` runs, releasing `nRST`. It is held HIGH permanently rather than pulsed — see the power-on timing note in §2.2. |
| LwIP version | `[FW-CONFIRMED]` v2.1.2_Cube |
| RAW API usage in final firmware | `[FW-CONFIRMED]` `NO_SYS=1`, `LWIP_NETCONN=0`, `LWIP_SOCKET=0` in `lwipopts.h` |
| Static IP values | `[FW-CONFIRMED]` Controller `192.168.5.10`, PC `192.168.5.100`, mask `255.255.255.0`, no gateway — see Section 15 |
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
| LwIP API | `[FW-CONFIRMED]` RAW API |
| IP Configuration | `[FW-CONFIRMED]` Static IPv4 — `192.168.5.10` / `255.255.255.0`, no gateway |
| Ethernet Transfer | `[HW-CONFIRMED]` DMA capable |
| Motion Protocol | `[TBD]` UDP-based application protocol |
| Host Integration | `[TBD]` Mach / LinuxCNC / custom host |
| Network Update Rate | `[TBD]` |
| Motion Timing | `[PROJECT-DECISION]` Must remain deterministic |
| CoreXY | `no` |
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
