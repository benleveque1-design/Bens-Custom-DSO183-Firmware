#!/usr/bin/env python3
"""Pure host-side codec for the DSO183 PC link (protocol v1).

Faithful Python port of app/link.h + app/link_cmd.h + app/link.c +
app/link_cmd.c.  No hardware, no HAL: the same checks the device performs are
exercised here against known vectors and round trips, so a protocol regression
is caught before it ever reaches the bench.

Wire formats (little-endian unless noted):

  sample frame  A5 5A | 0x01 | seq | n(LE16) | n x uint16 LE samples | crc16 LE
                CRC-16/CCITT-FALSE over type..samples (i.e. from offset 2 up to
                but not including the CRC), stored little-endian.

  command       A5 5A | 0x02 | seq | op | arg_lo | arg_hi | 0x00 | crc16 LE
                CRC-16/CCITT-FALSE over bytes 2..7 (type..reserved zero),
                stored little-endian.

  reply         A5 5A | 0x03 | seq | op | status | val_lo | val_hi | 0x00 | crc16 LE
                same CRC coverage as commands.

usage: python3 tools/remote_proto.py
"""

SOF0 = 0xA5
SOF1 = 0x5A
TYPE_FRAME = 0x01
TYPE_CMD = 0x02
TYPE_REP = 0x03

LINK_OVERHEAD = 8          # 2 SOF + type + seq + 2 count + 2 crc
LINK_MAX_SAMPLES = 1024
LINK_CMD_LEN = 10

ST_OK = 0x00
ST_BAD_ARG = 0x01
ST_BUSY = 0x02
ST_UNKNOWN = 0x03

OP_NOP = 0x00
OP_SET_TIMEBASE = 0x01
OP_SET_TRIGGER = 0x02
OP_SET_SPAN = 0x03
OP_RUN = 0x04
OP_STOP = 0x05
OP_GET_STATUS = 0x06
OP_STREAM = 0x07
OP_SET_TRIG_MODE = 0x08
OP_GET_VALUE = 0x09
OP_SET_TRIG_SLOPE = 0x0A
OP_CAL_SET_OFFSET_MV = 0x0B
OP_CAL_SET_GAIN_X1000 = 0x0C
OP_CAL_SAVE = 0x0D
OP_CAL_RESET = 0x0E
OP_WAVE_SAVE = 0x0F
OP_WAVE_RECALL = 0x10
OP_SET_AVG = 0x11
OP_COUNT = 0x12

GETV_TRIG_LEVEL  = 0
GETV_TRIG_MODE   = 1
GETV_TIMEBASE    = 2
GETV_SPAN        = 3
GETV_STATUS      = 4
GETV_VPP_COUNTS  = 5
GETV_VRMS_COUNTS = 6
GETV_MIN         = 7
GETV_MAX         = 8
GETV_FREQ_HZ     = 9
GETV_DUTY_X10    = 10
GETV_BUILD_SEQ   = 11
GETV_TRIG_SLOPE  = 12
GETV_CAL_OFFSET_MV = 13
GETV_CAL_GAIN_X1000 = 14
GETV_WAVE_SAVED_0 = 15
GETV_WAVE_SAVED_1 = 16
GETV_AVG_N = 17
GETV_DIAG_SEQ            = 18
GETV_DIAG_GATE_TRIGGERED = 19
GETV_DIAG_HW_INDEX       = 20
GETV_DIAG_REFINED_INDEX  = 21
GETV_DIAG_REFINED_FOUND  = 22
GETV_DIAG_FALLBACK       = 23
GETV_DIAG_TIMEBASE       = 24
GETV_DIAG_TRIG_SLOPE     = 25
GETV_DIAG_TRIG_LEVEL     = 26
GETV_DIAG_TRIG_MODE      = 27
# Stage 0 SRAM instrumentation
GETV_MEM_STACK_FREE_NOW  = 28
GETV_MEM_STACK_FREE_MIN  = 29
GETV_MEM_HEAP_USED       = 30
GETV_MEM_HEAP_HIGHWATER  = 31
# Stage 0.5 actual MSP low-water
GETV_MEM_MSP_LOW_WATER   = 32
GETV_MEM_MSP_FREE_MIN    = 33
# ETS accumulation diagnostics (volatile, epoch-scoped, read-only)
GETV_ETS_ACCEPTED_SWEEPS = 34
GETV_ETS_REJECTED_SWEEPS = 35  # triggered+ETS-eligible, reached decision point, failed acceptance; untriggered/non-eligible excluded
GETV_ETS_ACCUM_PROGRESS  = 36
GETV_ETS_FILLED_BINS     = 37
GETV_ETS_READY           = 38  # 1 only after successful reconstruction READY reset
GETV_ETS_RESET_REASON    = 39
GETV_ETS_DIAG_SEQ        = 40  # diagnostic update counter (not seqlock): +1 per logical reject, once per accepted sweep updates, +1 per reset; wraps u16
# B4.27 trigger-phase source tallies (epoch-scoped, saturating u16)
GETV_ETS_SRC_B1          = 41  # accepted sweeps placed by two-sample interpolation (resolved edge)
GETV_ETS_SRC_QUANTISED   = 42  # accepted sweeps placed at the trigger-quantised interval midpoint
GETV_ETS_REJECT_UNBOUND  = 43  # subset of 35: capture invalid or from an earlier arming generation
GETV_ETS_HW_ROLLOVER     = 44  # bound captures whose TIM3 count showed >= 1 wrap (diagnostic)
GETV_DIAG_HW_COUNT       = 45  # raw TIM3 count of last watchdog capture (latency marker, not a phase)
GETV_COUNT               = 46

