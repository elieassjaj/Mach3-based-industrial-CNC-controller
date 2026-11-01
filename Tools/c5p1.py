#!/usr/bin/env python3
"""
c5p1.py - PC-side client for the CNC5AX-ETH Motion Protocol v1 (C5P1).

Drives the controller from a plain PC, with no Mach3 and no plugin, so the
firmware's protocol layer can be tested on its own. It is also a working
reference for the wire format: Docs/PROTOCOL.md is authoritative, and this
file is what a plugin author can read instead of re-deriving it.

Standard library only. Runs on a bare Windows Python 3.8+ install:

    python c5p1.py info
    python c5p1.py watch
    python c5p1.py enable
    python c5p1.py start
    python c5p1.py move --axis X --steps 2000 --seconds 2
    python c5p1.py outputs --relay on --spindle 500
    python c5p1.py stop

Every command is a separate run, and each run opens a new socket. That
works because the tool re-synchronises with the device at connect (see
"Sessions" below). For a bench session, `shell` keeps one connection open
and reads commands from the keyboard or from a file:

    python c5p1.py shell
    python c5p1.py shell < bench_steps.txt

Sessions. The device keeps the highest message `seq` it has seen and
ignores anything at or below it (PROTOCOL.md §4.4). A new run starts again
at seq 1, which an earlier run has already used. So `connect()` sends
HELLO, and if the device answers with a STATUS instead of INFO, the tool
takes `last_seq_seen` from it and continues above that number. The motion
`block_seq` gets the same treatment before a move (§5.2): if the device is
still synced from an earlier run, the move continues from `last_block_seq`.

Replies. Once the device knows a host it also sends STATUS every 20 ms on
its own. A reply is therefore matched by its `seq` echo, never taken as
"the next packet that arrives".

Safety: `move` and `outputs` command real motion and real outputs. Run them
with the drives disconnected until the machine is trusted. Ctrl-C during a
move sends CONTROL:STOP before exiting.
"""

import argparse
import binascii
import shlex
import socket
import struct
import sys
import time

# ----------------------------------------------------------------- wire ---
# Docs/PROTOCOL.md is authoritative; these constants mirror it.

MAGIC = b"C5P1"
VERSION = 1

OP_HELLO, OP_MOTION, OP_CONTROL, OP_OUTPUTS, OP_STATUS_REQ = 1, 2, 3, 4, 5
OP_STATUS, OP_INFO = 0x81, 0x82

CTL = {
    "enable": 1, "disable": 2, "start": 3, "stop": 4,
    "abort": 5, "clear-fault": 6, "clear-estop": 7, "flush": 8,
}

AXES = ["X", "Y", "Z", "A", "B"]
N_AXES = len(AXES)

HDR = struct.Struct("<4sBBHI")          # magic, version, opcode, plen, seq
MOTION_HDR = struct.Struct("<IHBB")     # block_seq, slice_us, count, flags
RECORD = struct.Struct("<" + "i" * N_AXES)
CONTROL = struct.Struct("<BBH")         # cmd, arg, reserved
OUTPUTS = struct.Struct("<HHHH")        # mask, value, spindle, reserved

MOTION_FLAG_END_OF_PROGRAM = 0x01
OUT_RELAY = 0x0001
SPINDLE_KEEP = 0xFFFF
SPINDLE_MAX = 1000

STATE = {0: "UNINIT", 1: "SAFE_IDLE", 2: "READY", 3: "RUNNING",
         4: "FAULT", 5: "EMERGENCY_STOP"}
ST_READY, ST_RUNNING = 2, 3

REJ_NONE, REJ_QUEUE_FULL, REJ_STALE = 0, 1, 3
REJECT = {0: "none", 1: "queue full", 2: "sequence gap", 3: "stale",
          4: "bad parameter", 5: "not implemented", 6: "wrong state",
          7: "rate too high", 8: "slice not exact"}

PFAULT = {1: "SEQ_GAP", 2: "COMM_TIMEOUT", 4: "QUEUE_FULL", 8: "BAD_PARAM"}

SFLAG_SYNCED = 8
SFLAG = {1: "drives", 2: "link", 4: "host", 8: "synced", 16: "moving",
         32: "inputs", 64: "outputs"}

