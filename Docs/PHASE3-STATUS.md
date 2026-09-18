# Phase 3 Status — UDP and Motion Protocol (M13 / M14)

**Date:** 2026-09-17
**Scope:** The C5P1 protocol — wire format, session layer, UDP binding, and
the path from a received datagram to the Phase 1 motion queue. Plus a
PC-side test client so the firmware is exercisable without Mach3.
**Out of scope:** The Mach3 plugin (Phase 4), the input manager M3, and the
output/spindle modules M9/M10. Where the protocol touches those, it says so
rather than pretending.

---

## 1. Summary

| | |
|---|---|
| Protocol specification | `Docs/PROTOCOL.md`, ADR-014 — authoritative |
| Wire codec | Implemented, portable, 302 host checks passing |
| Session layer (sequence rules, dispatch, supervision) | Implemented, portable |
| UDP binding (lwIP RAW API) | Implemented, target-only |
| PC-side client | `Tools/c5p1.py`, verified byte-for-byte against the C codec |
| Motion engine changes | **None.** Phase 1 is untouched |
| Full firmware build | **Links clean** — see §5 |
| **Anything over real Ethernet** | **NOT DEMONSTRATED** — HV-30..HV-36 |
| Mach3 compatibility | **Not claimed, and must not be** |

The chain the prompt asked for exists and is tested end to end in
simulation:

```text
test client / plugin  ->  UDP  ->  lwIP  ->  cnc_protocol (validate)
                      ->  cnc_session (sequence, dispatch)
                      ->  stepgen_submit_segment()  ->  DDA  ->  STEP/DIR
```

What has *not* happened is any of it running on a board.

---

## 2. Protocol decisions, and where each came from

Full reasoning is in `Docs/PROTOCOL.md` §1 and ADR-014. The short version,
with the provenance that matters:

| Decision | Value | Basis |
|---|---|---|
| UDP port | **55010** | Free choice. No SDK evidence exists — `ncPod` is USB, Galil's `13887` is that vendor's |
| Magic / version | `C5P1` / 1 | Project |
| Byte order | little-endian | Both ends are; `ncPod` swaps only because its PIC was big-endian |
| Framing | 12-byte header + payload + CRC-32 | Project; CRC is IEEE 802.3 so the host can use `zlib` |
| Motion encoding | signed `int32`, 1/65536 step per axis per slice | Shape from `ncPod`; width and unit ours (§3) |
| Slice duration | explicit `uint16` µs per block | Project. `ncPod` fixes it host-side; making it explicit lets the device check it converts exactly |
| Block size | ≤ 32 records | `ncPod`'s `Send512Block()` |
| Recommended cadence | 4 ms slices × 32 = 128 ms at ~50 Hz | `ncPod` source comment, verbatim |
| Sequence | two spaces: `seq` per packet, `block_seq` per motion block | `ncPod` echoes a host sequence; Galil keeps a *separate* queue sequence |
| Lost motion block | reject, latch `SEQ_GAP`, controlled stop | Project — a gap is a hole in the toolpath |
| Backpressure | whole block or nothing; `queue_free` in every status | `ncPod`'s `ACKBUFFERNOTREADY`; Galil's *window* |
| Status | 96 bytes at 50 Hz | Contents follow `ncPod`'s status struct |
| Comm timeout | 200 ms, only while motion is active | Project (§4) |
| Axes | five slots X,Y,Z,A,B; **no C field at any offset** | `Docs/MACH3-INTERFACE.md` §3 |

### 2.1 Where this project deliberately diverged from `ncPod`

Worth recording, because "the reference device does it this way" is not by
itself a reason:

- **`int32` at 1/65536 step instead of `int16` at a device-specific ratio.**
  `ncPod`'s unit is tied to its own microstepping (`32768/75`), so a host
  cannot encode a packet without knowing the device's step scaling. A
  device-independent unit keeps the ADR-007 split intact: the host owns
  millimetres, the device owns steps.
