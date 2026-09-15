# CNC5AX-ETH — Firmware Implementation Plan

## 1. Purpose and status

This document is a planning pass, not an implementation. It answers, strictly from what the repository already establishes:

- what firmware modules must exist,
- what each one is required to do (with the exact document/ADR that requires it),
- which parts cannot be finished until the Mach3 UDP protocol is designed,
- which assumptions must be explicitly frozen before writing code (because leaving them implicit would mean different modules silently disagree),
- and a concrete implementation and verification order.

No protocol, port number, MAC scheme, or PC-side plugin behavior is invented here — every such item is listed as an open dependency, per `Docs/MACH3-INTERFACE.md` §7 and `Docs/ETHERNET.md`'s outstanding `[TBD]` items.

Module boundaries below follow the logical decomposition already fixed in `Docs/FIRMWARE-ARCHITECTURE.md` §5 and its suggested source tree (§43) — this plan does not introduce a new architecture, it schedules the one already documented.

---

## 2. Firmware modules and their requirements

| # | Module (path under `Firmware/`) | Requirement | Source |
|---|---|---|---|
| M1 | `System/State` | Central state model (e.g. `BOOT → INITIALIZING → READY → RUNNING → FAULT/EMERGENCY_STOP`); motion outputs must not enable before a known-safe state is reached | FIRMWARE-ARCHITECTURE §24, §31, §32 |
| M2 | `System/Fault` | Aggregates faults (E-stop, motion fault, Ethernet fault, invalid packet, buffer underflow, DMA/timer fault, init failure) into a safe response | FIRMWARE-ARCHITECTURE §23 |
| M3 | `Safety` (Input Manager) | Service `PE0`–`PE14` via EXTI (already configured, both edges, `GPIO_NOPULL` — board has external pull-ups); `PE2` (E-STOP) is latency-critical and hardware-isolated from the network | PINOUT.md, FIRMWARE-ARCHITECTURE §7–8, ADR-004 (NVIC: `EXTI2_IRQn` priority 0, other EXTI priority 1) |
| M4 | `Motion/StepGen` | Own the STEP BSRR double-buffer and the `TIM2`→`DMA1_Stream1`→`GPIOA->BSRR` transfer (already configured: PSC 0/ARR 20 = 4 MHz tick); refill on DMA half/full-transfer interrupt | ADR-001, ADR-002, ADR-004; MOTION-ENGINE §9–11 |
| M5 | `Motion/Interpolation` | Per-tick decision of which of the 5 axis bits (`PA8`–`PA12` = X,Y,Z,A,B) go into each BSRR word, for axes running at or below 2 MHz simultaneously | ADR-004 (fixes tick rate only, not the algorithm); MOTION-ENGINE §14–15, §27 |
| M6 | `Motion/Axis` | Per-axis state: position, direction, enable, target/commanded values | MOTION-ENGINE §23 (representation `[TBD]`) |
| M7 | `Motion/DIR` | Generate `PD8`–`PD12` (X–B) direction changes with ≥200 ns setup/hold around the relevant STEP edge, active-high | PINOUT.md; MOTION-ENGINE §6–7, §24 (method `[TBD]`) |
| M8 | `Motion/Planner` | Consume trajectory/command input, maintain the motion buffer, feed M5/M7 | MOTION-ENGINE §13, §16 (buffering strategy `[TBD]`); MACH3-INTERFACE.md §3–4 (shape of the input once the protocol exists) |
| M9 | `IO/Outputs` | Relay (`PB8`) and status LEDs (`PB2` run, `PB1` error) | PINOUT.md (relay polarity **not yet documented** — see §4) |
| M10 | `IO/Spindle` | Spindle PWM duty control over the already-configured `TIM3_CH1`/`PB4`, 10 kHz | PINOUT.md, ADR (TIM3 PSC 83/ARR 99 confirmed in `.ioc`) |
| M11 | `IO/DigitalInput` | Expose the non-E-STOP `PE0/1/3–14` states in a form the protocol layer / Mach3 signal table can consume | MACH3-INTERFACE.md §4 (`GetInputs()`/`Engine->InSigs[]` reference behavior) |
| M12 | `Communication/Ethernet` | Own PHY/link bring-up (LAN8742-compatible driver already wired), static IP (`192.168.5.10/24`, confirmed), `MX_LWIP_Process()` pump (already called from `main()`) | Docs/ETHERNET.md §2.2, §15–16 |
| M13 | `Communication/UDP` | Open/bind the motion UDP socket, send/receive datagrams | **Blocked** — needs port number (`Docs/MACH3-INTERFACE.md` §7) |
| M14 | `Communication/Protocol` | Parse/validate inbound packets into internal motion commands; encode outbound status/feedback | **Blocked** — needs packet format (`Docs/ETHERNET.md` §11, `Docs/MACH3-INTERFACE.md` §7) |
| M15 | `System/Diagnostics` | Low-overhead, disableable instrumentation (counters, state exposure via SWD) | FIRMWARE-ARCHITECTURE §29–30 — explicitly "not required" to be a full subsystem; grows incrementally alongside the others rather than being a discrete build phase |