DEFAULT_ADDR = ("192.168.5.10", 55010)
SEQ_MAX = 0xFFFFFFFF
STEPS_Q16 = 65536
INT32_MAX = 0x7FFFFFFF


class ProtocolError(ValueError):
    """The device sent something this tool cannot accept."""


class NoReply(Exception):
    """The device did not answer in time."""


class Refused(Exception):
    """A request the tool can check locally would be refused anyway."""


def crc32(data: bytes) -> int:
    """IEEE 802.3 CRC-32 - the same one the firmware computes."""
    return binascii.crc32(data) & 0xFFFFFFFF


def frame(opcode: int, seq: int, payload: bytes = b"") -> bytes:
    body = HDR.pack(MAGIC, VERSION, opcode, len(payload), seq) + payload
    return body + struct.pack("<I", crc32(body))


def unframe(pkt: bytes):
    """Validate a received packet. Returns (opcode, seq, payload)."""
    if len(pkt) < HDR.size + 4:
        raise ProtocolError("short packet")
    magic, version, opcode, plen, seq = HDR.unpack_from(pkt, 0)
    if magic != MAGIC:
        raise ProtocolError(f"bad magic {magic!r}")
    if version != VERSION:
        raise ProtocolError(f"unsupported version {version}")
    if len(pkt) != HDR.size + plen + 4:
        raise ProtocolError("length mismatch")
    body = pkt[:HDR.size + plen]
    want = struct.unpack_from("<I", pkt, HDR.size + plen)[0]
    if crc32(body) != want:
        raise ProtocolError("CRC mismatch")
    return opcode, seq, pkt[HDR.size:HDR.size + plen]


# ------------------------------------------------------------- payloads ---

def control_payload(name: str) -> bytes:
    return CONTROL.pack(CTL[name], 0, 0)


def outputs_payload(relay=None, spindle=None) -> bytes:
    """relay: None (leave), True, False. spindle: None (leave) or 0..1000."""
    mask = value = 0
    if relay is not None:
        mask |= OUT_RELAY
        if relay:
            value |= OUT_RELAY
    if spindle is None:
        spindle = SPINDLE_KEEP
    elif not 0 <= spindle <= SPINDLE_MAX:
        raise Refused(f"spindle {spindle} is outside 0..{SPINDLE_MAX} per-mille")
    return OUTPUTS.pack(mask, value, spindle, 0)


def motion_payload(block_seq, slice_us, records, flags=0) -> bytes:
    payload = MOTION_HDR.pack(block_seq, slice_us, len(records), flags)
    for r in records:
        payload += RECORD.pack(*r)
    return payload


def move_records(axis: int, steps: int, n_slices: int):
    """Split `steps` on one axis into n_slices records of 1/65536 step.

    The host owns the fractional remainder (PROTOCOL.md §5.4). Each record
    is the difference of two floored cumulative totals, in integers, so the
    records always add up to exactly steps * 65536 - no float drift.
    """
    total = steps * STEPS_Q16
    out = []
    prev = 0
    for i in range(1, n_slices + 1):
        cum = (total * i) // n_slices
        rec = [0] * N_AXES
        rec[axis] = cum - prev
        out.append(rec)
        prev = cum
    return out


# --------------------------------------------------------------- decode ---

STATUS = struct.Struct("<BBHBBHIIIHHHHIIIIIII")   # through uptime_ms


def parse_status(p: bytes) -> dict:
    if len(p) != 96:
        raise ProtocolError(f"status payload is {len(p)} bytes, expected 96")
    f = STATUS.unpack_from(p, 0)
    st = {
        "state": f[0], "faults": f[1], "flags": f[2],
        "queue_free": f[3], "queue_depth": f[4], "proto_faults": f[5],
        "last_seq_seen": f[6], "last_seq_accepted": f[7],
        "last_block_seq": f[8], "reject": f[9],
        "inputs": f[10], "outputs": f[11], "spindle": f[12],
        "segments": f[13], "underruns": f[14], "starved": f[15],
        "rx_accepted": f[16], "rx_rejected": f[17], "rx_dropped": f[18],
        "uptime_ms": f[19],
    }
    st["pos"] = list(struct.unpack_from("<5q", p, 56))
    return st


