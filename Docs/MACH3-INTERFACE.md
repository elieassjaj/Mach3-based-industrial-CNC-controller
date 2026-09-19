# CNC5AX-ETH — Mach3 Host Interface

## 1. Purpose and status

This document records what has been **established by reading the Mach3 SDK contained in this repository** about how Mach3 drives an external motion device, and what that implies for CNC5AX-ETH.

`MACH3/SDK-README.MD` is a real index of the extracted SDK (eight sample plugin projects, two copies of the shared `MachIncludes` headers) and requires that the project maintain a concise, project-specific summary of the SDK APIs it actually relies on, rather than copying the SDK or assuming behavior. This document is that summary.

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

The full SDK archive is extracted under `MACH3/` — `MACH3/SDK-README.MD` is a real index of what it contains (eight sample plugin projects plus two copies of the shared `MachIncludes` headers) and states plainly which samples are and are not relevant here. Of the eight, only three actually consume trajectory data at all: `SDK/BlankMovement/` (the unfilled skeleton — the entry-point reference, §2.1 below), and two real, shipped, non-skeleton plugins, `SDK/ncPod/` and `SDK/Galil PlugIn/`, which turn out to solve "consume `GMoves`" two genuinely different ways (§4, §4a). The other five samples (`Blank Plugin`, `JoyStickPlugIn`, `Probing`, `ShuttlePro`, `g100IO`) are jog/probing/I/O plugins with no `ExternalMovement` at all and are not evidence for anything in this document.

> **Correction (Phase 3).** An earlier revision of this section described `ncPod` as Ethernet-connected. It is not: `SDK/ncPod/ncPODDriver.h` defines **USB endpoints** ("64 byte usb packets are sent to and from the pod through 2 different endpoints"), and the project links `libusb.lib`. That does not weaken it as a reference — what this project takes from `ncPod` is the *shape of the motion data* (§4), which is transport-independent — but it does mean `ncPod` is no evidence at all about UDP, ports or framing. Those come from `SDK/Galil PlugIn/` and `MachIncludes/UDPSocket.h` (§5), and where the SDK is silent, from this project's own decisions in `Docs/PROTOCOL.md`.

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

## 4a. The other real reference, `Galil PlugIn` — a different pattern this project deliberately does not follow

`[SDK-CONFIRMED]` — `SDK/Galil PlugIn/ExternalMovement.cpp`:

`Galil PlugIn` is the *other* real, shipped, non-skeleton motion plugin in the SDK (for Galil DMC-family motion controllers), and it solves "consume `MainPlanner->Movements[]`" completely differently from `ncPod`:

- It does **not** batch moves or drip-feed velocity. It walks the same `Engine->TrajIndex`/`Engine->TrajHead` ring buffer `ncPod` uses, but one move at a time (`while( Engine->TrajIndex != Engine->TrajHead ) { GMoves move = MainPlanner->Movements[Engine->TrajIndex]; ... }`).
- For each move it forwards the move's own **cubic `DDA1[axis]`/`DDA2[axis]`/`DDA3[axis]` coefficients and `Time`** directly to the controller (`ExternalMovement.cpp` lines ~294–311). It relays Mach3's own segment math; it does not re-derive velocity itself the way `ncPod` does.
- This works because a Galil DMC controller has its own onboard interpolator that consumes exactly this cubic representation — the "device" in this pattern is doing Mach3-shaped math, not step generation from a velocity stream.

`[DESIGN-IMPLICATION]` This is real evidence that "relay the segment coefficients, let the device interpolate" is also a valid, shipped pattern — but it is not the one this project uses. ADR-007 (`Docs/FIRMWARE-ARCHITECTURE.md` §41) already put a DDA/Bresenham accumulator on the STM32 itself, running from step-domain, time-sliced per-axis velocity: CNC5AX-ETH's firmware has no onboard consumer for raw cubic `DDA1/2/3` coefficients, so following `Galil PlugIn`'s pattern here would mean building a second on-device interpolator to parse Mach3's own cubic math — exactly the duplicated work ADR-007's "Alternatives considered" already rejected for a different reason. Finding this second pattern in the SDK does not change ADR-007; it confirms the project picked the pattern that actually matches the firmware architecture already committed to, not the only pattern that exists.