`Core/`, `Drivers/`, `LWIP/App`+`LWIP/Target`, and `Middlewares/` already exist (CubeMX-generated) and are not new modules — M4/M9/M10/M12 are the thin application layers that sit on top of what's already initialized there.

---

## 3. Parts that depend on the still-open Mach3 UDP protocol

Everything else in this plan can be implemented and bench-tested without Mach3 or a PC. These cannot:

| Module | Why it's blocked | What must be decided first |
|---|---|---|
| M13 `Communication/UDP` | Can't bind/send without a port | UDP port number |
| M14 `Communication/Protocol` | Nothing to parse without a wire format | Packet header/opcode/sequence/CRC (`Docs/ETHERNET.md` §11); which of GMoves-style segments vs. `ncPod`-style time-sliced velocities (`Docs/MACH3-INTERFACE.md` §3–4) this project uses; feedback packet contents |
| M8 `Motion/Planner` (its *input side* only) | The buffer's producer is the protocol layer | Same as M14, plus the slice duration / block size this project adopts (the `ncPod` reference is evidence, at ~50 Hz / ~128 ms buffered — `Docs/MACH3-INTERFACE.md` §4 — not a decision) |
| M6 `Motion/Axis` (host-sync fields only) | Mach3 is natively 6-axis (X,Y,Z,A,B,**C**), this controller is 5 | Explicit axis-index mapping so C is never silently misrouted (`Docs/MACH3-INTERFACE.md` §3) |
| M11 `IO/DigitalInput` (feedback path only) | Bit layout of the input-state packet isn't defined | Feedback packet format |
| Mach3 PC-side plugin | Entirely separate deliverable | Everything above, plus a repository location for it (`Docs/MACH3-INTERFACE.md` §7 — none reserved yet) |

M4/M5/M7/M9/M10/M3/M12 have **no** dependency on the protocol — they can be fully implemented and verified against real hardware (a scope, a logic analyzer, a bench 24 V/5 V supply, and a plain `ping`) before the protocol exists at all. This is deliberate: it lets the highest-technical-risk part of the project (the 3-axis-at-2 MHz DMA/BSRR claim, MOTION-ENGINE §30) get real-hardware evidence long before Mach3 integration is even possible.

---

## 4. Assumptions that must be frozen before coding

These are not protocol questions — they're implementation choices the documents deliberately left open (per FIRMWARE-ARCHITECTURE Rule 6/ADR pattern) that nonetheless must be picked *once*, consistently, before the modules that depend on them can be written without contradicting each other.

1. **DIR generation method** (MOTION-ENGINE §24, explicitly `TBD`). Two real options given ADR-004's DMA layout: (a) CPU-timed `GPIOD` writes with a guard interval — one base tick (250 ns) already exceeds the 200 ns setup/hold requirement on its own, so this is simple and needs no extra hardware; or (b) a second, `TIM2`-synchronized DMA stream into `GPIOD->BSRR` (the freed `DMA1_Stream7`/`Channel3` slot noted in ADR-002/ADR-004) for full hardware-timed determinism. This choice changes M7's implementation shape entirely and must be picked before M4/M5/M7 are written together.
2. **Interpolation/DDA algorithm** (MOTION-ENGINE §14–15, §27). ADR-004 fixed *when* the tick fires (4 MHz), not *how* M5 decides which axis bits to set on a given tick for axes running below 2 MHz. A concrete algorithm (e.g. a per-axis Bresenham/DDA accumulator with a fixed-point rate register) must be chosen and its accumulator width fixed before M5/M8 are written.
3. **Motion buffer depth and unit** (MOTION-ENGINE §16, `TBD`). Needs a concrete number of ticks/segments now, even though the *final* tuning depends on the not-yet-designed protocol's packet rate; without a placeholder depth, M8 has no ring buffer to build. Pick a conservative depth consistent with the ~128 ms evidence in `Docs/MACH3-INTERFACE.md` §4, and revisit once the protocol is real.
4. **Position/step-count representation** (MOTION-ENGINE §23, `TBD`): width (32 vs 64-bit), units (raw step counts vs. engineering units), and whether commanded and generated position are tracked separately. M6 cannot be written without this.
5. **System/fault state model** (FIRMWARE-ARCHITECTURE §24, listed as examples only): the actual state names and transition table needed by M1/M2, since other modules (M9's error LED, M3's E-stop handling) need to reference specific states, not the illustrative list in the doc.
6. **Relay polarity** — genuinely undocumented. `Docs/PINOUT.md` defines `RELAY_PIN: PB8` but never states active-high/active-low, unlike every other output pin in that document (STEP, DIR, EN, spindle all have polarity stated). Must be confirmed against the relay driver circuit before M9 is written, or a relay could end up energized at boot.
7. **MAC address** — currently the CubeMX placeholder (`00:80:E1:00:00:00`) in `ethernetif.c`. Harmless on an isolated point-to-point link but should be replaced with a real, intentionally-chosen locally-administered address before M12 is considered final, independent of the UDP protocol.
8. **`PB0`/PHY reset hardening** (recorded as a recommendation in `Docs/ETHERNET.md` §2.2): decide now whether M12 will add the explicit ≥100 µs low-pulse before release, or defer it — either is acceptable, but it should be a decision, not an oversight, since it affects M12's `low_level_init()` shape.

