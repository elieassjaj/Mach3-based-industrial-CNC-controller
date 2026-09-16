# Motion Subsystem > Phase 1

This directory is the STM32CubeIDE project. The motion subsystem added in
Phase 1 lives alongside the generated code in:

```text
Core/Inc/cnc_motion_config.h   requirements, pin map, tunables
Motion/                        portable engine - no STM32/HAL/CMSIS
Platform/STM32F407/            TIM8 + DMA2 + GPIO BSRR port, self-tests
Platform/Host/                 simulation port + virtual logic analyser
Tests/                         host verification and benchmark
tools/fetch_cmsis.sh           standalone CMSIS for CI / bare checkouts
```

`Motion/` is pure C with no hardware dependency: that is what lets the
waveform be verified on a host, and what keeps the Phase 2/3 network and
protocol layers independent of the motion engine.

See `../Docs/PHASE1-STATUS.md` for results, assumptions and blockers, and
`../Docs/HARDWARE-VALIDATION.md` for the bring-up procedure.

---

## Building

```sh
make test     # host verification (1150 checks)
make bench    # algorithm cost proxy - NOT an STM32 CPU-load figure
make arm      # cross-compile the engine + STM32F407 port for Cortex-M4F
```

`make arm` uses the project's own `Drivers/CMSIS`. In a bare checkout or
CI, run `tools/fetch_cmsis.sh` and then `make arm CMSIS_DIR=.cmsis`.

`make arm` is a compile check of the motion subsystem. The flashable image
is built by STM32CubeIDE.

---

## Integration status: done

The subsystem is wired into the generated project and the full firmware
links (84 KB flash, SRAM1 36.7%, SRAM2 25%).

| Piece | State |
|---|---|
| `.ioc` | `TIM8_UP` → `DMA2_Stream1` Ch7, direct mode, word width, circular, very high priority, `NVIC.DMA2_Stream1_IRQn` preempt 2 (ADR-012) |
| `Core/Src/stm32f4xx_it.c` | `DMA2_Stream1_IRQHandler` calls `stepgen_dma_isr()` from its `USER CODE` block and returns before `HAL_DMA_IRQHandler()` |
| `STM32F407VGTX_FLASH.ld` | `RAM` split into SRAM1 112 K + `SRAM2` 16 K, with `.stepgen_ram (NOLOAD)` in SRAM2 |
| `Core/Src/main.c` | `stepgen_init()` in `USER CODE BEGIN 2`, after `MX_TIM8_Init()` |
| Regeneration-safe | Every edit is inside `USER CODE` markers; `Motion/` and `Platform/` sit outside `Core/`, which CubeMX never touches |

Two things to be aware of when rebuilding in CubeIDE:

- **Do not restore `HAL_DMA_IRQHandler(&hdma_tim8_up)`** in the DMA2
  Stream1 vector. The stream is configured at register level, not through
  the HAL handle, so HAL would clear transfer flags and fire callbacks
  behind the engine's back.
- **Do not enable TIM8's global interrupt.** At the 4 MHz tick it would
  fire four million times a second (ADR-004).

---

## Tuning CPU cost to your machine

The refill cost follows the **base tick**, not the commanded speed: at a
4 MHz tick the engine costs the same whether an axis runs at 2 MHz or at
100 Hz. If this machine's maximum STEP rate is below the 2 MHz project
ceiling, say so in `main.c` and get the CPU back:

```c
stepgen_configure_max_rate(1000000u);   /* 1 MHz ceiling -> 2 MHz tick */
```

| Configured max | Base tick | Relative refill cost |
|---|---|---|
| 2 MHz | 4 MHz | 1.00 |
| 1 MHz | 2 MHz | 0.50 |
| 500 kHz | 1 MHz | 0.25 |

Only exact integer dividers of the 168 MHz timer clock are accepted, so no
feed rate picks up a systematic divider error. Call it while the engine is
in `SAFE_IDLE`, before `stepgen_enable_drives()`.

---

## Using the engine

```c
#include "stepgen.h"

stepgen_init();            /* -> SAFE_IDLE, drives disabled   */
stepgen_enable_drives();   /* -> READY                        */
stepgen_start();           /* -> RUNNING                      */

motion_segment_t seg = {0};
seg.duration_ticks      = 4000;                            /* 1 ms @ 4 MHz */
seg.rate[MOTION_AXIS_X] = stepgen_rate_from_hz(2000000.0); /* 2 MHz, + dir */
seg.rate[MOTION_AXIS_Y] = stepgen_rate_from_hz(-500000.0); /* 500 kHz, -   */
stepgen_submit_segment(&seg);
```

Keep the queue fed: `stepgen_queue_free()` is the backpressure input for
the protocol layer. A refused push is normal flow control, not an error —
it also happens while the engine is waiting for a pending DIR reversal to
be written.

`stepgen_emergency_stop()` is interrupt-safe and does register writes
only, so the PE2 E-STOP handler can call it directly.

Fault recovery is never automatic (ADR-010): `stepgen_clear_fault()` for a
`FAULT`, and `stepgen_clear_emergency_stop()` for an E-stop — the former
cannot clear the latter.

---

## CubeIDE project settings still needed

The `.ioc` does not carry source folders or compiler flags, so set these
once in the project properties:

- Add `Motion/Inc` and `Platform/STM32F407/Inc` to the include paths, and
  `Motion/Src` + `Platform/STM32F407/Src` to the source locations.
- Add the symbol `STEPGEN_BUFFERS_SECTION="\".stepgen_ram\""`. Without it
  the ring falls back into SRAM1 and the Ethernet bus isolation quietly
  disappears — test HV-03 catches that at runtime.

---

## Before running this on a machine

- Confirm the DIR sign convention per axis (`dir_invert`), which §25
  forbids assuming.
- Confirm ADR-005's pin move to PA8..PA12 matches the actual board; if
  `PC9` is routed to the X driver, that is a hardware change.
- Run `stepgen_selftest_run_all()` with the drives disconnected and check
  every result. HV-00 is the one that verifies the DMA path really works.
- Work through `../Docs/HARDWARE-VALIDATION.md` with the drives
  disconnected first.
