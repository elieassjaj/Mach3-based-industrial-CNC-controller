# CNC5AX-ETH — Mach3 Host Interface

## 1. Purpose and status

This document records what has been **established by reading the Mach3 SDK contained in this repository** about how Mach3 drives an external motion device, and what that implies for CNC5AX-ETH.

`MACH3/SDK-README.MD` requires that the project maintain a concise, project-specific summary of the SDK APIs it actually relies on, rather than copying the SDK or assuming behavior. This document is that summary.

Status tags follow `Docs/ETHERNET.md`:

| Tag | Meaning |
|---|---|
| `[SDK-CONFIRMED]` | Read directly from the SDK sources in `MACH3/` |
| `[DESIGN-IMPLICATION]` | Conclusion this project draws from an SDK-confirmed fact |
| `[TBD]` | Not yet established |

Everything below marked `[SDK-CONFIRMED]` was read from the SDK archives in `MACH3/`. Nothing here is inferred from general knowledge of Mach3.

---

## 2. The integration model

`[SDK-CONFIRMED]`

Mach3 does not speak Ethernet. An external controller is driven by a **plugin DLL that runs on the PC inside Mach3**, and that plugin is responsible for talking to the hardware by whatever transport it likes.

```text
Mach3 (PC)
  │  trajectory planner, G-code interpreter, DROs, signal table
  ▼
Mach3 plugin  (our code — a COM DLL in Mach3/Plugins/)
  │  UDP datagrams
  ▼
CNC5AX-ETH (STM32F407)
  │  STEP/DIR generation, I/O, spindle
  ▼
Drives / machine
```

The SDK's `SDK/BlankMovement/` is the skeleton for exactly this kind of plugin, and `SDK/ncPod/`, `SDK/g100IO/` and `SDK/Galil PlugIn/` are worked examples of **Ethernet-connected** motion controllers built on it. `SDK/ncPod/` is the closest reference for this project.

### 2.1 Plugin entry points the device must service

`[SDK-CONFIRMED]` — from `SDK/BlankMovement/ExternalMovement.h` and `.cpp`:

| Function | Responsibility |
|---|---|
| `SendHoldingMovement()` | Drain Mach3's planned-move ring buffer and transmit the moves to the device |
| `GetInputs()` | Read device input states and publish them into Mach3's signal table |
| `SetOutputs()` | Read the outputs Mach3 wants and send them to the device |
| `MyJogOn(short axis, short direction, double SpeedNorm)` | Start a jog on one axis |
| `MyJogOff(short axis)` | Stop jogging that axis; when all axes stop, requests a position re-sync |
| `myDwell(double time)` | Dwell |
| `HandleSequences()` | Plugin-defined sequential logic (homing sequences etc.) |

Additionally `[SDK-CONFIRMED]` from `SDK/ncPod/MachDevImplementation.cpp`, Mach3 notifies the plugin of events through a `myNotify(int message)` callback, using message codes such as `EX_SPINON`, `EX_SPINOFF`, `EX_SPINSPEED` and `-1` (stop).

---

## 3. What Mach3 hands the device: `GMoves`

`[SDK-CONFIRMED]` — `MachIncludes/TrajectoryControl.h`:

```c
struct GMoves
{
    int      type;                     // 0 = linear, 1 = cubic
    double   cx, cy, cz;               // arc centre, for cubics
    double   ex, ey, ez, ea, eb, ec;   // end point,   6 axes
    double   sx, sy, sz, sa, sb, sc;   // start point, 6 axes
    __int64  DDA1[6];                  // per-axis DDA coefficients (cubics)
    __int64  DDA2[6];
    __int64  DDA3[6];
    double   Time;                     // segment duration
    bool     Stop;
};
```

These live in a **4096-entry ring buffer**, `MainPlanner->Movements[4096]`, with `Engine->TrajHead` as the producer index and `Engine->TrajIndex` as the consumer index; the plugin advances `TrajIndex` (masked with `0xfff`) as it consumes moves.

`[DESIGN-IMPLICATION]` Three things follow directly:

1. **Mach3 gives us planned trajectory segments, not step pulses.** The host does G-code interpretation, look-ahead and velocity planning. Converting a segment into STEP/DIR edges is *our* job. This is what `Docs/MOTION-ENGINE.md` §13 left open, and it confirms the DDA-style interpolation assumed by ADR-004.
2. **Mach3's native model is six axes** (X, Y, Z, A, B, C). CNC5AX-ETH implements five (X, Y, Z, A, B), so the plugin must map five and discard the sixth. This has to be explicit in the protocol so an axis is never silently mis-indexed.
3. **Segment duration is supplied** (`Time`), so the device can be driven on a time base rather than having to re-derive feed rate.

---

## 4. How a real Ethernet device consumes it: the `ncPod` reference

`[SDK-CONFIRMED]` — `SDK/ncPod/ExternalMovement.cpp`:

`ncPod` does **not** forward `GMoves` verbatim. It converts the planner output into a **fixed-time-slice velocity drip feed**:

