# CNC5AX-ETH — Prototype Test Board

## 1. Purpose and status

`prototype_test/prototype_sch.pdf` is a **bench prototype**, not the
product. It exists to bring the firmware up on real silicon before the
final PCB is laid out.

| | Prototype (this document) | Final PCB |
|---|---|---|
| Ethernet PHY | Waveshare **LAN8720 ETH Board** module on a 2×7 header | LAN8720A **IC on the board** |
| PHY reset | The module resets itself with its own RC network. `PB0` is **not connected** to it | `nRST` wired to `PB0` |
| Motor drive | One StepStick-style driver socket, axis selected with jumpers | External industrial drives |
| Digital inputs | Only `PE2` (E-STOP), `PE3` and `PE4` have switches; the other twelve are tied to one pull-up | Per-input conditioning |
| Relay, spindle | Routed to the MCU pins only, with no connector | Real outputs |

`Docs/PINOUT.md` stays the authority for pin assignment on **both** boards.
This document records how the prototype maps onto it, what was checked,
and what the firmware must be told to run on it.

The schematic was checked sheet by sheet against `Docs/PINOUT.md`, the
LAN8720 module schematic (`LAN8720A/LAN8720-ETH-Board-Schematic.pdf`), and
the STM32F407 and LAN8720A datasheets in this repository. The file is a
"Print to PDF", so it carries no netlist: every connection below was read
from the drawing. The items marked **verify in the netlist** are the ones
a drawing cannot settle.

---

## 2. Connection check against `Docs/PINOUT.md`

The MCU symbol was also checked pin by pin against the LQFP100 column of
the STM32F407 datasheet (Table 7). **Every pin that carries a net has the
right package pin number. There are no symbol errors.**

| Function | PINOUT.md | Prototype | Result |
|---|---|---|---|
| STEP X, Y, Z, A, B | `PA8`–`PA12` | `STEPX`–`STEPB` on `PA8`–`PA12` | ✅ |
| DIR X, Y, Z, A, B | `PD8`–`PD12` | `DIRX`–`DIRB` on `PD8`–`PD12` | ✅ |
| Drive enable | `PD15`, active **high** | `EN` on `PD15` → driver socket pin 1 | ⚠️ pin correct, **polarity inverted at the driver** — PR-2 |
| Spindle PWM | `PB4` (TIM3_CH1) | `spindle` on `PB4` | ✅ pin; no connector — PR-9 |
| Relay | `PB8`, active high | `RELAY` on `PB8` | ✅ pin; no connector — PR-9 |
| **Run LED** | **`PB2`** | `run` on **`PB1`** | ❌ **swapped** — PR-6 |
| **Error LED** | **`PB1`** | `err` on **`PB2`** | ❌ **swapped** — PR-6 |
| E-STOP | `PE2`, active low | `E_STOP` on `PE2` → S2 to GND, R6 10k pull-up | ✅ |
| Inputs | `PE0`–`PE14`, active low, external pull-ups | `PE3`, `PE4` → S3, S4 with R7, R8 pull-ups; `PE0`, `PE1`, `PE5`–`PE14` tied together to one 10k pull-up (R5) | ✅ for a prototype — §4 |
| ETH MDC | `PC1` | `ETH_MDC` | ✅ |
| ETH REF_CLK | `PA1` | `ETH_CLK` | ✅ |
| ETH MDIO | `PA2` | `ETH_MDIO` | ✅ |
| ETH CRS_DV | `PA7` | `ETH_CRS` | ✅ |
| ETH RXD0 / RXD1 | `PC4` / `PC5` | `ETH_RXD0` / `ETH_RXD1` | ✅ |
| ETH TX_EN | `PB11` | `ETH_TX_EN` | ✅ |
| ETH TXD0 / TXD1 | `PB12` / `PB13` | `ETH_TXD0` / `ETH_TXD1` | ✅ |
| PHY reset | `PB0` | `ETH_nRST` on `PB0`, **goes nowhere** | ✅ correct for the module — §5 |
| SWD | `PA13` / `PA14` | `SWDIO` / `SWCLK` on P1 | ✅ |
| HSE crystal | **8 MHz** (firmware, `.ioc`, README) | **16 MHz** (Y1) | ❌ — PR-1 |

### 2.1 LAN header against the module

The prototype's `LAN` header matches the module's `P2` pin for pin:

