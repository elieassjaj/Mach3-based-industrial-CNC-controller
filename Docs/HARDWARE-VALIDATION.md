# CNC5AX-ETH Hardware Validation Plan — Motion Engine (Phase 1)

## 1. Why this document exists

`Docs/MOTION-ENGINE.md` Rule 8 and §30, and `Docs/FIRMWARE-ARCHITECTURE.md`
Rule 10, forbid claiming compliance with the 2 MHz / 3-axis requirement
from code inspection, simulation or theoretical peripheral capability.

This is the procedure that turns those requirements into measurements.
Until a test here has been executed on the real board and its result
written into the table in §6, the corresponding claim **is not made**.

| Kind | Where it runs | What it proves |
|---|---|---|
| **HV-0x** | In firmware, on target | Hardware assumptions the source depends on |
| **HV-1x** | Scope / logic analyser | The externally observable STEP/DIR waveform |
| **HV-2x** | In firmware, on target | Ethernet/PHY configuration (Phase 2) |
| **HV-3x** | Wire + scope | The C5P1 protocol end to end (Phase 3) |
| **HV-4x** | In firmware, on target | Digital-input and E-STOP configuration (Phase 4) |
| **HV-5x** | In firmware + meter/scope | Relay, LEDs and spindle PWM (Phase 5) |

HV-0x is implemented in
`Firmware/Platform/STM32F407/Src/stepgen_selftest_stm32f4.c`
(`stepgen_selftest_run_all()`). The HV-1x stimulus patterns
(`stepgen_stim_*`) are in the same file.

---

## 2. Equipment

| Item | Minimum | Why |
|---|---|---|
| Oscilloscope | ≥ 200 MHz, ≥ 1 GSa/s, 2 ch | A 250 ns pulse needs ≳ 5 samples on each edge |
| Logic analyser | ≥ 100 MSa/s, 8 ch | 10 ns resolution; enough for ±1 tick on all 10 STEP/DIR lines |
| Probes | 10:1, short ground spring | A ground clip lead fabricates ringing on a 250 ns edge |
| Ethernet load generator | PC with `iperf3` or similar | HV-15 needs line-rate traffic at the controller |
| SWD probe | ST-LINK or similar | Reading self-test results and DWT counters |

**Probing note.** Probe at the driver-side end of the STEP/DIR track, not
at the MCU pin, and always with a short ground spring. At 2 MHz with
250 ns pulses a standard ground lead adds enough inductance to produce
overshoot that is an artefact of the probe, not of the controller.

---

## 3. Signals under test

From `Docs/PINOUT.md` (post-ADR-005):

| Signal | Pin | Note |
|---|---|---|
| STEP_X..STEP_B | PA8, PA9, PA10, PA11, PA12 | One GPIO port, one DMA stream, one BSRR word per tick |
| DIR_X..DIR_B | PD8..PD12 | CPU-timed writes (ADR-006) |
| EN | PD15 | **Active HIGH**; HIGH = drivers enabled |

Because all five STEP pins are in one 32-bit word, a multi-axis tick is a
single bus cycle — there is no cross-port skew to measure, which is one of
ADR-005's stated benefits and is worth confirming rather than assuming
(HV-11).

**Before powering a machine:** `PD15` is active high, so LOW is safe. The
firmware drives it LOW before configuring it as an output, and again on
every integrity fault and emergency stop. Confirm the LOW-at-reset
behaviour on the bench with the drives disconnected before trusting it.

---

## 4. Firmware self-tests (HV-0x)

Run `stepgen_selftest_run_all()` after `stepgen_init()`, with the drives
disconnected or unpowered. Read the results array over SWD.

### HV-00 — The STEP DMA path actually works (**load-bearing**)

This test settles ADR-012 empirically.

ADR-002/004/005 route `TIM2_UP → DMA1_Stream1 → GPIOA->BSRR`, and the
`.ioc` is still configured that way. Read against RM0090 Rev 22 that path
cannot work at all: §2.1 does not list the DMA1 *peripheral* bus among the
bus-matrix masters, Figure 33's note says the same, GPIOA is an AHB1
peripheral reachable only through the matrix, and §10.3.16/§10.3.17 put
`DMA_SxPAR` on the peripheral port. The Phase 1 firmware therefore uses
`TIM8_UP → DMA2_Stream1 Ch7` (RM0090 Table 44).

*Method.* Start the timebase, spin a known number of CPU cycles, confirm
the stream's `NDTR` fell by the expected number of transfers.

*Pass.* Advanced by the expected count ±5%, with zero DMA errors.

*Diagnostic value.* The same test built against DMA1_Stream1 should
**fail** — no transfers at all. If it unexpectedly passes on DMA1, the
reading above is wrong and ADR-012 should be withdrawn. Running both is
the cheapest way to close the question for good.

### HV-01 — BSRR set bits take priority over reset bits

Every tick writes `set_bits | (all_step_pins << 16)`. If `BR` won over
`BS`, no STEP pulse would ever appear.

