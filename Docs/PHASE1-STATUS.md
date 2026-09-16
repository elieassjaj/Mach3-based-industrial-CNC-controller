# Phase 1 Status — STEP/DIR Generation Engine

**Date:** 2026-09-16
**Scope:** STEP/DIR generation engine and its supporting motion modules,
implemented against the frozen decisions in
`Docs/PRE-IMPLEMENTATION-DECISIONS.md` and ADR-001..ADR-011.
**Out of scope:** Ethernet/LwIP (Phase 2), UDP protocol (Phase 3), Mach3
plugin (Phase 4), integration (Phase 5).

---

## 1. Summary

| | |
|---|---|
| Portable motion core | Implemented, 1131 host checks passing |
| STM32F407 hardware port | Implemented, cross-compiles clean for Cortex-M4F |
| On-target self-tests | Implemented, **NOT RUN** (no hardware in this environment) |
| Waveform validation plan | `Docs/HARDWARE-VALIDATION.md`, **NOT RUN** |
| 2 MHz / 3-axis compliance | **NOT CLAIMED** — requires HV-11 on real hardware |
| Documentation sync | ADR-012 added; MOTION-ENGINE.md §33 updated |
| **Blocking issue** | **ADR-012: the frozen TIM2/DMA1 STEP path cannot work on this MCU. Needs an owner decision and an `.ioc` change.** |

No compliance claim is made for any requirement Phase 1 could not measure.

---

## 2. BLOCKER-1 — The frozen STEP DMA path cannot work (needs your decision)

ADR-002/ADR-004/ADR-005 route `TIM2_UP → DMA1_Stream1 → GPIOA->BSRR`, and
`Firmware/CNC5AX-ETH.ioc` is configured that way. Checked against this
repository's own RM0090 Rev 22, **that transfer cannot happen**:

- **§2.1** lists the STM32F405xx/07xx bus-matrix masters as Cortex-M4
  I-bus, D-bus, S-bus; DMA1 **memory** bus; DMA2 memory bus; DMA2
  **peripheral** bus; Ethernet DMA; USB OTG HS DMA. The DMA1 *peripheral*
  bus is **not** among them.
- **§2.1** lists "AHB1 peripherals" among the *slaves*. GPIOA is an AHB1
  peripheral, so it is reachable only through the bus matrix.
- **Figure 33, note 1:** "The DMA1 controller AHB peripheral port is not
  connected to the bus matrix like DMA2 controller."
- **§10.3.16 / §10.3.17 step 2:** in a memory-to-peripheral transfer,
  `DMA_SxPAR` is driven by the AHB **peripheral** port.

So putting `&GPIOA->BSRR` in `DMA_SxPAR` on DMA1 asks the one port that is
not on the bus matrix to reach a slave that exists only on it. Inverting
the transfer does not help — DMA1's peripheral port cannot reach SRAM
either. The configured path would produce **no STEP pulses at all**.

ADR-006 already quotes Figure 33's note, but takes from it only the
memory-to-memory consequence the note happens to mention. The note is
about the port, not about that one transfer type.

**Consequence:** only DMA2 can write a GPIO BSRR, and RM0090 Table 44
gives DMA2 timer requests for **TIM1 and TIM8 only** — so the base timer
cannot be TIM2 either.

**What Phase 1 did.** Implemented the only mapping that can work, and
raised it as ADR-012 rather than substituting silently:

```text
TIM8 UP  ──►  DMA2 Stream 1, Channel 7  ──►  GPIOA->BSRR
```

TIM8 is on APB2, whose timer clock is 168 MHz in your own `.ioc`
(`RCC.APB2TimFreq_Value=168000000`), so `PSC = 0, ARR = 41` gives exactly
4.000 MHz and ADR-004's base tick is preserved bit-for-bit. Everything
else frozen is preserved: single GPIOA port (ADR-005), CPU-timed DIR with
`g = 3` (ADR-006), direct mode, one transfer per request, circular with
HT/TC, very-high stream priority, and the NVIC ordering (ADR-004).

**What you need to decide:**

1. Accept ADR-012 (TIM8 + DMA2_Stream1), or choose TIM1 + DMA2_Stream5
   Ch6 — both are verified in Table 44 and equivalent. TIM8 was picked so
   TIM1 stays free.
2. Update `CNC5AX-ETH.ioc`: TIM2 → TIM8, DMA request `TIM8_UP` on
   `DMA2_Stream1`, and `NVIC.DMA1_Stream1_IRQn` → `DMA2_Stream1_IRQn` at
   the same priority 2. Until then the generated code and the motion
   firmware disagree about the hardware.
