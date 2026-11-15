#!/usr/bin/env python3
"""B4.36 hardware validation campaign (safeguarded, read-mostly after deploy).

Run on the host connected to the DSO183 UART. Requires the instrument to be
running candidate build 41320 (deployed separately via tools/flash_cycle.py).
No reset, no calibration writes, no flash operations.

Usage:
  python3 tools/validate_b436.py --port /dev/ttyUSB0 --out evidence/B4_36_HW_VALIDATION/<ts>
"""
import argparse
import json
import os
import statistics
import sys
import time
import traceback

sys.path.insert(0, os.path.dirname(__file__))
import remote_proto as rp
from remote_client import SerialLink, LinkClient

CMD_TIMEOUT = 4.0
TARGET_BUILD = 41320

def jdump(path, obj):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=2, sort_keys=False, default=str)
        f.write("\n")

class Unresponsive(RuntimeError):
    pass

class Runner:
    def __init__(self, outdir, port):
        self.outdir = outdir
        self.port = port
        os.makedirs(outdir, exist_ok=True)
        self.rawdir = os.path.join(outdir, "raw")
        os.makedirs(self.rawdir, exist_ok=True)
        self.jsonl = open(os.path.join(outdir, "captures.jsonl"), "a", buffering=1, encoding="utf-8")
        self.logf = open(os.path.join(outdir, "campaign.log"), "a", buffering=1, encoding="utf-8")
        self.t0 = time.time()
        self.cmd_count = 0
        self.cmd_errors = 0
        self.consec_fail = 0
        self.link = SerialLink(port, timeout=CMD_TIMEOUT)
        self.client = LinkClient(self.link, timeout=CMD_TIMEOUT)
        self.link.drain(0.4)

    def note(self, msg):
        s = "[%8.2fs] %s" % (time.time() - self.t0, msg)
        self.logf.write(s + "\n"); self.logf.flush()
        try:
            print(s, flush=True)
        except Exception:
            pass

    def rec(self, kind, **f):
        r = {"ts": round(time.time() - self.t0, 3),
             "utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
             "kind": kind}
        r.update(f)
        self.jsonl.write(json.dumps(r, sort_keys=False, default=str) + "\n")
        self.jsonl.flush()
        try:
            os.fsync(self.jsonl.fileno())
        except OSError:
            pass
        return r

    def cmd(self, op, arg, tries=3, allow_fail=False):
        last = None
        for _ in range(tries):
            try:
                st, val = self.client.cmd(op, arg)
                self.consec_fail = 0
                self.cmd_count += 1
                return st, val
            except Exception as e:
                last = e
                self.cmd_errors += 1
                time.sleep(0.05)
        self.consec_fail += 1
        if allow_fail:
            return None, None
        if self.consec_fail >= 5:
            raise Unresponsive("unresponsive: %r" % (last,))
        return None, None

    def gv(self, idx, allow_fail=False):
        st, v = self.cmd(rp.OP_GET_VALUE, idx, allow_fail=allow_fail)
        if st is None or st != 0:
            return None
        return v

    def poll(self, idxs):
        out = {}
        t = time.time()
        for i in idxs:
            out[i] = self.gv(i)
        out["_poll_ms"] = round((time.time() - t) * 1000.0, 1)
        return out

    def set_stream(self, on):
        return self.cmd(rp.OP_STREAM, 1 if on else 0)

    def set_run(self, run):
        return self.cmd(rp.OP_RUN if run else rp.OP_STOP, 0)

    def configure(self, tb=None, mode=None, slope=None, level=None, avg=1):
        if tb is not None:
            self.cmd(rp.OP_SET_TIMEBASE, tb); time.sleep(0.08)
        if mode is not None:
            self.cmd(rp.OP_SET_TRIG_MODE, mode); time.sleep(0.05)
        if slope is not None:
            self.cmd(rp.OP_SET_TRIG_SLOPE, slope); time.sleep(0.04)
        if level is not None:
            self.cmd(rp.OP_SET_TRIGGER, level); time.sleep(0.04)
        if avg is not None:
            self.cmd(rp.OP_SET_AVG, avg); time.sleep(0.04)
        time.sleep(0.30)
        cfg = self.poll([2, 1, 12, 0, 17, 4, 11])
        self.rec("configure", requested={"tb": tb, "mode": mode, "slope": slope, "level": level, "avg": avg}, actual=cfg)
        return cfg

    def coherent_diag_snapshot(self):
        """B4.36: GETV 46 latches the whole per-capture struct; 18-27,45,47-50 read that copy."""
        latched = self.gv(rp.GETV_DIAG_LATCH)
        if latched is None:
            return {}, False, "latch_read_error"
        fields = [18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 45, 47, 48, 49, 50]
        vals = {rp.GETV_DIAG_LATCH: latched}
        for k in fields:
            v = self.gv(k)
            if v is None:
                return {}, False, "read_error"
            vals[k] = v
        if vals.get(18) != latched:
            # should not happen (18 reads latched copy), but record honestly
            return vals, False, "seq_field_mismatch"
        return vals, True, "ok"

    def read_frame(self, timeout=8.0):
        try:
            f = self.client.frame.read_frame(timeout=timeout)
        except Exception as e:
            return None, repr(e)
        if f is None:
            return None, "timeout"
        return f, None

    def crossings(self, samples, level):
        out = []
        for i in range(1, len(samples)):
            p = samples[i - 1]; c = samples[i]
            if p < level and c >= level:
                out.append({"i": i, "type": "rise", "p": p, "c": c})
            elif p > level and c <= level:
                out.append({"i": i, "type": "fall", "p": p, "c": c})
        return out

    def nearest_cross(self, crosses, typ, target=96):
        best = None
        for x in crosses:
            if x["type"] != typ:
                continue
            d = abs(x["i"] - target)
            if best is None or d < best["dist"]:
                best = dict(x); best["dist"] = d
        return best

    def capture_one(self, cfg_name, tb, mode, slope, level, idx, save_samples=True):
        self.set_stream(0)
        self.link.drain(0.05)
        pre_seq = self.gv(18)
        self.set_stream(1)
        frame, ferr = self.read_frame(timeout=8.0)
        snap, snap_ok, snap_status = self.coherent_diag_snapshot()
        self.set_stream(0)
        self.link.drain(0.03)

        post_seq = snap.get(18) if snap_ok else self.gv(18)
        rec = {
            "config": cfg_name,
            "tb": tb, "mode": mode, "slope_req": slope, "level_req": level,
            "index": idx,
            "build_seq": self.gv(11),
            "diag_seq_pre": pre_seq,
            "diag_seq_post": post_seq,
            "diag_snapshot_ok": snap_ok,
            "diag_snapshot_status": snap_status,
            "diag": snap if snap_ok else {},
            "frame_ok": frame is not None,
            "frame_error": ferr,
        }
        if frame is not None:
            samples = frame["samples"]
            rec["frame_seq"] = frame["seq"]
            rec["frame_n"] = frame["n"]
            rec["frame_min"] = min(samples)
            rec["frame_max"] = max(samples)
            rec["frame_mean"] = round(statistics.fmean(samples), 3)
            rec["frame_stdev"] = round(statistics.pstdev(samples), 3) if len(samples) > 1 else 0
            lvl = level
            if snap_ok and snap.get(26) is not None:
                lvl = snap.get(26)
            crosses = self.crossings(samples, lvl)
            rec["cross_level"] = lvl
            rec["crossings"] = crosses
            rec["nr"] = self.nearest_cross(crosses, "rise", 96)
            rec["nf"] = self.nearest_cross(crosses, "fall", 96)
            rec["rise_count"] = sum(1 for x in crosses if x["type"] == "rise")
            rec["fall_count"] = sum(1 for x in crosses if x["type"] == "fall")
            # B4.36 derived anchor offset (mod 1024 ring)
            if snap_ok:
                hw = snap.get(20)
                anch = snap.get(21)
                if hw is not None and anch is not None:
                    d = (anch - hw) % 1024
                    rec["anchor_offset"] = d if d <= 512 else d - 1024
                if snap.get(47) is not None and rec.get("anchor_offset") is not None:
                    rec["effective_span"] = int(snap[47]) - int(rec["anchor_offset"])
            if save_samples:
                fn = "cap_%s_%03d.json" % (cfg_name, idx)
                jdump(os.path.join(self.rawdir, fn), {"meta": {k: v for k, v in rec.items() if k != "crossings"}, "samples": samples, "crossings": crosses})
                rec["raw_file"] = fn
        else:
            rec["crossings"] = []

        coherent = rec["frame_ok"] and snap_ok and post_seq is not None and pre_seq is not None and post_seq != pre_seq
        rec["coherent"] = coherent
        reasons = []
        if not rec["frame_ok"]:
            reasons.append("no_frame")
        if not snap_ok:
            reasons.append("diag_" + snap_status)
        if rec["frame_ok"] and snap_ok and post_seq is not None and pre_seq is not None and post_seq == pre_seq:
            reasons.append("seq_unchanged_no_fresh_diag")
        rec["coherence_notes"] = reasons
        self.rec("capture", **rec)
        return rec

    def run_config(self, tb, mode, slope, level, name, n=50):
        self.note("CONFIG %s tb=%s mode=%s slope=%s level=%s n=%d" % (name, tb, mode, slope, level, n))
        self.configure(tb=tb, mode=mode, slope=slope, level=level, avg=1)
        self.set_run(1)
        time.sleep(0.5)
        self.set_stream(1)
        for _ in range(2):
            self.read_frame(timeout=5.0)
        self.set_stream(0)
        self.link.drain(0.1)
        caps = []
        for i in range(n):
            cap = self.capture_one(name, tb, mode, slope, level, i, save_samples=(i < 5))
            caps.append(cap)
            if (i + 1) % 10 == 0:
                coh = sum(1 for c in caps if c.get("coherent"))
                self.note("  %s %d/%d coherent_so_far=%d" % (name, i + 1, n, coh))
        self.sample_ets_diag()
        self.summarize_config(name, caps)
        return caps

    def sample_ets_diag(self):
        idxs = [38, 39, 40, 41, 42, 43, 44, 45, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 47, 48, 49, 50]
        out = self.poll(idxs)
        self.rec("ets_diag_sample", values=out)
        return out

    def summarize_config(self, name, caps):
        total = len(caps)
        coherent = [c for c in caps if c.get("coherent")]
        no_frame = [c for c in caps if not c.get("frame_ok")]
        diag_bad = [c for c in caps if c.get("diag_snapshot_status") != "ok"]

        def svals(v):
            v = [x for x in v if x is not None]
            if not v:
                return None
            return {"n": len(v), "min": min(v), "max": max(v),
                    "mean": round(statistics.fmean(v), 3),
                    "stdev": round(statistics.pstdev(v), 3) if len(v) > 1 else 0}

        def stats_for(lst):
            if not lst:
                return {}
            def dvals(k): return [c["diag"].get(k) for c in lst if c.get("diag")]
            nr_i = [c["nr"]["i"] for c in lst if c.get("nr")]
            nf_i = [c["nf"]["i"] for c in lst if c.get("nf")]
            offsets = [c.get("anchor_offset") for c in lst if c.get("anchor_offset") is not None]
            eff = [c.get("effective_span") for c in lst if c.get("effective_span") is not None]
            return {
                "n": len(lst),
                "hw_index": svals(dvals(20)),
                "anchor_index": svals(dvals(21)),
                "anchor_offset": svals(offsets),
                "post_span": svals(dvals(47)),
                "effective_span": svals(eff),
                "recover_back": svals(dvals(50)),
                "refined_found_counts": {str(k): dvals(22).count(k) for k in sorted(set(dvals(22)))},
                "fallback_counts": {str(k): dvals(23).count(k) for k in sorted(set(dvals(23)))},
                "anchor_kind_counts": {str(k): dvals(49).count(k) for k in sorted(set(dvals(49)))},
                "span_valid_counts": {str(k): dvals(48).count(k) for k in sorted(set(dvals(48)))},
                "hw_count": svals(dvals(45)),
                "nr_nearest96_i": svals(nr_i),
                "nf_nearest96_i": svals(nf_i),
                "frames_with_rising_crossing": sum(1 for c in lst if c.get("rise_count", 0) > 0),
                "frames_with_falling_crossing": sum(1 for c in lst if c.get("fall_count", 0) > 0),
            }

        summ = {
            "config": name, "total": total,
            "coherent_n": len(coherent), "incoherent_n": total - len(coherent),
            "no_frame_n": len(no_frame),
            "diag_not_ok_n": len(diag_bad),
            "coherent_stats": stats_for(coherent),
            "all_with_frame_stats": stats_for([c for c in caps if c.get("frame_ok")]),
        }
        self.rec("config_summary", **summ)
        jdump(os.path.join(self.outdir, "summary_%s.json" % name), summ)
        self.note("SUMMARY %s total=%d coherent=%d no_frame=%d diag_not_ok=%d" % (name, total, len(coherent), len(no_frame), len(diag_bad)))
        return summ

    def close(self):
        try:
            self.set_stream(0)
        except Exception:
            pass
        try:
            self.configure(tb=9, mode=0, slope=0, level=970, avg=1)
            self.set_run(1)
            self.set_stream(0)
        except Exception:
            pass
        try:
            self.link.ser.close()
        except Exception:
            pass
        try:
            self.jsonl.close(); self.logf.close()
        except Exception:
            pass