*Method.* Timebase stopped: write `mask | (mask << 16)`, read back `ODR`.
*Pass.* `ODR & mask == mask`, then `0` after a reset-only write.

### HV-02 — Base tick is exactly 4 MHz

*Pass.* `TIM8_CLK / (ARR+1) == 4 000 000` with no rounding. A fractional
divider would put a systematic error on every commanded feed rate.

### HV-03 — Ring buffer is in SRAM2

*Pass.* Base address within `0x2001C000 .. 0x2001FFFF`.
*If it fails,* the linker fragment
(`Firmware/Platform/STM32F407/linker/stepgen_sram2.ld`) was not applied
and the Ethernet/motion bus isolation does not exist.

### HV-04 — Worst-case refill cost (**the CPU-load figure**)

The host benchmark (`make bench`) is an x86 algorithm proxy and proves
nothing about the Cortex-M4. This is the only legitimate source.

*Method.* Run HV-12 (5 axes at 2 MHz) for ≥ 60 s, read
`stepgen_status_t.fill_cycles_max` (DWT-timed around each refill).

*Deadline.* One ring half = 512 ticks = 128 µs = 21 504 cycles at 168 MHz.
*Pass.* `fill_cycles_max * 2 < 21 504`, i.e. ≤ 50% duty (Rule 5: margin,
not the specification boundary).

*Expectation and risk.* Reading the generated code, the emission loop is
about **25 cycles per tick**, which would be roughly **60% duty** — an
expected **FAIL** of the 50% criterion. That is RISK-1 in
`Docs/PHASE1-STATUS.md`. The figure is instruction counting, not a
measurement; HV-04 decides. Mitigations are listed with RISK-1.

### HV-05 — Emergency-stop path duration

*Pass.* Completes in fewer cycles than one base tick (42 cycles at
168 MHz / 4 MHz), so no STEP edge can be emitted after the stop begins.

---

## 5. Waveform tests (HV-1x)

### HV-10 — Single-axis 2 MHz waveform

*Stimulus.* `stepgen_stim_max_rate(1, 10)`.
*Measure on STEP_X (PA8), 200 ns/div.*

| Quantity | Requirement | Expected |
|---|---|---|
| Period | 500 ns | 500 ns ± timer accuracy |
| Frequency | 2.000 MHz | 2.000 MHz |
| High time | > 100 ns | 250 ns |
| Low time | > 100 ns | 250 ns |
| Rise time | clean, monotonic | load-dependent |
| Overshoot / ringing | must not re-cross the driver's input threshold | — |

Also record a frequency-counter reading over ≥ 10 s. Drift here is a
clock-tree error, not a generator error.

### HV-11 — Three axes simultaneously at 2 MHz (**the requirement**)

*Stimulus.* `stepgen_stim_max_rate(3, 60)` — X, Y, Z.

| Quantity | Pass criterion |
|---|---|
| Per-axis frequency | 2.000 MHz ± 0.01% on all three |
| Missing pulses in 60 s | 0 |
| Extra pulses in 60 s | 0 |
| Inter-axis skew | record; expected ~0, since one BSRR write drives all five pins (ADR-005) |
| Edge-to-edge jitter | record — §20 leaves the limit TBD, so this measurement should be what sets it |

*Pulse accounting.* Compare the analyser's edge count against
`stepgen_status_t.pos_output`. They must agree exactly. This is the test
that demonstrates the **minimum guaranteed capability** of §4.2.

### HV-12 — Five axes simultaneously at 2 MHz

*Stimulus.* `stepgen_stim_max_rate(5, 60)`. Exceeds the requirement.
Record either way; if it passes it may be reported as *measured*, never
as *expected*.

### HV-13 — Mixed incommensurate rates

*Stimulus.* `stepgen_stim_mixed_rates(60)` — 2 MHz / 1999999 Hz / 1 MHz /
666667 Hz / 333331 Hz, chosen so the relationship between axes is
exercised at every relative phase.

*Pass.* Each axis's edge count matches `floor(rate × duration)` exactly;
no pulse narrower than 200 ns.

### HV-14 — DIR setup and hold (**ADR-006's margins**)

*Stimulus.* `stepgen_stim_dir_reversal(30)` — Y reverses every two ring
periods at 2 MHz, which is ADR-006's stated operating regime.

*Measure* STEP_Y (PA9) and DIR_Y (PD9), 10 ns resolution or better.

| Quantity | Requirement | ADR-006 prediction |
|---|---|---|
| DIR hold (last STEP edge → DIR change) | ≥ 200 ns | ≥ 250 ns structurally, independent of software latency |
| DIR setup (DIR change → first STEP edge) | ≥ 200 ns | `(g+1)×250 − L` = 498 ns at `g = 3`, `L ≈ 502 ns` |
| Spurious STEP during a DIR change | none | none |

*Method.* Capture ≥ 1000 reversals; report the **minimum** observed setup
and hold, not the average.