def parse_info(p: bytes) -> dict:
    if len(p) != 32:
        raise ProtocolError(f"info payload is {len(p)} bytes, expected 32")
    (ver, axes, depth, tick, maxrate, maxrec, period, timeout) = \
        struct.unpack_from("<BBHIIHHI", p, 0)
    mac = p[20:26]
    ip = p[26:30]
    port = struct.unpack_from("<H", p, 30)[0]
    return {
        "proto_version": ver, "axis_count": axes, "queue_depth": depth,
        "tick_hz": tick, "max_step_rate_hz": maxrate,
        "max_records": maxrec, "status_period_ms": period,
        "comm_timeout_ms": timeout,
        "mac": ":".join(f"{b:02X}" for b in mac),
        "ip": ".".join(str(b) for b in ip), "udp_port": port,
    }


def flags_str(v: int, table: dict) -> str:
    on = [name for bit, name in sorted(table.items()) if v & bit]
    return ",".join(on) if on else "-"


def show_status(st: dict) -> str:
    return (
        f"{STATE.get(st['state'], st['state']):<14} "
        f"q={st['queue_free']}/{st['queue_depth']} "
        f"blk={st['last_block_seq']} "
        f"flags={flags_str(st['flags'], SFLAG)} "
        f"faults={st['faults']:#04x}/{flags_str(st['proto_faults'], PFAULT)} "
        f"rej={REJECT.get(st['reject'], st['reject'])} "
        f"in={st['inputs']:#06x} out={st['outputs']:#06x} "
        f"spindle={st['spindle']} "
        f"rx={st['rx_accepted']}/{st['rx_rejected']}/{st['rx_dropped']} "
        f"pos={st['pos']}"
    )


# --------------------------------------------------------------- client ---

def _udp_socket():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    # Windows: an ICMP "port unreachable" (device not up yet, wrong address)
    # makes the NEXT recvfrom() raise ConnectionResetError (WSAECONNRESET)
    # instead of timing out. Switch that behaviour off where it exists.
    if hasattr(socket, "SIO_UDP_CONNRESET"):
        try:
            sock.ioctl(socket.SIO_UDP_CONNRESET, False)
        except OSError:
            pass
    return sock


