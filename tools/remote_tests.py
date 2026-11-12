#!/usr/bin/env python3
"""PC-link test suites: pure offline policy tests + live hardware suites.

Suites take a client with this interface (implemented by the real serial
client and by FakeClient for offline runs):

    client.cmd(op, arg, seq=None, expect_reply=True, timeout=None)
        -> (status, value) | None        # None => no reply came back

plus, for the live suites, a frame reader:

    client.frame.read_frame(timeout) -> dict | None

A TestResult is a dict: {'suite', 'name', 'ok', 'detail', 'skipped'}.
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import remote_proto as rp  # noqa: E402


# --------------------------------------------------------------------------
# Offline: pure protocol + control-policy suites (FakeClient, no hardware).
# --------------------------------------------------------------------------

def run_offline_suites() -> list:
    results = []

    fails = rp.offline_failures()
    results.append({
        "suite": "remote_proto",
        "name": "codec self-checks",
        "ok": len(fails) == 0,
        "detail": "; ".join(fails) if fails else "all checks passed",
        "skipped": False,
    })

    fake = FakeClient()
    results.extend(suite_control(fake, tag="offline"))

    return results


# --------------------------------------------------------------------------
# Control suite (policy behaviour of app/link_host.c).
# --------------------------------------------------------------------------

def suite_control(client, tag: str = "") -> list:
    out = []

    def run(name, op, arg, expect_status, expect_value=None, expect_reply=True):
        try:
            got = client.cmd(op, arg, expect_reply=expect_reply)
        except Exception as e:
            out.append({"suite": "control", "name": name, "ok": False,
                        "detail": "exception: %r" % e, "skipped": False})
            return
        if expect_status is None:
            # No reply expected (unknown opcode -> device drops silently).
            ok = got is None
            out.append({"suite": "control", "name": name, "ok": ok,
                        "detail": "no reply as expected" if ok else "unexpected reply",
                        "skipped": False})
            return
        if got is None:
            out.append({"suite": "control", "name": name, "ok": False,
                        "detail": "no reply", "skipped": False})
            return
        status, value = got
        ok = status == expect_status and (expect_value is None or value == expect_value)
        out.append({"suite": "control", "name": name, "ok": ok,
                    "detail": "status %d value 0x%04X (want status %d value %s)"
                              % (status, value, expect_status,
                                 ("0x%04X" % expect_value) if expect_value is not None else "?"),
                    "skipped": False})

    run("nop_arg_ignored", rp.OP_NOP, 0x1234, rp.ST_OK, 0)
    run("run", rp.OP_RUN, 0, rp.ST_OK, 1)
    run("get_status_after_run", rp.OP_GET_STATUS, 0, rp.ST_OK)
    run("stop", rp.OP_STOP, 0, rp.ST_OK, 0)
    run("get_status_after_stop", rp.OP_GET_STATUS, 0, rp.ST_OK)

    run("stream_on", rp.OP_STREAM, 1, rp.ST_OK, 1)
    run("get_status_streaming", rp.OP_GET_STATUS, 0, rp.ST_OK)
    run("stream_off", rp.OP_STREAM, 0, rp.ST_OK, 0)
    run("get_status_not_streaming", rp.OP_GET_STATUS, 0, rp.ST_OK)

    run("set_timebase_5", rp.OP_SET_TIMEBASE, 5, rp.ST_OK, 0)
    run("set_timebase_26", rp.OP_SET_TIMEBASE, 26, rp.ST_OK, 0)
    run("set_timebase_4_rejected", rp.OP_SET_TIMEBASE, 4, rp.ST_BAD_ARG, 0)
    run("set_timebase_27_rejected", rp.OP_SET_TIMEBASE, 27, rp.ST_BAD_ARG, 0)
    run("set_timebase_wrap_rejected", rp.OP_SET_TIMEBASE, 261, rp.ST_BAD_ARG, 0)

    run("set_trigger_2048", rp.OP_SET_TRIGGER, 2048, rp.ST_OK, 0)
    run("set_trigger_4095", rp.OP_SET_TRIGGER, 4095, rp.ST_OK, 0)
    run("set_trigger_4096_rejected", rp.OP_SET_TRIGGER, 4096, rp.ST_BAD_ARG, 0)

    run("set_span_16", rp.OP_SET_SPAN, 16, rp.ST_OK, 0)
    run("set_span_128", rp.OP_SET_SPAN, 128, rp.ST_OK, 0)
    run("set_span_15_rejected", rp.OP_SET_SPAN, 15, rp.ST_BAD_ARG, 0)
    run("set_span_4095", rp.OP_SET_SPAN, 4095, rp.ST_OK, 0)
    run("set_span_4096_rejected", rp.OP_SET_SPAN, 4096, rp.ST_BAD_ARG, 0)

    run("stream_2_rejected", rp.OP_STREAM, 2, rp.ST_BAD_ARG, 0)
    run("stream_ffff_rejected", rp.OP_STREAM, 0xFFFF, rp.ST_BAD_ARG, 0)

    # Unknown opcode: the device drops the packet without a reply.
    # For live clients, cmd() can't encode unknown ops (proto rejects them),
    # so we send a raw packet. The offline FakeClient handles it in cmd().
    try:
        if hasattr(client, 'send_raw') and hasattr(client, 'scan_reply'):
            # Craft raw packet with opcode 0x7F (bypassing proto validation)
            raw = bytearray([rp.SOF0, rp.SOF1, rp.TYPE_CMD, 0xFE, 0x7F, 0, 0, 0])
            crc = rp.crc16(bytes(raw[2:8]))
            raw += bytes((crc & 0xFF, (crc >> 8) & 0xFF))
            client.send_raw(bytes(raw))
            got = client.scan_reply(0xFE, timeout=0.75)
            ok = got is None
            out.append({"suite": "control", "name": "unknown_opcode_no_reply", "ok": ok,
                        "detail": "no reply as expected" if ok else "unexpected reply",
                        "skipped": False})
        else:
            run("unknown_opcode_no_reply", 0x7F, 0, None, None, expect_reply=False)
    except Exception as e:
        out.append({"suite": "control", "name": "unknown_opcode_no_reply", "ok": False,
                    "detail": "exception: %r" % e, "skipped": False})

    return out


# --------------------------------------------------------------------------
# FakeClient: in-memory implementation of the device policy (offline).
# --------------------------------------------------------------------------

class FakeClient:
    """In-memory stand-in speaking the same client interface as LinkClient."""

    def __init__(self):
        self.run = 0
        self.stream = 0
        self.timebase = None
        self.trigger = None
        self.span = None
        self.trigger_mode = 0
        self.trigger_slope = 2
        self.cal_offset_mv = 0
        self.cal_gain_x1000 = 1000
        self.avg_n = 1
        self.wave_saved = [False, False]

    def cmd(self, op: int, arg: int, seq: int = None, expect_reply: bool = True,
            timeout: float = None):
        if op == rp.OP_GET_STATUS:
            return (rp.ST_OK, (self.run & 1) | ((self.stream & 1) << 1))
        if op == rp.OP_NOP:
            return (rp.ST_OK, 0)
        if op == rp.OP_RUN:
            self.run = 1
            return (rp.ST_OK, 1)
        if op == rp.OP_STOP:
            self.run = 0
            return (rp.ST_OK, 0)
        if op == rp.OP_STREAM:
            if not rp.arg_valid(op, arg):
                return (rp.ST_BAD_ARG, 0)
            self.stream = arg & 1
            return (rp.ST_OK, self.stream)
        if op == rp.OP_SET_TIMEBASE:
            if not rp.arg_valid(op, arg):
                return (rp.ST_BAD_ARG, 0)
            self.timebase = arg
            return (rp.ST_OK, 0)
        if op == rp.OP_SET_TRIGGER:
            if not rp.arg_valid(op, arg):
                return (rp.ST_BAD_ARG, 0)
            self.trigger = arg
            return (rp.ST_OK, 0)
        if op == rp.OP_SET_SPAN:
            if not (16 <= arg <= 4095):
                return (rp.ST_BAD_ARG, 0)
            self.span = arg
            return (rp.ST_OK, 0)
        if op == rp.OP_SET_TRIG_MODE:
            if not rp.arg_valid(op, arg):
                return (rp.ST_BAD_ARG, 0)
            self.trigger_mode = arg
            return (rp.ST_OK, arg)
        if op == rp.OP_SET_TRIG_SLOPE:
            if not rp.arg_valid(op, arg):
                return (rp.ST_BAD_ARG, 0)
            self.trigger_slope = arg
            return (rp.ST_OK, arg)
        if op == rp.OP_GET_VALUE:
            if not rp.arg_valid(op, arg):
                return (rp.ST_BAD_ARG, 0)
            vals = {rp.GETV_TRIG_MODE: self.trigger_mode, rp.GETV_STATUS: (self.run & 1) | ((self.stream & 1) << 1),
                    rp.GETV_TIMEBASE: self.timebase or 0, rp.GETV_SPAN: self.span or 0,
                    rp.GETV_TRIG_LEVEL: self.trigger or 0, rp.GETV_TRIG_SLOPE: self.trigger_slope,
                    rp.GETV_CAL_OFFSET_MV: self.cal_offset_mv & 0xFFFF,
                    rp.GETV_CAL_GAIN_X1000: self.cal_gain_x1000 & 0xFFFF,
                    rp.GETV_WAVE_SAVED_0: 1 if self.wave_saved[0] else 0,
                    rp.GETV_WAVE_SAVED_1: 1 if self.wave_saved[1] else 0,
                    rp.GETV_AVG_N: self.avg_n}
            return (rp.ST_OK, vals.get(arg, 0))
        if op == rp.OP_CAL_SET_OFFSET_MV:
            if not rp.arg_valid(op, arg): return (rp.ST_BAD_ARG, 0)
            # interpret as signed16
            self.cal_offset_mv = arg if arg < 0x8000 else arg - 0x10000
            return (rp.ST_OK, arg & 0xFFFF)
        if op == rp.OP_CAL_SET_GAIN_X1000:
            if not rp.arg_valid(op, arg): return (rp.ST_BAD_ARG, 0)
            self.cal_gain_x1000 = arg
            return (rp.ST_OK, arg)
        if op == rp.OP_CAL_SAVE:
            return (rp.ST_OK, 0)
        if op == rp.OP_CAL_RESET:
            self.cal_offset_mv = 0; self.cal_gain_x1000 = 1000
            return (rp.ST_OK, 0)
        if op == rp.OP_WAVE_SAVE:
            if not rp.arg_valid(op, arg): return (rp.ST_BAD_ARG, 0)
            self.wave_saved[arg & 1] = True
            return (rp.ST_OK, arg & 1)
        if op == rp.OP_WAVE_RECALL:
            if not rp.arg_valid(op, arg): return (rp.ST_BAD_ARG, 0)
            if not self.wave_saved[arg & 1]: return (rp.ST_BAD_ARG, 0)
            self.run = 0
            return (rp.ST_OK, arg & 1)
        if op == rp.OP_SET_AVG:
            if not rp.arg_valid(op, arg): return (rp.ST_BAD_ARG, 0)
            self.avg_n = arg
            return (rp.ST_OK, arg)
        if op >= rp.OP_COUNT:
            return None
        return (rp.ST_BAD_ARG, 0)


# --------------------------------------------------------------------------
# Live hardware suites.
# --------------------------------------------------------------------------

def suite_serial(client) -> list:
    out = []
    try:
        st, val = client.cmd(rp.OP_GET_STATUS, 0)
        ok = st == rp.ST_OK
        out.append({"suite": "serial", "name": "get_status_reply",
                    "ok": ok, "detail": "status %d value 0x%04X" % (st, val),
                    "skipped": False})
        lat = client.latency_last
        ok = lat is not None and lat < 5.0
        out.append({"suite": "serial", "name": "reply_latency_under_5s",
                    "ok": ok, "detail": "last round trip %.3fs" % (lat or -1.0),
                    "skipped": False})

        # A command with a corrupted CRC must be dropped by the device without
        # killing the link: no reply, and the NEXT good command must still get
        # its reply (the RX stream stays framed correctly).
        bad = bytearray(rp.cmd_bytes(0xEE, rp.OP_NOP, 0x1234))
        bad[8] ^= 0x01  # corrupt CRC
        client.send_raw(bytes(bad))
        no_reply = client.scan_reply(0xEE, min(client.timeout, 0.75))
        ok = no_reply is None
        out.append({"suite": "serial", "name": "bad_crc_no_reply",
                    "ok": ok, "detail": "corrupted-CRC command dropped" if ok
                           else "unexpected reply after corruption",
                    "skipped": False})
        r = client.cmd(rp.OP_GET_STATUS, 0, seq=0xEF)
        ok = r is not None and r[0] == rp.ST_OK
        out.append({"suite": "serial", "name": "post_corruption_recovers",
                    "ok": ok, "detail": "status %d value 0x%04X" % (r if r is None else (r[0], r[1])),
                    "skipped": False})
    except Exception as e:
        out.append({"suite": "serial", "name": "transport_smoke", "ok": False,
                    "detail": "exception: %r" % e, "skipped": False})
    return out


def suite_stream(client) -> list:
    out = []
    frames = []
    try:
        r = client.cmd(rp.OP_STREAM, 1)
        ok1 = r is not None and r[0] == rp.ST_OK and r[1] == 1
        out.append({"suite": "stream", "name": "stream_on", "ok": ok1,
                    "detail": "status %d value %d" % (r[0], r[1]) if r else "no reply",
                    "skipped": False})
        r = client.cmd(rp.OP_RUN, 0)
        ok2 = r is not None and r[0] == rp.ST_OK and r[1] == 1
        out.append({"suite": "stream", "name": "run_for_stream", "ok": ok2,
                    "detail": "status %d value %d" % (r[0], r[1]) if r else "no reply",
                    "skipped": False})

        deadline = time.monotonic() + 15.0
        while len(frames) < 3 and time.monotonic() < deadline:
            f = client.frame.read_frame(timeout=2.0)
            if f is None:
                continue
            frames.append(f)
        ok = len(frames) == 3
        det = "%d frames in window" % len(frames)
        content_ok = True
        if ok:
            contiguous = all(
                ((frames[i + 1]["seq"] - frames[i]["seq"]) & 0xFF) == 1
                for i in range(len(frames) - 1))
            ok = contiguous
            det += " seq contiguous=%s" % contiguous
            content_ok = ok and all(
                f["n"] == 256 and all(0 <= s <= 4095 for s in f["samples"])
                for f in frames)
            if content_ok:
                det += " frame content ok (n=256, samples in 0..4095)"
        out.append({"suite": "stream", "name": "frames_captured", "ok": ok,
                    "detail": det, "skipped": False})

        r = client.cmd(rp.OP_STOP, 0)
        out.append({"suite": "stream", "name": "stop_after_stream",
                    "ok": r is not None and r[0] == rp.ST_OK,
                    "detail": "status %d value %d" % (r[0], r[1]) if r else "no reply",
                    "skipped": False})
        r = client.cmd(rp.OP_STREAM, 0)
        ok3 = r is not None and r[0] == rp.ST_OK and r[1] == 0
        out.append({"suite": "stream", "name": "stream_off_after_stream",
                    "ok": ok3,
                    "detail": "status %d value %d" % (r[0], r[1]) if r else "no reply",
                    "skipped": False})
    except Exception as e:
        out.append({"suite": "stream", "name": "stream_window", "ok": False,
                    "detail": "exception: %r" % e, "skipped": False})
    return out