| Pin | Signal | Pin | Signal |
|---|---|---|---|
| 1 | VCC (3.3 V) | 2 | VCC (3.3 V) |
| 3 | GND | 4 | GND |
| 5 | MDC | 6 | MDIO |
| 7 | CRS_DV | 8 | REF_CLK (module's 50 MHz) |
| 9 | RXD1 | 10 | RXD0 |
| 11 | TXD0 | 12 | TX_EN |
| 13 | **no connect** — on both boards | 14 | TXD1 |

The module has no regulator of its own, so its `VCC` must be 3.3 V. The
prototype supplies it from U2 (AMS1117-3.3). ✅

**Check at layout:** a 2-row header that mates face to face can swap rows
if the footprint is mirrored. The power pins would survive that, since
1/2 and 3/4 are pairs, but every signal would swap with its neighbour
(MDC↔MDIO, CRS_DV↔REF_CLK, and so on). Confirm pin 1 lands on pin 1.

---

## 3. Findings

Ordered by consequence. **PR-1 to PR-4 each stop the prototype from
working for its purpose.**

### 3.0 Status after owner review (2026-09-25)

| | Status |
|---|---|
| PR-1 | **Resolved at assembly.** The 16 MHz part was only the library symbol; an **8 MHz** crystal will be fitted, which matches the firmware. The schematic value and BOM should still say 8 MHz, especially if the board house does assembly, and C2/C3 should be sized for the 8 MHz part's load capacitance. |
| PR-2 | **Researched; see §3.1.** The code is correct against the project's own `EN` definition. Every common StepStick driver is active low, so on this socket the polarity is inverted. |
| PR-3 | **Owner will fix on the PCB.** |
| PR-4 | **Deferred to the test stage.** This is firmware configuration and does not affect the PCB. |

### 3.1 Enable polarity of common drivers

The manufacturer PDF hosts were not reachable from this environment, so
these come from datasheet quotations in search results, not from reading
the PDFs directly:

| Driver | Pin | Low | High | Floating |
|---|---|---|---|---|
| Allegro A4988 | `ENABLE` | outputs **on** | outputs off | carrier-dependent |
| TI DRV8825 | `nENBL` | outputs **on** | outputs off, STEP ignored | **on** (internal ~100 kΩ pull-down) |
| Trinamic TMC2208 / 2209 / 2225 / 2226 | `ENN` | outputs **on** | power stage off, outputs float | carrier-dependent |
| LV8729 StepStick modules | `EN` | sold as drop-in A4988/DRV8825 replacements; not confirmed from a primary source | | |
| Leadshine DM542 (industrial) | `ENA+/ENA−` (opto) | opto **on** → drive **disabled** | opto off → enabled | **enabled** |

So **all common StepStick drivers are active low**, and the firmware's
active-high `EN` is inverted on this socket. That is a firmware problem,
not a PCB problem, provided the firmware honours `CNC_EN_ACTIVE_HIGH`
(today nothing reads it). The PCB is still involved in one respect: while
the MCU is held in reset, `PD15` floats, and a DRV8825 reads a floating
pin as **enabled**. Startup safety (§32) therefore calls for a **10 kΩ
pull-up on the socket's `EN` net**, so the driver is off until the
firmware decides otherwise.

The final product's industrial drives also make polarity a wiring
question. A DM542's opto input disables the drive when current flows,
so whether "MCU high" means enabled depends on whether `ENA+` or `ENA−`
is driven. A floating input on those drives also reads as **enabled**.
The final PCB's `EN` interface should hold the drives disabled while the
MCU is in reset.

### PR-1 — The crystal is 16 MHz; the firmware assumes 8 MHz ❌ critical

`SystemClock_Config()`, `HSE_VALUE` and the `.ioc` all assume an 8 MHz HSE
(`PLLM=4`, `PLLN=168`, `PLLP=2`). With a 16 MHz crystal the same settings
give:

| | Required (RM0090, RCC_PLLCFGR) | With 16 MHz + `PLLM=4` |
|---|---|---|
| VCO input | 1–2 MHz | **4 MHz** |
| VCO output | 100–432 MHz | **672 MHz** |
| SYSCLK | ≤ 168 MHz | **336 MHz** |

All three are out of specification. Expect the board to hang in
`Error_Handler()` when the PLL fails to lock, or to run unstably if it does
lock. Every timer rate in the project is derived from SYSCLK, so none of
the motion timing would be right either way.

**Do not flash the current firmware onto this board as it is.** Choose one:

- **Fit an 8 MHz crystal.** This matches the firmware, the `.ioc` and
  `README.md`, and needs no firmware change.
- **Keep 16 MHz.** Change `HSE_VALUE` to `16000000`
  (`Core/Inc/stm32f4xx_hal_conf.h` and `RCC.HSE_VALUE` in the `.ioc`) and
  `PLLM` to `8` (`RCC.PLLM` in the `.ioc` and `SystemClock_Config()`). The
  VCO stays at 336 MHz, so every derived clock stays exactly as it is now.
  This is the right choice if the final PCB will also use 16 MHz.

Separately, C2/C3 are marked "15-20pF", which is a range, not a value.
Size them from the crystal's specified load capacitance.

### PR-2 — `EN` reaches the driver inverted ❌ high

`Docs/PINOUT.md` defines `EN` as **active high** (high = drives enabled),
and the firmware drives `PD15` low at boot, on every integrity fault and on
E-STOP. The socket is a StepStick pinout, and on the common StepStick
drivers (A4988, DRV8825) `ENABLE` is **active low**: low turns the outputs
**on**.

So on this board:

- At boot and on E-STOP, when the firmware means "drives off", the driver
  is **energised**. STEP pulses still stop, so the motor holds rather than
  moves.
- When the firmware enables the drives, the driver is **switched off**, and
  commanded motion goes nowhere.

`CNC_EN_ACTIVE_HIGH` in `cnc_motion_config.h` looks like the switch for
this, but **nothing reads it**: `stepgen_port_stm32f4.c` hard-codes
high = enabled in three places. Changing the macro changes nothing today.

Options, in order of preference:

1. Make the port honour `CNC_EN_ACTIVE_HIGH`, then build the prototype with
   it set to `0`. This is a small firmware change, and it removes a
   configuration switch that currently does nothing.
2. Put an inverter between `PD15` and socket pin 1 on the prototype.
3. Tie socket pin 1 to GND so the driver is always enabled. This is the
   simplest option, but E-STOP then no longer de-energises the motor.

*(The driver polarities come from the A4988 and DRV8825 datasheets, which
are not in this repository. Add the datasheet of the module you actually
fit to `prototype_test/`.)*

### PR-3 — Driver socket RESET and SLEEP float ❌ high

Socket pins 5 (`RESET`) and 6 (`SLEEP`) are drawn as bare stubs. Both are
active-low. On the DRV8825 both have internal pull-downs, so a floating
pin holds the driver in reset or sleep. On Pololu-style A4988 carriers
`RESET` floats, and the vendor's documented practice is to tie it to
`SLEEP`.

**Tie pins 5 and 6 together and to VCC.** That works with both drivers.

### PR-4 — The STEP pulse is shorter than the driver can see ❌ high

The motion engine emits a one-tick pulse: 250 ns at the default 4 MHz
tick, sized for the 2 MHz industrial requirement. StepStick drivers need a
much wider pulse:

| Driver | Minimum STEP high / low | Maximum step rate |
|---|---|---|
| A4988 | 1 µs / 1 µs | — |
| DRV8825 | 1.9 µs / 1.9 µs | 250 kHz |

A 250 ns pulse will be missed or will count unreliably. **No hardware
change is needed.** The engine already has the lever:

```c
/* main.c, after stepgen_init(), prototype build only */
stepgen_configure_max_rate(200000u);   /* tick 400 kHz -> 2.5 us pulse */
```

A 400 kHz tick is an exact divider of the 168 MHz timer clock (420).
2.5 µs clears the DRV8825's 1.9 µs with margin, and the DIR guard becomes
7.5 µs, far above either driver's setup and hold time. The refill cost also
drops tenfold.

This means the 2 MHz tests (HV-10 to HV-12) are **scope tests at the MCU
pins with the driver removed**, at the default tick. The driver socket is
for functional motor tests only.

### PR-5 — `P5` does not pair each axis's STEP with its DIR ⚠️ medium

`P5` is the jumper field used to route one axis to the driver socket. Its
DIR labels run `DIRX`…`DIRB` from top to bottom, but its STEP labels run
`STEPB`…`STEPX`. They are drawn rotated 180°, which is what a group
rotation does, and that reverses the order. Only Z ends up with its STEP
and DIR on the same row:

| Row | Left pin | Right pin |
|---|---|---|
| 1 | 10 `STEPB` | 9 `DIRX` |
| 2 | 8 `STEPA` | 7 `DIRY` |
| 3 | 6 `STEPZ` | 5 `DIRZ` |
| 4 | 4 `STEPY` | 3 `DIRA` |
| 5 | 2 `STEPX` | 1 `DIRB` |

A two-wire jumper taken from one row drives the motor with one axis's
pulses and another axis's direction. **Re-place the STEP labels so that
each row reads `STEPn`/`DIRn`**, then **verify in the netlist**. A rotated
label connects only where its hotspot lands, and a printed PDF cannot show
where that is.

### PR-6 — Run and error LEDs are swapped ⚠️ medium

`PB2` carries `err` and `PB1` carries `run`. `Docs/PINOUT.md` and
`cnc_io_config.h` have it the other way round, with run on `PB2` and error
on `PB1`. As drawn, the prototype would show every E-STOP on the run LED.
**Swap the two labels in the schematic.** The pinout is the authority, and
the firmware already follows it.

The LEDs themselves are wired active high (pin → 10k → LED → GND). That
**confirms `CNC_LED_ACTIVE_HIGH = 1`** for this board, and closes
ADR-016's RISK-5b as far as the prototype is concerned.

### PR-7 — Regulator capacitors are missing ⚠️ medium

- The `+5` net has no capacitor at all, so there is nothing on U2's
  (AMS1117) input.
- U3 (LM7805) has nothing on its output. Its only input capacitance is
  C13 (100 µF), located at J1.
- The AMS1117 is an LDO. Its output capacitor (C6, 10 µF) must meet the
  datasheet's minimum capacitance and ESR for stability.

Add a capacitor on `+5` and on U3's output, and check C6 against the
regulator datasheets. Neither datasheet is in this repository, so the
exact values should come from the parts you fit.

### PR-8 — LEDs will be barely visible ⚠️ low

10 kΩ series resistors give about 0.1–0.15 mA. During bring-up these two
LEDs are the only indicator on the board. **Use about 1 kΩ**, which gives
roughly 1.3 mA.

### PR-9 — Relay and spindle have no test access ⚠️ low

`RELAY` (`PB8`), `spindle` (`PB4`) and `ETH_nRST` (`PB0`) each appear once
on the sheet, so each ends at the MCU pin. HV-54 (spindle waveform) and
HV-55 (relay behaviour) would then mean probing LQFP pins directly. **Add a
small header or test pads for `PB8` and `PB4`.**

### PR-10 — Minor

| Item | Recommendation |
|---|---|
| No 4.7 µF at a VDD pin. C6 sits at the regulator | Datasheet power-supply scheme: 100 nF per VDD **plus 1 × 4.7 µF at one VDD pin** |
| No dedicated decoupling on VDDA / VREF+ | Datasheet: **100 nF + 1 µF** on each. VDDA also supplies the PLL and the RC oscillators, not just the ADC |
| SWD header P1 has no NRST | Add it as pin 5 to allow connect-under-reset |
| C1 (NRST capacitor) reaches GND only through S1's internal pin 3–4 link | Route C1 straight to GND. A two-leg switch or footprint would leave it floating |
| U3 dissipates (12 − 5 V) × I | At about 0.2 A that is about 1.4 W in a TO-220 with no heatsink. Estimated, not measured; measure it or fit a small heatsink |
| C13 (100 µF) is the driver's only VMOT bulk capacitor | Place it next to the socket's VMOT/GND pins, not only at J1 |

---

## 4. Correct as drawn

| Item | Note |
|---|---|
| Twelve unused inputs on one pull-up | `PE0`, `PE1` and `PE5`–`PE14` are tied together to R5 (10k). They read "not asserted" permanently. That is safe because the firmware configures all fifteen as inputs, and HV-42 will pass. Only `PE2`, `PE3` and `PE4` can be exercised on this board |
| `PE15` unconnected | Correct; it is not a project input |
| E-STOP, `PE3`, `PE4` switches | Each pulls its line to GND when pressed, with a 10k pull-up, matching `GPIO_NOPULL` in firmware |
| BOOT0 | 10k to GND: boots from flash |
| NRST | 10k pull-up, 100 nF, push button to GND |
| VCAP_1 / VCAP_2 | 2.2 µF each to GND, separately, as the datasheet requires |
| VBAT, VREF+, VDDA | All on VCC |
| Driver socket power and coils | VMOT on +12 V, logic VDD on VCC, both GNDs connected. Coil pairs (A1/A2, B1/B2) come from the same bridge |
| Microstepping | MS1–MS3 on VCC: the finest step size the fitted driver supports |
| STEP/DIR to socket | Through `P6`/`P7`, jumpered from `P5`: one axis at a time |

---

## 5. The LAN8720 module on this board

From the module's own schematic and netlist:

- **`nRST` is not on the module's header.** Pin 13 is unconnected, and
  `nRST` joins only R16 (4.7 kΩ to VCC), C7 (100 nF to GND) and the PHY.
  The prototype's `ETH_nRST` on `PB0` therefore goes nowhere, and that is
  the correct drawing for this module.
- **The module resets itself** through that RC. τ = 470 µs, and `nRST`
  crosses the Schmitt threshold (1.65 V typical, 1.90 V maximum;
  Table 5-5) **0.33–0.40 ms after power-up**.
- The LAN8720A datasheet requires `nRST` to stay asserted until **at least
  25 ms after the supplies reach 80 %** (`tpurstd`, Table 5-9). It also
  says that a hardware reset is required after power-up. **The module's RC
  is about 60× too short.** This is a property of the module and cannot be
  fixed from the firmware on this board.
- What keeps the PHY usable anyway is `LAN8742_Init()`: after locating the
  PHY it issues an MDIO software reset (BCR bit 15) and waits for it to
  finish. That also covers a warm MCU reset, which the RC never sees. A
  software reset does not re-latch the configuration straps (Note 5-22),
  and it cannot recover a PHY that has stopped answering MDIO. On this
  board only a power cycle does that.
- **ADR-013's reset pulse on `PB0` has no effect on this board**, and
  **HV-20 passes without measuring anything**: it reads back `PB0`, not
  `nRST`. HV-20 is meaningful on the final PCB only.
- The `PHYAD0` strap is tied high through R9, so **the PHY is at SMI
  address 1**. That agrees with the vendor example. The firmware scans all
  32 addresses anyway.

---

## 6. What the firmware needs in order to run on this board

| # | Setting | Why |
|---|---|---|
| 1 | 16 MHz HSE settings, **or** fit an 8 MHz crystal | PR-1. Without this the board does not boot |
| 2 | Inverted `EN` for the driver socket | PR-2. Needs `CNC_EN_ACTIVE_HIGH` to be honoured first |
| 3 | `stepgen_configure_max_rate(200000u)` for motor tests | PR-4 |
| 4 | Nothing for the PHY | The module resets itself (§5) |
| 5 | Nothing for the LEDs, **once the schematic is fixed** | PR-6 |

Items 1–3 are prototype build settings. They must not leak into the
product build: the final board keeps the 2 MHz ceiling and its own `EN`
polarity.

### 6.1 Which validation tests this board can run

| Test | On the prototype |
|---|---|
| HV-00 – HV-05 (motion self-tests) | Yes |
| HV-10 – HV-14 (STEP/DIR waveform, 2 MHz) | Yes, **scope at the MCU pins, driver removed, default tick** |
| HV-15, HV-36 (timing under Ethernet load) | Yes |
| HV-18, HV-40 – HV-45 (E-STOP, inputs) | Yes, using `PE2`, `PE3`, `PE4` only |
| HV-20 (PHY reset) | **No.** It passes but measures nothing (§5) |
| HV-21 – HV-25, HV-30 – HV-35 (network, protocol) | Yes |
| HV-50 – HV-53 (output self-tests) | Yes |
| HV-54, HV-55 (spindle waveform, relay) | Only by probing `PB4` / `PB8` directly (PR-9) |

---

## 7. Carry forward to the final PCB

1. **Wire `nRST` to `PB0`**, as planned, **and add a pull-down on `nRST`**
   so the PHY stays in reset from power-up until the firmware releases it,
   instead of releasing on its own the moment VCC appears. `nRST` has an
   internal pull-up that the datasheet gives only as "50 µA (typical)",
   with no maximum, so size the pull-down for margin. **4.7 kΩ** holds the
   pin at about 0.24 V at the typical current. The negative-going threshold
   is 1.41 V typical at 3.3 V, and 0.64 V is the lowest figure anywhere in
   Table 5-5. Releasing it then costs `PB0` only 0.7 mA.
2. **The firmware must not release `nRST` earlier than 25 ms after
   power-up** (`tpurstd`). Today it does: `MX_GPIO_Init()` drives `PB0`
   high early, and ADR-013's pulse ends a few milliseconds into boot.
   ADR-013 was written against `trstia` (100 µs) only. This needs a
   firmware change before the final board, not before the prototype.
3. **Decide the E-STOP wiring for fail-safety.** `Docs/PINOUT.md` defines
   the inputs as active low with pull-ups, so a normally-open button to
   GND reads as "released" when its wire breaks. Industrial practice is a
   latching normally-closed button wired so that a break *asserts* the
   stop. That needs either the input polarity inverted for `PE2` or an
   inverter in hardware. It is an owner decision, and it belongs in the
   final schematic.
4. Give each of the fifteen inputs its own conditioning in place of the
   prototype's shared pull-up.
5. Keep PR-5 to PR-10 fixed rather than re-deriving them.
