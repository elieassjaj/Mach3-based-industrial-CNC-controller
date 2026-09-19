# Network Subsystem (Phase 2 / M12, Phase 3 / M13-M14)

The Ethernet side of the STM32CubeIDE project: PHY reset, link bring-up,
static addressing and link state (Phase 2), plus the C5P1 motion protocol
over UDP (Phase 3).

```text
Net/Inc/net_config.h           AUTHORITATIVE: IP, MAC, UDP port, PHY timing,
                               interrupt priorities
Net/Inc/cnc_protocol.h         C5P1 wire format - Docs/PROTOCOL.md in code
Net/Src/cnc_session.c          sequence rules, dispatch, supervision
Net/                           portable throughout - no STM32/HAL/lwIP
Platform/STM32F407/            ADR-013 reset pulse, lwIP UDP binding,
                               glue, self-tests
Tests/test_net_link.c          host verification - link observer
Tests/test_protocol.c          host verification - protocol, against the
                               real motion engine
../../Tools/c5p1.py            PC-side client: drive the board without Mach3
```

The protocol layer reaches the motion engine only through `stepgen.h`, and
`Net/` has no hardware or lwIP dependency, so every protocol rule is tested
on a host.

See `../Docs/PHASE2-STATUS.md` and `../Docs/PHASE3-STATUS.md` for results,
assumptions and blockers; `../Docs/PROTOCOL.md` for the wire format; and
`../Docs/HARDWARE-VALIDATION.md` (HV-20..HV-25, HV-30..HV-36) for the
bring-up procedure.

---

## Building

```sh
make test       motion (1150) + inputs (151) + outputs (1160)
                + network (130) + protocol (347)
make test-net   the link-observer suite alone
make test-proto the protocol suite alone
make arm        cross-compile both subsystems for Cortex-M4F
make firmware   whole-image verification build, generated files included
```

`make firmware` is a **verification** build, not the flashable deliverable.
STM32CubeIDE produces the image; its build configuration differs, so the
section sizes will not match. What this target proves is that everything
compiles, links, fits, and puts its buffers in the regions ADR-012
requires.

---

## CubeIDE project settings: already in `.cproject`

Nothing to do. The project's own folders are registered in **both** the
Debug and Release configurations:

| Setting | Value |
|---|---|
| Include paths | `../Motion/Inc`, `../Net/Inc`, `../Safety/Inc`, `../IO/Inc`, `../Platform/STM32F407/Inc` |
| Source folders | `Motion`, `Net`, `Safety`, `IO`, `Platform/STM32F407` |

`Platform/Host` is deliberately **not** a source folder: it holds the
simulation ports, and compiling them for the target would collide with the
STM32 ports symbol for symbol.

The `.ioc` carries none of this — CubeMX does not manage source folders —
so a CubeMX regeneration cannot remove it either. What it can do is rewrite
`Core/`; see below.

---

## Where this touches the generated code

Three one-line calls, each inside a `USER CODE` block so a regeneration
keeps them:

| File | Block | Call |
|---|---|---|
| `LWIP/Target/ethernetif.c` | `MACADDRESS` | MAC from `net_config.h`, then `net_glue_phy_reset()` |
| `LWIP/App/lwip.c` | `3` | `net_glue_apply_static_ip()` |
| `LWIP/App/lwip.c` | `4_4` | `net_glue_poll_link()` |

If one is dropped, HV-20/HV-23, HV-24 and HV-25 fail respectively.

Things to be aware of when rebuilding in CubeIDE:

- **Do not move the PHY reset call after `HAL_ETH_Init()`.** That call
  performs the MAC's DMA soft reset; starting it while the PHY is still
  held in reset invites a timeout.
- **Do not let the static IP live only in the generated code.** That is
  exactly what was lost once already: the configuration was written into
  CubeMX-generated regions and a later regeneration restored
  `dhcp_start()`. There is no DHCP server on this link. See
  `../Docs/PHASE2-STATUS.md` §2.
- **Check the Ethernet interrupt priorities after any regeneration.**
  `ETH_IRQn` and `ETH_WKUP_IRQn` must stay numerically above
  `DMA2_Stream1_IRQn` (5 vs 2). There is no `USER CODE` block near them in
  `HAL_ETH_MspInit()`, so HV-22 is the safety net.

---

## Using the link state

```c
#include "net_link.h"

net_link_status_t st;
net_link_get(&st);

if (st.link_up && st.speed == NET_SPEED_100M) { /* ... */ }
```

`up_count`, `down_count` and `last_change_ms` are there to tell a pulled
cable apart from a quiet host — a flapping link is a cabling or
auto-negotiation problem, and it looks nothing like a link that simply
never came up.

Note that link-down is **not** ADR-010's `COMM_TIMEOUT`. That fault is a
protocol-level timeout, reported in `STATUS.proto_faults`; this module
reports the physical layer only.

---

## Driving the board without Mach3

`../Tools/c5p1.py` speaks C5P1 from a plain PC — no Mach3, no plugin, no
third-party Python packages. Set the PC NIC to `192.168.5.100/24` first.

```sh
./c5p1.py info                                   # HELLO -> INFO
./c5p1.py watch                                  # follow the status stream
./c5p1.py enable && ./c5p1.py start
./c5p1.py move --axis X --steps 2000 --seconds 2
./c5p1.py stop
./c5p1.py raw --corrupt crc                      # device must stay silent
```

`move` and `stream` command real motion — run them with the drives
disconnected until the machine is trusted.

The device is passive: it transmits nothing until it receives a valid
packet, so every session starts with a `HELLO`. If nothing comes back, that
is the expected behaviour of a device that has not heard anything valid,
not necessarily a dead link.

The tool is also a working reference decoder for the wire format. Whoever
writes the Mach3 plugin should read it alongside `../Docs/PROTOCOL.md`
rather than re-deriving the framing.

---

## Before running this on a machine

- Configure the PC NIC as `192.168.5.100 / 255.255.255.0`, then
  `ping 192.168.5.10`.
- Run `net_selftest_run_all()` and check every result. HV-24 is the one
  that catches a lost static IP; HV-21 and HV-22 are the two that keep
  Ethernet out of the motion engine's way.
- Record the PHY address that HV-25 reports. The LAN8720A strap allows 0
  or 1 and this repository's sources disagree about which the module uses,
  so the actual value is genuinely unknown until the board runs.
- Then run **HV-15** and **HV-36** from `../Docs/HARDWARE-VALIDATION.md` —
  motion timing under Ethernet load, and under real protocol traffic. They
  are the tests the project's central rule actually rests on.
- For the protocol itself, `../Tools/c5p1.py info` is the first thing to
  try: it is the first proof a C5P1 packet survives a real round trip.