*What this specifically validates.* ADR-006's latency budget `L` contains
two line items the ADR itself flags as external estimates (Cortex-M4
exception entry ≈ 12 cycles, AHB1 GPIO write ≈ 2 cycles) and one proposed
numeric reading of a qualitative rule (a ≈300 ns WCET ceiling for
priority-0/1 handlers). This test measures the outcome those estimates
feed. Record the **minimum setup** and compare against 498 ns: a
materially smaller value means `L` is worse than budgeted and ADR-006's
row 2 needs revisiting.

The host suite already asserts the same properties on the reconstructed
waveform and measures 12500 ns hold / 750 ns setup. HV-14 confirms it on
silicon, where the GPIOD write is a real CPU store subject to real
interrupt latency — which the host simulation cannot model.

### HV-15 — Timing isolation from Ethernet (**the isolation requirement**)

Run **after** Phase 2 brings up the link; the stimulus exists now so the
baseline can be captured immediately.

1. 60 s of HV-11 with the Ethernet cable unplugged. Record jitter, pulse
   count, `fill_cycles_max`.
2. Repeat with the link up and idle.
3. Repeat under a unicast flood at line rate plus broadcast traffic.
4. Repeat with the flood aimed at the controller's own UDP port.

| Quantity | Pass criterion |
|---|---|
| Missing or extra STEP pulses | 0 in every condition |
| STEP period | unchanged within measurement resolution |
| Added edge jitter | record; must be attributable to bus contention, not CPU scheduling |
| `underruns` | 0 |
| `starved_ticks` | 0 |

*What a failure means.* Any missing or duplicated pulse under load makes
the isolation claim false. Once a ring half is committed the waveform is
produced by the timer and DMA alone, so Ethernet can only affect it
through (a) AHB bus contention — which SRAM2 placement addresses, since
Ethernet uses SRAM1 and the two are separate bus-matrix slaves — or
(b) delaying a refill past its deadline, which hard-faults rather than
corrupting motion.

*Also check DIR under load.* Unlike STEP, the DIR write **is** CPU-timed,
so it is the part of the design genuinely exposed to interrupt latency.
Re-run HV-14 during condition 4 and confirm the minimum setup margin has
not collapsed.

### HV-16 — Underrun behaviour is safe

*Method.* Stall the refill deliberately (a breakpoint in
`DMA2_Stream1_IRQHandler`, or an artificially long higher-priority ISR)
while running at 2 MHz.

*Pass.* Within one ring half the engine enters `FAULT` with
`STEPGEN_FAULT_BUFFER_UNDERRUN`, STEP goes low, and `EN` deasserts.
**No stale replay of the previous half may reach the pins.**

This is a safety test: a stale replay is uncommanded machine motion.

### HV-17 — Command-stream pause holds position with drives live

The counterpart to HV-16, and the one that is easy to get backwards.

*Method.* Run at 2 MHz, then stop feeding segments.

*Pass.* STEP stops, `STEPGEN_FAULT_SEGMENT_STARVED` is reported, and
**`EN` stays HIGH** — per ADR-010's owner-confirmed decision, so a paused
link never de-energises a stepper and lets an axis drift or drop under
load. Verify with a meter on PD15, and mechanically by confirming the
axis still resists being turned by hand.

### HV-18 — E-STOP (**the one that matters**)

The PE2 EXTI handler exists as of Phase 4 (M3), so this is now runnable.

*Method.* Run HV-12's five-axis 2 MHz stimulus and flood the Ethernet link
at the same time. Trigger the scope on the falling edge of `PE2` and
capture `PE2`, one STEP pin and `EN` (`PD15`).

*Measure.* `PE2` falling edge → last STEP edge, and `PE2` falling edge →
`EN` low.

*Pass.* Both intervals are bounded and repeatable over ≥ 100 presses, with
no dependence on whether the link is loaded or how many axes are moving —
that independence is the actual claim of `Docs/FIRMWARE-ARCHITECTURE.md`
§8, and it is what the NVIC table in ADR-004 exists to deliver. Record the
worst case; do not average.

*Also check, in the same session:* the machine does **not** restart when
the button is released (ADR-010: recovery is explicit), and a clear issued
while the button is still down is refused (ADR-015 decision 4).

---

## 5b. Network tests (HV-2x) — Phase 2 / M12

Run by `net_selftest_run_all()`
(`Firmware/Platform/STM32F407/Src/net_selftest_stm32f4.c`), which
`main()` calls at boot when `CNC_RUN_SELFTEST_AT_BOOT` is set. Results are
in `net_selftest_results()`, readable over SWD.

HV-20..HV-24 need only power and the PHY module. **HV-25 needs a cable to
the Mach3 PC** and is excluded from the overall verdict for that reason —
at boot, auto-negotiation has had a few milliseconds and the link being
down there means nothing.

### HV-20 — PHY reset pulse (**ADR-013**)