# Observable ETS readiness states (diagnostic interpretation):
# - accumulating: accepted < target, ready == 0
# - target reached but reconstruction unsuccessful: accepted == target, ready == 0 (no auto reset)
# - successful reconstruction: ready == 1 after READY reset
ETS_SWEEPS_TARGET = 16

# ETS reset reason bitmask values (GETV_ETS_RESET_REASON)
ETS_RESET_INIT              = 0x0020
ETS_RESET_TB_CHANGE         = 0x0001
ETS_RESET_TRIG_MODE_CHANGE  = 0x0002
ETS_RESET_TRIG_SLOPE_CHANGE = 0x0004
ETS_RESET_TRIG_LEVEL_CHANGE = 0x0008
ETS_RESET_READY             = 0x0010

TIMEBASE_FIRST_SINGLE_SHOT = 5
TIMEBASE_COUNT = 24


class CodecError(ValueError):
    """Raised by the strict parsers on any malformed packet."""


def crc16(data) -> int:
    """CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflect, no xorout."""
    crc = 0xFFFF
    if data is None:
        return 0
    for b in bytearray(data):
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def _le16(v) -> bytes:
    return bytes((v & 0xFF, (v >> 8) & 0xFF))


def _from_le16(b) -> int:
    return b[0] | (b[1] << 8)


def frame_bytes(seq: int, samples) -> bytes:
    """Encode one sample frame.  Returns the packet bytes or raises."""
    n = len(samples)
    if n < 1 or n > LINK_MAX_SAMPLES:
        raise CodecError("sample count %d out of range" % n)
    pkt = bytearray([SOF0, SOF1, TYPE_FRAME, seq & 0xFF,
                     n & 0xFF, (n >> 8) & 0xFF])
    for s in samples:
        pkt += _le16(s)
    crc = crc16(bytes(pkt[2:6 + 2 * n]))
    pkt += bytes((crc & 0xFF, (crc >> 8) & 0xFF))
    return bytes(pkt)


def frame_parse(buf) -> dict:
    """Strict frame parse.  Returns {'seq','n','samples'} or raises."""
    pkt = bytes(buf)
    if len(pkt) < LINK_OVERHEAD:
        raise CodecError("short frame")
    if pkt[0] != SOF0 or pkt[1] != SOF1 or pkt[2] != TYPE_FRAME:
        raise CodecError("bad frame preamble/type")
    n = _from_le16(pkt[4:6])
    end = LINK_OVERHEAD + 2 * n
    if len(pkt) < end:
        raise CodecError("truncated frame")
    if crc16(pkt[2:6 + 2 * n]) != _from_le16(pkt[end - 2:end]):
        raise CodecError("frame CRC mismatch")
    samples = [_from_le16(pkt[6 + 2 * i:8 + 2 * i]) for i in range(n)]
    return {"seq": pkt[3], "n": n, "samples": samples}


