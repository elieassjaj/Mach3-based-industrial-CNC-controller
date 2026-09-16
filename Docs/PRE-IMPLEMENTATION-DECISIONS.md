# CNC5AX-ETH — Pre-Implementation Decision List

This document is the scannable companion to the full ADRs it references. It exists to answer, for each of the eight items `Docs/FIRMWARE-IMPLEMENTATION-PLAN.md` §4 identified as needing a decision before coding starts (items 1–8 below), and for later hardening confirmations that came out of the Phase 1 STEP-DMA correction (items 10–11 below, out of ADR-012): what the documented requirement is, what was actually undecided, which firmware modules depend on it, what default (if any) is now adopted, and — where the repository genuinely does not contain enough information to decide — that it is marked for the project owner rather than guessed.

Full reasoning, alternatives considered, and risk notes for every resolved item live in `Docs/FIRMWARE-ARCHITECTURE.md` §41, ADR-006 through ADR-013. This document does not repeat that reasoning; it summarizes the outcome.

---

## 1. DIR generation and setup/hold handling

- **Documented requirement:** DIR active-high (`Docs/PINOUT.md`); ≥200 ns setup and ≥200 ns hold around the relevant STEP edge (`Docs/MOTION-ENGINE.md` §7).
- **What was undecided:** whether DIR needs its own DMA/timer-hardware path or can use CPU-timed writes (`Docs/MOTION-ENGINE.md` §24, explicitly `TBD`).
- **Depends on this:** M4 (StepGen), M5 (Interpolation, which must enforce the scheduling guard), M7 (DIR).
- **Resolved — ADR-006 (fully re-derived, then cycle-precisely verified):** CPU-timed `GPIOD` writes, using a precisely defined two-stage mechanism synchronized to `DMA1_Stream1`'s Half-Transfer/Transfer-Complete (`HTIF`/`TCIF`) interrupts — explicitly **not** the same thing as a host-protocol "motion segment," a distinction an earlier review required be made explicit. A reversal is *armed* at fill-time (one half-period before the target half plays) and the actual `GPIOD` write happens at *play-time*, exactly when that half begins real playback. Worst-case reversal latency is `N × 250 ns`, best case `(N/2) × 250 ns`, where `N` is the STEP-DMA buffer's total depth (ADR-008, still open) — a precise range as a function of buffer size, not a single "one refill period" figure. Both bounds remain 100–1,000× the 200 ns requirement. A leading guard of `g = 3` ticks is reserved at the start of the target half for setup margin — a follow-up cycle-precise verification built an itemized worst-case software-latency budget (`L ≈ 502 ns`, from Cortex-M4 exception entry, possible NVIC priority-preemption, Flash/ART instruction fetch, and the `GPIOD` write itself — sourced from RM0090/PM0214 where the repo documents it, with two line items explicitly flagged as external/unconfirmed pending Section 39) and proved `g = 2` already sufficient (`248 ns` margin) and `g = 3` (adopted) safely conservative (`498 ns` margin); hold margin is satisfied unconditionally (a structural `≥250 ns` floor independent of software latency). No architecture change resulted — the mechanism's margin is confirmed real, not merely assumed. A DMA-hardware path (the `DMA1_Stream7`/`Channel3` slot ADR-005 freed) remains the documented fallback if a motion profile ever needs faster reversals than this range allows.
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
- **Decided by the project owner:**
  1. **Fault recovery policy — confirmed: always explicit, never automatic.** No condition (including communication resuming after a timeout) clears a `FAULT` or `EMERGENCY_STOP` on its own; every recovery requires an explicit clear/reset action.
  2. **Comm-timeout / buffer-underflow drive behavior — confirmed: hold position, keep drives enabled.** `EN` stays asserted; only new STEP output halts. This is the **one exception** to the general rule that `FAULT`/`EMERGENCY_STOP` deassert `EN` — chosen specifically so a paused network link or an emptied buffer never de-energizes a stepper (or step/dir servo) and lets an axis drift or drop under load. Hardware-integrity faults (DMA/timer fault, corrupt packet stream, init failure) still fully disable drives, since those indicate the system cannot trust its own step generation, unlike a merely-paused command stream.
