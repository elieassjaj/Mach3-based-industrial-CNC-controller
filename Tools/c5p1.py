#!/usr/bin/env python3
"""
c5p1.py - PC-side client for the CNC5AX-ETH Motion Protocol v1.

Lets the controller be exercised from a plain PC with no Mach3 and no
plugin, which is what makes the firmware's protocol layer testable on its
own. It is also a working reference for the wire format: anyone writing the
Mach3 plugin can read this file instead of re-deriving Docs/PROTOCOL.md.

The device is passive. It transmits nothing until it has received a valid
packet, so every session starts with a HELLO.

    ./c5p1.py info
    ./c5p1.py watch
    ./c5p1.py enable && ./c5p1.py start
    ./c5p1.py move --axis X --steps 2000 --seconds 2
    ./c5p1.py stop

Safety: `move` and `stream` command real motion. Run them with the drives
disconnected until the machine is trusted.

No third-party packages: standard library only, so it runs on a bare Windows
Python install next to Mach3.
"""

import argparse
import binascii
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

STATE = {0: "UNINIT", 1: "SAFE_IDLE", 2: "READY", 3: "RUNNING",
         4: "FAULT", 5: "EMERGENCY_STOP"}

REJECT = {0: "none", 1: "queue full", 2: "sequence gap", 3: "stale",
          4: "bad parameter", 5: "not implemented", 6: "wrong state",
          7: "rate too high", 8: "slice not exact"}

PFAULT = {1: "SEQ_GAP", 2: "COMM_TIMEOUT", 4: "QUEUE_FULL", 8: "BAD_PARAM"}

SFLAG = {1: "drives", 2: "link", 4: "host", 8: "synced", 16: "moving",
         32: "inputs", 64: "outputs"}

DEFAULT_ADDR = ("192.168.5.10", 55010)


def crc32(data: bytes) -> int:
    """IEEE 802.3 CRC-32 - the same one the firmware computes."""
    return binascii.crc32(data) & 0xFFFFFFFF


def frame(opcode: int, seq: int, payload: bytes = b"") -> bytes:
    body = HDR.pack(MAGIC, VERSION, opcode, len(payload), seq) + payload
    return body + struct.pack("<I", crc32(body))


def unframe(pkt: bytes):
    """Validate a received packet. Returns (opcode, seq, payload)."""
    if len(pkt) < HDR.size + 4:
        raise ValueError("short packet")
    magic, version, opcode, plen, seq = HDR.unpack_from(pkt, 0)
    if magic != MAGIC:
        raise ValueError(f"bad magic {magic!r}")
    if version != VERSION:
        raise ValueError(f"unsupported version {version}")
    if len(pkt) != HDR.size + plen + 4:
        raise ValueError("length mismatch")
    body = pkt[:HDR.size + plen]
    want = struct.unpack_from("<I", pkt, HDR.size + plen)[0]
    if crc32(body) != want:
        raise ValueError("CRC mismatch")
    return opcode, seq, pkt[HDR.size:HDR.size + plen]


# --------------------------------------------------------------- decode ---

STATUS = struct.Struct("<BBHBBHIIIHHHHIIIIIII")   # through uptime_ms


def parse_status(p: bytes) -> dict:
    if len(p) != 96:
        raise ValueError(f"status payload is {len(p)} bytes, expected 96")
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
        raise ValueError(f"info payload is {len(p)} bytes, expected 32")
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
        f"rx={st['rx_accepted']}/{st['rx_rejected']}/{st['rx_dropped']} "
        f"pos={st['pos']}"
    )


# --------------------------------------------------------------- client ---

