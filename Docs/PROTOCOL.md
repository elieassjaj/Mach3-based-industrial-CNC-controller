# CNC5AX-ETH Motion Protocol v1 (`C5P1`)

**Status:** `[PROJECT-DECISION]` for everything below unless a line says
otherwise. This document is the **single authoritative reference for the
wire format**. `Firmware/Net/Inc/cnc_protocol.h` implements exactly this and
nothing else; where the two disagree, this document is wrong and must be
fixed, not worked around.

Status tags follow `Docs/ETHERNET.md`:

| Tag | Meaning |
|---|---|
| `[PROJECT-DECISION]` | Decided here. Binding on both firmware and plugin |
| `[SDK-EVIDENCE]` | Taken from a shipping Mach3 device in `MACH3/` |
| `[DESIGN-RECOMMENDATION]` | Guidance for the host; not enforced by firmware |
| `[EXAMPLE]` | Illustrative only |
| `[TBD]` | Not decided. Must not be implemented as if it were |

**This protocol is not Mach3 compatibility.** It is the interface between
this firmware and a plugin that does not exist yet. No claim of Mach3
compatibility may be made until that plugin is written and tested
(`Docs/MACH3-INTERFACE.md`).

---

## 1. What the evidence fixed, and what it did not

The shape of this protocol is taken from two shipping Mach3 Ethernet/USB
devices whose sources are in `MACH3/`. Recording which parts are evidence
and which are this project's choice matters, because the two carry very
different weight.

### From `SDK/ncPod/` `[SDK-EVIDENCE]`

Read from `ExternalMovement.cpp` (`Send512Block()`) and `ncPODDriver.h`:

| Fact | Where it lands here |
|---|---|
| Motion is a **time-sliced per-axis velocity drip feed**, not `GMoves` | §5 |
| **32 records per block**, one block ≈ **128 ms**, sent at ≈**50 Hz**, 2–4 ms per record | §5, §8 |
| Record = opcode + 3-byte line ID + **one signed 16-bit value per axis** | §5 (we use int32; see §5.3) |
| Host keeps a **per-axis fractional accumulator** so truncation loses no motion | §5.4 |
| Host supplies a **sequence number**, device **echoes it** in status (`ncPODCommand.sequenceNumber` → `ncPODStatus.Q`) | §4 |
| Device reports a **buffer-full condition** and the host retries the same block (`ACKBUFFERNOTREADY`) | §7 |
| Status carries **input bitfield** (`inio`), **output bitfield** (`outio`), per-axis **position in steps** (`fullSteps*`), buffer occupancy (`NumBuffer`) | §6 |
| Explicit **E-stop / finish-move / reset / pause / continue** commands exist as distinct opcodes | §4.3 |

### From `SDK/Galil PlugIn/` and `SDK/g100IO/` `[SDK-EVIDENCE]`