- **Needs your decision:** no — both resolved. See ADR-010 for the full, updated behavior table.

---

## 6. Relay output polarity

- **Documented requirement:** none. `Docs/PINOUT.md` defines `RELAY_PIN: PB8` with no stated polarity — the only output pin in that document without one (STEP, DIR, `EN`, and spindle all state active-high/active-low explicitly).
- **What is undecided:** whether driving `PB8` HIGH energizes or de-energizes the relay.
- **Depends on this:** M9 (Outputs) directly; indirectly M1 (safe boot-time output level) and M2 (safe fault-time output level).
- **Resolved by the project owner, verified against hardware: ACTIVE HIGH** (`HIGH` = energized, `LOW` = de-energized). No default was proposed for this item — it was a hardware fact requiring verification, not a design choice, and no schematic for the relay driver stage exists in this repository (`LAN8720A/LAN8720-ETH-Board-Schematic.pdf` covers only the Ethernet PHY module). `Docs/PINOUT.md` is updated accordingly; `PB8` must default `LOW` at boot and in any `FAULT`/`EMERGENCY_STOP` state.
- **Needs your decision:** no — resolved.

---

## 7. MAC-address strategy

- **Documented requirement:** none beyond "a MAC address is needed" (`Docs/ETHERNET.md` §15).
- **What was undecided:** what address to use, and whether that answer differs between bench testing and a shipped product.
- **Depends on this:** M12 (Ethernet).
- **Resolved for bench testing — ADR-011:** a locally-administered unicast address (U/L bit set), e.g. `02:00:05:10:00:01` — chosen only as a memorable convention echoing the static IP (`192.168.5.10`), with no other significance. This replaces the current CubeMX placeholder `00:80:E1:00:00:00`, which is a real vendor's OUI prefix and must not be used even for bench testing on an isolated link.
- **Resolved for production — ADR-011, confirmed by the project owner:** per-unit, derived from the STM32F407's factory-programmed 96-bit unique device ID. Every unit gets a distinct, automatically-generated, locally-administered address — no purchase, no per-unit provisioning step, no collision risk across units. The exact byte-derivation scheme is an M12 implementation detail, not fixed here.
- **Needs your decision:** no — both halves resolved.

---

## 8. `PB0`/PHY reset pulse hardening

