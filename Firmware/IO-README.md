# Outputs and Spindle > Phase 5 (M9 / M10)

The output subsystem added in Phase 5 lives in:

```text
Core/Inc/cnc_io_config.h   pin map, polarity, PWM parameters, tunables
IO/                        portable core - no STM32/HAL/CMSIS
Platform/STM32F407/        GPIOB + TIM3_CH1 port, HV-50..HV-53 self-tests
Platform/Host/             simulated GPIOB + TIM3
Tests/test_io.c            host verification
```

`IO/` is pure C with no hardware dependency, like `Motion/`, `Net/` and
`Safety/`. It reaches the motion engine through `stepgen.h` and the input
manager through `safety_input.h`, so the dependencies run IO → Safety →
Motion and never back.

See `../Docs/PHASE5-STATUS.md` for results and risks, ADR-016 in
`../Docs/FIRMWARE-ARCHITECTURE.md` for the decisions, and
`../Docs/HARDWARE-VALIDATION.md` §5e for what a board still has to show.

---

## Building

```sh
make test-io   # 1160 host checks
make test      # everything
make arm       # cross-compile the core + STM32F407 port
```

---

## Using it

```c
#include "io_outputs.h"
#include "io_spindle.h"

io_init();                            /* AFTER safety_input_init() */

while (1) {
    io_poll(HAL_GetTick());
}

io_request_outputs(CNC_OUT_BIT_RELAY, CNC_OUT_BIT_RELAY);   /* relay on */
io_spindle_set_pmille(500);                                 /* 50 % duty */
```

**`io_init()` must run after `safety_input_init()`.** It registers the
interrupt-time kill with the E-STOP path, and `safety_input_init()` clears
that registration as part of its own reset. Getting the order backwards
leaves the relay closed until the next `io_poll()` — on a machine whose
relay is the spindle contactor, that is the difference between a safe stop
and a dangerous one. HV-52 is the test that catches it.

---

## A request is not a guarantee

Both calls above return `true` for "the request was taken", **not** "the
pin moved". The interlock decides separately, and `io_poll()` re-reads the
motion engine's state to do so on every iteration:

| Engine state | Outputs |
|---|---|
| `UNINIT`, `SAFE_IDLE` | held off — `IO_INHIBIT_NOT_READY` |
| `READY`, `RUNNING` | permitted |
| `FAULT` (**any** class) | off — `IO_INHIBIT_FAULT` |
| `EMERGENCY_STOP` | off — `IO_INHIBIT_ESTOP`, and the request is discarded |

Read what actually happened:

```c
io_status_t st;
io_get_status(&st);
/* st.out_requested vs st.out_actual, st.inhibit, st.inhibited_count */
```

That difference is the single most useful thing the subsystem can tell an
operator wondering why the relay did not click, and it is what
`STATUS.outputs` carries to the host.

Note that **every** fault drops the outputs, including `COMM_TIMEOUT` and
buffer underflow — the two where ADR-010 deliberately keeps `EN` asserted.
That exemption exists so an axis does not drop under gravity; it is about
holding force, and a spinning tool is not holding force (ADR-016).

---

## Spindle duty

`spindle_pmille` is 0…1000 and is a direct quantisation of the Mach3 SDK's
own `MainPlanner->Spindle.ratio` (0…1). **No RPM reaches this device** —
the host owns the RPM↔ratio map, because only it knows what spindle is
fitted.

| Value | Meaning |
|---|---|
| `0` | 0 %, and the PWM generator is **stopped**, not left running at zero |
| `1`…`999` | that per mille, resolved exactly (8400 counts per period) |
| `1000` | genuine 100 % — a constant high, no notch |
| `> 1000` | **refused**, never clamped |
| `0xFFFF` (wire only) | leave unchanged; resolved by the session layer |

The compare register is preloaded, so a duty change lands at a period
boundary and never produces a short or stretched pulse.

---

## PWM period: refined from the `.ioc`

The `.ioc` carries `PSC=83`/`ARR=99`. This port programs `PSC=0`/`ARR=8399`:

| | `.ioc` | this port |
|---|---|---|
| Frequency | 10.000 kHz | 10.000 kHz |
| Counts per period | 100 | 8400 |
| Duty steps the wire can express | 1000 | 1000 |
| Duty steps actually resolvable | **100** | 8400 |

With the `.ioc` values a host asking for 12.3 % would silently get 12 %.
The frequency is the only spindle number `Docs/PINOUT.md` fixes and it is
unchanged. The port re-applies its own configuration at boot, so a CubeMX
regeneration cannot revert it — and HV-51 reads the divider back from the
hardware and reports the actual frequency in Hz.

---

## Status LEDs

Not host-commandable, by design: a host-controlled "error" light can be
made to lie, and on a board with no display this pair is often the only
thing that says anything at all.

| Engine state | Run LED (`PB2`) | Error LED (`PB1`) |
|---|---|---|
| `SAFE_IDLE` | off | off |
| `READY` | blink, 500 ms | off |
| `RUNNING` | **on** | off |
| `FAULT` | off | blink, 125 ms |
| `EMERGENCY_STOP` | off | **on, solid** |

Solid versus blinking on the error LED is deliberate: one is cleared with a
command, the other needs somebody to release a physical button first.

---

## CubeIDE project settings: already in `.cproject`

Nothing to do — `IO/Inc` and the `IO` source folder are registered in both
the Debug and Release configurations, alongside `Motion`, `Net`, `Safety`
and `Platform/STM32F407`.

---

## Before running this on a machine

- **Wire nothing dangerous to the relay until HV-55 has passed.** A meter
  or a test lamp.
- Run `io_selftest_run_all()` and check every result. None of HV-50..HV-53
  closes the relay or turns the spindle — that is deliberate, and it is why
  HV-54/HV-55 exist and are done on a bench.
- Confirm `CNC_LED_ACTIVE_HIGH`. It is read from the CubeMX reset state,
  not from a drive circuit — no schematic for the LED stage exists in this
  repository. If the LEDs are inverted, flip that one constant.
- Scope the spindle at 1000 per mille specifically. 100 % duty is where a
  plausible implementation leaves a notch; this one is tested not to, in
  simulation, and HV-54 is the confirmation on the pin.
- Remember `PB4` is `NJTRST`. This pin is only free because the project
  debugs over SWD.