`G100-Structs.h` independently confirms the same shape from a different
vendor: a **window** field ("how many messages the device can accept in its
holding buffer"), a **motion-queue sequence number separate from the message
sequence** (`CurrentQueueSeq`, `NextQueue`, `EndQueueSeq`), a 32-bit input
word, and last-message-time link supervision. Two independent devices
arriving at the same design is why §4 and §7 look the way they do.

### What the evidence did *not* fix

- **A UDP port.** `ncPod` is a **USB** device — `ncPODDriver.h` defines USB
  endpoints and the project links `libusb.lib`. (`Docs/MACH3-INTERFACE.md`
  §2 describes it as Ethernet; that is inaccurate and is corrected there.)
  The only port literal anywhere in the SDK is Galil's `13887` config
  socket, which is that vendor's. Our port is therefore a free choice — §2.
- **Byte order.** `ncPod` byte-swaps every 16-bit value because, in its own
  words, *"the micro-controller on the pod uses reversed Most Significant
  Byte"*. That reason does not apply to a Cortex-M4 talking to an x86 PC —
  see §3.
- **Opcode numbers.** `ncPod`'s numbers are its USB API. Reusing them would
  imply a compatibility this device does not have.
- **Anything about jog, homing, probing or dwell**, beyond the observation
  that such commands exist. See §10.

---

## 2. Transport and addressing

| Item | Value |
|---|---|
| Transport | UDP over IPv4 |
| Device address | `192.168.5.10` (`Docs/ETHERNET.md` §15) |
| Host address | `192.168.5.100` |
| **Device UDP port** | **55010** |
| Host UDP port | Any. The device replies to the source port it last heard from |

**Why 55010** `[PROJECT-DECISION]`: it is in IANA's dynamic/private range
(49152–65535), so it collides with no registered service, and the digits
echo the device address `…5.10`. It has no other significance — the same
kind of arbitrary-but-recorded choice as ADR-011's MAC address. It is
defined once, in `Firmware/Net/Inc/net_config.h`, and is not repeated
anywhere else in the firmware.

**Endpoint learning.** The device replies to the source address and port of
the packet that triggered the reply, and sends unsolicited status to the
most recent source it accepted a valid packet from. Before the first valid
packet it transmits nothing at all.

`[DESIGN-RECOMMENDATION]` Last-writer-wins is acceptable only because this
is an isolated point-to-point link with exactly one host
(`Docs/ETHERNET.md` §15). On a shared network a second talker could steal
the status stream. If the link ever stops being point-to-point, bind the
accepted source address instead.

---

## 3. Byte order, units and common conventions

`[PROJECT-DECISION]`

- **Little-endian** for every multi-byte field. Both ends are
  little-endian, so neither side byte-swaps anything. `ncPod` swaps because
  its PIC was big-endian; copying that would cost work on both ends and buy
  nothing.
- **Signed values are two's complement.**
- **Reserved fields are written as zero and ignored on receipt.** They are
  not available for private use; a future version may define them.
- **Units**, fixed throughout:

| Quantity | Unit |
|---|---|
| Position, step counts | steps (`int64`, ADR-009) |
| Motion per slice | 1/65536 step, signed (`int32`) — see §5.3 |
| Time slice | microseconds (`uint16`) |
| Spindle | per-mille of full scale, 0…1000 |
| Timestamps / uptime | milliseconds |

---

## 4. Packet framing

Every packet, in both directions, is:

```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      magic  'C' '5' 'P' '1'                   |  0
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|    version    |    opcode     |         payload_len           |  4
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                             seq                               |  8
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     payload (payload_len bytes)               | 12
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                            crc32                              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Field | Size | Notes |
|---|---|---|
| `magic` | 4 | ASCII `C5P1`, i.e. bytes `43 35 50 31`; `0x31503543` read as a little-endian `uint32` |
| `version` | 1 | `1`. A packet with any other value is dropped, never guessed at |
| `opcode` | 1 | §4.2 / §4.3 |
| `payload_len` | 2 | Bytes of payload, excluding header and CRC |
| `seq` | 4 | Host-assigned, see §4.4. Devices echo it |
| `crc32` | 4 | Over the header and payload — every byte before the CRC itself |

- Header is **12 bytes**, CRC is **4**, so the smallest legal packet is
  **16 bytes** and `datagram_len` must equal `12 + payload_len + 4` exactly.
  A datagram that is longer is rejected, not truncated-and-accepted.
- Largest packet is a full motion block: `12 + 648 + 4` = **664 bytes**,
  comfortably inside a 1500-byte MTU, so no packet is ever fragmented.

### 4.1 CRC-32

IEEE 802.3 CRC-32: reflected polynomial `0xEDB88320`, initial value
`0xFFFFFFFF`, final XOR `0xFFFFFFFF`. This is the same CRC as
`zlib.crc32` / Python's `binascii.crc32`, so the host side needs no custom
code.

`[DESIGN-RECOMMENDATION]` Ethernet's FCS and the (hardware-offloaded) UDP
checksum already cover the wire. The application CRC is here to cover
everything after that: host stack bugs, driver and DMA corruption, and
truncated or spliced datagrams. It is cheap and it makes "a malformed
packet can never reach the motion queue" a property rather than a hope.

### 4.2 Opcodes, host → device

| Value | Name | Payload | §|
|---|---|---|---|
| `0x01` | `HELLO` | 0 bytes | §4.5 |
| `0x02` | `MOTION` | 8 + 20·n | §5 |
| `0x03` | `CONTROL` | 4 bytes | §4.3 |
| `0x04` | `OUTPUTS` | 8 bytes | §9 |
| `0x05` | `STATUS_REQ` | 0 bytes | §6 |

### 4.3 Opcodes, device → host

| Value | Name | Payload |
|---|---|---|
| `0x81` | `STATUS` | 96 bytes, §6 |
| `0x82` | `INFO` | 32 bytes, §4.5 |

There is deliberately **no NACK packet**. `STATUS` already carries
`last_reject_reason`, `last_reject_seq`, `queue_free` and the full fault
set, so a rejection is just a status with a reason in it. One packet type
instead of two means one state machine on each side instead of two.

### 4.4 `seq` — the message sequence

`[PROJECT-DECISION]`, modelled on `ncPod`'s `sequenceNumber` → `Q` echo.

- The host assigns `seq` to **every** packet it sends, starting at 1 and
  incrementing by exactly 1 each time, across all opcodes.
- The device tracks `last_seq_seen`. A packet whose `seq` is **less than or
  equal to** `last_seq_seen` is a duplicate or a reordered straggler: it is
  **not acted upon**, but it does get a `STATUS` reply.
  - Replying matters. The usual reason a host repeats itself is that it
    never saw the previous status, and answering re-synchronises it
    instead of leaving both ends waiting.
- `seq` gaps are **not** an error at this level. Losing a `STATUS_REQ`
  costs nothing. Losing *motion* is a different matter entirely, and has
  its own counter — §5.2.

### 4.5 `HELLO` and `INFO`

`HELLO` carries no payload and changes no device state. The device answers
with `INFO` — a single packet, not `INFO` followed by `STATUS`. Once the
device has heard a valid packet it knows where to send status, so the
periodic stream (§6) starts on its own immediately afterwards.

`INFO` payload, 32 bytes:

| Off | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 | `proto_version` | `1` |
| 1 | 1 | `axis_count` | `5` |
| 2 | 2 | `queue_depth` | motion segment slots, currently 64 |
| 4 | 4 | `tick_hz` | engine base tick, e.g. 4 000 000 |
| 8 | 4 | `max_step_rate_hz` | e.g. 2 000 000 |
| 12 | 2 | `max_records_per_block` | 32 |
| 14 | 2 | `status_period_ms` | 20 |
| 16 | 4 | `comm_timeout_ms` | 200 |
| 20 | 6 | `mac` | as transmitted, byte 0 first |
| 26 | 4 | `ip` | byte 0 = first octet (192) |
| 30 | 2 | `udp_port` | 55010 |

`tick_hz` is the field that matters most: it is what lets the host verify
that its chosen `slice_us` converts exactly (§5.3) and that its feed rates
are inside the device's ceiling. A plugin should call `HELLO` once at
connect and refuse to run against an unexpected `proto_version`.

---

## 5. `MOTION` — the motion stream

### 5.1 Payload

| Off | Size | Field |
|---|---|---|
| 0 | 4 | `block_seq` (`uint32`) |
| 4 | 2 | `slice_us` (`uint16`) |
| 6 | 1 | `record_count` (`uint8`, 1…32) |
| 7 | 1 | `flags` |
| 8 | 20·n | `records[record_count]` |

`flags`:

| Bit | Name | Meaning |
|---|---|---|
| 0 | `END_OF_PROGRAM` | Last block of this program; see §7.4 |
| 1–7 | reserved | Must be zero |

Each record is exactly 20 bytes — five signed 32-bit values, in fixed axis
order:

```text
+---------+---------+---------+---------+---------+
|    X    |    Y    |    Z    |    A    |    B    |
|  int32  |  int32  |  int32  |  int32  |  int32  |
+---------+---------+---------+---------+---------+
     0         4         8        12        16
```

`payload_len` must equal `8 + 20 × record_count` exactly.

### 5.2 `block_seq` — the motion sequence, and why it is separate

`[PROJECT-DECISION]`, and the single most important rule in this document.

`block_seq` counts **motion blocks only**, incrementing by one per block.
It exists separately from the header's `seq` because they answer different
questions, and Galil's firmware makes the same split (`CurrentQueueSeq`
alongside the message sequence).

| `block_seq` vs expected | Device behaviour |
|---|---|
| equal to last accepted | **Duplicate retry.** Do not re-queue. Reply `STATUS`. This is what makes the host's retry-after-buffer-full loop (§7) safe |
| exactly last + 1 | **Accept**, if the whole block fits (§7) |
| less than last | **Stale straggler.** Drop, reply `STATUS` |
| greater than last + 1 | **GAP — a motion block was lost.** Reject the block, latch `PROTO_FAULT_SEQ_GAP`, stop motion (§7.3). **Never skip forward** |

A block is also refused with `WRONG_STATE`, before any of the above is
acted on, if the engine is not in `READY` or `RUNNING` — the only states in
which it accepts segments. A host that streams motion before sending
`CONTROL:ENABLE_DRIVES` gets a clean refusal rather than a block that is
half-enqueued and then fails partway through.

A gap is not a recoverable hiccup: the missing block *is* a missing piece
of the toolpath, and continuing past it cuts the wrong shape. The device
therefore refuses to continue and requires the host to intervene
explicitly, which is ADR-010's rule ("recovery is never automatic") applied
to the command stream.

Synchronisation is established, not assumed. After boot, and after any
`FLUSH`, `ABORT` or fault clear, the device is *unsynced*: the next
`MOTION` block it accepts sets the baseline, whatever its `block_seq`.
From then on the rules above apply. This keeps the gap check strict during
a program without requiring the two ends to agree on a starting number.

### 5.3 What a record value means

`[PROJECT-DECISION]` Each value is **signed motion for that axis during
this slice, in units of 1/65536 step**. Sign is direction; zero means that
axis does not move in that slice.

`ncPod` sends `int16` in units tied to its own microstepping ratio
(`32768/75`). This project sends `int32` in a device-independent unit
instead, for two reasons: the host would otherwise have to know the
device's step scaling to encode a packet at all, and `int16` caps a slice
at well under one full step of headroom once the scale is fixed. `int32`
at 1/65536 step allows ±32767 steps per slice, which is more than the 2 MHz
ceiling can emit in any legal slice.

The firmware converts each record into one `motion_segment_t`
(`Firmware/Motion/Inc/motion_types.h`) — the existing Phase 1 type, unchanged:

```text
duration_ticks = slice_us × tick_hz / 1 000 000
rate_q32[i]    = round( steps_q16[i] × 65536 / duration_ticks )
```

Both conversions are checked, not assumed:

- `slice_us × tick_hz` must divide by 1 000 000 **exactly**. A slice that
  does not land on a whole number of ticks would put a systematic timing
  error on every feed, which is the same reason
  `stepgen_configure_max_rate()` accepts only exact dividers. Inexact →
  reject with `SLICE_NOT_EXACT`.
- `|rate_q32[i]|` must not exceed `STEPGEN_RATE_MAX_Q32` (2³¹, the 2 MHz
  ceiling). Over → reject with `RATE_TOO_HIGH`. **The device never clamps
  a rate**: silently capping one axis of a coordinated move turns a
  straight line into a curve.

Rounding is to nearest rather than truncating, which removes the bias.

**Measured accuracy, and the one thing a host must not assume.** The host
test suite counts the pulses the engine actually emits for a given block.
Two properties hold, and they are not the same:

- The device **never emits more steps than commanded.** Overshoot would be
  uncommanded motion; this is a safety property, not an accuracy one.
- The device may be **up to one step behind** the commanded total at any
  instant, and is, at the end of a run. The engine carries a fractional
  phase accumulator across segments, so the last step of a segment
  generally lands at the start of the next one. A run that continues makes
  it up; a run that stops does not.

`[DESIGN-RECOMMENDATION]` A host must therefore not treat "I sent N steps"
as "N steps were emitted". It should close the loop from `pos_output[]` in
the status (§6), exactly as `ncPod`'s plugin does from its own `vtotal[]`.
The long-run residual from rounding alone is far smaller — under
4 × 10⁻⁶ steps per 4 ms slice, a few steps per hour of continuous motion —
but the one-step lag is structural and present from the first block.

### 5.4 Fractional carry is the host's job

`[SDK-EVIDENCE]` `ncPod`'s plugin keeps `fractions[axis]` across records so
that truncating to an integer loses no motion over a block. The same
obligation lands on this project's plugin: the sum of `steps_q16` over a
program must equal the commanded travel, with the remainder carried, not
dropped. The firmware cannot do this for the host — it never sees the
millimetre-domain path.

### 5.5 Axes, and the sixth one

`[PROJECT-DECISION]` Records carry exactly five axes, in the order
**X, Y, Z, A, B**, matching `MOTION_AXIS_X`…`MOTION_AXIS_B`.

Mach3 is natively six-axis (`GMoves` carries `ec`/`sc` for C —
`Docs/MACH3-INTERFACE.md` §3). **There is no C field in this protocol, at
any offset.** That is the explicit handling the interface document asks
for: a device that cannot receive a sixth axis cannot silently mis-index
one into a fifth.

`[DESIGN-RECOMMENDATION]` The obligation moves to the plugin, and it is not
"ignore C". If a loaded program commands non-zero C motion, the plugin must
refuse the job and say so, because quietly dropping an axis machines the
wrong part. Recorded here as a Phase 4 requirement.

---

## 6. `STATUS` — device → host

96-byte payload. Sent: after every packet the device accepts, after every
packet it rejects for a semantic reason, in answer to `STATUS_REQ`, and
unsolicited every **20 ms** (50 Hz, matching the `ncPod` cadence) once a
host endpoint is known.

| Off | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 | `state` | engine state, §6.1 |
| 1 | 1 | `faults` | `stepgen_fault_t` bits |
| 2 | 2 | `flags` | §6.2 |
| 4 | 1 | `queue_free` | free motion slots — the flow-control number |
| 5 | 1 | `queue_depth` | total slots |
| 6 | 2 | `proto_faults` | §6.3, latched |
| 8 | 4 | `last_seq_seen` | |
| 12 | 4 | `last_seq_accepted` | |
| 16 | 4 | `last_block_seq` | last accepted motion block |
| 20 | 2 | `last_reject_reason` | §6.4 |
| 22 | 2 | `inputs` | `PE0`…`PE14`, bit n = `PEn` |
| 24 | 2 | `outputs` | relay and output bits |
| 26 | 2 | `spindle_pmille` | 0…1000 |
| 28 | 4 | `segments_consumed` | |
| 32 | 4 | `underruns` | STEP ring refill misses |
| 36 | 4 | `starved_ticks` | |
| 40 | 4 | `rx_accepted` | |
| 44 | 4 | `rx_rejected` | structurally valid, semantically refused |
| 48 | 4 | `rx_dropped` | bad magic/version/length/CRC — never trusted |
| 52 | 4 | `uptime_ms` | |
| 56 | 40 | `pos_output[5]` | `int64` steps actually emitted, per axis |

### 6.1 `state`

Mirrors `stepgen_state_t` (ADR-010) — no new state model is introduced:
`0` UNINIT, `1` SAFE_IDLE, `2` READY, `3` RUNNING, `4` FAULT,
`5` EMERGENCY_STOP.

### 6.2 `flags`

| Bit | Meaning |
|---|---|
| 0 | drives enabled (`EN` asserted) |
| 1 | Ethernet link up |
| 2 | a host endpoint is known |
| 3 | motion stream synced (`block_seq` baseline established) |
| 4 | motion active (between first block and `END_OF_PROGRAM`/stop) |
| 5 | inputs subsystem present — **0 until M3 exists**, see §11 |
| 6 | outputs subsystem present — **0 until M9/M10 exist**, see §11 |

Bits 5 and 6 exist so the host can tell "no inputs are active" from "this
firmware cannot report inputs yet". Reporting an all-zero bitfield without
that distinction would look exactly like a machine with every switch open.

### 6.3 `proto_faults` (latched, cleared only by `CLEAR_FAULT`)

| Bit | Name | Raised when |
|---|---|---|
| 0 | `SEQ_GAP` | a motion block was lost (§5.2) |
| 1 | `COMM_TIMEOUT` | no valid host packet for `comm_timeout_ms` while motion was active |
| 2 | `QUEUE_FULL` | a block was refused for lack of room (informational; normal flow control) |
| 3 | `BAD_PARAM` | a structurally valid packet carried unusable values |

### 6.4 `last_reject_reason`

`0` none, `1` queue full, `2` sequence gap, `3` stale/duplicate,
`4` bad parameter, `5` not implemented, `6` wrong state,
`7` rate above the 2 MHz ceiling, `8` slice does not convert exactly.

---

## 7. Flow control, loss, and what the device refuses to do

### 7.1 Backpressure is explicit and the block is atomic

`queue_free` in every status is the flow-control signal — Galil calls the
same thing a *window*. A `MOTION` block is accepted **entirely or not at
all**: if `record_count > queue_free`, nothing is enqueued, the reason is
`QUEUE_FULL`, and a status goes back immediately.

Partial acceptance is deliberately not offered. It would require the host
to track which records of a block survived, and a half-consumed block is
exactly the kind of state that produces a wrong toolpath after a retry.

`[DESIGN-RECOMMENDATION]` The host retries the *same block* with the *same*
`block_seq`, which §5.2 makes idempotent. `ncPod`'s plugin does the same
and gives up after ~1 s of continuous holding; a plugin here should do
something similar rather than retry forever.

### 7.2 Nothing is ever silently dropped or overwritten

An unconsumed command is never overwritten. Either it is in the queue, or
the host was told it was refused and why.

### 7.3 Motion stops on a gap, and recovery is explicit

On `SEQ_GAP` the device: refuses the block, latches the fault, performs a
**controlled stop** (`stepgen_stop()` — timebase halts, `EN` stays
asserted), and leaves the queue intact for inspection. It does **not**
emergency-stop, and it does **not** disable the drives — per ADR-010, a
command-stream problem holds position with the drives live so no axis
drifts or drops under load.

Clearing requires `CONTROL:CLEAR_FAULT`, then re-syncing the stream. No
condition clears it automatically, including the host simply resuming.

### 7.4 Communication timeout

If no valid host packet arrives for `comm_timeout_ms` (**200 ms**) *while
motion is active*, the device latches `COMM_TIMEOUT` and performs the same
controlled stop as §7.3.

**Why 200 ms** `[PROJECT-DECISION]`: the host cadence is 50 Hz (20 ms), so
200 ms is ten missed cycles — long enough to ride out ordinary Windows
scheduling jitter, which is the reason the buffer is deep in the first
place. It is also deliberately *shorter* than the time a full queue takes
to drain (64 slots × 4 ms ≈ 256 ms), so a dead host is diagnosed as a
timeout, explicitly, rather than surfacing later as motion starvation.

Supervision applies only while motion is active. A host that goes quiet
between programs is not a fault: nothing is moving.

**A dead link is not an E-stop.** `Docs/FIRMWARE-ARCHITECTURE.md` §8 and
ADR-010 both require the local hardware E-stop path (`PE2`, EXTI priority
0) to stay independent of the network, and nothing in this protocol
touches it. Equally, no network packet can clear a latched E-stop except
the explicit `CLEAR_ESTOP` command, which the engine itself refuses while
the physical input is still asserted.

### 7.5 Summary of adverse cases

| Case | Detected by | Response |
|---|---|---|
| Lost motion block | `block_seq` gap | Reject, latch `SEQ_GAP`, controlled stop |
| Lost non-motion packet | not treated as an error | none needed |
| Duplicate motion block | `block_seq` equal | Ignore content, reply status |
| Duplicate other packet | `seq` ≤ `last_seq_seen` | Do not act, reply status |
| Out-of-order / stale | `seq` or `block_seq` older | Do not act, reply status |
| Corrupt packet | magic / version / length / CRC | Drop, count in `rx_dropped`, **no reply** |
| Burst faster than consumption | `queue_free` | Refuse whole block, `QUEUE_FULL` |
| Host stops talking | `comm_timeout_ms` | Latch `COMM_TIMEOUT`, controlled stop |
| Host sends nonsense values | payload validation | Reject, `BAD_PARAM`/`RATE_TOO_HIGH`/`SLICE_NOT_EXACT` |

Corrupt packets get no reply on purpose: if the CRC failed, the `seq` in
that packet cannot be trusted either, so there is nothing meaningful to
answer and nothing to gain by generating traffic in response to garbage.

---

## 8. Timing

| Parameter | Value | Basis |
|---|---|---|
| Status period | 20 ms (50 Hz) | `[SDK-EVIDENCE]` `ncPod` runs its transport at ≈50 Hz |
| Comm timeout | 200 ms | §7.4 |
| Recommended `slice_us` | 4000 (4 ms) | `[SDK-EVIDENCE]` `ncPod`: "128ms of data … at 50hz … 4ms or 2ms" |
| Recommended records/block | 32 | `[SDK-EVIDENCE]` `Send512Block()` |
| Resulting block duration | 128 ms | 32 × 4 ms |
| Device buffer at those values | 64 slots × 4 ms = **256 ms** | Meets ADR-008's ≥128 ms target with 2× margin |

`[DESIGN-RECOMMENDATION]` The device accepts `slice_us` from 250 upwards
and does not enforce a minimum beyond exact conversion, because a finer
slice is legitimate — it simply buys less buffer. A host choosing
`slice_us < 2000` drops below ADR-008's 128 ms target with a 64-slot queue
and should know that it is doing so. `INFO` publishes `queue_depth` and
`tick_hz` precisely so the host can work this out rather than guess.

The 1 kHz packet rate that `Docs/ETHERNET.md` §7 carried as an early target
is superseded: it was never evidence, and both reference devices buffer
deeply at a modest packet rate instead.

---

## 9. `OUTPUTS` — spindle, relay and digital outputs

Payload, 8 bytes:

| Off | Size | Field |
|---|---|---|
| 0 | 2 | `out_mask` — which output bits this packet sets |
| 2 | 2 | `out_value` — their values |
| 4 | 2 | `spindle_pmille` — 0…1000, or `0xFFFF` to leave unchanged |
| 6 | 2 | reserved |

Bit 0 of `out_mask`/`out_value` is the relay (`PB8`, active high per
`Docs/PINOUT.md`). Remaining bits are reserved.

The mask/value pair is `ncPod`'s `USBOUTPUT` convention `[SDK-EVIDENCE]` —
"the output signal is set to the value of the corresponding bit in axisY if
the corresponding bit is 1 in axisX" — and it exists so a host can change
one output without knowing the state of the others.

**Phase 3 firmware rejects this opcode with `NOT_IMPLEMENTED`.** The
modules that own those pins — M9 (outputs) and M10 (spindle PWM) — do not
exist yet. The wire format is fixed here so the plugin can be written
against a stable interface; the behaviour behind it is Phase 5 work. Status
bit `flags.6` reports that absence explicitly rather than returning
plausible-looking zeros.

---

## 10. Deliberately not in v1

`[TBD]` — listed so their absence is a decision rather than an oversight.
Each needs firmware that does not exist yet, and inventing a wire format
for behaviour nobody has implemented would be guessing.

| Item | Blocked on |
|---|---|
| Jog (`MyJogOn`/`MyJogOff`) | Can be expressed as ordinary motion blocks by the plugin; a dedicated opcode is only worth adding if that proves inadequate |
| Dwell (`myDwell`) | Expressible as motion records with all axes zero. No opcode needed |
| Homing | M3 (input manager) — there are no limit/home inputs to watch yet |
| Probing | M3, plus a probe-input capture path |
| Feed-rate override | Host-side: the plugin re-encodes the slices. No device opcode planned |
| Position preset / `SETCOORDS` | Needs a decision on who owns machine coordinates. Real gap; add in v2 |
| Firmware update over UDP | Out of scope for this project |

Jog and dwell are marked "no opcode needed" rather than "TBD": both fall
out of the time-sliced model for free, and adding opcodes for them would be
adding surface without adding capability.

---

## 11. What this firmware does not yet report

Honest gaps in `STATUS`, all flagged in `flags` so a host cannot mistake
them for real readings:

| Field | State | Gate |
|---|---|---|
| `inputs` | always 0, `flags.5` = 0 | M3 — the `PE0`…`PE14` EXTI manager |
| `outputs`, `spindle_pmille` | always 0, `flags.6` = 0 | M9 / M10 |
| E-stop input state | not reported | M3. The engine's own `EMERGENCY_STOP` state *is* reported in `state` |

---

## 12. Conformance

A host implementation conforms to v1 if it:

1. sends only the opcodes in §4.2, with `version` = 1 and correct CRC;
2. increments `seq` by one per packet and `block_seq` by one per motion
   block, and repeats — never renumbers — a retried block;
3. treats `queue_free` as authoritative and never assumes a block was
   accepted without seeing it reflected in `last_block_seq`;
4. stops sending motion and requires operator action on any latched
   `proto_faults` bit, rather than clearing and continuing;
5. carries its own per-axis fractional remainder (§5.4);
6. refuses to run a program that commands the C axis (§5.5).

A device conforms if it never enqueues motion from a packet that failed any
check in §4 or §5, and never clears a fault by itself.