*Method.* Firmware self-check, plus a scope on `PB0` for the definitive
measurement.

*Pass.* `net_port_phy_reset_done()` is true, `PB0` reads back HIGH, and
the measured assertion is at least 150 µs in CPU cycles (168 cycles/µs →
≥ 25,200). The scope capture should show a single clean low pulse of
≥ 150 µs before any MDIO activity.

*Why both.* The self-check catches the two failures that are otherwise
invisible: a regeneration dropping the `USER CODE` call, and a delay loop
that is silently too short. It cannot catch a wrong `SystemCoreClock`
that is wrong in the *same* direction as the cycle-count conversion — the
scope can.

*Also test the case the status quo never covered.* Trigger a warm MCU
reset (SWD reset, not a power cycle) and confirm the link still comes up.
Before ADR-013 the PHY was only ever reset by its own internal power-on
circuit, so a warm reset left it in whatever state it was in.

### HV-21 — Ethernet and lwIP buffers are in SRAM1 (**ADR-012**)

*Method.* Firmware self-check.

*Pass.* `DMARxDscrTab`, `DMATxDscrTab` and the `RX_POOL` pool all lie
inside `0x20000000`–`0x2001C000`. On failure, `measured` holds the
offending address.

*Why it matters.* This is the other half of HV-03. SRAM2 is the STEP
ring's own bus-matrix slave port; an Ethernet buffer there puts back the
contention ADR-012 removed. CCM RAM is worse — RM0090 §2.1: not on the
bus matrix, reachable only by the CPU, so no DMA master including the
Ethernet MAC's own can touch it, and a descriptor there is not slow but
non-functional.

*Static result already available.* The Phase 2 verification build places
`DMATxDscrTab` at `0x20000438`, `DMARxDscrTab` at `0x200004D8` and
`memp_memory_RX_POOL_base` at `0x20000580` — all SRAM1.

### HV-22 — Ethernet interrupts below the STEP-DMA vector (**ADR-012**)

*Method.* Firmware self-check, read from the NVIC itself rather than from
the `.ioc` — the `.ioc` is what a regeneration rewrites.

*Pass.* `priority(ETH_IRQn) > priority(DMA2_Stream1_IRQn)` and
`priority(ETH_WKUP_IRQn) > priority(DMA2_Stream1_IRQn)`; expected 5, 5, 2.
`measured` packs them as `0xEEWWSS`.

*Why it matters.* This is the NVIC half of "Ethernet must not affect STEP
timing". If Ethernet could preempt the STEP-DMA vector, a burst of packets
would delay a ring refill or an ADR-006 DIR write, and the only evidence
would be jitter on a scope. HV-15 is the empirical counterpart.

### HV-23 — MAC address is ADR-011's (**not the vendor placeholder**)

*Method.* Firmware self-check, reading `MACA0HR`/`MACA0LR` — the register
the MAC actually sources frames with, not the C literal.

*Pass.* `02:00:05:10:00:01`. `measured` holds the first three octets;
`0x0080E1` means the CubeMX placeholder came back, which is a real
vendor's OUI and must not go on a wire.

### HV-24 — Static IP active, DHCP inactive

*Method.* Firmware self-check, then `ping 192.168.5.10` from the PC.

*Pass.* The interface carries `192.168.5.10/255.255.255.0`, and with
`LWIP_DHCP` compiled in, no DHCP state is attached to the netif.

*Why it exists.* This exact regression already happened: the static
configuration was written into CubeMX-generated regions and a later
regeneration restored `dhcp_start()` (see `Docs/ETHERNET.md` §15). There
is no DHCP server on this link, so the symptom is an unreachable
controller with no build-time warning.

### HV-25 — Link comes up at 100 Mbit full duplex

*Method.* Cable to the PC, then read `net_link_get()` after a second or
two. Informational; excluded from the verdict.

*Pass.* `link_up`, `NET_SPEED_100M`, `full_duplex`. Pull the cable and
confirm `down_count` increments and the speed reverts to
`NET_SPEED_NONE`; re-insert and confirm `up_count` increments.

*Also worth watching.* `phy_addr` records what the driver's scan found.
The LAN8720A strap allows 0 or 1 and this repository's sources disagree
about which the module uses, so this is the first time the actual value is
known — record it here when it is.

---

## 5c. Protocol tests (HV-3x) — Phase 3 / M13-M14

These need the board, a PC on `192.168.5.100`, and `Tools/c5p1.py`. No
Mach3 and no plugin: the point of Phase 3 is that the firmware's protocol
is testable on its own.

### HV-30 — The device answers on the wire

*Method.* `./c5p1.py info`

*Pass.* An `INFO` packet comes back with `proto_version` 1, `axis_count` 5,
`tick_hz` 4000000, and the MAC and IP matching `net_config.h`. This is the
first proof that a C5P1 packet survives a real Ethernet round trip — the
host suite establishes the format, not the transport.

