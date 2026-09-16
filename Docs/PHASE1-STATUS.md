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
| Portable motion core | Implemented, 1150 host checks passing |
| STM32F407 hardware port | Implemented, cross-compiles clean for Cortex-M4F |
| On-target self-tests | Implemented, **NOT RUN** (no hardware in this environment) |
| Waveform validation plan | `Docs/HARDWARE-VALIDATION.md`, **NOT RUN** |
| 2 MHz / 3-axis compliance | **NOT CLAIMED** — requires HV-11 on real hardware |
| Documentation sync | ADR-012 added; MOTION-ENGINE.md §33 updated |
| Full firmware build | **Links clean** — 84 KB flash (8.2%), SRAM1 36.7%, SRAM2 25% |
| ADR-012 (TIM2/DMA1 → TIM8/DMA2) | **RESOLVED** — `.ioc` updated by the project owner and verified; no blocking issues remain |

No compliance claim is made for any requirement Phase 1 could not measure.

---

## 2. ADR-012 — The frozen STEP DMA path could not work (RESOLVED)

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

**Resolution.** The project owner accepted ADR-012 and reconfigured the
`.ioc` accordingly. Verified in this repository:

```text
Dma.Request0=TIM8_UP
Dma.TIM8_UP.0.Instance=DMA2_Stream1
Dma.TIM8_UP.0.Direction=DMA_MEMORY_TO_PERIPH
Dma.TIM8_UP.0.FIFOMode=DMA_FIFOMODE_DISABLE      <- direct mode, as ADR-004
Dma.TIM8_UP.0.MemInc=DMA_MINC_ENABLE
Dma.TIM8_UP.0.PeriphInc=DMA_PINC_DISABLE
Dma.TIM8_UP.0.MemDataAlignment=DMA_MDATAALIGN_WORD
Dma.TIM8_UP.0.PeriphDataAlignment=DMA_PDATAALIGN_WORD
Dma.TIM8_UP.0.Mode=DMA_CIRCULAR
Dma.TIM8_UP.0.Priority=DMA_PRIORITY_VERY_HIGH
NVIC.DMA2_Stream1_IRQn=true:2:0:...              <- preempt 2, per ADR-004
```

All TIM2 references are gone, and TIM8's own global interrupt is correctly
left disabled (ADR-004 forbids it: at the 4 MHz tick it would fire four
million times a second).

**Independent corroboration.** CubeMX's own generated code assigns
`hdma_tim8_up.Init.Channel = DMA_CHANNEL_7` on `DMA2_Stream1` — the exact
mapping RM0090 Table 44 gives, arrived at by the tool without reference to
this analysis.