3. Optionally settle it empirically first: **HV-00** runs the timebase and
   checks the stream's `NDTR` actually advances. Built against DMA1 it
   should fail outright. Running both is the cheapest way to close the
   question.

**One knock-on to ADR-005.** Its "frees `DMA1_Stream7` for a future
DIR-DMA path" no longer applies, since STEP is not on DMA1. The DIR-DMA
fallback is still available and better placed: DMA2 Streams 2/3/4 carry
`TIM8_CH1/CH2/CH3` and can drive `GPIOD->BSRR` from the same timer.
ADR-005's other reasons — one BSRR word for all five axes, no cross-port
skew, one stream — are unaffected.

---

## 3. What was built

```text
Firmware/
├── Core/Inc/cnc_motion_config.h        requirements, pin map, tunables
├── Motion/                             PORTABLE - no STM32/HAL/CMSIS
│   ├── Inc/motion_types.h
│   ├── Inc/motion_segment_queue.h      lock-free SPSC queue
│   ├── Inc/stepgen_core.h              waveform generator
│   ├── Inc/stepgen_port.h              hardware abstraction
│   ├── Inc/stepgen.h                   the only interface higher layers use
│   └── Src/...
├── Platform/STM32F407/                 TIM8 + DMA2_Stream1 + GPIO BSRR
│   ├── Inc/stepgen_hw_map.h            ALL hardware allocation, one file
│   ├── Src/stepgen_port_stm32f4.c
│   ├── Src/stepgen_selftest_stm32f4.c  HV-00..HV-05 + stimulus patterns
│   └── linker/stepgen_sram2.ld         SRAM2 placement fragment
├── Platform/Host/Src/                  simulation port + virtual analyser
├── Tests/test_stepgen.c                1131 checks
├── Tests/bench_fill.c                  algorithm cost proxy
└── Makefile                            make test | bench | arm
```

**Modularity for later phases.** `Motion/` is pure C with no STM32, HAL or
CMSIS dependency. The only interface above the engine is `stepgen.h`, and
the only data crossing into it is `motion_segment_t` through a lock-free
queue. The engine has no notion of Ethernet, UDP, LwIP or Mach3, so Phase 3
can define the wire format without touching a line of motion code.

---

## 4. Build and test results

Reproduce with `cd Firmware && make test && make bench && make arm`.

### Host verification — 1131 checks, 0 failures

```
  2 MHz on X: one-tick pulse, one-tick gap, exact count   ok
  5 axes @ 2 MHz, single GPIOA word, zero skew            ok
  DDA long-run accuracy across mixed axis rates           ok
  DIR setup/hold >= 200 ns around a 2 MHz reversal        ok   hold=12500 ns setup=750 ns
  every reversal in a 200-segment run honours the guard   ok   200 reversals checked
  reversals faster than a buffer period stall, never drop ok   732 edges, 0 guard violations
  starvation holds position with drives still enabled     ok
  missed refill deadline faults and drops the drives      ok
  E-STOP latches and cannot be cleared by a fault clear   ok
  ADR-010 state transitions are gated                     ok
  segments above 2 MHz or of zero length are rejected     ok
  segment queue is exact at the full/empty boundaries     ok
```

The tests measure the **reconstructed pin waveform in nanoseconds**, not
engine internals — deliberately, so they assert what the oscilloscope will
be asked to confirm.

### Cross-compilation

`arm-none-eabi-gcc 13.2`, `-mcpu=cortex-m4 -mfpu=fpv4-sp-d16
-mfloat-abi=hard -O2`, warnings as errors including `-Wconversion` and
`-Wsign-conversion`, against the project's own `Drivers/CMSIS`. Clean.

```
   text    data     bss     dec
   5182    4096    4156   13434
```

`data` is the 4 KB DMA ring (`.stepgen_ram`, NOLOAD in SRAM2).

### Algorithm cost proxy (x86 host — NOT an STM32 CPU-load figure)

```
  idle (0 axes)                 1.724 ns/tick
  1 axis  @ 2 MHz               1.733 ns/tick
  3 axes  @ 2 MHz               1.720 ns/tick
  5 axes  @ 2 MHz               1.707 ns/tick
  5 axes  @ 1 kHz               1.703 ns/tick
  5 axes  @ 2 MHz, reversing    1.676 ns/tick
```

The architecturally important property: **cost is independent of the
commanded rate and of how many axes are moving.** Five axes at 2 MHz cost
the same as an idle engine, because the per-tick work does not depend on
whether a carry occurs.

---

## 5. Defects found and fixed during implementation

Recorded because both were silent-wrong-motion bugs, not build errors.