def cmd_bytes(seq: int, op: int, arg: int) -> bytes:
    """Encode a command (host -> device).  Raises on unknown op."""
    if op >= OP_COUNT:
        raise CodecError("unknown opcode 0x%02X" % op)
    pkt = bytearray([SOF0, SOF1, TYPE_CMD, seq & 0xFF, op & 0xFF,
                     arg & 0xFF, (arg >> 8) & 0xFF, 0x00])
    crc = crc16(bytes(pkt[2:8]))
    pkt += bytes((crc & 0xFF, (crc >> 8) & 0xFF))
    return bytes(pkt)


def _parse(buf, want_type: int) -> dict:
    pkt = bytes(buf)
    if len(pkt) < LINK_CMD_LEN:
        raise CodecError("short packet")
    if pkt[0] != SOF0 or pkt[1] != SOF1 or pkt[2] != want_type:
        raise CodecError("bad packet preamble/type")
    if crc16(pkt[2:8]) != _from_le16(pkt[8:10]):
        raise CodecError("packet CRC mismatch")
    op = pkt[4]
    if op >= OP_COUNT:
        raise CodecError("unknown opcode 0x%02X" % op)
    return {
        "seq": pkt[3],
        "op": op,
        "arg": _from_le16(pkt[5:7]),
    }


def cmd_parse(buf) -> dict:
    """Parse and validate a command (device-side rules)."""
    return _parse(buf, TYPE_CMD)


def rep_parse(buf) -> tuple:
    """Parse a reply.  Returns (cmd_dict, status, value)."""
    cmd = _parse(buf, TYPE_REP)
    pkt = bytes(buf)
    status = pkt[5]
    value = _from_le16(pkt[6:8])
    cmd["arg"] = 0  # replies never carry an argument
    return cmd, status, value


def rep_bytes(seq: int, op: int, status: int, value: int) -> bytes:
    """Encode a reply (device -> host)."""
    if op >= OP_COUNT:
        raise CodecError("unknown opcode 0x%02X" % op)
    pkt = bytearray([SOF0, SOF1, TYPE_REP, seq & 0xFF, op & 0xFF,
                     status & 0xFF, value & 0xFF, (value >> 8) & 0xFF])
    crc = crc16(bytes(pkt[2:8]))
    pkt += bytes((crc & 0xFF, (crc >> 8) & 0xFF))
    return bytes(pkt)


def arg_valid(op: int, arg: int) -> bool:
    """Range policy, mirroring app/link_cmd.c link_cmd_arg_valid()."""
    if op == OP_SET_TIMEBASE:
        return 0 <= arg < TIMEBASE_COUNT
    if op == OP_SET_TRIGGER:
        return 0 <= arg <= 4095
    if op == OP_SET_SPAN:
        return 16 <= arg <= 4095
    if op == OP_STREAM:
        return arg <= 1
    if op == OP_SET_TRIG_MODE:
        return 0 <= arg <= 2
    if op == OP_GET_VALUE:
        return 0 <= arg < GETV_COUNT
    if op == OP_SET_TRIG_SLOPE:
        return 0 <= arg <= 2
    if op == OP_CAL_SET_OFFSET_MV:
        return True  # int16 bit pattern
    if op == OP_CAL_SET_GAIN_X1000:
        return 100 <= arg <= 10000
    if op in (OP_CAL_SAVE, OP_CAL_RESET):
        return True
    if op in (OP_WAVE_SAVE, OP_WAVE_RECALL):
        return arg <= 1
    if op == OP_SET_AVG:
        return arg in (1, 2, 4, 8)
    if op in (OP_NOP, OP_RUN, OP_STOP, OP_GET_STATUS):
        return True
    return False


# --------------------------------------------------------------------------
# ETS diagnostics snapshot (host-side, transport-hardened)
# --------------------------------------------------------------------------

SNAPSHOT_OK = "ok"
SNAPSHOT_SEQ_MISMATCH = "seq_mismatch"  # persistent sequence mismatch / retry exhaustion
SNAPSHOT_READ_ERROR = "read_error"      # transport/read exception or malformed value