class Device:
    def __init__(self, addr, timeout=1.0, verbose=False):
        self.addr = addr
        self.verbose = verbose
        self.seq = 0
        self.block_seq = 0
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(timeout)

    def send(self, opcode, payload=b""):
        self.seq += 1
        pkt = frame(opcode, self.seq, payload)
        self.sock.sendto(pkt, self.addr)
        if self.verbose:
            print(f"-> op={opcode:#04x} seq={self.seq} len={len(pkt)}")
        return self.seq

    def recv(self):
        data, _ = self.sock.recvfrom(2048)
        opcode, seq, payload = unframe(data)
        if opcode == OP_STATUS:
            return "status", parse_status(payload)
        if opcode == OP_INFO:
            return "info", parse_info(payload)
        raise ValueError(f"unexpected opcode {opcode:#04x}")

    def request(self, opcode, payload=b""):
        self.send(opcode, payload)
        return self.recv()

    def hello(self):
        kind, info = self.request(OP_HELLO)
        if kind != "info":
            raise ValueError("HELLO did not return INFO")
        return info

    def status(self):
        kind, st = self.request(OP_STATUS_REQ)
        if kind != "status":
            raise ValueError("STATUS_REQ did not return STATUS")
        return st

    def control(self, name):
        payload = struct.pack("<BBH", CTL[name], 0, 0)
        return self.request(OP_CONTROL, payload)[1]

    def motion(self, records, slice_us, flags=0, retry_block=None):
        """Send one block. Returns the device status reply."""
        if retry_block is None:
            self.block_seq += 1
            block = self.block_seq
        else:
            block = retry_block          # retry keeps the same block_seq
        payload = MOTION_HDR.pack(block, slice_us, len(records), flags)
        for r in records:
            payload += RECORD.pack(*r)
        return self.request(OP_MOTION, payload)[1], block


# ------------------------------------------------------------- commands ---

def cmd_info(dev, args):
    info = dev.hello()
    width = max(len(k) for k in info)
    for k, v in info.items():
        print(f"  {k:<{width}} : {v}")
    if info["proto_version"] != VERSION:
        print(f"\nWARNING: device speaks v{info['proto_version']}, "
              f"this tool speaks v{VERSION}", file=sys.stderr)
        return 1
    return 0


def cmd_status(dev, args):
    dev.hello()
    print(show_status(dev.status()))
    return 0


def cmd_control(dev, args):
    dev.hello()
    st = dev.control(args.command)
    print(show_status(st))
    if st["reject"] != 0:
        print(f"refused: {REJECT.get(st['reject'])}", file=sys.stderr)
        return 1
    return 0


def cmd_watch(dev, args):
    dev.hello()
    dev.send(OP_STATUS_REQ)
    last = None
    try:
        while True:
            try:
                kind, st = dev.recv()
            except socket.timeout:
                print("(no status - device silent)", file=sys.stderr)
                dev.send(OP_STATUS_REQ)
                continue
            if kind != "status":
                continue
            line = show_status(st)
            if args.changes_only and line == last:
                continue
            last = line
            print(f"{st['uptime_ms']/1000:9.2f}s  {line}", flush=True)
    except KeyboardInterrupt:
        return 0


