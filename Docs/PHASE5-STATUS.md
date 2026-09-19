# Phase 5 Status — Outputs and Spindle (M9 / M10)

| | |
|---|---|
| Modules | M9 `IO/Outputs` (relay, status LEDs), M10 `IO/Spindle` (PWM) |
| Decisions | ADR-016 (`Docs/FIRMWARE-ARCHITECTURE.md` §41) |
| Code | `Firmware/IO/`, `Firmware/Platform/*/Src/io_port_*.c` |
| Host tests | `make test-io` — 1160 checks, all passing |
| Hardware tests | HV-50..HV-55 — **none run**, no board |
| Blocks removed | `OUTPUTS` opcode, `STATUS.outputs`, `STATUS.spindle_pmille`, `flags.6` |
| Blocks remaining | the PC-side Mach3 plugin — **and nothing else in firmware** |

---

## 1. Summary

**Every firmware module in the implementation plan now exists.** Before
this phase, `MX_TIM3_Init()` built a 10 kHz PWM generator that was never
started, `PB8`/`PB2`/`PB1` were driven low once at boot and never touched
again, and the `OUTPUTS` opcode was answered with `NOT_IMPLEMENTED`.

What Phase 5 actually adds is not "three pins". It is the rule that governs
them:

- The relay and the spindle are permitted only in `READY` and `RUNNING`,
  and that permission is recomputed from the motion engine's state on every
  superloop iteration — never cached.
- Both drop on **every** fault class, including the two ADR-010 exempts
  from dropping `EN`.
- Both drop from the **E-STOP interrupt**, not from the superloop, because
  on many machines the relay is the spindle contactor.
- An emergency stop *discards* pending requests rather than withholding
  them, so clearing it cannot restart anything by itself.

The two status LEDs now report the ADR-010 state model, which on a board
with no display is frequently the only diagnosis available during
bring-up.

None of this has been proved on silicon. `Docs/HARDWARE-VALIDATION.md` §5e
lists what a board has to show, and Rule 10 stands.

---

## 2. Decisions, and where each came from

§21 required an Output Manager separated from the STEP/DIR path; §22
required the spindle to be a separate subsystem again and *explicitly*
delegated its frequency, duty range, scaling, command source and update
mechanism. ADR-016 records the reasoning; the short version:

| Decision | Why, in one line |
|---|---|
| One interlock, recomputed every poll | An output must not outlive the condition that permitted it, whatever raced with what |
| Every fault inhibits, including the hold-position ones | ADR-010's exemption is about holding force; a spinning tool is not holding force |
| Off is reached from the interrupt | The relay may be the spindle contactor; one superloop iteration is too long |
| No RPM scaling — per mille is `Spindle.ratio × 1000` | `[SDK-CONFIRMED]` from `ncPod`; only the host knows what spindle is fitted |
| `PSC=0`/`ARR=8399` instead of the `.ioc`'s `83`/`99` | Same exact 10 kHz, 8400 duty steps instead of 100 |

Three values in that ADR are **assumptions or defaults, not measurements**:
`CNC_LED_ACTIVE_HIGH`, and the 500 ms / 125 ms blink periods. HV-55
confirms the first.

---

## 3. What was built

```text
Firmware/
  Core/Inc/cnc_io_config.h                  pin map, polarity, PWM, tunables
  IO/Inc/io_types.h                         the published snapshot
  IO/Inc/io_outputs.h                       M9 + the subsystem entry points
  IO/Inc/io_spindle.h                       M10
  IO/Inc/io_port.h                          the hardware abstraction
  IO/Src/io_outputs.c                       interlock, relay, LED patterns
  IO/Src/io_spindle.c                       per-mille -> compare arithmetic
  Platform/STM32F407/Inc/io_hw_map.h        GPIOB, TIM3, AF2
  Platform/STM32F407/Inc/io_selftest.h      HV-50..HV-53
  Platform/STM32F407/Src/io_port_stm32f4.c
  Platform/STM32F407/Src/io_selftest_stm32f4.c
  Platform/Host/Src/io_sim.h                bench readouts
  Platform/Host/Src/io_port_sim.c           simulated GPIOB + TIM3
  Tests/test_io.c                           1160 checks
```