def read_ets_diag_snapshot(read_value_fn, max_retries=8):
    """Host snapshot procedure for ETS diagnostics (GETV 34-40).

    read_value_fn(subindex) performs one GETV read and should return a u16 value.
    Procedure: read seq (40); read fields 34-39 in order; read seq again; accept
    only if both sequence values match. Retries on sequence mismatch up to
    max_retries attempts. Bounded retry is preserved.

    Returns (snapshot, ok, status):
      - (fields_dict, True, SNAPSHOT_OK) on a stable snapshot.
      - ({}, False, SNAPSHOT_SEQ_MISMATCH) when sequence never stabilizes.
      - ({}, False, SNAPSHOT_READ_ERROR) on transport/read exception or malformed,
        missing, None, bool, or non-numeric/non-u16 value at any read.

    No partial snapshot is ever reported as valid. No odd/even seqlock;
    writers/readers are synchronous main-loop in firmware.
    """
    ETS_DIAG_FIELDS = (
        GETV_ETS_ACCEPTED_SWEEPS,
        GETV_ETS_REJECTED_SWEEPS,
        GETV_ETS_ACCUM_PROGRESS,
        GETV_ETS_FILLED_BINS,
        GETV_ETS_READY,
        GETV_ETS_RESET_REASON,
        GETV_ETS_SRC_B1,
        GETV_ETS_SRC_QUANTISED,
        GETV_ETS_REJECT_UNBOUND,
        GETV_ETS_HW_ROLLOVER,
    )

    def _read_u16(subindex):
        try:
            raw = read_value_fn(subindex)
        except Exception:
            return None
        if raw is None or isinstance(raw, bool):
            return None
        try:
            # Accept integers and integral floats only; reject bool/str/object/non-numeric.
            if isinstance(raw, float):
                if not raw.is_integer():
                    return None
                val = int(raw)
            elif isinstance(raw, int):
                val = raw
            else:
                return None
        except Exception:
            return None
        if val < 0 or val > 0xFFFF:
            return None
        return val & 0xFFFF

    try:
        retries = int(max_retries)
    except Exception:
        retries = 0
    if retries < 1:
        retries = 0

    for _ in range(retries):
        seq0 = _read_u16(GETV_ETS_DIAG_SEQ)
        if seq0 is None:
            return {}, False, SNAPSHOT_READ_ERROR
        values = {}
        read_failed = False
        for idx in ETS_DIAG_FIELDS:
            v = _read_u16(idx)
            if v is None:
                read_failed = True
                break
            values[idx] = v
        if read_failed:
            return {}, False, SNAPSHOT_READ_ERROR
        seq1 = _read_u16(GETV_ETS_DIAG_SEQ)
        if seq1 is None:
            return {}, False, SNAPSHOT_READ_ERROR
        if seq0 == seq1:
            values[GETV_ETS_DIAG_SEQ] = seq0
            return values, True, SNAPSHOT_OK
    return {}, False, SNAPSHOT_SEQ_MISMATCH



# --------------------------------------------------------------------------
# Offline self-checks: known vectors + round trips.
# --------------------------------------------------------------------------