---

## 5. Transport

`[SDK-CONFIRMED]` — `MachIncludes/UDPSocket.h`, `MachIncludes/GenUDPSocket.h`, `SDK/Galil PlugIn/UDPSocket.cpp`:

Mach3 plugins use MFC's `CAsyncSocket`. The SDK's `CUDPSocket` wrapper creates a **`SOCK_DGRAM` (UDP)** socket with `Create(port)`, and its `OnReceive()` handler calls `ReceiveFrom(Buffer, sizeof Buffer, ip, port)` and hands the datagram to the plugin. `CreateStream()` exists for `SOCK_STREAM` where a plugin wants TCP instead.

**Precision on where this is actually used in `Galil PlugIn` itself:** `CUDPSocket` there is used by `G100Config.cpp` and `MessageTracker.cpp` — device discovery/configuration traffic — not by `GalilControl.cpp`/`ExternalMovement.cpp`, whose real-time motion path goes through Galil's own command library (`DMCMLIB`/`DMC32.lib`, `DMCWIN.CPP`), not raw UDP sockets. So `Galil PlugIn` is evidence for the `CUDPSocket` wrapper's API shape (confirmed above), but it is not evidence that a shipped Mach3 motion plugin runs its motion stream over plain UDP — that is this project's own choice (`Docs/ETHERNET.md` §5, `Docs/PROTOCOL.md`), not something the SDK demonstrates end-to-end.

`[DESIGN-IMPLICATION]` UDP as this project's transport (`Docs/ETHERNET.md` §5) is consistent with how the SDK's socket wrapper works and the host-side socket work is a solved, well-exemplified problem, but the specific choice to run motion (not just config/discovery) over that UDP socket is CNC5AX-ETH's own design decision, not a pattern copied from a shipped Galil deployment. Reliability remains our responsibility at the application layer, exactly as §27 of that document states.

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
| Repository location for the PC-side Mach3 plugin | `[TBD]` — still unreserved. `Tools/` now holds a Python test client, but that is a bench tool, not the plugin, and does not settle where the plugin lives |
| Our UDP packet format (header, opcode, sequence, payload, CRC) | **RESOLVED** — `Docs/PROTOCOL.md` §4, ADR-014 |
| Static IP values | `[FW-CONFIRMED]` Controller `192.168.5.10`, PC `192.168.5.100` — `Docs/ETHERNET.md` §15 |
| UDP port number | **RESOLVED** — **55010**, ADR-014 |
| Slice duration and block size for our own protocol | **RESOLVED** — host-chosen `slice_us`, up to 32 records per block; 4 ms × 32 recommended, `Docs/PROTOCOL.md` §8 |
| Device→host status packet contents and rate | **RESOLVED** — 96-byte `STATUS` at 50 Hz, `Docs/PROTOCOL.md` §6 |
| Mapping of Mach3's 6 axes onto this controller's 5 | **RESOLVED** — five wire slots X,Y,Z,A,B; **no C field exists at any offset**, `Docs/PROTOCOL.md` §5.5 |
| Homing, probing, spindle and jog command encodings | `[TBD]`, but the blocker moved: M3 exists as of Phase 4, so the inputs are readable. Homing still needs a per-input *meaning* (host-side, ADR-010/ADR-015) and a command encoding; probing needs a position-capture path in the input ISR, which M3 deliberately does not have — a 50 Hz status word is not a probe capture. Jog and dwell need no opcode — they are ordinary motion blocks. Spindle/relay have a fixed wire format that the firmware answers `NOT_IMPLEMENTED` until M9/M10 exist — `Docs/PROTOCOL.md` §9, §10 |
| Mach3 version/licence constraints for plugin distribution | `[TBD]` |

The SDK archives in `MACH3/` remain the authority for anything host-side.

## 8. What the plugin must do, recorded now

`[DESIGN-IMPLICATION]` Obligations that Phase 3 pushed onto the host, listed here so Phase 4 does not have to rediscover them from `Docs/PROTOCOL.md`:

