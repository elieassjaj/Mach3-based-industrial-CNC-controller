# Motion Subsystem (Phase 1)

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
make test     # host verification (1131 checks)
make bench    # algorithm cost proxy - NOT an STM32 CPU-load figure
make arm      # cross-compile the engine + STM32F407 port for Cortex-M4F
```

`make arm` uses the project's own `Drivers/CMSIS`. In a bare checkout or
CI, run `tools/fetch_cmsis.sh` and then `make arm CMSIS_DIR=.cmsis`.

`make arm` is a compile check of the motion subsystem. The flashable image
is built by STM32CubeIDE.

---

## READ FIRST: the `.ioc` and the firmware currently disagree

`CNC5AX-ETH.ioc` configures `TIM2_UP → DMA1_Stream1 → GPIOA->BSRR`, per
ADR-004/ADR-005. Verified against RM0090 Rev 22, **DMA1 cannot write a
GPIO register at all** — its peripheral port is not a bus-matrix master
(§2.1, Figure 33 note 1), and GPIOA is only reachable through the matrix.
That configuration produces no STEP pulses.

The motion firmware therefore uses `TIM8_UP → DMA2_Stream1 Ch7`
(RM0090 Table 44), which is the only mapping that can work while keeping
the 4 MHz tick exact. This is raised as **ADR-012** and needs a project
decision; see `../Docs/PHASE1-STATUS.md` §2.

Until the `.ioc` is updated, the generated peripheral init and the motion
port describe different hardware.

---

## Integrating with the CubeIDE project

1. Add `Core/Inc`, `Motion/Inc`, `Platform/STM32F407/Inc` to the include
   paths, and `Motion/Src` + `Platform/STM32F407/Src` to the sources.
2. Apply `Platform/STM32F407/linker/stepgen_sram2.ld` to the generated
   linker script. Without it the DMA ring falls back into SRAM1 and the
   Ethernet isolation silently disappears — test HV-03 catches this.
3. Compile with `-DSTEPGEN_BUFFERS_SECTION='".stepgen_ram"'`.
4. Route `DMA2_Stream1_IRQHandler` to the handler in
   `Platform/STM32F407/Inc/stepgen_port_stm32f4.h`.
5. Leave TIM8 and DMA2 Stream 1 out of CubeMX's own init — the port
   configures them directly, as ADR-004 anticipated for the BSRR
   redirection.
6. Do **not** enable the base timer's global interrupt: at the 4 MHz tick
   it would fire 4,000,000 times a second (ADR-004).

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

## Before running this on a machine

- Resolve ADR-012 and update the `.ioc`; otherwise there is no STEP output.
- Confirm the DIR sign convention per axis (`dir_invert`), which §25
  forbids assuming.
- Confirm ADR-005's pin move to PA8..PA12 matches the actual board; if
  `PC9` is routed to the X driver, that is a hardware change.
- Run `stepgen_selftest_run_all()` with the drives disconnected and check
  every result. HV-00 is the one that verifies the DMA path really works.
- Work through `../Docs/HARDWARE-VALIDATION.md` with the drives
  disconnected first.