*Also confirm the device stays silent until spoken to.* Run
`./c5p1.py watch` in one terminal before ever sending a packet: nothing
should arrive. A device that transmits unprompted has lost its endpoint
learning.

### HV-31 — Corrupt packets draw no reply

*Method.* `./c5p1.py raw --corrupt crc`, then `--corrupt magic`,
`--corrupt version`, `--corrupt length`, `--corrupt opcode`.

*Pass.* The tool reports that the device stayed silent in every case, and
the following status shows `rx_dropped` incremented once per attempt while
`rx_accepted` is unchanged. The tool exits non-zero if the device replies,
which it must not: with a failed CRC the sequence number is untrustworthy
too.

### HV-32 — Status cadence and comm timeout

*Method.* `./c5p1.py watch`, then stop the tool mid-program.

*Pass.* Status arrives at ~50 Hz (20 ms). After the tool stops, with motion
active, `proto_faults` shows `COMM_TIMEOUT` within ~200 ms and `state`
becomes `READY`. Confirm with a meter on `PD15` that **`EN` stays
asserted** — ADR-010 requires a broken link to hold position with the
drives live, not to drop them.

*This is the test that proves the network cannot cause an E-stop.* `state`
must never be `EMERGENCY_STOP` as a result of a dead link.

### HV-33 — A real move, end to end

*Method.* Drives disconnected first. `./c5p1.py enable`, `./c5p1.py start`,
then `./c5p1.py move --axis X --steps 2000 --seconds 2`, with a scope or
counter on `PA8`.

*Pass.* 2000 pulses ±1 at ~1 kHz (Docs/PROTOCOL.md §5.3: never more than
commanded, at most one step behind), `pos_output[0]` in the final status
agrees, and `queue_free` visibly dips and recovers during the stream.

### HV-34 — Backpressure under a real burst

*Method.* Stream faster than the machine consumes — a long `move` with a
short `--seconds` — and watch `queue_free`.

*Pass.* The device refuses blocks with `reject = queue full` rather than
dropping them, the tool's retry loop re-sends the **same** `block_seq`, and
no motion is lost or duplicated: the final `pos_output` equals the
commanded total to within one step.

### HV-35 — A lost block stops the machine

*Method.* This one needs a deliberate gap. Easiest is a small edit to the
tool's `block_seq` handling, or a firewall rule dropping one datagram.

*Pass.* `proto_faults` shows `SEQ_GAP`, motion stops, `EN` stays asserted,
and **no further motion is executed until `CONTROL:CLEAR_FAULT`** — sending
the missing block afterwards must not clear it by itself.

### HV-36 — Motion timing is unaffected by protocol traffic

The protocol-layer counterpart to HV-15, and the reason both exist.

*Method.* Run HV-11's three-axis 2 MHz stimulus while streaming motion
blocks and flooding the link with unrelated traffic.

*Pass.* STEP timing on the scope is indistinguishable from HV-11 with a
quiet link. Decoding a 32-record block is bounded work at NVIC priority 5,
strictly below the STEP-DMA vector at 2 (ADR-012), so this should hold —
but "should" is the word HV tests exist to replace.

---

## 5d. Digital input tests (HV-4x) — Phase 4 / M3

Run by `safety_selftest_run_all()`
(`Firmware/Platform/STM32F407/Src/safety_selftest_stm32f4.c`), which
`main()` calls at boot when `CNC_RUN_SELFTEST_AT_BOOT` is set. Results are
in `safety_selftest_results()`, readable over SWD.

None of these asserts an E-STOP. Firing one at every boot to see whether it
works would leave the controller in a latched `EMERGENCY_STOP` that an
operator then has to clear, every boot. They check the conditions that make
the E-STOP path work; **HV-18 checks the path itself**, on a scope, and
neither substitutes for the other.

### HV-40 — The E-STOP release interlock is registered

*Method.* `stepgen_has_estop_gate()` and `safety_input_present()`.

*Pass.* Both true.

*Why it matters.* ADR-010 says `EMERGENCY_STOP` clears only once `PE2` has
been released, and ADR-015 implements that as a predicate the motion engine
holds. With nothing registered, the engine has no way to see `PE2` and a
clear succeeds on the state machine alone — correct for the host test
suite, wrong on a machine. This test is what tells the two apart.

### HV-41 — EXTI and NVIC configuration

*Method.* Read back `EXTI->IMR/RTSR/FTSR`, `SYSCFG->EXTICR[]`,
`GPIOE->MODER/PUPDR` and the NVIC priorities.

*Pass.* Lines 0–14 unmasked with both edges; every one selected onto port
E; `PE0`–`PE14` inputs with no internal pull (external pull-ups are fitted,
`Docs/PINOUT.md`); `EXTI2` at priority 0, the other five vectors at 1, and
the STEP-DMA vector numerically above both (ADR-004).

*Why it matters.* A line silently routed to the wrong port is a dead input,
and one of the fifteen is the E-STOP. A priority inversion against the
STEP-DMA refill would be invisible until it mattered.