1. **Carry the per-axis fractional remainder** across slices, as `ncPod`'s plugin does with `fractions[]`. The firmware never sees the millimetre-domain path and cannot do this for the host.
2. **Refuse a program that commands the C axis.** Quietly dropping a sixth axis machines the wrong part. There is no C field to drop it into, which is the point.
3. **Never assume a block was accepted.** Read `last_block_seq` and `queue_free` from the status; retry a refused block with the *same* `block_seq`.
4. **Close the position loop from `pos_output[]`.** The device is never more than one step behind but is, in general, one step behind — `Docs/PROTOCOL.md` §5.3.
5. **Stop on any latched `proto_faults` bit and require operator action.** Clearing and continuing past a `SEQ_GAP` means cutting a path with a hole in it.
6. **Check `tick_hz` from `INFO`** before choosing `slice_us`, so the slice converts exactly, and before trusting a feed rate to be inside the device ceiling.

---

## 9. What M3 gives the plugin (Phase 4)

`[DESIGN-IMPLICATION]` The input manager exists, so `GetInputs()` now has a
real source. Three things follow, and the first is the one a plugin author
is most likely to get wrong:

1. **`STATUS.inputs` is already normalised to logical assertion.** Bit *n*
   is set when `PE`*n* is asserted, not when the pin is HIGH. The board's
   inputs are active low; the firmware applies that once (ADR-015) so the
   plugin does not. What the plugin *does* apply is Mach3's own per-signal
   `Negated` flag when writing `Engine->InSigs[]`, exactly as `ncPod` does
   from `Pod->PodStatus.inio` — that flag is machine configuration, not
   board polarity, and the two must not be conflated.

2. **Which pin means what is the plugin's decision, and nothing in this
   repository has made it.** The firmware assigns no meaning to any input
   except `PE2`. There is no "X+ limit is `PE5`" anywhere, deliberately
   (ADR-010, ADR-015 decision 5). The plugin owns that table, and
   `ncPod`'s `GetInputs()` — bit → `InSigs[]` index, honouring `Negated`,
   raising `Engine->EStop` on a limit — is the reference shape for it.

3. **`inputs` is a 50 Hz state report, not an event stream and not a
   capture.** It is adequate for limits and for anything Mach3 polls. It is
   **not** adequate for probing, which needs the machine position latched
   at the probe edge, in the interrupt — that path does not exist
   (`Docs/PROTOCOL.md` §10, §11). A plugin must not synthesise a probe hit
   from two status packets.

`[DESIGN-IMPLICATION]` **Phase 5 adds `SetOutputs()` and the spindle.**
`OUTPUTS` is now acted on rather than refused, and three things follow:

4. **An accepted `OUTPUTS` is not a switched relay.** The firmware holds
   every output off unless the machine is in `READY` or `RUNNING`, and
   drops them on any fault. `STATUS.outputs` and `STATUS.spindle_pmille`
   report what the pins are doing, so the plugin should drive Mach3's
   output LEDs from those and never from what it sent.
5. **`spindle_pmille` is `MainPlanner->Spindle.ratio × 1000`, and nothing
   else.** No RPM crosses the wire. The plugin keeps the RPM↔ratio map,
   exactly as `ncPod` does, because only it knows what spindle is fitted.
   Out of range is refused, not clamped. Note that `ncPod` never sends a
   zero duty while the spindle is on; that policy stays host-side, and this
   device reads zero as zero.
6. **An emergency stop discards pending output requests.** After a clear,
   the plugin must re-send the relay and duty it wants. Nothing comes back
   by itself.

Two firmware-side facts worth knowing host-side:

- The E-STOP input is reported (bit 2) **and** independently visible as the
  engine's own `EMERGENCY_STOP` in `STATUS.state`. Those are two different
  observations of the same event and either can arrive first; the machine
  is already stopped before either is transmitted.
- `CONTROL:CLEAR_ESTOP` is refused while `PE2` still reads asserted, and
  for a further release-stabilisation window after that. A plugin should
  expect `WRONG_STATE` and retry, not treat the first refusal as an error.
- **Spindle direction has no pin.** `Docs/PINOUT.md` gives one relay and
  nothing in this repository says it means CW/CCW rather than on/off. A
  plugin that needs `M4` must treat that as a hardware question, not a
  protocol one (`Docs/PROTOCOL.md` §10).