- **Documented requirement:** none beyond LAN8720A datasheet power-on timing (`Docs/ETHERNET.md` §2.2, `tpurstd`/`trstia`); the driver requires `nRST` released and the PHY given its reset-release time before MDIO access is attempted.
- **What was undecided (`Docs/FIRMWARE-IMPLEMENTATION-PLAN.md` §4, item 8):** whether to add an explicit, firmware-driven low pulse on `PB0` (`PHY_NRST`, active-low) before releasing it, since the board has no RC delay or reset supervisor on `nRST` (a plain pull-up per the schematic) and `PB0` is currently only ever driven HIGH (`MX_GPIO_Init()`), relying entirely on the LAN8720A's own internal power-on reset plus `LAN8742_Init()`'s MDIO soft-reset.
- **Depends on this:** M12 (Ethernet) — specifically `low_level_init()` in `Firmware/LWIP/Target/ethernetif.c`, which must run its reset sequence before any MDIO read.
- **Resolved by the project owner — confirmed: explicit low pulse, ≥150 µs.** `PB0` must be driven LOW for **at least 150 µs** before being released HIGH, at the start of the PHY reset sequence (i.e., before `MX_LWIP_Init()`/`low_level_init()` performs any MDIO access). This exceeds the LAN8720A's general `trstia` reset-assertion minimum (~100 µs) with deliberate margin, and removes the dependency on the PHY's own internal power-on reset for deterministic behavior on every reset path (cold boot and warm/software reset alike), not only cold power-up. See ADR-013 (`Docs/FIRMWARE-ARCHITECTURE.md` §41) for the full reasoning and the open firmware-implementation note (a microsecond-precision delay is needed — `HAL_Delay()`'s 1 ms `SysTick` resolution is too coarse — and the reset pulse must run before `low_level_init()`'s MDIO access, which is earlier in the boot sequence than where this project's existing `DWT->CYCCNT` cycle-count utility, in `Firmware/Platform/STM32F407/Src/stepgen_port_stm32f4.c`, is currently enabled).
- **Needs your decision:** no — value confirmed. Firmware implementation of the pulse itself is still open (tracked as a to-do against M12, not a design question).

---

## 9. UDP protocol dependencies

No packet fields, port number, or PC-side plugin behavior are decided or invented here — this section only names what depends on the protocol, per `Docs/FIRMWARE-IMPLEMENTATION-PLAN.md` §3 and `Docs/MACH3-INTERFACE.md` §7:

- **UDP port number** — needed by M13 before it can bind a socket.
- **Packet format** (header, opcode, sequence, payload, CRC/checksum) — needed by M14 to parse/encode anything. `Docs/ETHERNET.md` §11's illustrative structure is explicitly `[EXAMPLE]`, not a specification.
- **Motion command wire encoding** — now constrained by item 2 above to be step-domain, time-sliced, 5-axis data (not raw `GMoves`), but the exact field layout, slice duration, and block size are not decided — the `ncPod` reference's ~50 Hz / 32-records-per-block is evidence for a starting point, not a requirement.
- **Feedback packet format** (device→host: position, input states, buffer occupancy, fault flags) — needed by M11's feedback path and by the host-side E-stop/limit reaction described in `Docs/MACH3-INTERFACE.md` §6.
- **Explicit backpressure mechanism** for item 3's overflow behavior — needs a field in whichever direction the protocol carries device status.
- **Communication-timeout threshold** — needed to make item 5's `COMM_TIMEOUT` fault concrete (how long without a valid packet before it fires); depends on the protocol's own expected send rate, which doesn't exist yet.
- **PC-side Mach3 plugin behavior**, and its repository location (`Docs/MACH3-INTERFACE.md` §7 — none reserved yet) — a separate deliverable, co-designed with the protocol, not firmware.

None of these block M1–M12 (everything not listed in `Docs/FIRMWARE-IMPLEMENTATION-PLAN.md` §3 as protocol-dependent) from being implemented and bench-verified first, per that document's §5 phased order.

---

## 10. SRAM placement of Ethernet/`lwIP` buffers

- **Documented requirement:** "Ethernet processing... cannot introduce unacceptable timing variation into STEP generation" (`Docs/MOTION-ENGINE.md` §8); ADR-012's STEP-DMA correction (item 1 above) put the STEP-DMA ring in SRAM2 specifically so it never shares a bus-matrix slave port with Ethernet traffic.
- **What was implicit but not stated as a binding rule:** that the *other* side of that isolation — every Ethernet MAC DMA descriptor/buffer and the entire `lwIP` buffer pool (zero-copy RX pbuf pool, TX descriptors, `lwIP`'s own internal heap/pools) — must stay in SRAM1, and specifically must never end up in CCM RAM.
- **Depends on this:** M12 (Ethernet), and indirectly M4 (StepGen), since a violation here would reintroduce the exact bus-port contention ADR-012's SRAM2 split was designed to remove — or worse.
- **Resolved — confirmed as a binding rule, elevated into ADR-012 (`Docs/FIRMWARE-ARCHITECTURE.md` §41):** Ethernet MAC DMA descriptors (`DMARxDscrTab`/`DMATxDscrTab`) and buffers, and every `lwIP` buffer/pool, **must reside in SRAM1 only** — never SRAM2 (reserved exclusively for the STEP-DMA ring, `Platform/STM32F407/linker/stepgen_sram2.ld`) and never CCM RAM. The CCM restriction is the stronger of the two reasons: RM0090 states plainly that the 64 KB CCM data RAM "is not part of the bus matrix and can be accessed only through the CPU" — no DMA master on this part, **including the Ethernet MAC's own dedicated DMA engine**, can reach CCM at all, so placing any Ethernet/`lwIP` buffer there would not be a performance risk but an outright non-functional configuration. **Verified against the current Phase 1 firmware:** `DMARxDscrTab`, `DMATxDscrTab`, and the `lwIP` `RX_POOL` memory pool (`Firmware/LWIP/Target/ethernetif.c`) are all plain static/global declarations with no section attribute, so they land in the linker's default `RAM` region — which `Firmware/STM32F407VGTX_FLASH.ld` defines as exactly SRAM1 (`ORIGIN = 0x20000000, LENGTH = 112K`), with SRAM2 and CCMRAM declared as the two separate, explicitly-carved-out regions the STEP ring and (currently unused) CCM-RAM section respectively target. No Ethernet/`lwIP` code touches `.ccmram` or `.stepgen_ram` anywhere in the repository today — the rule is already satisfied by construction, not merely by intent.
- **Needs your decision:** no — confirmed and already satisfied. Recommended (not yet implemented) hardening: a runtime self-test symmetric to the existing HV-03 (which confirms the STEP ring lands in SRAM2) that asserts `DMARxDscrTab`/`DMATxDscrTab`/`RX_POOL` addresses fall inside the SRAM1 range, so a future regression (e.g. a well-meaning but wrong `__attribute__((section(".ccmram")))` on an Ethernet buffer) fails a test instead of failing silently on hardware.

---

## 11. Ethernet interrupt priority relative to the STEP-DMA interrupt

- **Documented requirement:** Ethernet/networking activity must not affect STEP timing (`Docs/MOTION-ENGINE.md` §8); ADR-004's original NVIC ordering placed the STEP-DMA refill/DIR-write interrupt above Ethernet specifically for this reason.
- **What needed re-confirming:** ADR-012 moved the STEP-DMA stream from `DMA1_Stream1` to `DMA2_Stream1` (item 1 above) — the isolation requirement is only actually enforced if the *new* interrupt, `DMA2_Stream1_IRQn`, still sits at strictly higher NVIC priority (lower preempt-priority number) than the Ethernet interrupts, not the old, no-longer-used `DMA1_Stream1_IRQn`.
- **Depends on this:** M4 (StepGen)/M7 (DIR), whose refill-and-DIR-write ISR runs on `DMA2_Stream1_IRQn`; M12 (Ethernet), whose `ETH_IRQn`/`ETH_WKUP_IRQn` must never be able to preempt it.
- **Resolved — confirmed as a binding rule, elevated into ADR-012 (`Docs/FIRMWARE-ARCHITECTURE.md` §41): `ETH_IRQn` and `ETH_WKUP_IRQn` must always be configured at a strictly lower NVIC preempt-priority (a numerically higher priority value) than `DMA2_Stream1_IRQn`.** **Verified against the current Phase 1 firmware:** `Firmware/Core/Src/dma.c` sets `HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 2, 0)`, and `Firmware/CNC5AX-ETH.ioc` configures `NVIC.ETH_IRQn` and `NVIC.ETH_WKUP_IRQn` both at preempt-priority `5` — `5 > 2`, so the requirement already holds in the current configuration. This preserves the full ordering carried over from ADR-004 through ADR-012: E-STOP (`EXTI2`, priority 0) above the other digital inputs (`EXTI0/1/3/4/9_5/15_10`, priority 1) above STEP-DMA (`DMA2_Stream1`, priority 2) above Ethernet (`ETH_IRQn`/`ETH_WKUP_IRQn`, priority 5).
- **Needs your decision:** no — confirmed and already satisfied. This is now a binding invariant, not an incidental CubeMX default: any future change to either interrupt's priority (e.g. CubeMX regeneration resetting a value) must preserve `priority(ETH_IRQn), priority(ETH_WKUP_IRQn) > priority(DMA2_Stream1_IRQn)`, and should be checked against this item before being accepted.