**One gap found and fixed during integration.** The regenerated
`MX_TIM8_Init()` carried `htim8.Init.Period = 65535` (CubeMX's default);
the Counter Period had not been set to 41. The firmware overrides ARR at
runtime in `stepgen_port_set_tick_hz()`, so the base tick would have been
correct either way — but a `.ioc` that disagrees with the firmware is the
exact condition that produced ADR-012 in the first place, so both
`CNC5AX-ETH.ioc` (`TIM8.Period=41`) and the generated `Core/Src/tim.c`
were corrected to agree.

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
├── Tests/test_stepgen.c                1150 checks
├── Tests/bench_fill.c                  algorithm cost proxy
└── Makefile                            make test | bench | arm
```

**Modularity for later phases.** `Motion/` is pure C with no STM32, HAL or
CMSIS dependency. The only interface above the engine is `stepgen.h`, and
the only data crossing into it is `motion_segment_t` through a lock-free
queue. The engine has no notion of Ethernet, UDP, LwIP or Mach3, so Phase 3
can define the wire format without touching a line of motion code.

---

## 3b. CubeIDE integration (completed)

The motion subsystem is now wired into the generated project and the whole
firmware links.

| Step | What was done |
|---|---|
| Interrupt vector | `DMA2_Stream1_IRQHandler` stays owned by the generated `stm32f4xx_it.c`; its `USER CODE BEGIN 0` block calls `stepgen_dma_isr()` and returns. The port's handler was renamed from the vector name to avoid a duplicate-symbol clash on every regeneration |
| HAL bypass | That `return` also skips `HAL_DMA_IRQHandler(&hdma_tim8_up)`. The stream is configured at register level, not through the HAL handle, so letting HAL service it would clear transfer flags and fire callbacks behind the engine's back. Verified in the linked image: the vector is `push / bl stepgen_dma_isr / pop` and never reaches the HAL call |
| Linker script | `STM32F407VGTX_FLASH.ld` now splits `RAM` into SRAM1 (112 K) + `SRAM2` (16 K) and adds the `.stepgen_ram (NOLOAD)` section |
| `main.c` | `stepgen_init()` runs after `MX_TIM8_Init()` in `USER CODE BEGIN 2`, leaving the engine in `SAFE_IDLE` with the drives disabled. **Nothing moves at boot** — starting motion is the Phase 3 host protocol's job. An optional boot self-test sits behind `CNC_RUN_SELFTEST_AT_BOOT` (default off) |
| TIM8 period | `.ioc` and `tim.c` corrected to `41` (see §2) |

**Everything lives in `USER CODE` blocks**, so regenerating from CubeMX
does not undo any of it. The `Motion/` and `Platform/` trees sit outside
`Core/`, which CubeMX never touches.

### Linked image

```
Memory region      Used Size   Region Size   %age Used
       CCMRAM:           0 B         64 KB       0.00%
          RAM:       42040 B        112 KB      36.66%   (SRAM1: Ethernet, LwIP, app)
        SRAM2:         4096 B         16 KB      25.00%   (STEP DMA ring, isolated)
        FLASH:       86152 B          1 MB       8.22%
```

`__stepgen_ram_start__ = 0x2001C000`, `__stepgen_ram_end__ = 0x2001D000` —
the ring is in SRAM2, so the HV-03 placement check already passes
statically. Plenty of headroom in both regions for Phases 2-4.

---

## 4. Build and test results

Reproduce with `cd Firmware && make test && make bench && make arm`.

### Host verification — 1150 checks, 0 failures

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
  max-rate lever scales the tick and stays exact          ok
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

### RISK-1 — Refill CPU cost at the 2 MHz ceiling

Reading the generated Cortex-M4 code in the linked firmware, the emission
loop is about **29 instructions per base tick**, fully register-resident
with a single store and no stack traffic. At the 4 MHz tick that is
roughly **60-70% of a 168 MHz core**, above the 50% duty target implied by
`Docs/MOTION-ENGINE.md` Rule 5 and tested by HV-04.

**This is a ceiling cost, not a running cost — but not for the reason one
might expect.** The refill cost is proportional to the **base tick rate**,
not to how fast the axes are commanded. At a fixed 4 MHz tick the engine
costs the same whether an axis is running at 2 MHz or crawling at 100 Hz,
because the per-tick work does not depend on whether an accumulator
carries. So "the machine will not always run at 2 MHz" does not by itself
reduce CPU load.

What *does* reduce it is lowering the tick, and that is now a first-class
API:

```c
stepgen_configure_max_rate(1000000u);   /* 1 MHz ceiling -> 2 MHz tick */
```

| Configured max rate | Base tick | Relative refill cost | Est. CPU |
|---|---|---|---|
| 2 MHz (project ceiling) | 4 MHz | 1.00 | ~60-70% |
| 1 MHz | 2 MHz | 0.50 | ~30-35% |
| 500 kHz | 1 MHz | 0.25 | ~15-18% |

Only exact integer dividers of the 168 MHz timer clock are accepted, so no
commanded feed rate ever picks up a systematic divider error. Most
machines are limited by mechanics and microstepping well below 2 MHz; for
those, this single call returns most of the CPU. The 2 MHz figure remains
the *demonstrated capability* the project requires, not the operating
point every machine has to pay for.

All of the above is instruction counting, not measurement. **HV-04
decides.** It could be optimistic (flash wait states, bus contention) or
pessimistic (the ART accelerator caches this loop well).

**A build-configuration trap, found and closed.** STM32CubeIDE's Debug
configuration builds at `-Og`, which spilled three loop values back to the
stack and cost ~30% more per tick than `-O2` — margin lost silently, and
only in the configuration people actually debug with. `stepgen_core.c` now
pins its own optimisation level (`#pragma GCC optimize ("O2")`, opt out
with `STEPGEN_NO_OPT_PRAGMA`). Verified: 0 stack accesses in the loop in a
full `-Og` firmware build.

Two earlier formulations were rejected during Phase 1: a per-axis pass
OR-ing into a pre-blanked buffer (~8 cycles/axis/tick, ~95% CPU), and a
version tracking step counts and last-step ticks per tick (spilled, ~29
cycles/tick at `-O2`). The current form removes the read-modify-write, the
blanking pass, and all per-tick bookkeeping — step counts and last-step
ticks are computed in closed form after each run.

Further mitigations if HV-04 still fails at a genuinely required 2 MHz:

1. **Hand-written inner loop.** GCC emits `ITE CS / MOVCS / MOVCC` plus an
   `ADD LSL#1` per axis where `ADCS b, b, b` alone would do — five
   instructions replaced by one, per axis per tick. Estimated 13-15
   cycles/tick (~35% duty). Deliberately not written in Phase 1: shipping
   untested assembly in the path that drives a machine is worse than
   shipping a measured number. §16 permits it once measurement justifies it.
2. **Larger ring**, to amortise per-refill overhead — small win, and SRAM2
   bounds it (12 KB still free there).

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
| Host verification of waveform, rates, DIR timing, fault paths, state model | Done — 1150 checks |
| Hardware validation path for multi-axis STEP, 2 MHz, DIR timing, DMA/BSRR, Ethernet isolation | Done — `Docs/HARDWARE-VALIDATION.md` + on-target self-tests |
| Documentation synchronised with implementation | Done — ADR-012, §33 |
| Assumptions, blockers, risks reported | Done — this document |
| Frozen DMA path contradiction resolved | Done — ADR-012 accepted, `.ioc` updated and verified (§2) |
| Integrated into the CubeIDE project, full firmware links | Done (§3b) |
| **2 MHz / 3-axis behaviour measured on hardware** | **NOT DONE — HV-11 is the gate** |
| **CPU headroom measured** | **NOT DONE — HV-04; see RISK-1** |

Phase 1 is complete as an implementation and verification-infrastructure
deliverable, and is now integrated into the CubeIDE project: the full
firmware compiles and links, with the STEP ring verified in SRAM2 and the
DMA vector verified to reach the engine.

It is **not** complete as a compliance claim, and per
`Docs/MOTION-ENGINE.md` Rule 8 must not be reported as one — the 2 MHz
three-axis requirement is demonstrated only in host simulation. Closing
that gap needs the board: flash the image, run
`stepgen_selftest_run_all()` (HV-00..HV-05), then work through the HV-1x
waveform tests.

Nothing blocks Phase 2. The Ethernet work must keep its buffers in SRAM1
and its interrupt priority numerically above 2 (§9).
