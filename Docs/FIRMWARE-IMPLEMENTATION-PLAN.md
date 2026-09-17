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
| M13 | `Communication/UDP` | Open/bind the motion UDP socket, send/receive datagrams | **Implemented (Phase 3)** — `Platform/STM32F407/Src/net_udp_stm32f4.c`, UDP 55010 (ADR-014) |
| M14 | `Communication/Protocol` | Parse/validate inbound packets into internal motion commands; encode outbound status/feedback | **Implemented (Phase 3)** — `Net/Src/cnc_protocol.c` + `Net/Src/cnc_session.c`; format in `Docs/PROTOCOL.md` |
| M15 | `System/Diagnostics` | Low-overhead, disableable instrumentation (counters, state exposure via SWD) | FIRMWARE-ARCHITECTURE §29–30 — explicitly "not required" to be a full subsystem; grows incrementally alongside the others rather than being a discrete build phase |

`Core/`, `Drivers/`, `LWIP/App`+`LWIP/Target`, and `Middlewares/` already exist (CubeMX-generated) and are not new modules — M4/M9/M10/M12 are the thin application layers that sit on top of what's already initialized there.

---

## 3. Parts that depend on the still-open Mach3 UDP protocol

> **Phase 3 closed this section's central dependency.** The protocol is specified in `Docs/PROTOCOL.md` and implemented: port number, packet format, motion encoding, feedback format, backpressure mechanism and comm-timeout threshold are all decided (ADR-014). M13 and M14 are built and host-tested. What remains open below is what depends on *other* missing modules — M3 for inputs, M9/M10 for outputs — and the plugin itself, not on the protocol.

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

**Status: all 8 items are now resolved** (ADR-006 through ADR-013 in `Docs/FIRMWARE-ARCHITECTURE.md` §41 — see `Docs/PRE-IMPLEMENTATION-DECISIONS.md` for the full reasoning behind each, including which parts came from repository evidence versus an explicit project-owner decision). Item 8's numeric value is confirmed; its firmware implementation (the actual pulse) is still a to-do against M12, tracked separately, not a design question.

1. ~~DIR generation method~~ — **Resolved, ADR-006**: CPU-timed `GPIOD` writes with a one-tick (250 ns) guard interval; the DMA-hardware alternative remains the documented fallback.
2. ~~Interpolation/DDA algorithm~~ — **Resolved, ADR-007**: per-axis DDA/Bresenham accumulator at the 4 MHz tick, operating entirely in step-domain; all engineering-unit conversion happens on the PC-side plugin, matching the SDK's own `ncPod` reference behavior.
3. ~~Motion buffer depth and unit~~ — **Resolved (target, not final bytes), ADR-008**: two independent buffers (STEP-DMA refill buffer, size TBD pending hardware measurement; motion command buffer targeting ≥128 ms per the `ncPod` evidence), statically allocated; underflow → controlled halt + `FAULT`, overflow → explicit backpressure, never silent overwrite.
4. ~~Position/step-count representation~~ — **Resolved, ADR-009**: `int64_t`, raw step counts, matching the SDK's own `GMoves.DDA1/2/3` field width.
5. ~~System/fault state model~~ — **Fully resolved, ADR-010**: `BOOT → INIT → SAFE_IDLE → READY ⇄ RUNNING`, `FAULT`/`EMERGENCY_STOP` distinct and always requiring explicit clear (confirmed, never auto-clear). One refinement from the project owner: `COMM_TIMEOUT`/buffer-underflow `FAULT`s hold position with `EN` still asserted rather than disabling drives — the one exception to the general "faults disable drives" rule, chosen to avoid de-energizing a stepper/servo under load merely because the link paused.
6. ~~Relay polarity~~ — **Resolved, verified against hardware by the project owner: ACTIVE HIGH.** `Docs/PINOUT.md` updated accordingly.
7. ~~MAC address~~ — **Fully resolved, ADR-011**: locally-administered address for bench testing; per-unit, STM32-unique-ID-derived address for production (confirmed by the project owner — no purchase, no collision risk).
8. ~~`PB0`/PHY reset hardening~~ — **Resolved, ADR-013**: the project owner confirmed an explicit ≥150 µs low-pulse on `PB0` before release, at the start of `low_level_init()`. The value is decided; the firmware change itself (a microsecond-precision delay, since `HAL_Delay()`'s 1 ms resolution is too coarse) is still a to-do against M12.

Items 1–8 no longer block M4–M9/M12 from being coded.

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