def characterize_signal(r):
    r.note("Signal characterization")
    r.configure(tb=5, mode=0, slope=0, level=970, avg=1)
    r.set_run(1); time.sleep(0.5)
    r.set_stream(1)
    frames = []
    for _ in range(10):
        f, err = r.read_frame(timeout=6.0)
        if f:
            s = f["samples"]
            frames.append({"n": f["n"], "min": min(s), "max": max(s),
                           "mean": round(statistics.fmean(s), 2), "vpp": max(s) - min(s)})
    r.set_stream(0)
    meas = r.poll([5, 6, 7, 8, 9, 10, 0, 2, 11, 4, 1, 12, 17])
    out = {"frames": frames, "meas_getv": meas}
    r.rec("signal_characterization", **out)
    jdump(os.path.join(r.outdir, "signal_characterization.json"), out)
    r.note("Signal: " + json.dumps(out, default=str))
    return out

def functional_checks(r):
    r.note("Functional/protocol/ETS checks start")
    res = {}
    res['build_seq'] = r.gv(11)
    res['status'] = r.gv(4)
    stable = []
    for tb in [5, 6, 9]:
        r.configure(tb=tb, mode=0, slope=0, level=970, avg=1)
        r.set_run(1); time.sleep(0.4)
        r.set_stream(1)
        got = 0; errs = 0
        for _ in range(12):
            f, err = r.read_frame(timeout=6.0)
            if f: got += 1
            else: errs += 1
        r.set_stream(0)
        stable.append({"tb": tb, "got": got, "errs": errs})
        r.note("stability tb=%s got=%d errs=%d" % (tb, got, errs))
    res['stability'] = stable
    toggles = []
    for _ in range(5):
        st1, v1 = r.cmd(rp.OP_STREAM, 1)
        f, err = r.read_frame(timeout=6.0)
        st0, v0 = r.cmd(rp.OP_STREAM, 0)
        toggles.append({"stream_on": (st1, v1), "frame_ok": f is not None, "stream_off": (st0, v0)})
    res['stream_toggles'] = toggles
    ets = r.poll([38, 39, 40, 41, 42, 43, 44, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 45, 46, 47, 48, 49, 50, 11, 2, 4])
    res['ets_diag_snapshot'] = ets
    r.rec("functional_checks", **res)
    jdump(os.path.join(r.outdir, "functional_checks.json"), res)
    r.note("Functional/protocol/ETS checks done")
    return res

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--per-config", type=int, default=50)
    args = ap.parse_args()
    r = Runner(args.out, args.port)
    try:
        r.rec("start", port=args.port, build_target=TARGET_BUILD)
        bseq = r.gv(11)
        ident = r.poll([11, 4, 2, 1, 12, 0, 17, 5, 6, 7, 8, 9, 10])
        r.rec("identity", build_seq=bseq, poll=ident)
        jdump(os.path.join(args.out, "identity.json"), {"build_seq": bseq, "poll": ident})
        r.note("BUILD_SEQ=%s" % bseq)
        if bseq != TARGET_BUILD:
            r.note("ERROR: build_seq != %d" % TARGET_BUILD)
            r.rec("abort", reason="wrong_build", build_seq=bseq, expected=TARGET_BUILD)
            return 2
        characterize_signal(r)
        functional_checks(r)
        configs = [
            (5, 0, 0, 970, "tb5_auto_rise"),
            (5, 1, 0, 970, "tb5_norm_rise"),
            (5, 0, 1, 970, "tb5_auto_fall"),
            (5, 1, 1, 970, "tb5_norm_fall"),
            (6, 0, 0, 970, "tb6_auto_rise"),
            (6, 1, 0, 970, "tb6_norm_rise"),
            (6, 0, 1, 970, "tb6_auto_fall"),
            (6, 1, 1, 970, "tb6_norm_fall"),
            (9, 0, 0, 970, "tb9_auto_rise"),
            (9, 1, 0, 970, "tb9_norm_rise"),
            (9, 0, 1, 970, "tb9_auto_fall"),
            (9, 1, 1, 970, "tb9_norm_fall"),
        ]
        for tb, mode, slope, level, name in configs:
            r.run_config(tb, mode, slope, level, name, n=args.per_config)
        jdump(os.path.join(args.out, "summaries_index.json"), {"configs": [c[4] for c in configs], "per_config": args.per_config})
        r.rec("end", rc=0, cmd_count=r.cmd_count, cmd_errors=r.cmd_errors)
    except Unresponsive as e:
        r.rec("abort", reason="unresponsive", detail=str(e))
        r.note("ABORT unresponsive: %s" % e)
        return 3
    except Exception:
        r.rec("abort", reason="exception", tb=traceback.format_exc())
        raise
    finally:
        r.close()
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
