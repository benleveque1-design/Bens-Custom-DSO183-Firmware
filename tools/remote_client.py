#!/usr/bin/env python3
"""Serial transport + PC-link client + frame reader for the DSO183 scope.

Uses pyserial on the console UART (PA9/PA10, 115200 8N1, 3.3 V logic) - the
same link the in-firmware PC protocol speaks.

The device shares this UART with its periodic console output ("alive", bench
stats), so the client must scan a persistent receive buffer for reply packets
and discard noise in between.

usage: python3 tools/remote_client.py --port /dev/ttyUSB0 --selftest
"""

import argparse
import glob
import os
import time

import serial

import remote_proto as rp

SOF0 = 0xA5
SOF1 = 0x5A


class SerialError(RuntimeError):
    pass


class ProtocolError(RuntimeError):
    pass


class SerialLink:
    def __init__(self, port: str, baud: int = 115200, timeout: float = 1.0):
        self.port = port
        self.baud = baud
        self.rx = bytearray()
        self.ser = serial.Serial(
            port=port, baudrate=baud, timeout=timeout,
            write_timeout=5.0, bytesize=8, parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
        )
        # Avoid toggling control lines on open: on many USB-UART adapters DTR/RTS
        # are wired to MCU reset/boot and asserting them can disturb or reset
        # the target (especially when a monitor opens/closes the port often).
        try:
            self.ser.dtr = False
            self.ser.rts = False
        except Exception:
            pass

    def write(self, data: bytes):
        self.ser.write(bytes(data))

    def drain(self, seconds: float = 0.05) -> bytes:
        end = time.monotonic() + seconds
        out = bytearray()
        while time.monotonic() < end:
            chunk = self.ser.read(64)
            if chunk:
                out += chunk
        return bytes(out)

    def read(self, n: int, deadline: float) -> bytes:
        """Read up to n bytes before `deadline` (monotonic).  Nothing is
        consumed into self.rx here; callers own the buffer."""
        out = bytearray()
        while len(out) < n:
            if time.monotonic() > deadline:
                break
            chunk = self.ser.read(n - len(out))
            if not chunk:
                break
            out += chunk
        return bytes(out)


class FrameReader:
    def __init__(self, link: SerialLink, timeout: float = 3.0):
        self.link = link
        self.timeout = timeout

    def read_frame(self, timeout: float = None) -> dict:
        """Read one device -> host sample frame.

        Returns {'seq': int, 'n': int, 'samples': [..]}, or None on read
        timeout.  Raises SerialError / rp.CodecError on a bad frame.
        """
        link = self.link
        tf = self.timeout if timeout is None else timeout
        deadline = time.monotonic() + tf
        while True:
            buf = link.rx
            i = 0
            while i + 8 <= len(buf):
                if buf[i] == 0xA5 and buf[i + 1] == 0x5A and buf[i + 2] == rp.TYPE_FRAME:
                    n = buf[i + 4] | (buf[i + 5] << 8)
                    end = i + 8 + 2 * n
                    if len(buf) >= end:
                        pkt = bytes(buf[i:end])
                        del buf[:end]
                        return rp.frame_parse(pkt)  # raises on CRC / structure error
                    break
                i += 1
            now = time.monotonic()
            if now >= deadline:
                return None
            data = link.read(32, min(deadline, now + min(0.25, deadline - now)))
            if len(data) == 0:
                time.sleep(0.005)
                continue
            link.rx += data


class LinkClient:
    def __init__(self, link: SerialLink, timeout: float = 3.0):
        self.link = link
        self.frame = FrameReader(link, timeout=timeout)
        self.timeout = timeout
        self.seq = 0
        self.latency_max = 0.0
        self.latency_last = None
        self.stale_replies = 0

    def close(self):
        try:
            self.link.ser.close()
        except Exception:
            pass

    def send_raw(self, pkt: bytes):
        self.link.write(pkt)

    def _scan_reply(self, seq: int, timeout: float):
        """Scan self.link.rx for a reply to `seq`.  Returns (status, value) or
        None on timeout; raises ProtocolError on a malformed packet.

        A reply carrying a DIFFERENT sequence number is stale (the device may
        duplicate a reply or the host may have missed one); it is discarded and
        the scan continues, so one lost reply cannot wedge the client."""
        deadline = time.monotonic() + timeout
        buf = self.link.rx
        while True:
            i = 0
            while i + 10 <= len(buf):
                if buf[i] == SOF0 and buf[i + 1] == SOF1 and buf[i + 2] == rp.TYPE_REP:
                    cmd, status, value = rp.rep_parse(bytes(buf[i:i + 10]))
                    if cmd["seq"] == seq:
                        del buf[:i + 10]
                        return (status, value)
                    # stale/duplicate reply: drop the packet, keep scanning
                    self.stale_replies += 1
                    del buf[:i + 10]
                    i = 0
                    continue
                i += 1
            now = time.monotonic()
            if now >= deadline:
                return None
            data = self.link.read(10, min(deadline, now + min(0.25, deadline - now)))
            if len(data) == 0:
                time.sleep(0.005)
                continue
            buf += data

    def cmd(self, op: int, arg: int, seq: int = None, expect_reply: bool = True,
            timeout: float = None):
        """Send one command; return (status, value) or None when no reply is
        expected (unknown opcode -> device drops silently)."""
        if seq is None:
            seq = self.seq & 0xFF
            self.seq = (self.seq + 1) & 0xFF
        t0 = time.monotonic()
        self.link.write(rp.cmd_bytes(seq, op, arg))
        if not expect_reply:
            got = self._scan_reply(seq, min(timeout or self.timeout, 0.75))
            return None if got is None else got
        got = self._scan_reply(seq, timeout or self.timeout)
        if got is None:
            raise SerialError("reply timeout for seq %d op %d" % (seq, op))
        dt = time.monotonic() - t0
        if dt > self.latency_max:
            self.latency_max = dt
        self.latency_last = dt
        return got

    def get_status(self, seq: int = None):
        return self.cmd(rp.OP_GET_STATUS, 0, seq=seq)

    def scan_reply(self, seq: int, timeout: float = None):
        """Scan for an incoming reply without sending anything."""
        return self._scan_reply(seq, timeout or self.timeout)


def find_port(explicit: str) -> str:
    if explicit:
        return explicit
    for pat in ("/dev/ttyUSB*", "/dev/ttyACM*", "/dev/serial/by-id/*"):
        for p in sorted(glob.glob(pat)):
            if os.path.exists(p):
                return p
    raise SerialError("no serial port found (looked in /dev/ttyUSB*, /dev/ttyACM*, /dev/serial/by-id/*)")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=None)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=3.0)
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    port = find_port(args.port)
    link = SerialLink(port, args.baud, timeout=args.timeout)
    try:
        client = LinkClient(link, timeout=args.timeout)
        st, val = client.cmd(rp.OP_GET_STATUS, 0)
        print("remote_client: GET_STATUS -> status %d value 0x%04X (latency %.3fs)"
              % (st, val, client.latency_last))
        if args.selftest:
            for _ in range(3):
                st, val = client.cmd(rp.OP_GET_STATUS, 0)
            print("remote_client: selftest OK, max latency %.3fs" % client.latency_max)
    finally:
        link.ser.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