### HV-42 — All inputs read idle at rest

*Method.* One `GPIOE->IDR` read with the machine's switches in their rest
position.

*Pass.* All fifteen read HIGH.

*Why it matters.* A line stuck LOW is a wiring or pull-up fault, and if it
is `PE2` the controller will refuse to leave `EMERGENCY_STOP`. Better to be
told that by a numbered test than to debug it as "the machine will not
start".

*Caveat.* This test assumes nothing is holding a switch. On a machine
parked on a limit it will fail correctly; note the reason and re-run.

### HV-43 — Input ISR cost

*Method.* DWT cycle count across `safety_input_on_edge()`, worst of 16,
with `PE2` idle.

*Pass.* Recorded, not thresholded — the firmware's 2000-cycle bound is a
sanity limit, not a budget.

*Why it matters.* These vectors sit above the STEP ring refill (ADR-004),
so their cost comes out of the refill's 128 µs deadline every time an input
moves. HV-04 holds the other half of that budget. This figure deliberately
**excludes** the stop itself (HV-05) and NVIC entry latency (neither), so
it is not an E-STOP latency number and must not be quoted as one.

### HV-44 — Chatter does not reach the motion engine

*Method.* Wire a real (ideally worn) mechanical switch to a non-E-STOP
input. Operate it 50 times while HV-11's three-axis 2 MHz stimulus runs.

*Pass.* STEP timing on the scope is unchanged; the reported input state
changes exactly 50 times, not once per bounce; `safety_input_get_status()`
reports the edges that were suppressed rather than pretending the contact
was clean.

### HV-45 — Debounce and release windows, measured (**replaces the defaults**)

*Method.* Capture the raw `PE0`–`PE14` lines on a logic analyser while
operating each switch the machine actually has, including the E-STOP
button, ≥ 20 times each. Measure the longest bounce burst on assertion and
on release.

*Pass.* `SAFETY_DEBOUNCE_MS` and `SAFETY_ESTOP_RELEASE_MS` exceed the
measured worst case with margin, and are then **set from this measurement**
in `Firmware/Core/Inc/cnc_safety_config.h`.

*Why it matters.* Those two constants are currently documented defaults,
not measurements (ADR-015). Until this test runs, Rule 9 forbids describing
them as validated.

---

## 5e. Output tests (HV-5x) — Phase 5 / M9 + M10

HV-50..HV-53 are run by `io_selftest_run_all()`
(`Firmware/Platform/STM32F407/Src/io_selftest_stm32f4.c`) at boot when
`CNC_RUN_SELFTEST_AT_BOOT` is set. Results are in `io_selftest_results()`.

**None of them closes the relay or turns the spindle.** A boot-time test
that span a tool to prove it could is a test nobody dares enable on a
machine. HV-54 and HV-55 do exercise the outputs, on a bench, deliberately,
with nothing dangerous wired to the relay.

### HV-50 — Outputs are safe at boot

*Method.* Read `PB8`'s output level, the relay's logical state, and whether
the PWM generator is running.

*Pass.* Relay de-energised, `PB8` at its inactive level, generator stopped
(not merely at 0 % duty), `STATUS.outputs` zero.

*Why it matters.* `Docs/PINOUT.md` requires `PB8` LOW at boot so the relay
defaults off, and §32 requires nothing to energise before a known-safe
state. This is that requirement, read back from the pin.

### HV-51 — Spindle PWM configuration (**ADR-016**)

*Method.* Read `TIM3->PSC`, `ARR`, `CR1`, `CCMR1`, `CCER` and `GPIOB`'s
mode and alternate-function bits for `PB4`.

*Pass.* `(PSC+1)×(ARR+1)` divides 84 MHz to exactly 10 000 Hz; `ARPE` and
`OC1PE` both set; `CC1E` set; `PB4` in alternate-function mode on AF2.
`measured` carries the computed frequency in Hz, so a failure says what the
spindle is actually running at.

*Why it matters.* The preload bits are what keep a duty change from landing
mid-pulse and handing a VFD a short or stretched pulse. The AF check
catches the `PB4`/`NJTRST` trap: re-enabling JTAG silently takes the pin.

### HV-52 — The E-STOP kill is registered

*Method.* `io_present()` and `safety_has_estop_action()`.

*Pass.* Both true.

*Why it matters.* Without the registration an E-STOP stops the axes and
leaves the relay closed and the spindle turning until the superloop next
runs `io_poll()`. On a machine whose relay is the spindle contactor, that
is the difference between a safe stop and a dangerous one.

### HV-53 — The interlock holds a request made at boot

*Method.* At boot the engine is in `SAFE_IDLE`. Request the relay and 50 %
duty, run one `io_poll()`, then restore.

*Pass.* Both requests accepted, neither applied: generator stopped, relay
off.

*Why it matters.* This is the safe half of ADR-016's interlock. The other
half is HV-55.

### HV-54 — Spindle waveform (**scope**)

