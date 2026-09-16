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

### HV-18 — E-STOP

Deferred to the phase implementing the PE2 EXTI handler. Recorded so it
is not lost: measure PE2 edge → STEP low → `EN` low, with the engine at
2 MHz on all five axes.

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
| HV-15 Ethernet isolation | NOT RUN | — | — | Needs Phase 2 |
| HV-16 Underrun safety | NOT RUN | — | — | |
| HV-17 Pause holds with EN live | NOT RUN | — | — | ADR-010 |
| HV-18 E-STOP response | NOT RUN | — | — | Needs the EXTI phase |

---

## 7. What the host test suite already establishes

`cd Firmware && make test` — 1150 checks, all passing at the time of
writing. It reconstructs the pin waveform from the BSRR word stream and
the CPU-timed DIR writes, then measures it in nanoseconds, so it checks
the same properties HV-1x will.

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
