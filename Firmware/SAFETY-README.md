# Digital Inputs and E-STOP > Phase 4 (M3)

The input subsystem added in Phase 4 lives in:

```text
Core/Inc/cnc_safety_config.h   pin map, debounce windows, tunables
Safety/                        portable core - no STM32/HAL/CMSIS
Platform/STM32F407/            GPIOE + EXTI port, HV-40..HV-43 self-tests
Platform/Host/                 simulated GPIOE + EXTI
Tests/test_safety.c            host verification
```

`Safety/` is pure C with no hardware dependency, like `Motion/` and `Net/`.
It reaches the motion engine only through `stepgen.h`, so the dependency
runs Safety → Motion and never back.

See `../Docs/PHASE4-STATUS.md` for results and risks, ADR-015 in
`../Docs/FIRMWARE-ARCHITECTURE.md` for the decisions, and
`../Docs/HARDWARE-VALIDATION.md` §5d for what a board still has to show.

---

## Building

```sh
make test-safety   # 151 host checks
make test          # everything
make arm           # cross-compile the core + STM32F407 port
```

---

## Using it

```c
#include "safety_input.h"

safety_input_init();                  /* after stepgen_init() */

while (1) {
    safety_input_poll(HAL_GetTick()); /* filter + release timer */
}

uint16_t in = safety_inputs();        /* bit n = PEn, 1 = ASSERTED */
```

`safety_inputs()` returns **logical assertion**, not pin level. The inputs
are active low; the firmware normalises that once so nothing above it has
to know the board's polarity (ADR-015). That word is exactly what
`STATUS.inputs` carries to the host.

`safety_input_init()` must run **after** `stepgen_init()`. It samples `PE2`
before arming any interrupt, so an E-STOP already held down at power-on is
honoured rather than waiting for an edge that already happened, and it
registers the release interlock with the engine.

---

## The two paths, and why they are not the same

| | E-STOP (`PE2`) | The other 14 |
|---|---|---|
| Vector | `EXTI2`, its own | five shared vectors |
| NVIC priority | 0 | 1 |
| Assertion | acted on **in the ISR**, unfiltered | captured only |
| Release | filtered, 50 ms default | filtered, 3 ms default |
| Meaning | fixed by `Docs/PINOUT.md` | **none** — host-side |

Assert fast, release slow. Filtering an assertion would add latency to the
one path `Docs/FIRMWARE-ARCHITECTURE.md` §8 requires to be the fastest in
the system; not filtering the release would let a bouncing contact present
itself as released and unlock a machine.

---

## Tuning the windows

```c
/* cnc_safety_config.h, or -D on the command line */
#define SAFETY_DEBOUNCE_MS          3    /* the 14 ordinary inputs   */
#define SAFETY_ESTOP_RELEASE_MS     50   /* PE2 release only         */
#define SAFETY_CHATTER_EDGES_PER_S  200  /* diagnostics threshold    */
```

**These are documented defaults, not measurements.** No switch datasheet
exists in this repository. HV-45 measures the real bounce on the machine's
own switches and replaces them; until it has run, do not describe them as
validated.

The filter runs on the 1 ms `HAL_GetTick()` timebase, so the effective
window is `[T, T+1]` ms. That is fine at 3 ms and would not be at 1 ms.

---

## CubeIDE project settings: already in `.cproject`

Nothing to do. The project's own folders are registered in **both** the
Debug and Release configurations:

| Setting | Value |
|---|---|
| Include paths | `../Motion/Inc`, `../Net/Inc`, `../Safety/Inc`, `../Platform/STM32F407/Inc` |
| Source folders | `Motion`, `Net`, `Safety`, `Platform/STM32F407` |

`Platform/Host` is deliberately **not** a source folder: it holds the
simulation ports, and compiling them for the target would collide with the
STM32 ports symbol for symbol.

The `.ioc` carries none of this — CubeMX does not manage source folders —
so a CubeMX regeneration cannot remove it either. What it can do is rewrite
`Core/`; see below.

The `.ioc` itself needs no change either: `PE0`–`PE14` are already configured as
`GPIO_MODE_IT_RISING_FALLING` with `GPIO_NOPULL`, and the NVIC entries
already carry ADR-004's priorities. The port re-applies all of it at boot
anyway, so a pin dropped by a future regeneration fails HV-41 rather than
becoming a silently dead input.

Do **not** restore `HAL_GPIO_EXTI_IRQHandler()` in the six input vectors,
for the same reason the STEP-DMA vector does not run `HAL_DMA_IRQHandler()`:
the lines are serviced at register level, and the HAL path would clear
pending bits and dispatch callbacks behind this module's back.

---

## Before running this on a machine

- Run `safety_selftest_run_all()` and check every result. HV-40 is the one
  that says whether the E-STOP release interlock is actually registered on
  this build; HV-42 catches a line stuck low before it becomes "the machine
  will not start".
- Confirm on a scope that pressing E-STOP drops STEP and `EN` while five
  axes run at 2 MHz and the link is flooded — that independence is the
  whole claim, and it is HV-18. It has not been run.
- Check `safety_input_get_status().chattering` after exercising the
  machine's switches. These vectors pre-empt the STEP ring refill, so a
  worn contact is not only a reporting problem.
- Nothing here decides which pin is a limit or a home. If the machine needs
  that, it belongs in the PC-side plugin, not in this module.