- It waits until at least **32 moves** are queued (`TrajHead - TrajIndex`, wrapped at 4096) before sending anything, unless the program is ending.
- `Send512Block()` builds a **512-byte block of 32 records** and sends it. The source comment states this is *"128ms of data ... at 50hz ... 4ms or 2ms"* per record.
- Each record contains an opcode (`SDVELOCITYMOVE`), a 3-byte G-code line ID, and **one signed 16-bit velocity value per axis**, computed as roughly `(axis_distance × 32768) / DripTime`, with the sign taken from `Engine->Directions[axis]`.
- Sub-count remainders are carried between records in a per-axis fractional accumulator (`Pod->fractions[axis]`), so no motion is lost to truncation, and `Pod->vtotal[]` accumulates commanded position.
- Flow control is explicit: the device reports a full buffer (`BufferHolding`), the host retries, and after 10 consecutive held attempts (≈1 s) the plugin raises an error and gives up on the hold.
- End of program sends `SDFLUSHBUFFER`.
- `GetInputs()` reads an input bitfield from the device status (`Pod->PodStatus.inio`), maps bits onto Mach3's `Engine->InSigs[]` signal table honouring each signal's `Negated` flag, and sets `Engine->EStop` when a limit input becomes active.

`[DESIGN-IMPLICATION]` This is a proven shape for our protocol and it settles several open questions:

- **Network update rate.** `Docs/ETHERNET.md` §7 carried "1 kHz" as an unconfirmed initial target. A real Mach3 Ethernet device runs its motion transport at **~50 Hz with ~128 ms of motion buffered on the device**. Deep buffering plus a modest packet rate is the established pattern; a 1 kHz packet rate is not a requirement. The figure in §7 should be treated as superseded by this evidence rather than as a target.
- **Buffer depth.** ≈128 ms of motion on the device is a concrete, evidence-backed starting point for `Docs/MOTION-ENGINE.md` §16, and is what makes the system tolerant of Windows scheduling jitter and occasional packet loss.
- **Representation.** Per-axis signed velocity per fixed time slice, with fractional carry, is a proven encoding that maps cleanly onto a DDA running at our 4 MHz base tick (ADR-004).
- **Feedback.** The device must return at least an input bitfield and buffer occupancy; E-stop and limit reaction is partly host-side, driven from that bitfield.

---

## 5. Transport

`[SDK-CONFIRMED]` — `MachIncludes/UDPSocket.h`, `MachIncludes/GenUDPSocket.h`, `SDK/Galil PlugIn/UDPSocket.cpp`:

Mach3 plugins use MFC's `CAsyncSocket`. The SDK's `CUDPSocket` wrapper creates a **`SOCK_DGRAM` (UDP)** socket with `Create(port)`, and its `OnReceive()` handler calls `ReceiveFrom(Buffer, sizeof Buffer, ip, port)` and hands the datagram to the plugin. `CreateStream()` exists for `SOCK_STREAM` where a plugin wants TCP instead.

`[DESIGN-IMPLICATION]` UDP as this project's transport (`Docs/ETHERNET.md` §5) is consistent with how shipping Mach3 Ethernet plugins work, and the host-side socket work is a solved, well-exemplified problem. Reliability remains our responsibility at the application layer, exactly as §27 of that document states.

---

## 6. What this means for CNC5AX-ETH

`[DESIGN-IMPLICATION]`

- The project needs a **PC-side Mach3 plugin** as a real deliverable, not just firmware. It is a Visual C++ COM DLL built from the SDK skeleton. The repository currently has no home for it; see §7.
- The **firmware's motion input** should be time-sliced per-axis motion with an explicit slice duration, not raw step counts and not G-code.
- The firmware must maintain a **motion buffer deep enough to cover host-side jitter** (the reference design buffers ~128 ms) and must report its occupancy so the host can throttle.
- The firmware must return an **input-state bitfield** frequently enough for the host to react to limits, plus its own fault/E-stop state.
- Because the STM32 also handles E-stop locally through EXTI (`PE2`, priority 0), there are **two independent E-stop paths**: the local hardware one and the host-side one driven from reported inputs. `Docs/FIRMWARE-ARCHITECTURE.md` §8 already requires that the local path never depend on the network.

---

## 7. Open items

| Item | Status |
|---|---|
| Repository location for the PC-side Mach3 plugin | `[TBD]` — no directory reserved yet |
| Our UDP packet format (header, opcode, sequence, payload, CRC) | `[TBD]` |
| Static IP values | `[FW-CONFIRMED]` Controller `192.168.5.10`, PC `192.168.5.100` — `Docs/ETHERNET.md` §15 |
| UDP port number | `[TBD]` |
| Slice duration and block size for our own protocol | `[TBD]` — reference design uses 2–4 ms slices, 32 per block |
| Device→host status packet contents and rate | `[TBD]` |
| Mapping of Mach3's 6 axes onto this controller's 5 | `[TBD]` |
| Homing, probing, spindle and jog command encodings | `[TBD]` |
| Mach3 version/licence constraints for plugin distribution | `[TBD]` |

The SDK archives in `MACH3/` remain the authority for anything host-side. `SDK/ncPod/` and `SDK/Galil PlugIn/` should be re-read in detail before the protocol is specified.