`IO/` is pure C with no hardware dependency, like `Motion/`, `Net/` and
`Safety/`. It reaches the motion engine through `stepgen.h` and the input
manager through `safety_input.h`, so the dependencies run IO → Safety →
Motion and never back.

### 3.1 The two paths

```text
host ──OUTPUTS──► io_request_outputs() ─┐
                  io_spindle_set_pmille()├─► (recorded, not yet applied)
                                         │
superloop ──────► io_poll() ─────────────┘
                     ├─ read the engine state, recompute the inhibit
                     ├─ relay  PB8, spindle CCR, LEDs PB2/PB1
                     └─ never caches a decision

PE2 ──EXTI2, prio 0──► stepgen_emergency_stop()      ← axes first
                       io_emergency_off()            ← relay + PWM, same ISR
```

### 3.2 Changes to existing code

| File | Change |
|---|---|
| `Safety/Inc/safety_input.h`, `Src/safety_input.c` | `safety_set_estop_action()` / `safety_has_estop_action()`; the E-STOP ISR and the poll-side assert both invoke it right after stopping the motion engine |
| `Net/Src/cnc_session.c` | `handle_outputs()` replaces the `NOT_IMPLEMENTED` stub; `flags.6`, `STATUS.outputs` and `STATUS.spindle_pmille` |
| `Core/Src/main.c` | `io_init()` after `safety_input_init()` (ordering matters — see below); `io_poll()` in the superloop; HV-50..HV-53 under `CNC_RUN_SELFTEST_AT_BOOT` |
| `Makefile`, `.cproject` | `make test-io`; `IO` in the ARM, firmware and CubeIDE builds |

**Ordering:** `safety_input_init()` clears its own state including any
registered E-STOP action, so `io_init()` must run *after* it. Getting that
backwards leaves the relay closed until the next `io_poll()`. It is
commented at the call site in `main.c` and checked by HV-52.

---

## 4. Safety behaviour, stated precisely

1. **Nothing energises below `READY`.** Reaching `READY` takes an explicit
   `CONTROL:ENABLE_DRIVES`, and that deliberate act is when the machine
   becomes live (§32).
2. **Every fault drops the outputs**, including `COMM_TIMEOUT` and buffer
   underflow — the two where ADR-010 keeps `EN` asserted. Drives hold the
   axes; the work stops.
3. **An E-STOP de-energises from the interrupt**, in the same handler that
   stops the axes, motion first.
4. **Clearing a fault or an E-stop restores nothing.** Requests are
   discarded, not withheld. The host must ask again.
5. **A request is refused whole or taken whole.** A packet that sets the
   relay and an out-of-range duty changes neither.
6. **Out-of-range duty is refused, not clamped.**
7. **The LEDs are not host-commandable.** They report the engine state, so
   they cannot be made to lie.

---

## 5. Build and test results

```text
make test        1150 motion + 151 input + 1160 output + 130 network
                 + 347 protocol checks, 0 failures
make arm         clean under -Wall -Wextra -Werror -Wconversion ...
make firmware    text 63892  data 132  bss 45592   (ELF links and fits)
```

Cost of M9 + M10 against the same build without them:

| | Before | After | Delta |
|---|---|---|---|
| `.text` | 62 748 | 63 892 | **+1 144 B** |
| `.bss` | 45 568 | 45 592 | **+24 B** |

The CubeIDE Debug and Release configurations were also re-checked by
driving a build from `.cproject`'s own include paths, defines, source
folders and linker script: 128 translation units, 0 failed, links.

### 5.1 A real bug the host suite caught

The first version clamped the spindle compare register to `ARR`. In PWM
mode 1 the output is active while `CNT < CCR` and `CNT` counts `0..ARR`, so
`CCR == ARR` leaves **one inactive count in every period** — 99.99 % duty,
with a 12 ns notch every 100 µs that a VFD input is entitled to read as an
edge. True 100 % needs `CCR == ARR+1`.