- **Little-endian**, for the reason above.
- **No opcode numbers reused.** `ncPod`'s are its USB API; matching them
  would imply a compatibility this device does not have.
- **A motion gap is fatal to the stream.** `ncPod`'s host-side retry logic
  gives up after ~1 s and raises an error; this device refuses to continue
  at all, which is the same instinct applied at the end that can actually
  enforce it.

---

## 3. What was built

```text
Firmware/
├── Net/                                PORTABLE - no STM32/HAL/lwIP
│   ├── Inc/cnc_protocol.h              wire format: the spec, in code
│   ├── Src/cnc_protocol.c              CRC-32, parse, encode - pure, stateless
│   ├── Inc/cnc_session.h               sequence rules, dispatch, supervision
│   └── Src/cnc_session.c               the only thing that calls stepgen.h
├── Platform/STM32F407/
│   ├── Inc/net_udp.h
│   └── Src/net_udp_stm32f4.c           lwIP RAW PCB; copies, calls, transmits
├── Tests/test_protocol.c               302 host checks
└── Core/Src/main.c                     net_udp_init() + net_udp_poll()

Tools/c5p1.py                           PC-side client and reference decoder
```

**Layering.** `cnc_protocol.c` is pure: no state, no side effects, decodes
and encodes buffers. `cnc_session.c` holds every stateful protocol rule and
is the only file that includes `stepgen.h` — the facade Phase 1 declared
for exactly this. `net_udp_stm32f4.c` does nothing that could have been
tested on a host: it copies a pbuf, calls the session, transmits the reply.

**The motion engine was not modified.** Not one line of `Motion/` or
`Platform/STM32F407/Src/stepgen_*` changed in Phase 3, and its 1150 checks
still pass unchanged. `motion_segment_t` already carried a `seq` field and
`motion_segment_queue.h` already named "the Phase 3 protocol layer" as its
producer, so the interface was waiting.

### 3.1 Where the receive path actually runs

Load-bearing, and easy to get wrong: in lwIP's `NO_SYS` configuration the
UDP receive callback runs from `ethernetif_input()`, which
`MX_LWIP_Process()` calls **in the superloop**. It is not an interrupt
handler; the ETH interrupt only services the MAC's DMA.

That is what makes it acceptable to decode a whole 32-record block inline.
The work is bounded — no allocation, no loop that depends on anything but
`record_count` — and it is preemptible throughout by the STEP-DMA vector at
NVIC priority 2, which ADR-012 keeps strictly above Ethernet's 5. HV-36 is
the measurement that will confirm it.

---

## 4. Reliability and safety behaviour

Every case from the brief, and what the firmware does:

| Case | Response |
|---|---|
| Lost motion block | Reject, latch `SEQ_GAP`, controlled stop, explicit clear required |
| Duplicate motion block | Ignored idempotently; status still replied |
| Out-of-order / stale | Not acted on; status still replied |
| Corrupt packet | Dropped, counted, **no reply at all** |
| Burst / queue full | Whole block refused, `queue_free` reported, host retries same `block_seq` |
| Comm timeout | Latched after 200 ms of silence *while motion is active*; controlled stop |
| Motion before drives are enabled | Refused with `WRONG_STATE` before anything is enqueued |
| Rate above 2 MHz | Block refused — **never clamped** |
| Slice that does not convert exactly | Block refused |

**ADR-010 is preserved, not extended.** No new state model was invented.
Every command-stream problem produces the same response the ADR already
fixed: a controlled stop that **holds position with `EN` still asserted**,
so a broken link never de-energises a stepper and lets an axis drift or
drop under load. Nothing here is an emergency stop.

**The three rules the brief called out, explicitly:**

1. No fault or E-stop clears because communication returned. `CLEAR_FAULT`
   is the only thing that clears `proto_faults`, and it cannot clear an
   E-stop — that needs `CLEAR_ESTOP`, which the engine itself refuses while
   the physical input is asserted.
