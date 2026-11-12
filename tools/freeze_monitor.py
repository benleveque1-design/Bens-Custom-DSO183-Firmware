#!/usr/bin/env python3
"""Long-running freeze/reset monitor for DSO183 (SWD + UART + optional camera).

Samples device health periodically and appends timestamped records to a log so
a user-reproduced freeze can be correlated with counter/comm state.

- SWD (read-only, pyocd): build magic/seq, boot_count, heartbeat, main_loops,
  init/acq errors. Detects stalls (no progress), resets (boot_count increase),
  and nonzero error flags.
- UART PC link (optional): GET_STATUS, latency, and key measurements/values.
  Timeouts/errors are recorded but do not stop monitoring.
- Camera (optional): periodic read-only snapshot fetch.

No writes to target memory/flash, no resets, no halts, no ESP32 changes.

Example (bench host, device connected via ST-Link + /dev/ttyUSB0).  The camera
URL is optional and specific to the local bench LAN:
  python3 tools/freeze_monitor.py --out evidence/freeze_run/log.txt \
      --elf build/dso183.elf --port /dev/ttyUSB0 --interval 1.0 \
      --camera-url <bench-camera-url> --camera-every 60
"""
import argparse
import datetime
import json
import os
import signal
import sys
import time
import urllib.request

# Allow running from repo root or tools/.
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
for p in (HERE, ROOT, os.path.join(ROOT, "tools")):
    if p not in sys.path:
        sys.path.insert(0, p)

import remote_client as rc  # noqa: E402
import remote_proto as rp  # noqa: E402

SYMBOLS = [
    "status_build_magic",
    "status_build_seq",
    "status_boot_count",
    "status_heartbeat",
    "status_main_loops",
    "status_display_id",
    "status_init_errors",
    "status_acq_hw_errors",
]
EXPECT_MAGIC = 0x4453424D

stop = False


def on_sig(_sig, _frm):
    global stop
    stop = True


def ts():
    return datetime.datetime.now().astimezone().isoformat(timespec="milliseconds")


def log_line(fp, msg):
    line = "%s %s" % (ts(), msg)
    print(line, flush=True)
    if fp:
        fp.write(line + "\n")
        fp.flush()


def elf_symbols(path):
    import re, subprocess
    out = subprocess.run(["arm-none-eabi-nm", path], capture_output=True, text=True)
    if out.returncode != 0:
        raise RuntimeError(out.stderr.strip() or "nm failed")
    table = {}
    for line in out.stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and re.fullmatch(r"[0-9a-fA-F]+", parts[0]):
            table[parts[2]] = int(parts[0], 16)
    return table


class SWDReader:
    def __init__(self, elf):
        self.elf = elf
        self.syms = None
        self.session = None
        self.mi = None
        self.err = None

    def ensure(self):
        if self.mi is not None:
            return True
        try:
            self.syms = elf_symbols(self.elf)
            missing = [s for s in SYMBOLS if s not in self.syms]
            if missing:
                self.err = "missing symbols: %s" % ",".join(missing)
                return False
            from pyocd.core.helpers import ConnectHelper
            from pyocd.coresight.ap import APv1Address
            self.session = ConnectHelper.session_with_chosen_probe(
                target_override="cortex_m", blocking=False)
            if self.session is None:
                self.err = "no probe found"
                return False
            probe = self.session.probe
            probe.open()
            probe.connect()
            self.mi = probe.get_memory_interface_for_ap(APv1Address(0))
            self.err = None
            return True
        except Exception as e:
            self.err = repr(e)
            self.close()
            return False

    def close(self):
        try:
            if self.session:
                self.session.close()
        except Exception:
            pass
        self.session = None
        self.mi = None

    def read(self):
        if not self.ensure():
            return None, self.err
        try:
            v = {}
            for s in SYMBOLS:
                v[s] = self.mi.read32(self.syms[s]) & 0xFFFFFFFF
            return v, None
        except Exception as e:
            self.err = repr(e)
            self.close()
            return None, self.err


def uart_sample(port, timeout):
    if not port:
        return None, "no-port"
    link = None
    try:
        p = rc.find_port(port)
        link = rc.SerialLink(p, 115200, timeout=timeout)
        c = rc.LinkClient(link, timeout=timeout)
        link.drain(0.05)
        t0 = time.monotonic()
        st, val = c.cmd(rp.OP_GET_STATUS, 0)
        lat = time.monotonic() - t0
        meas = {}
        for k in ("GETV_BUILD_SEQ", "GETV_STATUS", "GETV_TIMEBASE",
                  "GETV_TRIG_MODE", "GETV_TRIG_LEVEL", "GETV_FREQ_HZ",
                  "GETV_DUTY_X10", "GETV_VPP_COUNTS", "GETV_MIN", "GETV_MAX"):
            try:
                mst, mv = c.cmd(rp.OP_GET_VALUE, getattr(rp, k))
                meas[k] = [mst, mv]
            except Exception as e:
                meas[k] = ["ERR", str(e)]
        return {"status": [st, val], "latency_s": round(lat, 4), "meas": meas}, None
    except Exception as e:
        return None, repr(e)
    finally:
        try:
            if link:
                link.ser.close()
        except Exception:
            pass