The test that found it asks for 1000 per mille and checks the compare value
against the period. Both ports were wrong in the same way, which is exactly
why the simulation port models the ceiling rather than assuming it. HV-54
confirms it on the pin.

### 5.2 What the host suite establishes

| Property | Checked by |
|---|---|
| Nothing is energised at boot, or below `READY` | startup, interlock cases |
| A held request comes on the moment the machine is armed | interlock cases |
| Every fault class drops relay and spindle together | interlock cases |
| Clearing a fault does not spin the tool back up | interlock cases |
| E-STOP drops both with **zero** superloop iterations elapsed | E-STOP cases |
| The relay makes exactly one transition across a stop (no glitch) | E-STOP cases |
| An E-STOP already down at power-on leaves the outputs off | E-STOP cases |
| Per-mille → compare across the whole range, every step distinct | spindle cases |
| 0 stops the generator; 1000 is genuine 100 % | spindle cases |
| Out-of-range refused, reserved output bits refused, nothing partial | API cases |
| LEDs follow the ADR-010 states and actually toggle | LED cases |
| `OUTPUTS` end to end, and `STATUS` reporting pins not requests | `test_protocol.c` |

### 5.3 What it does not establish

Nothing about GPIOB, TIM3, the AF2 mapping, or what a VFD makes of the
waveform. The host suite simulates a timer; it cannot simulate being wrong
about one.

---

## 6. Risks and unresolved items

| # | Item | Status |
|---|---|---|
| RISK-5a | ADR-016 decision 2 extends an ADR-010 clause the owner confirmed for `EN` only. It is the safe direction, but it is inference | Open — owner confirmation |
| RISK-5b | `CNC_LED_ACTIVE_HIGH` is read from the CubeMX reset state, not from a drive circuit — none is documented here | Open — HV-55 |
| RISK-5c | Blink periods are defaults, not measurements | Accepted |
| RISK-5d | `PB4` is `NJTRST` at reset; this works only because the project debugs over SWD (§29). Re-enabling JTAG silently takes the spindle pin | Documented; HV-51 catches it |
| RISK-5e | The `.ioc` still carries `PSC=83`/`ARR=99`. The port overrides it at boot, so a regeneration cannot break it — but the two now disagree on paper | Documented — ADR-016, HV-51 |
| RISK-5f | Spindle direction (`M4`) has no pin and no wire format. This is a hardware question | Open — `Docs/PROTOCOL.md` §10 |
| RISK-5g | Unverified on hardware | Open — HV-50..HV-55 |

---

## 7. What the plugin must know

1. **An accepted `OUTPUTS` is not a switched relay.** Drive Mach3's output
   LEDs from `STATUS.outputs`, never from what was sent.
2. **`spindle_pmille` is `MainPlanner->Spindle.ratio × 1000`.** No RPM
   crosses the wire; the plugin keeps the RPM↔ratio map.
3. **After an E-stop, re-send.** Pending requests are discarded.
4. **Out-of-range duty draws `BAD_PARAM`, not a clamp**, and nothing else
   in that packet is applied either.
5. **Spindle direction is unassigned.** `M4` needs a hardware decision
   first.

---

## 8. Phase 5 exit criteria

| Criterion | State |
|---|---|
| Relay driven with the documented polarity, off at boot and in every fault | Done |
| Spindle PWM at the documented frequency, with duty resolvable to the wire format | Done |
| Outputs separated from the STEP/DIR path (§21) | Done — no shared peripheral, no shared interrupt |
| Spindle a separate subsystem from the pulse generator (§22) | Done — TIM3, no DMA |
| Frequency, duty range, scaling, command source and update mechanism derived and recorded (§22) | Done — ADR-016 |
| E-STOP de-energises without waiting for the superloop | Done |
| Host told what the pins do, distinguishably from "no data" | Done — `flags.6` |
| Host verification | Done — 1160 checks |
| Hardware verification | **Not done** — HV-50..HV-55 |
