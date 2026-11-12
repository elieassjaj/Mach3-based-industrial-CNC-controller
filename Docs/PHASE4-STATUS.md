# Phase 4 Status — Digital Inputs and the E-STOP Path (M3)

| | |
|---|---|
| Module | M3 `Safety` (Input Manager), and M11 `IO/DigitalInput` folded into it |
| Decisions | ADR-015 (`Docs/FIRMWARE-ARCHITECTURE.md` §41) |
| Code | `Firmware/Safety/`, `Firmware/Platform/*/Src/safety_port_*.c` |
| Host tests | `make test-safety` — 151 checks, all passing |
| Hardware tests | HV-40..HV-45 and HV-18 — **none run**, no board |
| Blocks removed | `STATUS.inputs`, ADR-010's E-STOP release interlock |
| Blocks remaining | M9/M10 (outputs), probe capture, the PC-side plugin |

---

## 1. Summary

The controller can now see its own machine. Before this phase the firmware
had fifteen EXTI lines configured by CubeMX and nothing behind them: an
E-STOP press produced an interrupt that returned without doing anything,
`STATUS.inputs` was a hard zero, and `stepgen.h`'s promise that an
emergency stop "can only be cleared once the physical input has released"
was a comment with no code under it.

All three are now true statements rather than intentions:

- `PE2` reaches the motion engine from inside the EXTI2 interrupt, at NVIC
  priority 0, without touching the superloop, the network or any state
  machine on the way.
- `STATUS.inputs` carries the real, debounced state of all fifteen inputs,
  and `flags.5` tells the host it is real.
- `CONTROL:CLEAR_ESTOP` is refused while `PE2` is down, by the engine
  itself, not by a check in the packet handler that a future caller could
  bypass.

What this phase did **not** do is prove any of it on silicon. Everything
below was verified against a host simulation of EXTI. `Docs/HARDWARE-VALIDATION.md`
§5d is the list of what a board would have to show, and Rule 10 stands:
none of it may be described as validated until it has been run.

---

## 2. Decisions and where each came from

`Docs/FIRMWARE-ARCHITECTURE.md` §7 and §20 fixed the hardware and demanded
an Input Manager, then explicitly left the handling method to the
implementation. ADR-015 records the five choices made and the reasoning;
the short version:

| Decision | Why, in one line |
|---|---|
| Level-based, never edge-count-based | Five of six vectors are shared, pending bits coalesce, and an E-STOP already down at boot has no edge left to give |
| Assertion immediate, release filtered | Filtering an assertion adds latency to the one path §8 requires to be fastest; not filtering the release lets a bouncing contact unlock a machine |
| Publish logical assertion, not pin level | An all-zero word must mean "nothing asserted", which is the reading `flags.5` exists to protect (`Docs/PROTOCOL.md` §6.2) |
| Interlock as a predicate the engine holds | One registration at init cannot be forgotten at a call site; a check inside the packet handler can |
| No per-input meaning in firmware | ADR-010 already placed that host-side, in Mach3's `InSigs[]` table |

Two numbers in that ADR are **defaults, not measurements**: the 3 ms
debounce window and the 50 ms E-STOP release window. No switch datasheet
exists in this repository. HV-45 is the test that replaces them.

---

## 3. What was built

```text
Firmware/
  Core/Inc/cnc_safety_config.h              pin map, windows, tunables
  Safety/Inc/safety_types.h                 the published snapshot
  Safety/Inc/safety_input.h                 the facade the firmware uses
  Safety/Inc/safety_port.h                  the hardware abstraction
  Safety/Src/safety_input.c                 filter + E-STOP state machine
  Platform/STM32F407/Inc/safety_hw_map.h    GPIOE, EXTI, ADR-004 priorities
  Platform/STM32F407/Inc/safety_port_stm32f4.h   the six vector entries
  Platform/STM32F407/Inc/safety_selftest.h  HV-40..HV-43
  Platform/STM32F407/Src/safety_port_stm32f4.c
  Platform/STM32F407/Src/safety_selftest_stm32f4.c
  Platform/Host/Src/safety_sim.h            bench controls for the sim port
  Platform/Host/Src/safety_port_sim.c       simulated GPIOE + EXTI
  Tests/test_safety.c                       151 checks
```

