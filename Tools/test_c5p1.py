#!/usr/bin/env python3
"""
Tests for c5p1.py. Standard library only; runs on Windows and Linux:

    python -m unittest -v test_c5p1        (from the Tools directory)
    python Tools/test_c5p1.py              (from anywhere)

Two kinds of test:

* Codec tests against golden packets produced by the firmware's own C
  encoder (Firmware/Net/Src/cnc_protocol.c), so the tool and the firmware
  cannot drift apart byte for byte.
* Session tests against FakeDevice, a UDP device on 127.0.0.1 that mirrors
  the rules of Firmware/Net/Src/cnc_session.c that matter to a host: the
  stale-seq rule, block_seq sync, atomic blocks, backpressure, the STATUS
  reply echo and the unsolicited 20 ms status stream. Each rule that the
  old tool tripped over has a test here.
"""

import io
import os
import socket
import struct
import sys
import threading
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import c5p1  # noqa: E402

# ---------------------------------------------------------------- golden --
# Printed by a host build of Firmware/Net/Src/cnc_protocol.c.

GOLDEN = {
    # cnc_encode_simple(seq=7, HELLO)
    "hello_seq7": "4335503101010000070000007b254112",
    # cnc_encode_control(seq=2, ENABLE_DRIVES, arg=0)
    "control_enable_seq2": "433550310103040002000000010000003c03fc18",
    # cnc_encode_motion(seq=3, block=9, slice=4000, flags=END_OF_PROGRAM,
    #   r0 = {X: 3 steps, B: -12345/65536}, r1 = {Z: -1 step})
    "motion_seq3_blk9": (
        "43355031010230000300000009000000a00f020100000300000000000000"
        "000000000000c7cfffff00000000000000000000ffff0000000000000000"
        "5d685488"),
    # cnc_encode_status(seq=1000, ...) - see test_status_decodes
    "status_seq1000": (
        "4335503101816000e803000003025f0011400400e8030000e70300002a00"
        "0000010014000100ee0240e20100000000004d0000008403000005000000"
        "03000000f1fb0900fbffffffffffffff0000000000010000000000000000"
        "000000000000000000000700000000000000678af2c6"),
    # cnc_encode_info(seq=1, cnc_session_get_info()) on the default build
    "info_seq1": (
        "4335503101822000010000000105400000093d0080841e00200014"
        "00c8000000020005100001c0a8050ae2d657a02464"),
}


def g(name):
    return bytes.fromhex(GOLDEN[name])