class Device:
    """One host session with the controller.

    `seq` and `block_seq` hold the LAST value used, so the next packet uses
    seq + 1. Both are re-synchronised from the device, never assumed.
    """

    HELLO_ATTEMPTS = 3

    def __init__(self, addr=DEFAULT_ADDR, timeout=1.0, verbose=False,
                 sock=None, log=None):
        self.addr = addr
        self.timeout = timeout
        self.verbose = verbose
        self.seq = 0
        self.block_seq = 0
        self.info = None
        self.sock = sock if sock is not None else _udp_socket()
        self.log = log if log is not None else (lambda m: print(m, file=sys.stderr))

    def close(self):
        self.sock.close()

    # ------------------------------------------------------- transport ---

    def send(self, opcode, payload=b""):
        if self.seq >= SEQ_MAX:
            raise ProtocolError("message seq exhausted; power-cycle the device")
        self.seq += 1
        self.sock.sendto(frame(opcode, self.seq, payload), self.addr)
        if self.verbose:
            self.log(f"-> op={opcode:#04x} seq={self.seq} len={len(payload)}")
        return self.seq

    def _recv(self, deadline):
        """Next valid packet before `deadline`, or None. Never raises on
        timeouts, Windows connection resets or a corrupt datagram."""
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                return None
            self.sock.settimeout(left)
            try:
                data, _ = self.sock.recvfrom(2048)
            except socket.timeout:
                return None
            except ConnectionResetError:
                continue
            try:
                opcode, seq, payload = unframe(data)
            except ProtocolError as e:
                if self.verbose:
                    self.log(f"<- dropped a bad packet: {e}")
                continue
            if self.verbose:
                self.log(f"<- op={opcode:#04x} seq={seq}")
            return opcode, seq, payload

    def drain(self):
        """Discard queued datagrams - mostly unsolicited STATUS."""
        self.sock.setblocking(False)
        try:
            while True:
                try:
                    self.sock.recvfrom(2048)
                except (BlockingIOError, ConnectionResetError):
                    break
                except OSError:
                    break
        finally:
            self.sock.setblocking(True)

    def recv_any(self, timeout=None):
        """Next STATUS or INFO, whatever its seq. Used by `watch`."""
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while True:
            pkt = self._recv(deadline)
            if pkt is None:
                raise NoReply()
            kind = self._decode(pkt)
            if kind is not None:
                return kind

    @staticmethod
    def _decode(pkt):
        opcode, _, payload = pkt
        if opcode == OP_STATUS:
            return "status", parse_status(payload)
        if opcode == OP_INFO:
            return "info", parse_info(payload)
        return None

    def request(self, opcode, payload=b""):
        """Send one packet and return the reply that echoes its seq.

        Unsolicited STATUS carries the seq of an EARLIER packet until the
        device has processed this one, so matching on the echo is what
        separates the reply from the background stream.
        """
        self.drain()
        seq = self.send(opcode, payload)
        deadline = time.monotonic() + self.timeout
        while True:
            pkt = self._recv(deadline)
            if pkt is None:
                raise NoReply()
            if pkt[1] != seq:
                continue
            kind = self._decode(pkt)
            if kind is not None:
                return kind

    # -------------------------------------------------------- session ---

    def connect(self):
        """HELLO, re-synchronising `seq` with the device. Returns INFO.

        The device answers a HELLO whose seq it has already seen with a
        STATUS (PROTOCOL.md §4.4). That STATUS carries last_seq_seen, so
        continuing above it makes the next HELLO current.
        """
        for _ in range(self.HELLO_ATTEMPTS):
            kind, data = self.request(OP_HELLO)
            if kind == "info":
                if data["proto_version"] != VERSION:
                    raise ProtocolError(
                        f"device speaks v{data['proto_version']}, "
                        f"this tool speaks v{VERSION}")
                self.info = data
                return data
            if self.verbose:
                self.log(f"device already at seq {data['last_seq_seen']}; "
                         f"continuing above it")
            self.seq = max(self.seq, data["last_seq_seen"])
        raise ProtocolError("HELLO never returned INFO")

    hello = connect

    def ensure_connected(self):
        return self.info if self.info is not None else self.connect()

    def status(self):
        kind, st = self.request(OP_STATUS_REQ)
        if kind != "status":
            raise ProtocolError("STATUS_REQ did not return STATUS")
        return st

    def control(self, name):
        kind, st = self.request(OP_CONTROL, control_payload(name))
        if kind != "status":
            raise ProtocolError("CONTROL did not return STATUS")
        return st

    def outputs(self, relay=None, spindle=None):
        kind, st = self.request(OP_OUTPUTS, outputs_payload(relay, spindle))
        if kind != "status":
            raise ProtocolError("OUTPUTS did not return STATUS")
        return st

    def sync_blocks(self, st):
        """Continue the motion stream from the device's view of it.

        Synced: the next block must be last_block_seq + 1, or the device
        drops it as stale. Unsynced: any number sets the baseline.
        """
        self.block_seq = st["last_block_seq"] if st["flags"] & SFLAG_SYNCED else 0

    def motion(self, records, slice_us, flags=0, retry_block=None):
        """Send one block. Returns (status, block_seq)."""
        if retry_block is None:
            self.block_seq += 1
            block = self.block_seq
        else:
            block = retry_block          # a retry keeps the same block_seq
        payload = motion_payload(block, slice_us, records, flags)
        kind, st = self.request(OP_MOTION, payload)
        if kind != "status":
            raise ProtocolError("MOTION did not return STATUS")
        return st, block

    def send_block(self, records, slice_us, flags=0, max_holds=50,
                   max_lost=3):
        """One block, with backpressure and lost-reply handling.
        Returns (status, block_seq, accepted).

        Queue full: wait one status period and resend the SAME block_seq.
        Lost reply: resend the same block. If the first copy had arrived,
        the device answers 'stale' with last_block_seq == block, which
        means accepted (PROTOCOL.md §5.2, duplicate retry).
        """
        period = (self.info or {}).get("status_period_ms", 20) / 1000.0
        block = None
        holds = lost = 0
        while True:
            try:
                st, block = self.motion(records, slice_us, flags,
                                        retry_block=block)
            except NoReply:
                lost += 1
                if block is None:
                    block = self.block_seq
                if lost > max_lost:
                    raise
                continue
            if st["reject"] == REJ_NONE:
                return st, block, True
            if st["reject"] == REJ_STALE and st["last_block_seq"] == block \
                    and st["flags"] & SFLAG_SYNCED and lost:
                return st, block, True
            if st["reject"] == REJ_QUEUE_FULL:
                holds += 1
                if holds > max_holds:
                    raise Refused("device held the buffer too long; "
                                  "is the engine started (RUNNING)?")
                time.sleep(period)
                continue
            return st, block, False