None of items 1–8 requires the UDP protocol to be decided; all of them should be decided (even if only recorded as a short ADR-style note) before M4–M9 are coded, because M4/M5/M7 in particular are tightly coupled — changing DIR strategy after StepGen and Interpolation are written would mean reworking both.

---

## 5. Implementation and verification order

Ordered so that the highest-risk, protocol-independent subsystems get real-hardware evidence as early as possible, and so nothing is built on an assumption from §4 that hasn't been frozen yet.

**Phase 0 — System skeleton (M1, M2, M9 partial)**
Implement the state machine and fault aggregation with the states frozen in §4.5, wire the run/error LEDs to it. This is small enough to be the first thing that runs on real hardware, and satisfies the startup-safety requirement (FIRMWARE-ARCHITECTURE §32) that nothing else in this plan may violate.
*Verify:* power-on behavior on the real board — LED reflects state transitions, no unintended STEP/DIR/relay/spindle activity during boot (scope on a couple of representative pins is enough).

**Phase 1 — Safety / Input Manager (M3)**
EXTI callback wiring; `PE2` drives M2 directly and independently of everything else; the other 14 inputs just capture state for now (their Mach3-facing meaning is protocol-layer work, §3).
*Verify:* toggle each input by hand; measure E-STOP response latency with a logic analyzer — this must hold regardless of what Phase 3 is doing on the STEP outputs, which is the point of ADR-004's priority scheme (EXTI2 above the DMA refill interrupt).

**Phase 2 — Output Management (M9 complete, M10)**
Relay and spindle PWM duty control. Needs §4.6 (relay polarity) resolved first.
*Verify:* scope the spindle PWM output, confirm 10 kHz and duty response; confirm relay switches with the correct sense.

**Phase 3 — Motion Engine (M4, M5, M6, M7)** — the project's central technical risk, and independent of the protocol
Needs §4.1, §4.2, §4.4 frozen first. Build incrementally:
- 3a. Get the DMA/timer mechanism itself working with a fixed, hardcoded BSRR test pattern on one axis (no interpolation yet).
- 3b. Add the DIR module per the frozen method and verify its timing in isolation.
- 3c. Add the interpolation/DDA algorithm driven by an internal synthetic test command (not from the network), and exercise all five axes together.
*Verify:* this is where MOTION-ENGINE §29–30's acceptance criteria get their first real evidence — maximum STEP frequency, STEP pulse width/edge integrity, DIR setup/hold, and specifically **at least 3 axes simultaneously at 2 MHz**, all on a scope/logic analyzer, all before any Ethernet traffic exists. Per the project's own Rule 10 (FIRMWARE-ARCHITECTURE §42), none of this may be reported as compliant until measured here.

**Phase 4 — Ethernet/LwIP bring-up (M12)**, in parallel with Phase 3 if convenient
Needs §4.7/§4.8 (MAC, reset hardening) resolved. This validates the link itself, not the protocol — `ping` and ARP already exercise the full RMII/PHY/MAC/LwIP path without any project-specific packet format.
*Verify:* link LED and `LAN8742_GetLinkState()` agree with a physical cable pull; `ping 192.168.5.10` succeeds from the PC; and — this is the one cross-phase test that matters — flood the link with traffic while Phase 3's synthetic motion test is running, and confirm on the scope that STEP timing is unaffected (this is literally the project's stated central rule, FIRMWARE-ARCHITECTURE's "Deterministic motion generation has priority over networking," and it's testable now, without the protocol).

**Phase 5 — Protocol Layer, UDP, Motion Command Interface (M8 producer side, M11 feedback side, M13, M14)** — blocked
Cannot start until the items in §3 are decided. Once they are, M8's consumer side and M4–M7 need no rework — only their producer (M14 feeding M8) is new work.
*Verify:* loop-back / synthetic packet injection first (no PC needed — a test harness can send the agreed packet format over the same LAN), then real traffic from the finished Mach3 plugin.

**Phase 6 — Mach3 plugin (PC-side)** — not firmware, but gates Phase 5's real-world verification; needs its own repository location decided (`Docs/MACH3-INTERFACE.md` §7).

**Phase 7 — Full integration**
Real G-code in Mach3 → plugin → UDP → firmware → verified motion, with both E-stop paths (local hardware and host-reported) exercised, buffer-underflow and packet-loss behavior tested, and the 3-axis-2 MHz claim re-verified under realistic sustained Ethernet load — the full acceptance bar in MOTION-ENGINE §29–30.

Diagnostics (M15) is not a phase — add the minimum instrumentation useful for whichever phase is currently being bench-tested (e.g. a tick counter for Phase 3, a link-state counter for Phase 4), consistent with FIRMWARE-ARCHITECTURE §30's preference for low-overhead, removable diagnostics over a dedicated subsystem.