1. **The DIR write armed for the first half never fired.** ADR-006's
   play-time write happens in the boundary interrupt for the target half,
   but logical half 0 starts playing the instant the timer is enabled and
   no boundary interrupt precedes it. The first commanded direction was
   therefore never written, so the first move of every run could go the
   wrong way. Fixed by performing half 0's play-time write in
   `stepgen_start()` before the timer runs — which also gives that first
   write unbounded setup margin.

2. **A second reversal overwrote an unrealized armed one.** ADR-006 gives
   each axis an armed slot of depth one and says a new reversal "simply
   waits for the armed one to be realized first". A first cut overwrote
   instead, so when reversals arrived faster than one ring period the
   pending direction change was discarded and the axis ran the wrong way
   while the rate alternated. Fixed by holding the new segment until the
   armed change has been written; the stall is bounded by one half-period
   and surfaces to the protocol layer as ordinary queue backpressure.
   Regression test: "reversals faster than a buffer period stall, never
   drop" — 732 edges, 0 guard violations, 286 pushes back-pressured.

---

## 6. Assumptions

| # | Assumption | Basis | How to confirm |
|---|---|---|---|
| A-1 | APB2 timer clock = 168 MHz | `RCC.APB2TimFreq_Value` in `CNC5AX-ETH.ioc` | Already consistent with the project's own clock tree; HV-02 detects a mismatch |
| A-2 | Base tick = 4 MHz | ADR-004 | — |
| A-3 | DIR guard `g = 3` ticks | ADR-006 | HV-14 |
| A-4 | Ring `N` = 1024 ticks | **ADR-008 leaves this open.** 1024 gives 256 µs ring, 128 µs deadline, 4 KB, 7.8 kHz ISR, 128–256 µs reversal latency | HV-04; smaller `N` cuts reversal latency but raises the ISR rate |
| A-5 | Ethernet buffers will live in SRAM1 | Required for the SRAM2 bus isolation to mean anything | **Phase 2 must honour this** |
| A-6 | `EN` (PD15) is active HIGH | `Docs/PINOUT.md`, owner-verified | Already resolved |
| A-7 | Positive axis motion ⇒ DIR HIGH | §25 forbids assuming it, so it is a per-axis runtime parameter with this default | Machine configuration; likely settled with the plugin in Phase 4 |
| A-8 | Segment queue depth 64 slots | ADR-008's ≥128 ms target is a *time*, and the slice duration is a Phase 3 protocol decision | Revisit once the protocol exists |

---

## 7. Risks

### RISK-1 — Refill CPU cost may exceed the headroom target (**principal risk**)

Reading the generated Cortex-M4 code, the emission loop is about **25
cycles per base tick**: ~100 M cycles/s at 4 MHz, or **~60% of a 168 MHz
core**. That is above the 50% duty target implied by
`Docs/MOTION-ENGINE.md` Rule 5 and tested by HV-04.

This is instruction counting, not a measurement. It could be optimistic
(flash wait states, bus contention) or pessimistic (the ART accelerator
caches tight loops well, and the loop is now entirely register-resident
with a single store per tick and no stack traffic). **HV-04 decides.**

Two earlier formulations were rejected during Phase 1: a per-axis pass
OR-ing into a pre-blanked buffer (~8 cycles/axis/tick, ~95% CPU), and a
version that tracked step counts and last-step ticks per tick (spilled to
stack, ~29 cycles/tick). The current form removes the read-modify-write,
the blanking pass, and all per-tick bookkeeping — step counts and
last-step ticks are computed in closed form after each run.

Mitigations if HV-04 fails, in order of preference:

1. **Hand-written inner loop.** GCC emits `ITE CS / MOVCS / MOVCC` plus an
   `ADD LSL#1` per axis where `ADCS b, b, b` alone would do — five
   instructions replaced by one, per axis per tick. Estimated 13–15
   cycles/tick (~35% duty). Deliberately not written in Phase 1: shipping
   untested assembly in the path that drives a machine is worse than
   shipping a measured number. §16 permits it once the measurement
   justifies it.
2. **Lower the base tick.** `stepgen_port_set_tick_hz()` already supports
   it and rejects non-integer dividers. A machine whose configured
   maximum is 1 MHz runs a 2 MHz tick at half the cost; only machines that
   genuinely need 2 MHz pay for it.
3. **Larger ring**, to amortise per-refill overhead — small win, and
   SRAM2 bounds it.

What this risk is **not**: a threat to STEP timing. The CPU is not in the
pulse path. A refill that is too slow is *detected* and hard-faults
(ADR-005/ADR-008); it cannot produce wrong motion.