`Safety/` is pure C with no hardware dependency, like `Motion/` and `Net/`
before it. It reaches the motion engine only through `stepgen.h`, so the
dependency runs Safety → Motion and never back, and the host tests drive
the **real** engine rather than a mock.

### 3.1 The two paths

```text
PE2  ──EXTI2, prio 0──► safety_estop_isr()
                          └─► safety_input_on_estop_edge()
                                └─► stepgen_emergency_stop()   ← register writes
PE0,1,3..14 ──EXTI, prio 1──► safety_inputs_isr(mask)
                                └─► safety_input_on_edge()      ← state capture only

superloop ──► safety_input_poll(HAL_GetTick())
                ├─ debounce filter (14 inputs, symmetric)
                ├─ E-STOP release timer (one direction only)
                └─ chatter counters
```

The interrupt never performs a read-modify-write on a word the superloop
also writes, so no ISR observation can be lost to a half-finished update in
the main loop — the observation that would get lost is an E-STOP.

### 3.2 Changes to existing code

| File | Change |
|---|---|
| `Core/Src/stm32f4xx_it.c` | All six input vectors call the port from their `USER CODE` blocks and return before `HAL_GPIO_EXTI_IRQHandler()`, the same pattern the STEP-DMA vector already used. Each clears only its own `EXTI->PR` bits |
| `Core/Src/main.c` | `safety_input_init()` after `stepgen_init()`; `safety_input_poll()` in the superloop; HV-40..HV-43 under `CNC_RUN_SELFTEST_AT_BOOT` |
| `Motion/Inc/stepgen.h`, `Motion/Src/stepgen.c` | `stepgen_set_estop_gate()` / `stepgen_has_estop_gate()`; `stepgen_clear_emergency_stop()` consults the gate. Not cleared by `stepgen_init()` — an interlock a re-init can drop is not one |
| `Net/Src/cnc_session.c` | `STATUS.inputs` and `CNC_SFLAG_INPUTS_PRESENT` |
| `Makefile` | `make test-safety`; safety sources in the `arm` and `firmware` targets |

Every edit inside CubeMX-generated files is within `USER CODE` markers, so
regenerating the project does not lose them.

---

## 4. Safety behaviour, stated precisely

1. **An E-STOP assertion is never filtered, never queued and never waits
   for the superloop.** The machine is stopping before the ISR has finished
   its own bookkeeping.
2. **An E-STOP release is filtered, in one direction only**, over a window
   deliberately longer than the ordinary debounce. A contact that bounces
   during release never reads as released.
3. **A release grants permission and does nothing else.** ADR-010's
   "recovery is never automatic" is unchanged: the machine does not restart
   because somebody let go of the button.
4. **An E-STOP already asserted at power-on is honoured.** `safety_input_init()`
   samples the level before arming interrupts, precisely because the edge
   has already happened and will not happen again.
5. **A lost edge costs nothing.** Every ISR and every poll re-reads the
   port; nothing in the subsystem trusts an edge count.
6. **Nothing about an input's meaning is decided here** except `PE2`, whose
   meaning `Docs/PINOUT.md` and §8 fix. A limit is a bit in a word until
   the host says otherwise.

---

## 5. Build and test results

```text
make test        1150 motion + 151 input + 130 network + 316 protocol checks, 0 failures
make arm         clean under -Wall -Wextra -Werror -Wconversion -Wsign-conversion ...
make firmware    text 62748  data 132  bss 45568   (ELF links and fits)
```

Cost of M3 against the same build without it:

| | Before | After | Delta |
|---|---|---|---|
| `.text` | 61 556 | 62 748 | **+1 192 B** |
| `.bss` | 45 320 | 45 568 | **+248 B** |