*Method.* Scope on `PB4`, drives disconnected. Bring the machine to
`READY`, then command 0, 1, 10, 250, 500, 750, 999 and 1000 per mille with
`Tools/c5p1.py` (or from `main.c`). Measure frequency and duty at each.

*Pass.* 10.000 kHz ± the crystal's tolerance at every duty; measured duty
within one count (0.012 %) of commanded; **1000 per mille is a constant
high with no notch**, and 0 leaves the pin low with the generator stopped.

*Why it matters.* The 1000-per-mille case is where a plausible
implementation gets it wrong: `CCR = ARR` looks right and leaves one
inactive count per period — a 12 ns notch every 100 µs that a VFD input is
entitled to read as an edge. Only `CCR = ARR+1` is genuinely 100 %. The
host suite catches this in simulation; confirm it on the pin.

### HV-55 — Outputs come on, and drop (**meter + scope**)

**Wire nothing dangerous to the relay for this.** A meter or a test lamp.

*Method.*
1. From `SAFE_IDLE`, request the relay. Confirm it does **not** close.
2. Enable the drives (`READY`). Confirm it closes now, with no glitch on
   the way.
3. With relay closed and spindle at 50 %, press E-STOP. Capture `PE2`,
   `PB8` and `PB4` together.
4. Release E-STOP, clear it, re-enable. Confirm **nothing comes back on
   its own**.
5. Force a `COMM_TIMEOUT` (unplug the cable mid-program). Confirm the relay
   and spindle drop while `EN` (`PD15`) stays high.

*Pass.* Step 3's `PE2`→relay-open and `PE2`→spindle-low intervals are
bounded and comparable to HV-18's — they happen in the same interrupt.
Step 4 shows no output returning without a fresh request. Step 5 is
ADR-016 decision 2 made visible: drives held, work stopped.

*Also confirm here:* `CNC_LED_ACTIVE_HIGH`. The run LED should be lit in
`RUNNING`, blinking in `READY`, dark in `SAFE_IDLE`; the error LED solid in
`EMERGENCY_STOP` and blinking in `FAULT`. If they are inverted, flip that
one constant in `cnc_io_config.h` — it is an assumption, not a measurement
(ADR-016).

---

## 6. Results table

`NOT RUN` is the correct entry until hardware exists. It must not be
replaced by an expectation.

| Test | Result | Measured | Date | Notes |
|---|---|---|---|---|
| HV-00 STEP DMA path | NOT RUN | — | — | Settles ADR-012 |
| HV-01 BSRR priority | NOT RUN | — | — | |
| HV-02 Tick frequency | NOT RUN | — | — | |
| HV-03 SRAM2 placement | **PASS (static)** | `0x2001C000`–`0x2001D000` | 2026-09-16 | Confirmed in the linked map; re-confirm at runtime on target |
| HV-04 Refill cost | NOT RUN | — | — | RISK-1; estimate ~60% duty |
| HV-05 E-STOP path | NOT RUN | — | — | |
| HV-10 1 axis @ 2 MHz | NOT RUN | — | — | |
| HV-11 3 axes @ 2 MHz | NOT RUN | — | — | **The §30 requirement** |
| HV-12 5 axes @ 2 MHz | NOT RUN | — | — | Beyond requirement |
| HV-13 Mixed rates | NOT RUN | — | — | |
| HV-14 DIR setup/hold | NOT RUN | — | — | Validates ADR-006's `L` budget |
| HV-15 Ethernet isolation | NOT RUN | — | — | Phase 2 code now exists; needs the board |
| HV-16 Underrun safety | NOT RUN | — | — | |
| HV-17 Pause holds with EN live | NOT RUN | — | — | ADR-010 |
| HV-18 E-STOP response | NOT RUN | — | — | Runnable since Phase 4; **the one that matters** |
| HV-20 PHY reset pulse | NOT RUN | — | — | ADR-013; also test a warm MCU reset |
| HV-21 Buffers in SRAM1 | **PASS (static)** | `0x20000438`, `0x200004D8`, `0x20000580` | 2026-09-17 | Confirmed in the linked map; re-confirm at runtime on target |
| HV-22 ETH IRQ priority | NOT RUN | — | — | ADR-012; expect 5, 5, 2 |
| HV-23 MAC address | NOT RUN | — | — | ADR-011; expect `02:00:05:10:00:01` |
| HV-24 Static IP / no DHCP | NOT RUN | — | — | Guards the b409ba5 regression |
| HV-25 Link 100M full duplex | NOT RUN | — | — | Needs a cable; not part of the verdict |
| HV-30 Device answers on the wire | NOT RUN | — | — | First real C5P1 round trip |
| HV-31 Corrupt packets draw no reply | NOT RUN | — | — | |
| HV-32 Status cadence / comm timeout | NOT RUN | — | — | **Proves the network cannot E-stop** |
| HV-33 A real move, end to end | NOT RUN | — | — | |
| HV-34 Backpressure under a burst | NOT RUN | — | — | |
| HV-35 A lost block stops the machine | NOT RUN | — | — | |
| HV-36 Timing unaffected by protocol traffic | NOT RUN | — | — | Protocol-layer counterpart to HV-15 |
| HV-40 E-STOP interlock registered | NOT RUN | — | — | ADR-010 / ADR-015 decision 4 |
| HV-41 EXTI / NVIC configuration | NOT RUN | — | — | ADR-004; expect 0, 1, 2 |
| HV-42 Inputs idle at rest | NOT RUN | — | — | Expect `0x7FFF` |
| HV-43 Input ISR cost | NOT RUN | — | — | Not an E-STOP latency figure |
| HV-44 Chatter isolation | NOT RUN | — | — | Needs a real switch |
| HV-45 Debounce windows measured | NOT RUN | — | — | **Replaces ADR-015's defaults** |
| HV-50 Outputs safe at boot | NOT RUN | — | — | PINOUT: PB8 LOW at boot |
| HV-51 Spindle PWM configuration | NOT RUN | — | — | ADR-016; expect 10000 Hz |
| HV-52 E-STOP output kill registered | NOT RUN | — | — | ADR-016 decision 3 |
| HV-53 Interlock holds at boot | NOT RUN | — | — | Safe half of the interlock |
| HV-54 Spindle waveform | NOT RUN | — | — | 100 % must have no notch |
| HV-55 Outputs come on and drop | NOT RUN | — | — | Also confirms LED polarity |