def cmd_move(dev, args):
    """One axis, constant rate, split into slices - a bench move."""
    info = dev.hello()
    axis = AXES.index(args.axis.upper())

    slice_us = args.slice_us
    n_slices = max(1, round(args.seconds * 1e6 / slice_us))

    # The host owns the fractional remainder (Docs/PROTOCOL.md §5.4): the
    # firmware never sees the real-world path, so it cannot carry it for us.
    per_slice = args.steps / n_slices
    carry = 0.0
    slices = []
    for _ in range(n_slices):
        want = per_slice + carry
        whole = int(want * 65536)
        carry = want - whole / 65536
        rec = [0] * N_AXES
        rec[axis] = whole
        slices.append(rec)

    print(f"{args.steps} steps on {AXES[axis]} over {args.seconds}s: "
          f"{n_slices} slices of {slice_us} us "
          f"({per_slice:.3f} steps/slice, "
          f"{args.steps/args.seconds:.0f} steps/s)")

    max_rec = info["max_records"]
    sent = 0
    while sent < len(slices):
        chunk = slices[sent:sent + max_rec]
        st, block = dev.motion(chunk, slice_us)

        # Backpressure: the device refuses a block it cannot hold whole, and
        # the retry must reuse the same block_seq or the device will see a
        # gap (Docs/PROTOCOL.md §5.2).
        holds = 0
        while st["reject"] == 1:                  # queue full
            holds += 1
            if holds > args.max_holds:
                print("device held the buffer too long, giving up",
                      file=sys.stderr)
                return 1
            time.sleep(info["status_period_ms"] / 1000.0)
            st, _ = dev.motion(chunk, slice_us, retry_block=block)

        if st["reject"] != 0:
            print(f"block {block} refused: {REJECT.get(st['reject'])}",
                  file=sys.stderr)
            print(show_status(st), file=sys.stderr)
            return 1
        sent += len(chunk)

    # Tell the device the program is over, so its comm-timeout supervision
    # stands down instead of faulting on our silence.
    st, _ = dev.motion([[0] * N_AXES], slice_us, flags=1)
    print(show_status(st))
    return 0


def cmd_raw(dev, args):
    """Send a deliberately malformed packet - for testing the device's
    validation from the outside, not just in the host test suite."""
    dev.hello()
    pkt = bytearray(frame(OP_STATUS_REQ, dev.seq + 1))
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
    dev.sock.sendto(bytes(pkt), dev.addr)
    print(f"sent a packet with a corrupted {args.corrupt}")

    try:
        kind, st = dev.recv()
        print("device replied:", show_status(st) if kind == "status" else st)
        print("NOTE: a corrupt packet should draw no reply at all "
              "(Docs/PROTOCOL.md §7.5)", file=sys.stderr)
        return 1
    except socket.timeout:
        print("device stayed silent, as it should")
        # Confirm it is still alive and counted the drop.
        st = dev.status()
        print(show_status(st))
        return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=DEFAULT_ADDR[0])
    ap.add_argument("--port", type=int, default=DEFAULT_ADDR[1])
    ap.add_argument("--timeout", type=float, default=1.0)
    ap.add_argument("-v", "--verbose", action="store_true")
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("info", help="HELLO, and print the device identity")
    sub.add_parser("status", help="print one status line")

    w = sub.add_parser("watch", help="follow the status stream")
    w.add_argument("--changes-only", action="store_true")

    for name in CTL:
        sub.add_parser(name, help=f"send CONTROL:{name}")

    m = sub.add_parser("move", help="stream a single-axis move")
    m.add_argument("--axis", required=True, choices=AXES + [a.lower() for a in AXES])
    m.add_argument("--steps", type=int, required=True)
    m.add_argument("--seconds", type=float, default=1.0)
    m.add_argument("--slice-us", type=int, default=4000)
    m.add_argument("--max-holds", type=int, default=50)

    r = sub.add_parser("raw", help="send a malformed packet on purpose")
    r.add_argument("--corrupt", required=True,
                   choices=["magic", "version", "crc", "length", "opcode"])

    args = ap.parse_args()
    dev = Device((args.host, args.port), args.timeout, args.verbose)

    try:
        if args.cmd == "info":
            return cmd_info(dev, args)
        if args.cmd == "status":
            return cmd_status(dev, args)
        if args.cmd == "watch":
            return cmd_watch(dev, args)
        if args.cmd == "move":
            return cmd_move(dev, args)
        if args.cmd == "raw":
            return cmd_raw(dev, args)
        if args.cmd in CTL:
            args.command = args.cmd
            return cmd_control(dev, args)
    except socket.timeout:
        print(f"no reply from {args.host}:{args.port}", file=sys.stderr)
        print("the device is passive and answers only valid packets; "
              "check the link, the address and that the firmware is running",
              file=sys.stderr)
        return 2
    except ValueError as e:
        print(f"protocol error: {e}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