class CodecTests(unittest.TestCase):

    def test_frames_match_firmware_encoder(self):
        self.assertEqual(c5p1.frame(c5p1.OP_HELLO, 7), g("hello_seq7"))
        self.assertEqual(
            c5p1.frame(c5p1.OP_CONTROL, 2, c5p1.control_payload("enable")),
            g("control_enable_seq2"))
        payload = c5p1.motion_payload(
            9, 4000, [[3 * 65536, 0, 0, 0, -12345], [0, 0, -65536, 0, 0]],
            c5p1.MOTION_FLAG_END_OF_PROGRAM)
        self.assertEqual(c5p1.frame(c5p1.OP_MOTION, 3, payload),
                         g("motion_seq3_blk9"))

    def test_status_decodes(self):
        op, seq, p = c5p1.unframe(g("status_seq1000"))
        self.assertEqual((op, seq), (c5p1.OP_STATUS, 1000))
        st = c5p1.parse_status(p)
        want = {"state": 3, "faults": 2, "flags": 0x5F, "queue_free": 17,
                "queue_depth": 64, "proto_faults": 4, "last_seq_seen": 1000,
                "last_seq_accepted": 999, "last_block_seq": 42, "reject": 1,
                "inputs": 0x14, "outputs": 1, "spindle": 750,
                "segments": 123456, "underruns": 0, "starved": 77,
                "rx_accepted": 900, "rx_rejected": 5, "rx_dropped": 3,
                "uptime_ms": 654321}
        for k, v in want.items():
            self.assertEqual(st[k], v, k)
        self.assertEqual(st["pos"], [-5, 1 << 40, 0, 0, 7])
        self.assertIn("RUNNING", c5p1.show_status(st))

    def test_info_decodes(self):
        op, seq, p = c5p1.unframe(g("info_seq1"))
        self.assertEqual(op, c5p1.OP_INFO)
        info = c5p1.parse_info(p)
        self.assertEqual(info["tick_hz"], 4_000_000)
        self.assertEqual(info["max_step_rate_hz"], 2_000_000)
        self.assertEqual(info["queue_depth"], 64)
        self.assertEqual(info["max_records"], 32)
        self.assertEqual(info["mac"], "02:00:05:10:00:01")
        self.assertEqual(info["ip"], "192.168.5.10")
        self.assertEqual(info["udp_port"], 55010)

    def test_unframe_rejects_every_corruption(self):
        good = g("hello_seq7")
        for i in range(len(good)):
            bad = bytearray(good)
            bad[i] ^= 0x01
            with self.assertRaises(c5p1.ProtocolError, msg=f"byte {i}"):
                c5p1.unframe(bytes(bad))
        with self.assertRaises(c5p1.ProtocolError):
            c5p1.unframe(good[:-1])

    def test_move_records_add_up_exactly(self):
        for steps, n in [(2000, 500), (-3000, 125), (1, 3), (7, 1),
                         (123457, 999), (-1, 7), (0, 10)]:
            recs = c5p1.move_records(2, steps, n)
            self.assertEqual(len(recs), n)
            self.assertEqual(sum(r[2] for r in recs), steps * 65536)
            for r in recs:
                self.assertEqual([r[0], r[1], r[3], r[4]], [0, 0, 0, 0])
            spread = max(r[2] for r in recs) - min(r[2] for r in recs)
            self.assertLessEqual(spread, 1)

    def test_outputs_payload(self):
        self.assertEqual(c5p1.outputs_payload(),
                         struct.pack("<HHHH", 0, 0, 0xFFFF, 0))
        self.assertEqual(c5p1.outputs_payload(True, 500),
                         struct.pack("<HHHH", 1, 1, 500, 0))
        self.assertEqual(c5p1.outputs_payload(False, 0),
                         struct.pack("<HHHH", 1, 0, 0, 0))
        with self.assertRaises(c5p1.Refused):
            c5p1.outputs_payload(None, 1001)

    def test_plan_move_checks_before_sending(self):
        info = c5p1.parse_info(c5p1.unframe(g("info_seq1"))[2])
        recs, n = c5p1.plan_move(info, 0, 2000, 2.0, 4000)
        self.assertEqual(n, 500)
        c5p1.plan_move(info, 0, 10, 1.0, 3)          # 3 us = 12 ticks at 4 MHz
        with self.assertRaises(c5p1.Refused):        # 3 MHz > 2 MHz ceiling
            c5p1.plan_move(info, 0, 3_000_000, 1.0, 4000)
        c5p1.plan_move(info, 0, 2_000_000, 1.0, 4000)  # exactly the ceiling
        slow = dict(info, tick_hz=400_000, max_step_rate_hz=200_000)
        c5p1.plan_move(slow, 0, 200_000, 1.0, 4000)
        with self.assertRaises(c5p1.Refused):        # 1.2 ticks: not exact
            c5p1.plan_move(slow, 0, 10, 1.0, 3)
        with self.assertRaises(c5p1.Refused):
            c5p1.plan_move(slow, 0, 250_000, 1.0, 4000)


# ------------------------------------------------------------ fake device --