---

## 7. What the host test suite already establishes

`cd Firmware && make test` — 1150 motion checks, 151 input/E-STOP checks,
1160 output/spindle checks, 130 network checks and 347 protocol checks,
all passing at the time of writing. The motion suite reconstructs the pin
waveform from the BSRR word stream and the CPU-timed DIR writes, then
measures it in nanoseconds, so it checks the same properties HV-1x will.

The network suite (`make test-net`) is narrower on purpose. It establishes
that the link state machine counts transitions correctly and never reports
a speed on a down link, and that the configuration constants still hold
the values the documents fix and still satisfy ADR-011/012/013's
invariants. It establishes **nothing** about the PHY, the MAC, the reset
pulse or the wire — those are HV-20..HV-25, and they need the board.

The input suite (`make test-safety`) drives the safety core against the
real motion engine too, on a host simulation of EXTI, so "the E-STOP fires"
is checked by asking the engine what state it reached and "the machine will
not restart" by actually being refused a clear. It establishes the filter's
timing in milliseconds and the polarity normalisation the protocol depends
on. It establishes **nothing** about EXTI, the NVIC, or how long any of it
takes on silicon — those are HV-40..HV-45 and HV-18.

The output suite (`make test-safety`'s companion, `make test-io`) drives
the output manager against the real motion engine **and** the real input
manager, so "the relay drops on E-STOP" is checked by actually asserting
PE2 and looking at the pin, not by calling a kill function and trusting
that something would have called it. It establishes the interlock in every
engine state and the per-mille→compare arithmetic across its whole range —
that arithmetic is where it caught a real off-by-one that would have made
100 % duty unreachable. It establishes **nothing** about GPIOB, TIM3 or
what a VFD makes of the waveform: those are HV-50..HV-55.

The protocol suite (`make test-proto`) drives the session against the real
motion engine on its simulation port, so backpressure, abort and the
sequence rules are exercised against the real queue rather than a mock, and
a motion packet's step count is checked by counting emitted pulses. It
establishes nothing about UDP, lwIP or the wire — that is HV-30..HV-36.

| Property | Host result |
|---|---|
| 2 MHz on one axis, exact step count | pass |
| 5 axes at 2 MHz simultaneously, all exact | pass |
| Every tick is all-five-high or all-five-low (one BSRR word) | pass |
| Pulse exactly one tick (250 ns) wide, never wider | pass |
| At least one low tick between pulses | pass |
| Long-run rate accuracy across mixed rates | pass (exact, 4 M ticks) |
| DIR hold ≥ 200 ns | pass (12500 ns) |
| DIR setup ≥ 200 ns | pass (750 ns) |
| All 200 reversals in a long run honour the guard | pass |
| No STEP on the tick the DIR line changes | pass |
| Reversals faster than a ring period stall, never drop | pass (0 violations) |
| Max-rate lever scales the tick, waveform stays exact | pass |
| Starvation holds position with drives enabled | pass |
| Missed refill deadline faults and drops the drives | pass |
| E-STOP latches; a fault-clear cannot clear it | pass |
| State transitions are gated | pass |

**What it cannot establish, and why hardware testing is still required:**
it uses an idealised DMA with no bus contention, no interrupt latency, no
flash wait states, no Ethernet traffic and no electrical loading. Every
timing figure above is exact by construction in simulation; on silicon
they are subject to all of those. The host suite proves the *algorithm* is
right. HV-0x and HV-1x prove the *machine* is right.