def offline_failures() -> list:
    fails = []

    def check(name, cond):
        if not cond:
            fails.append(name)

    # CRC-16/CCITT-FALSE check value.
    check("crc16 check value", crc16(b"123456789") == 0x29B1)
    check("crc16 null", crc16(None) == 0)

    # Command round trip, seq 7, SET_TIMEBASE 9.
    c = cmd_bytes(7, OP_SET_TIMEBASE, 9)
    check("cmd len", len(c) == LINK_CMD_LEN)
    check("cmd preamble", c[0] == SOF0 and c[1] == SOF1)
    check("cmd type", c[2] == TYPE_CMD)
    check("cmd seq/op", c[3] == 7 and c[4] == OP_SET_TIMEBASE)
    check("cmd arg le", c[5] == 9 and c[6] == 0)
    check("cmd crc le", c[8] == (crc16(c[2:8]) & 0xFF) and c[9] == (crc16(c[2:8]) >> 8))
    d = cmd_parse(c)
    check("cmd roundtrip", d == {"seq": 7, "op": OP_SET_TIMEBASE, "arg": 9})

    # Rejections leave the parse result unset -> use fresh dicts.
    d = {"seq": 0x42, "op": 0xFF, "arg": 0xFFFF}
    try:
        cmd_parse(c[:5])
        check("cmd short rejected", False)
    except CodecError:
        pass
    bad = bytearray(c); bad[0] ^= 0xFF
    try:
        cmd_parse(bytes(bad)); check("cmd bad preamble rejected", False)
    except CodecError:
        pass
    bad = bytearray(c); bad[8] ^= 0x01
    try:
        cmd_parse(bytes(bad)); check("cmd bad crc rejected", False)
    except CodecError:
        pass
    bad = bytearray(c); bad[4] = 0x7F
    try:
        crc = crc16(bytes(bad[2:8])); bad[8] = crc & 0xFF; bad[9] = (crc >> 8) & 0xFF
        cmd_parse(bytes(bad)); check("cmd unknown op rejected", False)
    except CodecError:
        pass
    try:
        cmd_bytes(1, 0x7F, 0); check("cmd encode unknown op", False)
    except CodecError:
        pass
    try:
        cmd_bytes(1, OP_RUN, 0, )  # noqa
    except CodecError:
        pass

    r = rep_bytes(3, OP_GET_STATUS, ST_OK, 0x1234)
    rd = rep_parse(r)
    check("rep ok", rd[0] == {"seq": 3, "op": OP_GET_STATUS, "arg": 0})
    check("rep status/value", rd[1] == ST_OK and rd[2] == 0x1234)

    # Frame round trip: seq 7, 3 samples {0x1234, 0x00FF, 0xABCD}.
    samples = [0x1234, 0x00FF, 0xABCD]
    f = frame_bytes(7, samples)
    check("frame len", len(f) == LINK_OVERHEAD + 2 * 3)
    check("frame preamble", f[0] == SOF0 and f[1] == SOF1 and f[2] == TYPE_FRAME)
    check("frame seq/n", f[3] == 7 and f[4] == 3 and f[5] == 0)
    check("frame samples", f[6] == 0x34 and f[7] == 0x12 and f[8] == 0xFF and f[9] == 0x00
          and f[10] == 0xCD and f[11] == 0xAB)
    check("frame crc", f[12] == (crc16(f[2:12]) & 0xFF) and f[13] == (crc16(f[2:12]) >> 8))
    fr = frame_parse(f)
    check("frame roundtrip", fr == {"seq": 7, "n": 3, "samples": samples})

    # Frame with n == 0 and n > LINK_MAX_SAMPLES must be refused.
    try:
        frame_bytes(0, []); check("frame n=0 rejected", False)
    except CodecError:
        pass
    try:
        frame_bytes(0, [0] * (LINK_MAX_SAMPLES + 1)); check("frame n>max rejected", False)
    except CodecError:
        pass

    # Argument policy boundaries (mirrors link_cmd_arg_valid).
    # ETS timebases 0..4 are valid (equivalent-time); engine handles them.
    check("tb 0 valid", arg_valid(OP_SET_TIMEBASE, 0))
    check("tb 4 valid", arg_valid(OP_SET_TIMEBASE, 4))
    check("tb 5 valid", arg_valid(OP_SET_TIMEBASE, 5))
    check("tb 23 valid", arg_valid(OP_SET_TIMEBASE, 23))
    check("tb 24 rejected", not arg_valid(OP_SET_TIMEBASE, 24))
    check("tr 4095 valid", arg_valid(OP_SET_TRIGGER, 4095))
    check("tr 4096 invalid", not arg_valid(OP_SET_TRIGGER, 4096))
    check("sp 16 valid", arg_valid(OP_SET_SPAN, 16))
    check("sp 15 invalid", not arg_valid(OP_SET_SPAN, 15))
    check("sp 4095 valid", arg_valid(OP_SET_SPAN, 4095))
    check("sp 4096 invalid", not arg_valid(OP_SET_SPAN, 4096))
    check("stream 1 valid", arg_valid(OP_STREAM, 1))
    check("stream 2 invalid", not arg_valid(OP_STREAM, 2))
    check("run arg ignored", arg_valid(OP_RUN, 0xFFFF))

    # Protocol v1 extensions (backward-compatible): trigger mode, slope, get value.
    c2 = cmd_bytes(9, OP_SET_TRIG_MODE, 1)
    d2 = cmd_parse(c2)
    check("set_trig_mode roundtrip", d2 == {"seq": 9, "op": OP_SET_TRIG_MODE, "arg": 1})
    c3 = cmd_bytes(10, OP_GET_VALUE, GETV_FREQ_HZ)
    d3 = cmd_parse(c3)
    check("get_value roundtrip", d3 == {"seq": 10, "op": OP_GET_VALUE, "arg": GETV_FREQ_HZ})
    c4 = cmd_bytes(11, OP_SET_TRIG_SLOPE, 2)
    d4 = cmd_parse(c4)
    check("set_trig_slope roundtrip", d4 == {"seq": 11, "op": OP_SET_TRIG_SLOPE, "arg": 2})
    check("trig_mode 2 valid", arg_valid(OP_SET_TRIG_MODE, 2))
    check("trig_mode 3 invalid", not arg_valid(OP_SET_TRIG_MODE, 3))
    check("getv last valid", arg_valid(OP_GET_VALUE, GETV_COUNT - 1))
    check("getv count invalid", not arg_valid(OP_GET_VALUE, GETV_COUNT))
    check("slope 2 valid", arg_valid(OP_SET_TRIG_SLOPE, 2))
    check("slope 3 invalid", not arg_valid(OP_SET_TRIG_SLOPE, 3))

    # ETS diagnostics GETV indices (append-only from 34) and decoding constants.
    check("getv ets accepted idx", GETV_ETS_ACCEPTED_SWEEPS == 34)
    check("getv ets rejected idx", GETV_ETS_REJECTED_SWEEPS == 35)
    check("getv ets accum idx", GETV_ETS_ACCUM_PROGRESS == 36)
    check("getv ets filled idx", GETV_ETS_FILLED_BINS == 37)
    check("getv ets ready idx", GETV_ETS_READY == 38)
    check("getv ets reset reason idx", GETV_ETS_RESET_REASON == 39)
    check("getv ets diag seq idx", GETV_ETS_DIAG_SEQ == 40)
    check("getv ets src b1 idx", GETV_ETS_SRC_B1 == 41)
    check("getv ets src quantised idx", GETV_ETS_SRC_QUANTISED == 42)
    check("getv ets reject unbound idx", GETV_ETS_REJECT_UNBOUND == 43)
    check("getv ets hw rollover idx", GETV_ETS_HW_ROLLOVER == 44)
    check("getv diag hw count idx", GETV_DIAG_HW_COUNT == 45)
    check("getv count 46", GETV_COUNT == 46)
    for idx in (GETV_ETS_ACCEPTED_SWEEPS, GETV_ETS_REJECTED_SWEEPS,
                GETV_ETS_ACCUM_PROGRESS, GETV_ETS_FILLED_BINS,
                GETV_ETS_READY, GETV_ETS_RESET_REASON, GETV_ETS_DIAG_SEQ,
                GETV_ETS_SRC_B1, GETV_ETS_SRC_QUANTISED,
                GETV_ETS_REJECT_UNBOUND, GETV_ETS_HW_ROLLOVER,
                GETV_DIAG_HW_COUNT):
        check("getv ets valid %d" % idx, arg_valid(OP_GET_VALUE, idx))
    check("getv 33 still valid", arg_valid(OP_GET_VALUE, 33))
    check("getv 46 invalid", not arg_valid(OP_GET_VALUE, 46))
    c_ets = cmd_bytes(12, OP_GET_VALUE, GETV_ETS_DIAG_SEQ)
    d_ets = cmd_parse(c_ets)
    check("getv ets diag seq roundtrip", d_ets == {"seq": 12, "op": OP_GET_VALUE, "arg": GETV_ETS_DIAG_SEQ})
    # Reset reason bitmask/code values
    check("ets reset init", ETS_RESET_INIT == 0x0020)
    check("ets reset tb", ETS_RESET_TB_CHANGE == 0x0001)
    check("ets reset mode", ETS_RESET_TRIG_MODE_CHANGE == 0x0002)
    check("ets reset slope", ETS_RESET_TRIG_SLOPE_CHANGE == 0x0004)
    check("ets reset level", ETS_RESET_TRIG_LEVEL_CHANGE == 0x0008)
    check("ets reset ready", ETS_RESET_READY == 0x0010)
    check("ets sweeps target", ETS_SWEEPS_TARGET == 16)

    # ETS_DIAG_SEQ is a diagnostic update counter (u16 wrap), not an odd/even seqlock.
    seq = 0
    for _ in range(3):
        seq = (seq + 1) & 0xFFFF  # logical rejected outcome / accepted update / reset
    check("ets seq increments", seq == 3)
    seq = 0xFFFF
    seq = (seq + 1) & 0xFFFF
    check("ets seq wraps u16", seq == 0)

    # Snapshot retry: accept only if seq before==after; reject on change; bounded retry.
    state = {GETV_ETS_DIAG_SEQ: 7, 34: 1, 35: 2, 36: 1, 37: 10, 38: 0, 39: ETS_RESET_TB_CHANGE,
             41: 3, 42: 2, 43: 0, 44: 5}
    snap, ok, st = read_ets_diag_snapshot(lambda idx: state[idx])
    check("snapshot stable accepted", ok and st == SNAPSHOT_OK and snap[GETV_ETS_DIAG_SEQ] == 7 and snap[GETV_ETS_ACCEPTED_SWEEPS] == 1)
    check("snapshot includes source tallies", ok and snap[GETV_ETS_SRC_B1] == 3 and snap[GETV_ETS_SRC_QUANTISED] == 2 and snap[GETV_ETS_HW_ROLLOVER] == 5)
    state2 = {GETV_ETS_DIAG_SEQ: 1, 34: 0, 35: 0, 36: 0, 37: 0, 38: 0, 39: ETS_RESET_INIT,
              41: 0, 42: 0, 43: 0, 44: 0}
    def always_change(idx):
        if idx == GETV_ETS_DIAG_SEQ:
            state2[GETV_ETS_DIAG_SEQ] = (state2[GETV_ETS_DIAG_SEQ] + 1) & 0xFFFF
        return state2[idx]
    snap2, ok2, st2 = read_ets_diag_snapshot(always_change, max_retries=3)
    check("snapshot persistent seq mismatch", (not ok2) and st2 == SNAPSHOT_SEQ_MISMATCH and snap2 == {})
    # changes between attempts then stabilizes => retry succeeds
    state3 = {GETV_ETS_DIAG_SEQ: 5, 34: 2, 35: 1, 36: 2, 37: 20, 38: 0, 39: 0,
              41: 2, 42: 0, 43: 1, 44: 3, "reads": 0}
    def change_once_between(idx):
        if idx == GETV_ETS_DIAG_SEQ:
            state3["reads"] += 1
            # first group: seq0 sees 5, mutate before seq1 so group fails; later groups stable at 6
            if state3["reads"] == 2:
                state3[GETV_ETS_DIAG_SEQ] = 6
        return state3[idx]
    snap3, ok3, st3 = read_ets_diag_snapshot(change_once_between, max_retries=4)
    check("snapshot retry then accept", ok3 and st3 == SNAPSHOT_OK and snap3[GETV_ETS_DIAG_SEQ] == 6)

    # Transport/read failures and malformed values must not return partial valid snapshot.
    base = {GETV_ETS_DIAG_SEQ: 9, 34: 1, 35: 1, 36: 1, 37: 2, 38: 0, 39: 0,
            41: 1, 42: 0, 43: 0, 44: 0}

    def exc_initial(idx):
        if idx == GETV_ETS_DIAG_SEQ:
            raise IOError("timeout")
        return base[idx]
    snap, ok, st = read_ets_diag_snapshot(exc_initial)
    check("exception initial seq -> read_error", (not ok) and st == SNAPSHOT_READ_ERROR and snap == {})

    def exc_field(idx):
        if idx == GETV_ETS_ACCEPTED_SWEEPS:
            raise ValueError("transport")
        return base[idx]
    snap, ok, st = read_ets_diag_snapshot(exc_field)
    check("exception field read -> read_error", (not ok) and st == SNAPSHOT_READ_ERROR and snap == {})

    calls = {"n": 0}
    def exc_final(idx):
        if idx == GETV_ETS_DIAG_SEQ:
            calls["n"] += 1
            if calls["n"] == 2:
                raise RuntimeError("late")
        return base[idx]
    snap, ok, st = read_ets_diag_snapshot(exc_final)
    check("exception final seq -> read_error", (not ok) and st == SNAPSHOT_READ_ERROR and snap == {})

    for bad in (None, "1", object(), True, -1, 65536, 1.5, float("nan")):
        bstate = dict(base)
        bstate[GETV_ETS_FILLED_BINS] = bad
        snap, ok, st = read_ets_diag_snapshot(lambda idx, bs=bstate: bs[idx])
        check("malformed/non-numeric %r -> read_error" % (bad,), (not ok) and st == SNAPSHOT_READ_ERROR and snap == {})

    # retry exhaustion distinct from read error
    snap, ok, st = read_ets_diag_snapshot(always_change, max_retries=1)
    check("retry exhaustion bounded", (not ok) and st == SNAPSHOT_SEQ_MISMATCH and snap == {})

    return fails


def main() -> int:
    fails = offline_failures()
    if fails:
        for f in fails:
            print("OFFLINE FAIL: %s" % f)
        return 1
    print("remote_proto: all offline checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
