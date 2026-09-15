# CNC5AX-ETH — Pre-Implementation Decision List

This document is the scannable companion to the full ADRs it references. It exists to answer, for each of the eight items `Docs/FIRMWARE-IMPLEMENTATION-PLAN.md` §4 identified as needing a decision before coding starts: what the documented requirement is, what was actually undecided, which firmware modules depend on it, what default (if any) is now adopted, and — where the repository genuinely does not contain enough information to decide — that it is marked for the project owner rather than guessed.

Full reasoning, alternatives considered, and risk notes for every resolved item live in `Docs/FIRMWARE-ARCHITECTURE.md` §41, ADR-006 through ADR-011. This document does not repeat that reasoning; it summarizes the outcome.

---

## 1. DIR generation and setup/hold handling

- **Documented requirement:** DIR active-high (`Docs/PINOUT.md`); ≥200 ns setup and ≥200 ns hold around the relevant STEP edge (`Docs/MOTION-ENGINE.md` §7).
- **What was undecided:** whether DIR needs its own DMA/timer-hardware path or can use CPU-timed writes (`Docs/MOTION-ENGINE.md` §24, explicitly `TBD`).
- **Depends on this:** M4 (StepGen), M5 (Interpolation, which must enforce the scheduling guard), M7 (DIR).
- **Resolved — ADR-006:** CPU-timed `GPIOD` writes, with the Motion Engine required to schedule at least one base tick (250 ns) between a direction change and the next/previous STEP edge for that axis. One tick already exceeds the 200 ns requirement. A DMA-hardware path (the DMA1_Stream7/Channel3 slot ADR-005 freed) remains the documented fallback if hardware measurement later shows this insufficient.
- **Needs your decision:** no.

---

## 2. Interpolation/DDA algorithm and how Mach3 `GMoves` maps into it

- **Documented requirement:** the motion engine must interpolate multi-axis motion (`Docs/MOTION-ENGINE.md` §14–15); Mach3 supplies planned trajectory segments (`GMoves`, 6-axis, engineering units, `Docs/MACH3-INTERFACE.md` §3) via a plugin, not raw steps.
- **What was undecided:** the actual interpolation algorithm (`Docs/MOTION-ENGINE.md` §27, `TBD`), and — critically — *where* `GMoves`'s engineering-unit, 6-axis, arc/cubic data gets converted into whatever the firmware actually consumes.
- **Depends on this:** M5 (Interpolation), M8 (Planner), and indirectly the entire UDP protocol design (M13/M14), since it fixes what domain the wire format needs to carry.
- **Resolved — ADR-007:** per-axis DDA/Bresenham accumulator running at the 4 MHz base tick, operating entirely in **step-domain** (step counts / step rates). All mm↔step conversion, all `GMoves`→time-sliced-command conversion, and the 6-axis→5-axis mapping happen on the **PC-side Mach3 plugin**, before anything crosses the network. This is not a new design — it is the same technique the SDK's own `SDK/ncPod/` reference device uses (`Docs/MACH3-INTERFACE.md` §4): a per-axis signed velocity per fixed time slice, with a fractional accumulator carried between slices, computed host-side from `GMoves.ex/ey/ez...`.
- **Needs your decision:** no — but note this fixes a hard boundary: the UDP protocol (item 8) must carry step-domain, 5-axis, time-sliced data, not raw `GMoves`. That constraint is now locked in regardless of the protocol's other details.

---

## 3. Motion buffer depth and underflow/overflow behavior