The 248 bytes are the per-input filter timestamps, edge counters and
chatter baselines — static, no dynamic allocation. The selftest code is
excluded by `--gc-sections` unless `CNC_RUN_SELFTEST_AT_BOOT` is set.

### 5.1 What the host suite establishes

| Property | Checked by |
|---|---|
| A pin pulled LOW is published as *asserted*, not as a zero bit | polarity cases |
| A bounce shorter than the window never reaches the reported state | debounce cases |
| The suppressed edges are still counted, not hidden | debounce cases |
| E-STOP stops the **real** engine with zero milliseconds elapsed | E-STOP cases |
| An E-STOP already down at boot, and one whose edge was lost, both fire | E-STOP cases |
| A bouncing E-STOP counts as one assertion | E-STOP cases |
| Release is refused before the window and granted after | release cases |
| A clear is refused while down, refused mid-window, accepted after | interlock cases |
| A generic fault clear still cannot clear an E-stop | interlock cases |
| Latches survive the release and cannot be wiped while live | latch cases |
| A chattering input is named without being filtered differently | chatter cases |
| `STATUS.inputs` and `flags.5` carry it end to end | `test_protocol.c` |

### 5.2 What it does not establish

Nothing about EXTI, SYSCFG, the NVIC, or how long anything takes on
silicon. The host suite simulates the interrupt controller; it cannot
simulate being wrong about it.

---

## 6. Risks and unresolved items

| # | Item | Status |
|---|---|---|
| RISK-4a | The 3 ms and 50 ms windows are defaults, not measurements | Open — HV-45 |
| RISK-4b | The debounce runs on the 1 ms `HAL_GetTick()` tick, so the real window is [T, T+1] ms | Accepted at 3 ms; would not be at 1 ms |
| RISK-4c | `s.estop_asserted` is written by both the ISR and the release timer. The clear re-reads the pin to close the window rather than masking EXTI2 | Accepted — a briefly deaf E-STOP line is the worse trade. Residual window is one port read |
| RISK-4d | Input vectors sit **above** the STEP refill (ADR-004), so a chattering contact spends time that comes out of the 128 µs refill deadline | Mitigated by reporting; bounded by HV-43 + HV-04, neither run |
| RISK-4e | The whole subsystem is unverified on hardware | Open — HV-18, HV-40..HV-45 |
| RISK-4f | Probing has no capture path. A 50 Hz status word is not a probe hit | Open, and deliberately not faked — `Docs/PROTOCOL.md` §10 |

---

## 7. What the next phase must know

1. **The input word is already normalised.** A plugin applies Mach3's
   `Negated` flags to it; it does not re-derive the board's active-low
   polarity. `Docs/MACH3-INTERFACE.md` §9.
2. **`CLEAR_ESTOP` will be refused, legitimately, for up to the release
   window after the button comes back up.** That is `WRONG_STATE`, not an
   error to escalate.
3. **The E-STOP arrives twice**, as `inputs` bit 2 and as the engine's
   `EMERGENCY_STOP` state. Either may be seen first; the machine stopped
   before either was transmitted.
4. **Assigning meaning to the other fourteen inputs is unclaimed work.**
   Nothing in this repository says which pin is a limit. `ncPod`'s
   `GetInputs()` is the reference shape.
5. **M9/M10 are now the only firmware modules left before the plugin.**
   `flags.6` and `OUTPUTS`/`NOT_IMPLEMENTED` mark exactly where.

---

## 8. Phase 4 exit criteria

| Criterion | State |
|---|---|
| All 15 inputs serviced by EXTI, both edges, no internal pull | Done |
| `PE2` handled independently of the network and the superloop | Done |
| Filtering decided, implemented, and its numbers labelled as unmeasured | Done — ADR-015, HV-45 open |
| ADR-010's physical-release interlock enforced in code | Done |
| Input state published to the host, distinguishable from "no data" | Done — `STATUS.inputs`, `flags.5` |
| No per-input semantics invented in firmware | Done |
| Host verification | Done — 151 checks |
| Hardware verification | **Not done** — HV-18, HV-40..HV-45 |