2. A dead link is not an E-stop. It is a `COMM_TIMEOUT` fault.
3. The local `PE2` E-stop path is untouched. Nothing in Phase 3 goes near
   it, and no packet can reach it.

---

## 5. Build and test results

`cd Firmware && make test && make arm && make firmware`

### Host verification — 302 protocol checks, 0 failures

Covering: CRC vectors; framing round-trip for every packet type; short,
over-long, bad-magic, bad-version, bad-opcode, length-mismatch, bad-CRC and
inconsistent-record-count packets; corrupt packets reaching nothing;
duplicate and stale sequences; motion → steps with pulse counting; per-axis
mapping; negative direction; sub-step accumulation; block gap, duplicate
and straggler; atomic backpressure and recovery; inexact slice; rate
ceiling refused not clamped; every control command and an illegal
transition; comm timeout and its non-recovery; status cadence; full status
and info round-trips; `OUTPUTS` answered `NOT_IMPLEMENTED`.

Phase 1 and Phase 2 suites still pass unchanged: **1150** and **130**.

### Cross-check against the PC tool

`Tools/c5p1.py` was verified in both directions against the C codec: a
`STATUS` and an `INFO` packet encoded in C decode field-for-field in
Python — including a position value above 2⁵³, which would not survive a
float round-trip — and a motion packet built in Python parses in C with the
correct `block_seq`, `slice_us`, record count and signed per-axis values.

This matters more than it looks: a protocol with one implementation is a
format nobody has checked.

### Cross-compilation and link

`arm-none-eabi-gcc 13.3`, `-O2`, warnings as errors including
`-Wconversion` and `-Wsign-conversion`. Clean.

```
motion + network objects:   text 10374   data 4096   bss 5661
full firmware image:        text 61604   data  132   bss 45320
```

Placement re-confirmed — ADR-012 still holds with the protocol buffers
added:

```
DMATxDscrTab              0x20000440   SRAM1
DMARxDscrTab              0x200004E0   SRAM1
memp_memory_RX_POOL_base  0x20000588   SRAM1
s_tx / s_rx (protocol)    0x20009648 / 0x200098E0   SRAM1
__stepgen_ram_start__     0x2001C000   SRAM2, unchanged
```

BSS ends at `0x20009B90`, leaving ~74 KB below the stack top.

---

## 6. Assumptions

| # | Assumption | Basis | How to confirm |
|---|---|---|---|
| C-1 | The lwIP receive callback is superloop context, not an ISR | `NO_SYS = 1`; `ethernetif_input()` is called from `MX_LWIP_Process()` | HV-36 measures the consequence that matters |
| C-2 | One host, on an isolated link | `Docs/ETHERNET.md` §15 | Endpoint learning is last-writer-wins; see §7 RISK-10 |
| C-3 | A 664-byte packet is never fragmented | 1500-byte MTU | HV-30 |
| C-4 | `HAL_GetTick()` millisecond resolution is adequate for a 20 ms status period and a 200 ms timeout | Both are ≥ 20 ticks | HV-32 |
| C-5 | 64 slots × 4 ms = 256 ms satisfies ADR-008's ≥128 ms | Arithmetic | HV-34 shows the real occupancy |

---

## 7. Risks and unresolved items

### RISK-8 — Nothing has been on a wire

The same honest position Phase 2 ended on, one layer up. The protocol is
correct against its own tests and against a second implementation; it has
never exchanged a datagram with a real MAC. HV-30..HV-36.

### RISK-9 — `OUTPUTS` is specified but not implemented

The wire format is fixed so Phase 4 can be written against it, and the
firmware answers `NOT_IMPLEMENTED` rather than accepting silently. The
`STATUS` flags report the absence explicitly so an all-zero output word is
not mistaken for a reading. It becomes real work when M9/M10 exist.

### RISK-10 — Endpoint learning is last-writer-wins