# ------------------------------------------------------------- commands ---
# Each takes (dev, args, out) and returns an exit code: 0 ok, 1 refused.

def cmd_info(dev, args, out):
    info = dev.connect()
    width = max(len(k) for k in info)
    for k, v in info.items():
        out(f"  {k:<{width}} : {v}")
    return 0


def cmd_status(dev, args, out):
    dev.ensure_connected()
    out(show_status(dev.status()))
    return 0


def cmd_control(dev, args, out):
    dev.ensure_connected()
    st = dev.control(args.cmd)
    out(show_status(st))
    if st["reject"] != REJ_NONE:
        out(f"refused: {REJECT.get(st['reject'], st['reject'])}")
        return 1
    return 0


def cmd_watch(dev, args, out):
    dev.ensure_connected()
    dev.send(OP_STATUS_REQ)
    last = None
    t_end = None if args.seconds is None else time.monotonic() + args.seconds
    try:
        while t_end is None or time.monotonic() < t_end:
            try:
                kind, st = dev.recv_any()
            except NoReply:
                out("(no status - device silent)")
                dev.send(OP_STATUS_REQ)
                continue
            if kind != "status":
                continue
            line = show_status(st)
            if args.changes_only and line == last:
                continue
            last = line
            out(f"{st['uptime_ms'] / 1000:9.2f}s  {line}")
    except KeyboardInterrupt:
        pass
    return 0


def plan_move(info, axis, steps, seconds, slice_us):
    """Check a move against the device's INFO before sending any of it."""
    if slice_us <= 0 or slice_us > 0xFFFF:
        raise Refused(f"slice_us {slice_us} is outside 1..65535")
    tick = info["tick_hz"]
    if (slice_us * tick) % 1_000_000 != 0:
        raise Refused(f"{slice_us} us is not a whole number of "
                      f"{tick} Hz ticks (device would reject: slice not exact)")
    n_slices = max(1, round(seconds * 1e6 / slice_us))
    records = move_records(axis, steps, n_slices)
    worst = max(abs(r[axis]) for r in records)
    ticks = slice_us * tick // 1_000_000
    # Device ceiling: |steps_q16| <= 32768 * ticks (cnc_session.c rate_fits)
    if worst > 32768 * ticks or worst > INT32_MAX:
        rate = abs(steps) / (n_slices * slice_us / 1e6)
        raise Refused(f"{rate:.0f} steps/s is above the device ceiling of "
                      f"{info['max_step_rate_hz']} steps/s")
    return records, n_slices


