# Prototype test board

`prototype_sch.pdf` is the schematic of the **bench prototype** used to
bring the firmware up before the final PCB. **It is not the product.**

- Ethernet is the Waveshare **LAN8720 ETH Board module** on a 2×7 header.
  The final PCB carries the LAN8720A IC directly, with `nRST` on `PB0`.
- Motor drive is one StepStick-style driver socket, with the axis selected
  by jumpers. The final PCB drives external industrial drives.
- Only `PE2` (E-STOP), `PE3` and `PE4` have switches.

**Read [`../Docs/PROTOTYPE-BOARD.md`](../Docs/PROTOTYPE-BOARD.md) before
building or powering this board.** It records the pin-by-pin check against
`Docs/PINOUT.md` and the findings. Four of them stop the board from working
as drawn:

| | Finding |
|---|---|
| **PR-1** | The crystal is 16 MHz, but the firmware assumes 8 MHz. The PLL would run out of spec, so **do not flash the current firmware as it is** |
| **PR-2** | The driver socket's `EN` is active low; the project's `EN` is active high |
| **PR-3** | The driver socket's RESET and SLEEP float |
| **PR-4** | The 250 ns STEP pulse is too short for a StepStick; use `stepgen_configure_max_rate()` |

If you fit a driver module, add its datasheet to this folder. The driver
figures in the review come from datasheets that are not in the repository.