### RISK-2 — The CPU-timed DIR write is the part exposed to interrupt latency

STEP is immune to interrupt latency once a half is committed; the DIR
write is not, by ADR-006's design. Its budget `L ≈ 502 ns` contains two
line items that ADR itself flags as external estimates, plus a proposed
numeric reading of §27's qualitative rule. HV-14 measures the outcome,
and HV-15 re-runs it under Ethernet load — which is the condition most
likely to stress row 2 of that budget.

### RISK-3 — Jitter limit is still TBD

`Docs/MOTION-ENGINE.md` §20 leaves it TBD and Rule 2 forbids inventing
one. The design contributes ±1 tick (±250 ns) of phase quantisation,
non-cumulative. HV-11 should measure the real figure and set the limit.

### RISK-4 — ADR-005's pin reassignment is still unverified against the PCB

ADR-005 itself flags this: if `PC9` is already routed to the X-axis driver
on fabricated hardware, moving STEP_X to `PA8` is a hardware change, not
a firmware change. Phase 1 implements the post-ADR-005 pinout. Not a
Phase 1 finding — repeated here because it gates any hardware test.

---

## 8. Contradictions found and how they were handled

Per the instruction not to resolve contradictions silently:

1. **The frozen STEP DMA path is not implementable.** Raised as ADR-012
   and §2 above, implemented the only way that works, flagged for an owner
   decision rather than quietly substituted.

2. **ADR-004 assumes a 84 MHz TIM2 clock; ADR-012 moves to a 168 MHz TIM8
   clock.** Both give exactly 4 MHz (ARR 20 vs 41). No requirement changes;
   noted so the two ARR values are not read as a contradiction.

3. **`Docs/MOTION-ENGINE.md` §31 references files that do not exist** —
   `SYSTEM_ARCHITECTURE.md` (actual: `SYSTEM-ARCHITECTURE.md`),
   `ethernet.md` (actual: `ETHERNET.md`), `MACH3/SDK_README.MD` (actual:
   `SDK-README.MD`). Cosmetic, outside Phase 1's scope, **not** changed —
   silently editing them would hide the inconsistency.

4. **§5 says pulse width "is not a primary system-level constraint" while
   §30's acceptance table lists "> 100 ns preferred".** Treated as a hard
   floor; 250 ns delivered, 2.5×.

5. **§19 requires low CPU use; §4.2 requires 2 MHz on 3+ axes.** In
   genuine tension on this MCU at this tick rate. That tension is RISK-1,
   surfaced rather than resolved by weakening either.

---

## 9. What Phase 2 must not break

1. Ethernet descriptors and the LwIP pbuf pool go in **SRAM1**, never
   SRAM2 (assumption A-5; the bus isolation depends on it).
2. Ethernet interrupt priority stays numerically **above** (lower priority
   than) the STEP-DMA vector at 2, per ADR-004.
3. TIM8 and DMA2 Stream 1 are owned by the motion engine. The Ethernet MAC
   has its own dedicated DMA and needs neither.
4. Nothing in the Ethernet path may call into `stepgen.h` except
   `stepgen_submit_segment()`, `stepgen_queue_free()` and
   `stepgen_get_status()`.
5. Run HV-15 after link bring-up — that is the test the Ethernet isolation
   requirement actually rests on — and re-run HV-14 under load, since the
   DIR write is the CPU-timed part.

---

## 10. Phase 1 exit criteria

| Criterion | State |
|---|---|
| Engine implemented per the frozen ADRs, modular, network-independent | Done |
| Builds clean for host and Cortex-M4F | Done |
| Host verification of waveform, rates, DIR timing, fault paths, state model | Done — 1131 checks |
| Hardware validation path for multi-axis STEP, 2 MHz, DIR timing, DMA/BSRR, Ethernet isolation | Done — `Docs/HARDWARE-VALIDATION.md` + on-target self-tests |
| Documentation synchronised with implementation | Done — ADR-012, §33 |
| Assumptions, blockers, risks reported | Done — this document |
| **Frozen DMA path contradiction resolved** | **BLOCKED — needs your decision (§2)** |
| **2 MHz / 3-axis behaviour measured on hardware** | **NOT DONE — HV-11 is the gate** |
| **CPU headroom measured** | **NOT DONE — HV-04; see RISK-1** |

Phase 1 is complete as an implementation and verification-infrastructure
deliverable. It is **not** complete as a compliance claim, and per
`Docs/MOTION-ENGINE.md` Rule 8 must not be reported as one. Phase 2 can
proceed in parallel, but the ADR-012 decision should be made before any
hardware bring-up, because the `.ioc` currently generates a configuration
that cannot produce STEP pulses.