def cmd_move(dev, args, out):
    """One axis, constant rate, split into slices - a bench move."""
    info = dev.ensure_connected()
    axis = AXES.index(args.axis.upper())
    records, n_slices = plan_move(info, axis, args.steps, args.seconds,
                                  args.slice_us)

    st = dev.status()
    if st["state"] not in (ST_READY, ST_RUNNING):
        out(show_status(st))
        out(f"refused: engine is {STATE.get(st['state'])}; "
            f"send 'enable' (and 'start') first")
        return 1
    if st["state"] == ST_READY:
        out("note: engine is READY, not RUNNING - blocks will queue but "
            "nothing moves until 'start'")
    dev.sync_blocks(st)

    out(f"{args.steps} steps on {AXES[axis]} over {args.seconds}s: "
        f"{n_slices} slices of {args.slice_us} us "
        f"({args.steps / n_slices:.3f} steps/slice, "
        f"{args.steps / args.seconds:.0f} steps/s)")

    max_rec = info["max_records"]
    sent = blocks = 0
    try:
        while sent < len(records):
            chunk = records[sent:sent + max_rec]
            if args.gap_after is not None and blocks == args.gap_after:
                # HV-35: skip one block_seq on purpose. The device must
                # refuse the next block with a sequence gap and stop.
                dev.block_seq += 1
                out(f"skipping block {dev.block_seq} on purpose (--gap-after)")
            blocks += 1
            st, block, ok = dev.send_block(chunk, args.slice_us,
                                           max_holds=args.max_holds)
            if not ok:
                out(f"block {block} refused: "
                    f"{REJECT.get(st['reject'], st['reject'])}")
                out(show_status(st))
                return 1
            sent += len(chunk)

        # End of program, so the device's comm-timeout supervision stands
        # down instead of faulting on our silence (PROTOCOL.md §7.4).
        st, _, ok = dev.send_block([[0] * N_AXES], args.slice_us,
                                   flags=MOTION_FLAG_END_OF_PROGRAM,
                                   max_holds=args.max_holds)
    except KeyboardInterrupt:
        out("interrupted - sending STOP")
        try:
            out(show_status(dev.control("stop")))
        except NoReply:
            out("no reply to STOP; the device stops on its own after "
                f"{info['comm_timeout_ms']} ms of silence")
        return 130
    out(show_status(st))
    if not ok:
        out(f"end-of-program block refused: "
            f"{REJECT.get(st['reject'], st['reject'])}")
        return 1
    return 0


def cmd_outputs(dev, args, out):
    dev.ensure_connected()
    relay = {"on": True, "off": False, "keep": None}[args.relay]
    st = dev.outputs(relay, args.spindle)
    out(show_status(st))
    if st["reject"] != REJ_NONE:
        out(f"refused: {REJECT.get(st['reject'], st['reject'])}")
        return 1
    return 0


def cmd_raw(dev, args, out):
    """Send a deliberately malformed packet - tests the device's validation
    from the outside. A correct device stays silent (PROTOCOL.md §7.5)."""
    dev.ensure_connected()
    seq = dev.seq + 1          # not consumed: the device must not see it
    pkt = bytearray(frame(OP_STATUS_REQ, seq))
    if args.corrupt == "magic":
        pkt[0] ^= 0xFF
    elif args.corrupt == "version":
        pkt[4] = 99
    elif args.corrupt == "crc":
        pkt[-1] ^= 0x01
    elif args.corrupt == "length":
        pkt[6] = 0xFF
    elif args.corrupt == "opcode":
        pkt[5] = 0x7F
    dev.drain()
    dev.sock.sendto(bytes(pkt), dev.addr)
    out(f"sent a packet with a corrupted {args.corrupt}")

    # Unsolicited STATUS keeps arriving and is not a reply: only a packet
    # echoing the corrupt packet's seq would be.
    deadline = time.monotonic() + dev.timeout
    while True:
        p = dev._recv(deadline)
        if p is None:
            break
        if p[1] == seq:
            out("device replied - a corrupt packet should draw no reply")
            return 1
    out("device stayed silent, as it should")
    out(show_status(dev.status()))
    return 0


def cmd_sleep(dev, args, out):
    time.sleep(args.seconds)
    return 0