- **Documented requirement:** a motion buffer of some kind is required (`Docs/MOTION-ENGINE.md` §16); "Motion Buffer Underflow" is already a named fault category (`Docs/FIRMWARE-ARCHITECTURE.md` §23).
- **What was undecided:** buffering strategy, depth, and what happens on underflow/overflow — all explicitly `TBD`.
- **Depends on this:** M8 (Planner), M4 (StepGen's own separate refill buffer), M2 (Fault).
- **Resolved — ADR-008:** two independent buffers. (a) A small, fixed-size STEP-DMA refill buffer inside M4 — an internal parameter, not sized here, tuned after real interrupt-latency measurement. (b) A statically-allocated motion command buffer in M8, **targeting ≥128 ms of buffered motion** — a number taken directly from the SDK's `ncPod` reference device evidence (`Docs/MACH3-INTERFACE.md` §4), not invented, and stated as a time target rather than a byte size because the exact command encoding is protocol-dependent. Underflow → the interpolator holds at zero commanded rate and M8 raises a `FAULT` (not `EMERGENCY_STOP`) via ADR-010's state model. Overflow → the firmware must never silently drop or overwrite an unconsumed command; it must backpressure (the exact wire mechanism is a protocol dependency, item 8).
- **Needs your decision:** no, for the principles above. The exact final buffer depth in bytes is correctly left open pending the protocol (this is not something left undecided by oversight — `Docs/MOTION-ENGINE.md` §16 itself says the final depth depends on packet rate and latency, which don't exist yet).

---

## 4. Internal position representation and units

- **Documented requirement:** "sufficiently wide integer representations... to prevent overflow during long machine operation" (`Docs/MOTION-ENGINE.md` §23), width/units/mechanism otherwise `TBD`.
- **What was undecided:** integer width, and whether position is tracked in steps or engineering units.
- **Depends on this:** M6 (Axis state), M8 (Planner), the feedback half of M14 (Protocol) once it exists.
- **Resolved — ADR-009:** `int64_t`, raw step counts. 64-bit removes the overflow question outright (>100,000 years of continuous full-speed stepping at the fixed 2 MHz ceiling before wraparound); step-count units follow directly from item 2's step-domain decision. This also matches the SDK's own `GMoves.DDA1/DDA2/DDA3` fields, which are `__int64` in the Mach3 SDK itself.
- **Needs your decision:** no.

---

## 5. System state/fault model — E-stop, limits, communication timeout, recovery

- **Documented requirement:** a centralized state model "may be used" with example (non-mandatory) states (`Docs/FIRMWARE-ARCHITECTURE.md` §24); E-STOP must be serviced independently of the network (§8); "Motion-affecting fault → controlled motion response," "Safety-critical fault → immediate safety response" as categories, not a concrete model (§33); communication-failure behavior options listed but not chosen (`Docs/MOTION-ENGINE.md` §22).
- **What was undecided:** the actual state names/transitions, and specifically what recovers automatically versus what requires an explicit action.
- **Depends on this:** M1 (State), M2 (Fault), M3 (Safety/E-stop), M9 (error LED), effectively every module that can fail.
- **Resolved (structure) — ADR-010:**
  ```
  BOOT → INIT → SAFE_IDLE → READY ⇄ RUNNING
  any state → FAULT (recoverable: comm timeout, persistent underflow, invalid packet stream, non-fatal peripheral fault)
  any state → EMERGENCY_STOP (PE2 EXTI, latching — kept structurally distinct from FAULT so a generic fault-clear can never clear an E-stop)
  ```
  Entering `FAULT` or `EMERGENCY_STOP` deasserts `EN` immediately and halts the STEP-DMA. Communication timeout is a `FAULT`, not an `EMERGENCY_STOP` — the local E-stop path must stay independent of the network (§8), so a dead link is not itself a safety event; it is handled identically to a buffer underflow (item 3). The 14 non-E-STOP inputs are reported raw by firmware; assigning "this input means stop" semantics is host/Mach3-signal-table work (`Docs/MACH3-INTERFACE.md` §4), not firmware policy, unless a future revision of this repository decides otherwise.
- **Needs your decision — two specific, genuine policy calls, not derivable from the repository:**
  1. **Fault recovery policy.** ADR-010 defaults to "no automatic recovery — `FAULT` and `EMERGENCY_STOP` require an explicit clear/reset action, ever," as the conservative choice. Should any condition (e.g. communication resuming after a timeout) be allowed to auto-clear instead of requiring the operator/host to explicitly acknowledge it?
  2. **Comm-timeout drive behavior.** ADR-010 deasserts `EN` (fully disables drives) on any `FAULT`, including comm timeout, rather than merely halting new STEP output while holding position with drives still enabled. Nothing in the repository documents whether the connected drives are open-loop steppers or step/dir-command servos (no ADC/DAC/encoder pins are used anywhere per `README.md`'s Hardware Platform table, which points toward open-loop steppers, but this is an inference, not a documented fact) — please confirm the drive type and whether "hold energized, stop stepping" vs. "fully disable" is the intended behavior on comm loss.

---

## 6. Relay output polarity

- **Documented requirement:** none. `Docs/PINOUT.md` defines `RELAY_PIN: PB8` with no stated polarity — the only output pin in that document without one (STEP, DIR, `EN`, and spindle all state active-high/active-low explicitly).
- **What is undecided:** whether driving `PB8` HIGH energizes or de-energizes the relay.
- **Depends on this:** M9 (Outputs) directly; indirectly M1 (safe boot-time output level) and M2 (safe fault-time output level).
- **No default proposed.** This is a hardware fact, not a design choice, and no schematic for the relay driver stage exists in this repository — `LAN8720A/LAN8720-ETH-Board-Schematic.pdf` covers only the Ethernet PHY module. Guessing risks the relay being energized exactly when firmware intends it off, including at boot and during a fault.
- **Needs your decision: yes.** Please check the actual relay driver circuit (or the physical board) and report which level of `PB8` energizes the relay. `Docs/PINOUT.md` now carries an explicit `[TBD — HARDWARE VERIFICATION REQUIRED]` flag on this pin so it cannot be mistaken for an oversight.

---

## 7. MAC-address strategy

- **Documented requirement:** none beyond "a MAC address is needed" (`Docs/ETHERNET.md` §15).
- **What was undecided:** what address to use, and whether that answer differs between bench testing and a shipped product.
- **Depends on this:** M12 (Ethernet).
- **Resolved for bench testing — ADR-011:** a locally-administered unicast address (U/L bit set), e.g. `02:00:05:10:00:01` — chosen only as a memorable convention echoing the static IP (`192.168.5.10`), with no other significance. This replaces the current CubeMX placeholder `00:80:E1:00:00:00`, which is a real vendor's OUI prefix and must not be used even for bench testing on an isolated link.
- **Needs your decision — production strategy:** a single hardcoded address (fine for one unit, a collision risk if this project is ever built in quantity and multiple units share a network), an address derived per-unit from the STM32F407's factory-programmed 96-bit unique ID (each unit distinct, no purchase required, but still locally-administered rather than a "real" registered address), or a purchased IEEE OUI block (real registered address, has a cost, only worth it at production volume). This is a product/business decision this repository cannot resolve on its own.

---

## 8. UDP protocol dependencies

No packet fields, port number, or PC-side plugin behavior are decided or invented here — this section only names what depends on the protocol, per `Docs/FIRMWARE-IMPLEMENTATION-PLAN.md` §3 and `Docs/MACH3-INTERFACE.md` §7:

- **UDP port number** — needed by M13 before it can bind a socket.
- **Packet format** (header, opcode, sequence, payload, CRC/checksum) — needed by M14 to parse/encode anything. `Docs/ETHERNET.md` §11's illustrative structure is explicitly `[EXAMPLE]`, not a specification.
- **Motion command wire encoding** — now constrained by item 2 above to be step-domain, time-sliced, 5-axis data (not raw `GMoves`), but the exact field layout, slice duration, and block size are not decided — the `ncPod` reference's ~50 Hz / 32-records-per-block is evidence for a starting point, not a requirement.
- **Feedback packet format** (device→host: position, input states, buffer occupancy, fault flags) — needed by M11's feedback path and by the host-side E-stop/limit reaction described in `Docs/MACH3-INTERFACE.md` §6.
- **Explicit backpressure mechanism** for item 3's overflow behavior — needs a field in whichever direction the protocol carries device status.
- **Communication-timeout threshold** — needed to make item 5's `COMM_TIMEOUT` fault concrete (how long without a valid packet before it fires); depends on the protocol's own expected send rate, which doesn't exist yet.
- **PC-side Mach3 plugin behavior**, and its repository location (`Docs/MACH3-INTERFACE.md` §7 — none reserved yet) — a separate deliverable, co-designed with the protocol, not firmware.

None of these block M1–M12 (everything not listed in `Docs/FIRMWARE-IMPLEMENTATION-PLAN.md` §3 as protocol-dependent) from being implemented and bench-verified first, per that document's §5 phased order.
