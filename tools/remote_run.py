#!/usr/bin/env python3
"""PC-link test runner: offline suites by default, --live for the bench.

usage:
  python3 tools/remote_run.py                 # offline: codec + control policy
  python3 tools/remote_run.py --live        # + serial transport + stream suites

Exit code 0 = all executed suites passed, 1 = at least one failure or error.
"""

import argparse
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import remote_proto as rp  # noqa: E402
import remote_tests as rt  # noqa: E402
from remote_client import LinkClient, SerialLink, SerialError  # noqa: E402


def find_port(explicit: str) -> str:
    if explicit:
        return explicit
    for pat in ("/dev/ttyUSB*", "/dev/ttyACM*", "/dev/serial/by-id/*"):
        for p in sorted(glob.glob(pat)):
            if os.path.exists(p):
                return p
    raise SerialError("no serial port found (looked in /dev/ttyUSB*, /dev/ttyACM*, /dev/serial/by-id/*)")


def main() -> int:
    ap = argparse.ArgumentParser(description="DSO183 PC-link test runner")
    ap.add_argument("--live", action="store_true", help="run against the bench device on the console UART")
    ap.add_argument("--port", default=None, help="serial port (default: auto-detect)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=3.0)
    args = ap.parse_args()

    results = []
    results.extend(rt.run_offline_suites())

    client = None
    if args.live:
        port = find_port(args.port)
        link = SerialLink(port, args.baud, timeout=args.timeout)
        client = LinkClient(link, timeout=args.timeout)
        results.extend(rt.suite_control(client, tag="live"))
        results.extend(rt.suite_serial(client))
        results.extend(rt.suite_stream(client))
        try:
            client.close()
        finally:
            link.ser.close()
        client = None

    bad = 0
    for r in results:
        flag = "PASS" if r["ok"] else "FAIL"
        if not r["ok"]:
            bad += 1
        print("  [%s] %s :: %s  (%s)" % (flag, r["suite"], r["name"], r["detail"]))

    print("remote_run: %d result(s), %d failure(s)" % (len(results), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