class FakeDevice:
    """Mirror of the host-visible rules in cnc_session.c.

    Not a simulation of motion: when RUNNING it consumes `drain_per_tick`
    records per status period, which is enough to exercise backpressure.
    """

    DEPTH = 64

    def __init__(self, stream_ms=20, drain_per_tick=8, tick_hz=4_000_000):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(0.002)
        self.addr = self.sock.getsockname()
        self.stream_ms = stream_ms
        self.drain_per_tick = drain_per_tick
        self.tick_hz = tick_hz
        self.lock = threading.Lock()
        self.drop_replies_to = set()     # opcodes whose next reply is lost
        self.reset()
        self._stop = False
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def reset(self):
        self.state = 1                   # SAFE_IDLE
        self.drives = False
        self.seq_started = False
        self.last_seq_seen = 0
        self.last_seq_accepted = 0
        self.synced = False
        self.last_block = 0
        self.queue = 0
        self.reject = 0
        self.pfaults = 0
        self.rx = [0, 0, 0]
        self.peer = None
        self.received = []               # (opcode, seq) of every valid packet
        self.relay = 0
        self.spindle = 0
        self.pos = 0

    def close(self):
        self._stop = True
        self.thread.join(1)
        self.sock.close()

    # -- encoding
    def _status(self, seq):
        flags = (1 if self.drives else 0) | 2 | 4 | (8 if self.synced else 0)
        flags |= 64
        head = struct.pack("<BBHBBHIIIHHHHIIIIIII",
                           self.state, 0, flags, self.DEPTH - self.queue,
                           self.DEPTH, self.pfaults, self.last_seq_seen,
                           self.last_seq_accepted,
                           self.last_block if self.synced else 0,
                           self.reject, 0, self.relay, self.spindle,
                           0, 0, 0, self.rx[0], self.rx[1], self.rx[2], 0)
        payload = head + struct.pack("<5q", self.pos, 0, 0, 0, 0)
        return c5p1.frame(c5p1.OP_STATUS, seq, payload)

    def _info(self, seq):
        payload = struct.pack("<BBHIIHHI", 1, 5, self.DEPTH, self.tick_hz,
                              self.tick_hz // 2, 32, 20, 200)
        payload += bytes([2, 0, 5, 16, 0, 1, 192, 168, 5, 10])
        payload += struct.pack("<H", 55010)
        return c5p1.frame(c5p1.OP_INFO, seq, payload)

    # -- rules
    def _rej(self, reason, latch=0):
        self.reject = reason
        self.pfaults |= latch
        self.rx[1] += 1

    def _handle(self, data):
        try:
            op, seq, p = c5p1.unframe(data)
        except c5p1.ProtocolError:
            self.rx[2] += 1
            return None
        if op not in (1, 2, 3, 4, 5):
            self.rx[2] += 1              # firmware: unknown opcode is dropped
            return None
        self.received.append((op, seq))
        if self.seq_started and seq <= self.last_seq_seen:
            self.reject = c5p1.REJ_STALE          # cnc_session.c:522
            return self._status(seq)
        self.last_seq_seen = seq
        self.seq_started = True
        ok = False
        if op == c5p1.OP_HELLO:
            self.rx[0] += 1
            self.last_seq_accepted = seq
            self.reject = 0
            return self._info(seq)
        if op == c5p1.OP_STATUS_REQ:
            ok = True
        elif op == c5p1.OP_CONTROL:
            ok = self._control(p[0])
        elif op == c5p1.OP_MOTION:
            ok = self._motion(p)
        elif op == c5p1.OP_OUTPUTS:
            mask, value, spindle, _ = struct.unpack("<HHHH", p)
            if mask & ~1 or (spindle != 0xFFFF and spindle > 1000):
                self._rej(4, 8)
            else:
                if mask & 1:
                    self.relay = value & 1
                if spindle != 0xFFFF:
                    self.spindle = spindle
                self.reject = 0
                ok = True
        if ok:
            self.rx[0] += 1
            self.last_seq_accepted = seq
        return self._status(seq)

    def _control(self, cmd):
        ok = False
        if cmd == 1 and self.state == 1:
            self.state, self.drives, ok = 2, True, True
        elif cmd == 3 and self.state == 2:
            self.state, ok = 3, True
        elif cmd in (2, 4, 5, 8):
            if self.state == 3:
                self.state = 2
            self.queue = 0
            if cmd in (5, 8):
                self.synced = False
            ok = True
        elif cmd == 6 and self.state == 4:
            self.state, self.drives, self.synced, ok = 1, False, False, True
        if ok:
            self.reject = 0
        else:
            self._rej(6)
        return ok

    def _motion(self, p):
        block, slice_us, count, flags = struct.unpack_from("<IHBB", p)
        if self.synced and block == self.last_block:
            self.reject = c5p1.REJ_STALE
            return False
        if self.synced and block < self.last_block:
            self.reject = c5p1.REJ_STALE
            return False
        if self.synced and block != self.last_block + 1:
            self._rej(2, 1)
            self.queue = 0
            return False
        if self.state not in (2, 3):
            self._rej(6)
            return False
        if (slice_us * self.tick_hz) % 1_000_000:
            self._rej(8, 8)
            return False
        if self.DEPTH - self.queue < count:
            self._rej(1, 4)
            return False
        ticks = slice_us * self.tick_hz // 1_000_000
        for i in range(count):
            rec = struct.unpack_from("<5i", p, 8 + 20 * i)
            if any(abs(v) > 32768 * ticks for v in rec):
                self._rej(7, 8)
                return False
        for i in range(count):
            self.pos += struct.unpack_from("<5i", p, 8 + 20 * i)[0]
        self.queue += count
        self.last_block = block
        self.synced = True
        self.reject = 0
        return True

    def _run(self):
        last_stream = time.monotonic()
        while not self._stop:
            try:
                data, src = self.sock.recvfrom(2048)
            except socket.timeout:
                data = None
            except OSError:
                if self._stop:
                    return
                continue
            with self.lock:
                if data is not None:
                    reply = self._handle(data)
                    if self.seq_started:
                        self.peer = src
                    if reply is not None:
                        op = data[5]
                        if op in self.drop_replies_to:
                            self.drop_replies_to.discard(op)
                        else:
                            self.sock.sendto(reply, src)
                now = time.monotonic()
                if (now - last_stream) * 1000 >= self.stream_ms:
                    last_stream = now
                    if self.state == 3:
                        self.queue = max(0, self.queue - self.drain_per_tick)
                    if self.peer is not None and self.stream_ms > 0:
                        self.sock.sendto(self._status(self.last_seq_seen),
                                         self.peer)


class SessionTests(unittest.TestCase):

    def setUp(self):
        self.fake = FakeDevice()
        self.devs = []

    def tearDown(self):
        for d in self.devs:
            d.close()
        self.fake.close()

    def new_dev(self, timeout=0.5):
        d = c5p1.Device(self.fake.addr, timeout=timeout, log=lambda m: None)
        self.devs.append(d)
        return d

    def run_cli(self, *argv):
        lines = []
        rc = c5p1.main(["--host", self.fake.addr[0],
                        "--port", str(self.fake.addr[1]),
                        "--timeout", "0.5", *argv], out=lines.append)
        return rc, lines

    # -- the bug this rewrite fixes -------------------------------------

    def test_fake_device_applies_the_firmware_stale_rule(self):
        """Guards the test itself: without this, the fake would hide the
        bug the next test is about."""
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(0.5)
        try:
            s.sendto(c5p1.frame(c5p1.OP_HELLO, 5), self.fake.addr)
            self.assertEqual(c5p1.unframe(s.recvfrom(2048)[0])[0], c5p1.OP_INFO)
            s.sendto(c5p1.frame(c5p1.OP_HELLO, 1), self.fake.addr)
            while True:
                op, seq, _ = c5p1.unframe(s.recvfrom(2048)[0])
                if seq == 1:
                    break
            self.assertEqual(op, c5p1.OP_STATUS)
        finally:
            s.close()

    def test_every_run_is_a_new_session_and_works(self):
        """B-1: `c5p1.py info`, then `c5p1.py enable`, then `start`, each a
        separate process starting from seq 0."""
        for cmd, state in [("info", None), ("enable", 2), ("start", 3),
                           ("status", 3), ("stop", 2)]:
            rc, out = self.run_cli(cmd)
            self.assertEqual(rc, 0, f"{cmd}: {out}")
            if state is not None:
                self.assertEqual(self.fake.state, state, cmd)

    def test_connect_continues_above_the_devices_seq(self):
        self.fake.last_seq_seen, self.fake.seq_started = 1000, True
        d = self.new_dev()
        info = d.connect()
        self.assertEqual(info["tick_hz"], 4_000_000)
        self.assertEqual(d.seq, 1001)
        self.assertEqual(d.control("enable")["reject"], 0)
        self.assertEqual(self.fake.state, 2)

    # -- reply matching ---------------------------------------------------

    def test_unsolicited_status_is_not_taken_as_the_reply(self):
        fake = self.fake
        fake.stream_ms = 1               # a status every millisecond
        d = self.new_dev()
        d.connect()
        time.sleep(0.05)                 # let the stream pile up
        st = d.control("start")          # wrong state: must be refused
        self.assertEqual(st["reject"], 6)
        self.assertEqual(st["last_seq_seen"], d.seq)
        st = d.control("enable")
        self.assertEqual(st["reject"], 0)
        self.assertEqual(st["state"], 2)

    def test_raw_ignores_the_background_stream(self):
        self.fake.stream_ms = 1
        for kind in ("crc", "magic", "version", "length", "opcode"):
            rc, out = self.run_cli("raw", "--corrupt", kind)
            self.assertEqual(rc, 0, f"{kind}: {out}")
        self.assertEqual(self.fake.rx[2], 5)

    # -- motion -----------------------------------------------------------

    def test_move_twice_from_separate_runs(self):
        """The second run starts at block_seq 0 while the device is still
        synced at the first run's last block."""
        self.assertEqual(self.run_cli("enable")[0], 0)
        self.assertEqual(self.run_cli("start")[0], 0)
        rc, out = self.run_cli("move", "--axis", "X", "--steps", "1000",
                               "--seconds", "1")
        self.assertEqual(rc, 0, out)
        first = self.fake.last_block
        rc, out = self.run_cli("move", "--axis", "X", "--steps", "-400",
                               "--seconds", "0.5")
        self.assertEqual(rc, 0, out)
        self.assertGreater(self.fake.last_block, first)
        self.assertEqual(self.fake.pfaults & 1, 0, "no sequence gap")
        self.assertEqual(self.fake.pos, 600 * 65536)

    def test_move_waits_out_backpressure(self):
        self.fake.drain_per_tick = 32
        self.run_cli("enable")
        self.run_cli("start")
        # 2 s at 4 ms = 500 records: several times the 64-slot queue.
        rc, out = self.run_cli("move", "--axis", "Y", "--steps", "5000",
                               "--seconds", "2")
        self.assertEqual(rc, 0, out)
        self.assertEqual(self.fake.pos, 0)          # Y is not axis 0
        self.assertGreater(self.fake.pfaults & 4, 0, "queue-full was hit")

    def test_gap_after_produces_a_sequence_gap(self):
        self.run_cli("enable")
        self.run_cli("start")
        rc, out = self.run_cli("move", "--axis", "X", "--steps", "1000",
                               "--seconds", "1", "--gap-after", "1")
        self.assertEqual(rc, 1)
        self.assertTrue(any("sequence gap" in line for line in out), out)
        self.assertEqual(self.fake.pfaults & 1, 1)

    def test_move_refuses_before_enable(self):
        rc, out = self.run_cli("move", "--axis", "X", "--steps", "10")
        self.assertEqual(rc, 1)
        self.assertTrue(any("enable" in line for line in out))
        self.assertFalse(any(op == c5p1.OP_MOTION
                             for op, _ in self.fake.received))

    def test_lost_motion_reply_is_retried_without_a_gap(self):
        d = self.new_dev(timeout=0.2)
        d.connect()
        d.control("enable")
        d.sync_blocks(d.status())
        self.fake.drop_replies_to.add(c5p1.OP_MOTION)
        st, block, ok = d.send_block([[65536, 0, 0, 0, 0]], 4000)
        self.assertTrue(ok)
        st, block2, ok = d.send_block([[65536, 0, 0, 0, 0]], 4000)
        self.assertTrue(ok)
        self.assertEqual(block2, block + 1)
        self.assertEqual(self.fake.pos, 2 * 65536, "queued once, not twice")
        self.assertEqual(self.fake.pfaults & 1, 0)

    # -- outputs, shell ---------------------------------------------------

    def test_outputs(self):
        self.run_cli("enable")
        rc, out = self.run_cli("outputs", "--relay", "on", "--spindle", "250")
        self.assertEqual(rc, 0, out)
        self.assertEqual((self.fake.relay, self.fake.spindle), (1, 250))
        rc, out = self.run_cli("outputs", "--spindle", "1001")
        self.assertEqual(rc, 1)
        self.assertEqual(self.fake.spindle, 250)

    def test_shell_script_runs_in_one_session(self):
        script = io.StringIO(
            "# bench script\n"
            "info\n"
            "enable\n"
            "start\n"
            "move --axis X --steps 200 --seconds 0.2\n"
            "sleep 0.05\n"
            "status\n")
        d = self.new_dev()
        out = []
        args = c5p1.build_parser().parse_args(["shell"])
        rc = c5p1.cmd_shell(d, args, out.append, lines=script)
        self.assertEqual(rc, 0, out)
        self.assertEqual(self.fake.pos, 200 * 65536)
        hellos = [op for op, _ in self.fake.received if op == c5p1.OP_HELLO]
        self.assertEqual(len(hellos), 1, "one connection for the whole script")

    def test_shell_script_stops_at_the_first_failure(self):
        script = io.StringIO("start\nmove --axis X --steps 200\n")
        d = self.new_dev()
        out = []
        args = c5p1.build_parser().parse_args(["shell"])
        rc = c5p1.cmd_shell(d, args, out.append, lines=script)
        self.assertEqual(rc, 1)
        self.assertFalse(any(op == c5p1.OP_MOTION
                             for op, _ in self.fake.received))

    def test_shell_reports_a_typo_and_continues_with_keep_going(self):
        script = io.StringIO("bogus\nstatus\n")
        d = self.new_dev()
        out = []
        args = c5p1.build_parser().parse_args(["shell", "--keep-going"])
        rc = c5p1.cmd_shell(d, args, out.append, lines=script)
        self.assertEqual(rc, 1)
        self.assertTrue(any(line.startswith("error:") for line in out))
        self.assertTrue(any("SAFE_IDLE" in line for line in out))


class TransportTests(unittest.TestCase):

    def test_no_device_is_reported_not_raised(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.bind(("127.0.0.1", 0))         # bound, silent
        try:
            lines = []
            rc = c5p1.main(["--host", "127.0.0.1", "--port",
                            str(s.getsockname()[1]), "--timeout", "0.2",
                            "info"], out=lines.append)
            self.assertEqual(rc, 2)
            self.assertTrue(any("no reply" in line for line in lines))
        finally:
            s.close()

    def test_windows_connection_reset_is_a_timeout(self):
        """WSAECONNRESET after an ICMP port-unreachable must look like
        silence, not crash the tool."""

        class ResettingSocket:
            def __init__(self):
                self.calls = 0

            def settimeout(self, t):
                pass

            def setblocking(self, b):
                pass

            def sendto(self, data, addr):
                pass

            def recvfrom(self, n):
                self.calls += 1
                if self.calls < 3:
                    raise ConnectionResetError(10054, "reset")
                raise socket.timeout()

            def close(self):
                pass

        d = c5p1.Device(("127.0.0.1", 9), timeout=0.1,
                        sock=ResettingSocket(), log=lambda m: None)
        with self.assertRaises(c5p1.NoReply):
            d.status()


if __name__ == "__main__":
    unittest.main(verbosity=2)