Acceptable only because the link is point-to-point with exactly one host.
On a shared network a second talker could capture the status stream. If the
link ever stops being isolated, bind the first accepted source instead.
Recorded rather than fixed, because fixing it now would add a configuration
knob for a topology this project does not have.

### RISK-11 — The device is one step behind, by design

A DDA carries its fractional phase across segments, so the last step of a
block generally lands at the start of the next. Measured and bounded: never
more than commanded, never more than one step short. The host must close
the loop from `pos_output[]` rather than assuming open-loop agreement. This
is documented in `Docs/PROTOCOL.md` §5.3 and is a Phase 4 obligation.

### Unresolved, and deliberately so

| Item | Why it is open |
|---|---|
| Repository location for the Mach3 plugin | Still unreserved. `Tools/` holds a bench client, not the plugin |
| Homing, probing | Need M3; there are no limit or probe inputs to watch yet |
| Position preset / `SETCOORDS` | Needs a decision on who owns machine coordinates. A real gap — v2 |
| Feed-rate override | Host-side by design: the plugin re-encodes slices. No device opcode planned |
| Input reporting | M3. `STATUS.inputs` is zero with `flags.5` clear so it cannot be misread |
| Mach3 version / licence constraints | Unchanged from `Docs/MACH3-INTERFACE.md` §7 |

---

## 8. What Phase 4 must know

Beyond the six obligations now recorded in `Docs/MACH3-INTERFACE.md` §8:

1. **`Tools/c5p1.py` is an executable reference.** Its `frame`/`unframe`,
   `parse_status` and `parse_info` are a working decoder; the `move`
   command demonstrates fractional carry, block chunking and the
   retry-with-the-same-`block_seq` backpressure loop.
2. **The plugin's `SendHoldingMovement()` maps onto `MOTION`**, and it must
   do the `GMoves` → time-sliced conversion host-side (ADR-007). The
   firmware will not accept anything else.
3. ~~**`GetInputs()` has nothing to read yet.**~~ **Superseded by Phase 4:**
   M3 exists, `STATUS.inputs` carries real readings and `flags.5` is set
   once the subsystem has initialised. The instruction stands unchanged — a
   plugin must still check that bit rather than publishing zeros into
   Mach3's signal table as if every switch were open — but the bit is now
   normally 1. See `Docs/PHASE4-STATUS.md` and `Docs/MACH3-INTERFACE.md` §9.
4. **`SetOutputs()` and spindle control will be refused** until M9/M10.
5. **`MyJogOn`/`MyJogOff` and `myDwell` need no new opcode** — both are
   ordinary motion blocks. If that proves inadequate in practice, that is
   evidence for a v2 opcode, not a reason to add one now.

---

## 9. Phase 3 exit criteria

| Criterion | State |
|---|---|
| Protocol specified, with evidence and decisions distinguished | Done — `Docs/PROTOCOL.md`, ADR-014 |
| UDP layer bound, validating, non-blocking | Done |
| Connected to the motion engine through its existing facade | Done — no Phase 1 changes |
| Backpressure explicit and atomic | Done — §4 |
| Loss, duplication, reordering, timeout handled per ADR-010 | Done — §4 |
| Feedback path | Done — 96-byte `STATUS` at 50 Hz |
| Host-side tests | Done — 302 checks |
| PC-side tool for board testing without Mach3 | Done — `Tools/c5p1.py` |
| Phase 1 tests still pass unchanged | Done — 1150 |
| Full firmware builds and links | Done — §5 |
| **A datagram exchanged with real hardware** | **NOT DONE — HV-30** |
| **Motion timing verified under protocol traffic** | **NOT DONE — HV-36** |
| **Mach3 compatibility** | **Not claimed. No plugin exists** |

Phase 3 is complete as a firmware deliverable: the protocol is implemented,
independently testable, and exercisable from a PC without Mach3, which was
the stated goal. It is **not** a demonstrated link to anything, and per
`Docs/MOTION-ENGINE.md` Rule 8 must not be reported as one.