def add_commands(sub, interactive=False):
    sub.add_parser("info", help="HELLO, and print the device identity")
    sub.add_parser("status", help="print one status line")

    w = sub.add_parser("watch", help="follow the status stream (Ctrl-C ends)")
    w.add_argument("--changes-only", action="store_true")
    w.add_argument("--seconds", type=float, default=None,
                   help="stop after this long (default: until Ctrl-C)")

    for name in CTL:
        sub.add_parser(name, help=f"send CONTROL:{name}")

    m = sub.add_parser("move", help="stream a single-axis move")
    m.add_argument("--axis", required=True,
                   choices=AXES + [a.lower() for a in AXES])
    m.add_argument("--steps", type=int, required=True)
    m.add_argument("--seconds", type=float, default=1.0)
    m.add_argument("--slice-us", type=int, default=4000)
    m.add_argument("--max-holds", type=int, default=50)
    m.add_argument("--gap-after", type=int, default=None, metavar="N",
                   help="HV-35 test aid: skip one block_seq after N blocks")

    o = sub.add_parser("outputs", help="relay and spindle (OUTPUTS packet)")
    o.add_argument("--relay", choices=["on", "off", "keep"], default="keep")
    o.add_argument("--spindle", type=int, default=None,
                   help="duty in per-mille, 0..1000 (default: unchanged)")

    r = sub.add_parser("raw", help="send a malformed packet on purpose")
    r.add_argument("--corrupt", required=True,
                   choices=["magic", "version", "crc", "length", "opcode"])

    if interactive:
        s = sub.add_parser("sleep", help="pause a script")
        s.add_argument("seconds", type=float)
    else:
        sh = sub.add_parser("shell", help="one session, commands from "
                                          "the keyboard or a file")
        sh.add_argument("--keep-going", action="store_true",
                        help="in a script, continue after a failed command")


COMMANDS = {"info": cmd_info, "status": cmd_status, "watch": cmd_watch,
            "move": cmd_move, "outputs": cmd_outputs, "raw": cmd_raw,
            "sleep": cmd_sleep}
COMMANDS.update({name: cmd_control for name in CTL})


def run_command(dev, args, out):
    """Run one parsed command. 0 ok, 1 refused, 2 no reply / protocol."""
    try:
        return COMMANDS[args.cmd](dev, args, out)
    except Refused as e:
        out(f"refused: {e}")
        return 1
    except NoReply:
        out(f"no reply from {dev.addr[0]}:{dev.addr[1]} - the device answers "
            f"only valid packets; check the link, the address, the firewall "
            f"and that the firmware is running")
        return 2
    except ProtocolError as e:
        out(f"protocol error: {e}")
        return 2


class _ShellParser(argparse.ArgumentParser):
    def error(self, message):
        raise ValueError(message)

    def exit(self, status=0, message=None):
        if message:
            print(message, end="")
        raise ValueError(None)


def cmd_shell(dev, args, out, lines=None):
    """Read commands one per line; one socket, one seq space."""
    parser = _ShellParser(prog="", add_help=False)
    add_commands(parser.add_subparsers(dest="cmd", required=True),
                 interactive=True)

    interactive = lines is None and sys.stdin.isatty()
    keep_going = interactive or args.keep_going
    it = iter(sys.stdin if lines is None else lines)
    if interactive:
        out("c5p1 shell - commands as on the command line, plus 'sleep N', "
            "'help' and 'quit'")

    worst = 0
    while True:
        if interactive:
            try:
                line = input("c5p1> ")
            except EOFError:
                break
            except KeyboardInterrupt:
                out("")
                continue
        else:
            line = next(it, None)
            if line is None:
                break
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        if line in ("quit", "exit"):
            break
        if line == "help":
            out(parser.format_help())
            continue
        if not interactive:
            out(f"> {line}")
        try:
            cargs = parser.parse_args(shlex.split(line))
        except ValueError as e:
            if e.args and e.args[0]:
                out(f"error: {e.args[0]}")
            rc = 1
        else:
            rc = run_command(dev, cargs, out)
        worst = max(worst, rc)
        if rc != 0 and not keep_going:
            out("stopping: a command failed (use --keep-going to continue)")
            break
    return worst


def build_parser():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=DEFAULT_ADDR[0])
    ap.add_argument("--port", type=int, default=DEFAULT_ADDR[1])
    ap.add_argument("--timeout", type=float, default=1.0,
                    help="reply timeout in seconds (default 1.0)")
    ap.add_argument("-v", "--verbose", action="store_true")
    add_commands(ap.add_subparsers(dest="cmd", required=True))
    return ap


def main(argv=None, out=print):
    args = build_parser().parse_args(argv)
    dev = Device((args.host, args.port), args.timeout, args.verbose)
    try:
        if args.cmd == "shell":
            return cmd_shell(dev, args, out)
        return run_command(dev, args, out)
    finally:
        dev.close()


if __name__ == "__main__":
    sys.exit(main())