def save_camera(url, path):
    try:
        with urllib.request.urlopen(url, timeout=10) as r, open(path, "wb") as f:
            f.write(r.read())
        return True, None
    except Exception as e:
        return False, repr(e)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, help="log file path (dir created)")
    ap.add_argument("--elf", default="build/dso183.elf")
    ap.add_argument("--port", default="/dev/ttyUSB0", help="UART port, or empty to skip")
    ap.add_argument("--interval", type=float, default=1.0)
    ap.add_argument("--duration", type=float, default=36000.0, help="seconds (default 10h)")
    ap.add_argument("--uart-every", type=int, default=1, help="UART sample every N SWD samples")
    ap.add_argument("--camera-url", default="")
    ap.add_argument("--camera-every", type=float, default=60.0, help="seconds between snapshots")
    ap.add_argument("--camera-dir", default="", help="defaults to <out>.camera")
    args = ap.parse_args()

    signal.signal(signal.SIGINT, on_sig)
    signal.signal(signal.SIGTERM, on_sig)

    os.makedirs(os.path.dirname(os.path.abspath(args.out)) or ".", exist_ok=True)
    cam_dir = args.camera_dir or (args.out + ".camera")
    if args.camera_url:
        os.makedirs(cam_dir, exist_ok=True)

    swd = SWDReader(args.elf)
    prev = None
    start = time.monotonic()
    deadline = start + max(1.0, args.duration)
    next_cam = start
    n = 0
    alerts = 0

    with open(args.out, "a", encoding="utf-8") as fp:
        log_line(fp, "START freeze_monitor interval=%.2fs duration=%.0fs elf=%s port=%s"
                 % (args.interval, args.duration, args.elf, args.port))
        while not stop and time.monotonic() < deadline:
            n += 1
            now = time.monotonic()
            rec = {"ts": ts(), "n": n, "t_s": round(now - start, 3)}
            # SWD
            v, err = swd.read()
            rec["swd"] = v if v else {"error": err}
            # analyze
            flags = []
            if v and prev:
                hb_d = (v["status_heartbeat"] - prev["status_heartbeat"]) & 0xFFFFFFFF
                ml_d = (v["status_main_loops"] - prev["status_main_loops"]) & 0xFFFFFFFF
                rec["delta"] = {"heartbeat": hb_d, "main_loops": ml_d,
                                "boot_count": (v["status_boot_count"] - prev["status_boot_count"]) & 0xFFFFFFFF}
                if hb_d == 0:
                    flags.append("STALL_HEARTBEAT")
                if ml_d == 0:
                    flags.append("STALL_MAIN_LOOP")
                if v["status_boot_count"] != prev["status_boot_count"]:
                    flags.append("BOOT_COUNT_CHANGED")
            if v:
                if v["status_build_magic"] != EXPECT_MAGIC:
                    flags.append("BAD_MAGIC")
                if v["status_init_errors"] != 0:
                    flags.append("INIT_ERRORS_%d" % v["status_init_errors"])
                if v["status_acq_hw_errors"] != 0:
                    flags.append("ACQ_HW_ERRORS_%d" % v["status_acq_hw_errors"])
            # UART
            if args.port and (n % max(1, args.uart_every) == 0):
                u, uerr = uart_sample(args.port, timeout=min(2.0, max(0.5, args.interval)))
                rec["uart"] = u if u else {"error": uerr}
                if uerr:
                    flags.append("UART_ERR")
            # camera
            if args.camera_url and now >= next_cam:
                cpath = os.path.join(cam_dir, "cam_%06d.jpg" % n)
                ok, cerr = save_camera(args.camera_url, cpath)
                rec["camera"] = {"path": cpath, "ok": ok, "error": cerr}
                next_cam = now + max(5.0, args.camera_every)
            if flags:
                rec["flags"] = flags
                alerts += 1
            # concise console line
            summary = []
            if v:
                d = rec.get("delta", {})
                summary.append("hb=%s%+s" % (v.get("status_heartbeat"), d.get("heartbeat", "?")))
                summary.append("loop=%s%+s" % (v.get("status_main_loops"), d.get("main_loops", "?")))
                summary.append("boot=%s" % v.get("status_boot_count"))
                summary.append("seq=%s" % v.get("status_build_seq"))
            else:
                summary.append("swd_err=%s" % err)
            if "uart" in rec:
                if "error" in rec["uart"]:
                    summary.append("uart=ERR")
                else:
                    summary.append("uart=ok/%sms" % int(1000 * rec["uart"].get("latency_s", 0)))
            if flags:
                summary.append("FLAGS=" + ",".join(flags))
            log_line(fp, "SAMPLE " + " ".join(str(x) for x in summary))
            # write structured record sidecar occasionally? keep in same log as JSON every time for parseability
            fp.write("JSON " + json.dumps(rec, sort_keys=True) + "\n")
            fp.flush()
            prev = v if v else prev
            # sleep remainder
            elapsed = time.monotonic() - now
            sleep_for = args.interval - elapsed
            if sleep_for > 0:
                # sleep in small chunks so SIGINT responsive
                end_sleep = time.monotonic() + sleep_for
                while not stop and time.monotonic() < end_sleep:
                    time.sleep(min(0.2, end_sleep - time.monotonic()))
        log_line(fp, "STOP samples=%d alerts=%d stopped=%s" % (n, alerts, stop))

    swd.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
