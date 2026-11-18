# DSO183 PC-link protocol (implemented contract)

**Scope:** authoritative reference for the PC-link protocol as implemented in current firmware and host tooling/tests. This document describes existing behavior; it does **not** define a redesign and does **not** add protocol guarantees beyond what is confirmed below.

**Primary sources (implementation/tooling/tests/docs):** `app/link.h`, `app/link.c`, `app/link_cmd.h`, `app/link_cmd.c`, `app/link_host.h`, `app/link_host.c`, `app/main.c` (`op_*`, `op_get_value`, `link_handle`, `link_cmd_is_readonly`, `link_dispatch_cmd`, `link_flush_pending`, `link_rx_byte`, `link_poll`), `tools/remote_proto.py`, `tools/remote_client.py`, `tools/remote_tests.py`, `tests/test_link.c`, `tests/test_link_cmd.c`, `tests/test_link_host.c`, `tests/test_link_e2e.c`, `tests/test_ets_diag.c`, `docs/ARCHITECTURE.md`, `docs/TESTING.md`.

**Labeling used in this document**
- **[Confirmed]** — behavior confirmed by inspected implementation and/or host tests/tooling code.
- **[Documented]** — stated in existing in-repo documentation/comments (may also be confirmed).
- **[Observed/tested]** — behavior evidenced in specific tested builds/campaigns or by current test expectations; not a universal guarantee unless also confirmed by implementation contract.
- **[Assumption/undocumented]** — not established from inspected evidence; do not rely on it as a guarantee.

---

## 1. Transport and serial configuration

- **Physical link:** USART1 on PA9 (TX) / PA10 (RX), 3.3 V logic, via USB-serial adapter (DSO183 USB connector is power-only). **[Confirmed/Documented]** (`docs/ARCHITECTURE.md`, `docs/TESTING.md`, `app/link.h` comment)
- **Serial settings:** 115200 baud, 8 data bits, no parity, 1 stop bit (8N1). **[Confirmed/Documented]** (`docs/ARCHITECTURE.md`, `docs/TESTING.md`, `tools/remote_client.py:SerialLink`)
- **Shared UART:** protocol shares the console UART with periodic console output; host clients must scan/resynchronize for framed packets and tolerate interleaved noise. **[Documented/Confirmed by tooling]** (`docs/TESTING.md`, `tools/remote_client.py` RX scan)
- **Streaming cleanliness:** when streaming sample frames, console text is suppressed for binary cleanliness (documented behavior). **[Documented]** (`docs/ARCHITECTURE.md`, `README.md`); exact suppression conditions beyond streaming state are **[Assumption/undocumented]** here except as implied.

---

## 2. Framing, byte layout, endianness, and CRC

### 2.1 Common properties
- **Byte order:** little-endian for multi-byte fields unless noted. **[Confirmed]** (`app/link.c`, `app/link_cmd.c`, `tools/remote_proto.py`)
- **SOF:** `0xA5 0x5A`. **[Confirmed]** (`app/link.h:LINK_SOF0/SOF1`, `app/link_cmd.h`)
- **Packet types:** sample frame `0x01`, command `0x02`, reply `0x03`. **[Confirmed]**
- **CRC:** CRC-16/CCITT-FALSE, poly `0x1021`, init `0xFFFF`, no reflection, no xorout; check value for `"123456789"` is `0x29B1`. Stored little-endian in packets. **[Confirmed]** (`app/link.c:link_crc16`, `tools/remote_proto.py:crc16`, `tests/test_link.c`)

### 2.2 Sample frame (device → host, type `0x01`)
| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | SOF `A5 5A` |
| 2 | 1 | Type `0x01` |
| 3 | 1 | Sequence (wraps 0..255) |
| 4 | 2 | Sample count `n` (LE u16) |
| 6 | 2*n | Samples, `uint16` LE each (ADC domain 0..4095 in practice) |
| 6+2*n | 2 | CRC16 LE |

- **CRC coverage:** bytes `[2 .. 6+2*n)` (type, seq, count, samples), i.e. from type up to but excluding CRC. **[Confirmed]** (`app/link.c:link_encode_frame`, `tools/remote_proto.py:frame_bytes/frame_parse`)
- **Constraints:** encoding requires non-NULL buffers, `1 <= n <= 1024` (`LINK_MAX_SAMPLES`), and sufficient capacity; nothing written on encode failure. **[Confirmed]** (`app/link.h`, `app/link.c`)
- **Streamed frame length:** codec permits up to 1024 samples; current live stream test expects contiguous frames with `n==320` and samples in 0..4095 under its preconditioned case. **[Confirmed/Observed]** (`tools/remote_tests.py:suite_stream`). A universal fixed `n` guarantee for all modes/builds is **[Assumption/undocumented]**.

### 2.3 Command (host → device, type `0x02`, fixed 10 bytes)
| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | SOF `A5 5A` |
| 2 | 1 | Type `0x02` |
| 3 | 1 | Sequence |
| 4 | 1 | Opcode |
| 5 | 2 | Argument (LE u16) |
| 7 | 1 | Reserved `0x00` |
| 8 | 2 | CRC16 LE |

- **CRC coverage:** bytes `[2..7]` (6 bytes: type, seq, op, arg_lo, arg_hi, reserved). **[Confirmed]** (`app/link_cmd.c:encode_packet/parse_packet/link_cmd_encode`, `tools/remote_proto.py:cmd_bytes/_parse`)

### 2.4 Reply (device → host, type `0x03`, fixed 10 bytes)
| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | SOF `A5 5A` |
| 2 | 1 | Type `0x03` |
| 3 | 1 | Sequence (echoes command) |
| 4 | 1 | Opcode (echoes command) |
| 5 | 1 | Status (`LINK_ST_*`) |
| 6 | 2 | Value (LE u16) |
| 8 | 2 | CRC16 LE |

- **Byte detail:** reply stores `status` at byte 5, `value_lo` at byte 6, and `value_hi` at byte 7 (byte 7 is **not** reserved zero in replies). **[Confirmed]** (`app/link_cmd.c:link_rep_encode`, `tools/remote_proto.py:rep_bytes`)
- **CRC coverage:** bytes `[2..7]` (type, seq, op, status, value_lo, value_hi). **[Confirmed]**

---

## 3. Opcodes (0x00–0x11), argument ranges, and effects

`LINK_OP_COUNT = 0x12` (`app/link_cmd.h:enum link_op`, `tools/remote_proto.py:OP_COUNT`). Argument range policy is enforced by `app/link_cmd.c:link_cmd_arg_valid` (firmware) and mirrored by `tools/remote_proto.py:arg_valid`.

| Op | Name | Arg (u16 LE) | Range/validation | Effect (confirmed) | Success reply value |
|---:|---|---|---|---|---|
| 0x00 | `NOP` | ignored | valid | No side effect. | 0 |
| 0x01 | `SET_TIMEBASE` | timebase index | 0..23 (`TIMEBASE_COUNT=24`); 0–4 are equivalent-time/ETS ranges per docs/comments | Applies timebase; on success resets averaging and forces scope redraw (`app/main.c:op_timebase`, `acq_set_timebase`). | 0 |
| 0x02 | `SET_TRIGGER` | trigger level | 0..4095 | Applies trigger level (`app/main.c:op_trigger`, `acq_set_trigger_level`). | 0 |
| 0x03 | `SET_SPAN` | display span (counts) | 16..4095 | Sets display span (`app/main.c:op_span`). | 0 |
| 0x04 | `RUN` | ignored | valid | Sets run state, clears recalled-wave flag, resets averaging, forces redraw (`app/main.c:op_run`). | 1 |
| 0x05 | `STOP` | ignored | valid | Sets hold/stopped run state (`app/main.c:op_run(0)`). | 0 |
| 0x06 | `GET_STATUS` | ignored | valid | Reads packed status (`app/main.c:op_status`). | bit0=run, bit1=stream |
| 0x07 | `STREAM` | 0 stop, 1 start | 0..1 | Sets streaming state (`app/main.c:op_stream`, `runstate_stream_set`). | echoes 0/1 |
| 0x08 | `SET_TRIG_MODE` | trigger mode | 0 AUTO, 1 NORM, 2 SINGLE | Applies mode; if SINGLE arms trigger; updates menu mode and marks settings dirty (`app/main.c:op_trig_mode`). | echoes mode |
| 0x09 | `GET_VALUE` | subindex | 0..50 (`GETV_COUNT=51`) | Returns subindex value (`app/main.c:op_get_value`). | subindex-dependent u16 |
| 0x0A | `SET_TRIG_SLOPE` | slope | 0 rising, 1 falling, 2 auto | Sets slope and marks settings dirty (`app/main.c:op_trig_slope`). | echoes slope |
| 0x0B | `CAL_SET_OFFSET_MV` | int16 mV (bit pattern) | always accepted by wire policy | Sets active cal offset (`offset_uv = mv*1000`), mirrors menu, marks cal dirty (`app/main.c:op_cal_set_offset_mv`). | echoes raw arg |
| 0x0C | `CAL_SET_GAIN_X1000` | gain×1000 | 100..10000 | Sets active cal gain (Q16 conversion), mirrors menu, marks cal dirty (`app/main.c:op_cal_set_gain_x1000`). | echoes arg |
| 0x0D | `CAL_SAVE` | unused (carried) | valid | Immediate calibration store write attempt; on success clears cal dirty/failure state; on failure schedules retry and hook returns failure (`app/main.c:op_cal_save`, `menu_cal_retry`). | 0 on success; failure mapped to `ST_BAD_ARG` |
| 0x0E | `CAL_RESET` | unused | valid | Resets active cal/table/menu fields to identity/cleared state; immediate store attempt; on failure preserves in-memory reset but keeps dirty/retry and returns failure (`app/main.c:op_cal_reset`). | 0 on success; failure mapped to `ST_BAD_ARG` |
| 0x0F | `WAVE_SAVE` | slot | 0..1 | Saves current (possibly averaged/capped) frame to slot (`app/main.c:op_wave_save`, `wave_store_save`). | echoes slot |
| 0x10 | `WAVE_RECALL` | slot | 0..1 | Loads slot into frame, enters HOLD, recomputes stats/display-related state, redraw behavior depends on UI ownership (`app/main.c:op_wave_recall`). | echoes slot |
| 0x11 | `SET_AVG` | averaging factor | 1,2,4,8 | Sets averaging factor and resets accumulator (`app/main.c:op_set_avg`). | echoes arg |

**Notes**
- Range/validity is checked at dispatch (`app/link_host.c:link_host_exec` + `link_cmd_arg_valid`) before calling hooks; out-of-range valid opcodes produce `ST_BAD_ARG` without invoking the hook. **[Confirmed]**
- `CAL_SAVE`/`CAL_RESET` perform immediate persistence attempts by design; failure does not imply “no retry scheduled” (firmware schedules retry via menu persistence state on failure paths shown above). **[Confirmed]** (`app/main.c`, `docs/TESTING.md`)

---

## 4. Reply status codes and error behavior

- **Status codes:** `ST_OK (0x00)`, `ST_BAD_ARG (0x01)`, `ST_BUSY (0x02)`, `ST_UNKNOWN (0x03)`. **[Confirmed]** (`app/link_cmd.h`, `tools/remote_proto.py`)
- **Parse/dispatch outcomes:** **[Confirmed]** (`app/link_cmd.c`, `app/link_host.c`, `app/main.c:link_handle`)
  - Invalid packet (short, bad SOF/type, CRC mismatch) or unknown opcode at parse (`op >= LINK_OP_COUNT`) → not dispatched, no reply generated.
  - Dispatched unknown opcode (`ST_UNKNOWN`) → reply is **not sent** (`link_handle` drops silently).
  - Known opcode with invalid arg (per range policy) → `ST_BAD_ARG`, hook not called.
  - `GET_STATUS` / `GET_VALUE` → `ST_OK` with hook value (or 0 if hook pointer is NULL).
  - Other commands → hook success returns `ST_OK` (with opcode-specific echoed value where defined), hook failure/NULL hook returns `ST_BAD_ARG`.
- **`ST_BUSY`:** defined in protocol enums/constants but not observed emitted by inspected dispatch path. **[Assumption/undocumented]** as a currently emitted status in this implementation.

---

## 5. `GET_VALUE` index map (0–50)

Definitions: `app/link_cmd.h:enum link_getv`, `tools/remote_proto.py`. Values: `app/main.c:op_get_value`. Valid subindices: `0 <= idx < 51`.

| Idx | Name | Returned value / encoding | Units/meaning | Coherence/source |
|---:|---|---|---|---|
| 0 | `TRIG_LEVEL` | trigger level | ADC counts | live getter |
| 1 | `TRIG_MODE` | 0 AUTO, 1 NORM, 2 SINGLE | trigger mode | live getter |
| 2 | `TIMEBASE` | timebase index | 0..23 | live getter |
| 3 | `SPAN` | display span | counts | live state `s_span` |
| 4 | `STATUS` | packed status | bit0 run, bit1 stream (same as `GET_STATUS`) | live |
| 5 | `VPP_COUNTS` | Vpp | ADC counts | cached measurement |
| 6 | `VRMS_COUNTS` | Vrms | ADC counts (stored/computed RMS) | cached measurement |
| 7 | `MIN` | min sample/stat | ADC counts | cached measurement |
| 8 | `MAX` | max sample/stat | ADC counts | cached measurement |
| 9 | `FREQ_HZ` | frequency | Hz, clamped to 65535 | cached measurement |
| 10 | `DUTY_X10` | duty cycle | 0.1% units | cached measurement |
| 11 | `BUILD_SEQ` | build sequence | low 16 bits (`status_build_seq & 0xFFFF`) | global |
| 12 | `TRIG_SLOPE` | 0 rising, 1 falling, 2 auto | trigger slope | live state |
| 13 | `CAL_OFFSET_MV` | signed offset in mV encoded in u16 bit pattern | mV (`offset_uv/1000`, cast to int16) | live calibration |
| 14 | `CAL_GAIN_X1000` | gain×1000 (rounded, clamped to 65535) | dimensionless ×1000 | live calibration |
| 15 | `WAVE_SAVED_0` | 0/1 | slot 0 valid | waveform store |
| 16 | `WAVE_SAVED_1` | 0/1 | slot 1 valid | waveform store |
| 17 | `AVG_N` | averaging factor | 1,2,4,8 | live state |
| 18 | `DIAG_SEQ` | per-capture diag sequence | latched copy | **per-capture latch** |
| 19 | `DIAG_GATE_TRIGGERED` | 1 if HW AWD triggered | flag | latched |
| 20 | `DIAG_HW_INDEX` | raw gate/ring index | sample index/count-domain marker | latched |
| 21 | `DIAG_REFINED_INDEX` | final alignment/anchor index | sample index | latched |
| 22 | `DIAG_REFINED_FOUND` | 1 if requested-slope edge verified | flag | latched |
| 23 | `DIAG_FALLBACK` | fallback/anchor outcome | code (0 verified edge, 1 HW idx, 2 tail/untriggered, 3 span-invalid, 4 no-edge per header semantics) **[Confirmed by enum/comment]** | latched |
| 24 | `DIAG_TIMEBASE` | capture timebase | index | latched |
| 25 | `DIAG_TRIG_SLOPE` | capture slope | 0/1/2 | latched |
| 26 | `DIAG_TRIG_LEVEL` | capture trigger level | level value | latched |
| 27 | `DIAG_TRIG_MODE` | capture trigger mode | 0/1/2 | latched |
| 28 | `MEM_STACK_FREE_NOW` | stack free now | bytes, clamped to 65535 | memdiag |
| 29 | `MEM_STACK_FREE_MIN` | stack free minimum watermark | bytes, clamped | memdiag |
| 30 | `MEM_HEAP_USED` | heap used | bytes, clamped | memdiag |
| 31 | `MEM_HEAP_HIGHWATER` | heap high-water | bytes, clamped | memdiag |
| 32 | `MEM_MSP_LOW_WATER` | **low 16 bits of raw minimum MSP address** | address fragment (not full 32-bit address), `u16(v & 0xFFFF)` | memdiag |
| 33 | `MEM_MSP_FREE_MIN` | derived MSP headroom minimum | bytes, clamped | memdiag |
| 34 | `ETS_ACCEPTED_SWEEPS` | accepted sweeps in epoch | 0..target | ETS epoch |
| 35 | `ETS_REJECTED_SWEEPS` | rejected sweeps in epoch | saturating u16; triggered+ETS-eligible+decision-point failures (untriggered/non-eligible excluded) | ETS epoch |
| 36 | `ETS_ACCUM_PROGRESS` | accumulation progress | accepted sweeps in epoch | ETS epoch |
| 37 | `ETS_FILLED_BINS` | filled bins | count (epoch-scoped) | ETS epoch |
| 38 | `ETS_READY` | readiness | 1 only per completed-reconstruction semantics (see §6.2) | ETS epoch |
| 39 | `ETS_RESET_REASON` | last reset/outcome | bitmask/code (see §6.2) | ETS epoch |
| 40 | `ETS_DIAG_SEQ` | ETS diagnostic update counter | u16 wrap, not odd/even seqlock | ETS epoch |
| 41 | `ETS_SRC_B1` | accepted sweeps (interpolated/resolved edge) | count | ETS epoch |
| 42 | `ETS_SRC_QUANTISED` | accepted sweeps (quantised midpoint placement) | count | ETS epoch |
| 43 | `ETS_REJECT_UNBOUND` | rejected subset (invalid/earlier-arming capture) | subset of 35 | ETS epoch |
| 44 | `ETS_HW_ROLLOVER` | bound captures with >=1 TIM3 wrap observed | diagnostic count | ETS epoch |
| 45 | `DIAG_HW_COUNT` | raw TIM3 count at last watchdog capture | latency marker (not sub-sample phase) | **per-capture latch** |
| 46 | `DIAG_LATCH` | latch per-capture diag copy; returns latched seq | snapshot command | performs latch |
| 47 | `DIAG_POST_SPAN` | HW latch → frozen write-position advance | samples | latched |
| 48 | `DIAG_SPAN_VALID` | 1 if 47 is valid triggered-capture measurement | flag | latched |
| 49 | `DIAG_ANCHOR_KIND` | 0 local, 1 recovery, 2 HW index, 3 none | anchor selection | latched |
| 50 | `DIAG_RECOVER_BACK` | recovery bound offered to anchor policy | samples | latched |

**General notes**
- Unknown/unhandled subindex inside `op_get_value` returns 0 by default, but valid requests should be limited to 0..50 by argument validation. **[Confirmed]** (`app/main.c`, `app/link_cmd.c`)
- Measurement values (5–10) reflect firmware’s cached/computed stats at read time; physical calibration/voltage correctness is outside protocol scope and subject to project limitations (e.g. Issue #2). **[Confirmed/Documented]**

---

## 6. Snapshot procedures and coherence guarantees

### 6.1 Per-capture trigger/anchor diagnostics latch (`GETV_DIAG_LATCH`, 46)
- **Procedure:** issue `GET_VALUE(46)` to copy the per-capture diagnostic struct from live state to a stable latched copy; the command returns the latched sequence. Then read fields from the latched set. **[Confirmed]** (`app/main.c:op_get_value` case 46: `s_diag_pub = s_diag; return s_diag_pub.seq`)
- **Latched/coherent field set:** `18–27, 45, 47–50` read from `s_diag_pub` until the next latch command. **[Confirmed]** (`app/main.c` GETV cases)
- **Guarantee:** values in this set are coherent with each other with respect to the latched snapshot copy (same `s_diag_pub` contents). Coherence with non-latched domains (e.g. ETS epoch fields, current measurements) is not implied. **[Confirmed]**

### 6.2 ETS epoch diagnostics snapshot (`GETV_ETS_DIAG_SEQ`, 40 and epoch fields)
- **Domain:** ETS epoch diagnostics are read live from `s_ets` getters (`app/main.c` cases 34–44). **[Confirmed]**
- **Sequence/snapshot procedure (host-observable contract):** read `GETV_ETS_DIAG_SEQ` (40), read desired ETS fields, read `GETV_ETS_DIAG_SEQ` again; accept only if both sequence values match; otherwise retry with a bounded retry count. **[Confirmed/Documented]** (`tools/remote_proto.py:read_ets_diag_snapshot`, `tests/test_ets_diag.c:test_snapshot_retry_contract`, `app/link_cmd.h` comment)
- **`ETS_DIAG_SEQ` semantics:** diagnostic update counter (u16 wrap), **not** an odd/even seqlock. Increments on each logical rejected-sweep outcome, once after each accepted sweep’s diagnostic updates complete, and on every ETS epoch reset; excluded/unrelated polls do not mutate diagnostics or increment sequence. **[Documented/Confirmed by tests/contracts]** (`app/link_cmd.h`, `tests/test_ets_diag.c:test_diag_seq_update_contract`)
- **Readiness/epoch semantics:** `ETS_SWEEPS_TARGET = 16` (host/tool constant; epoch behavior as defined). Accumulating: accepted < target and ready 0. Target reached but reconstruction unsuccessful: invalidated epoch/outcome reported with `ready==0` and `ETS_RESET_INCOMPLETE` per contract; successful reconstruction: `ready==1` for completed epoch and diagnostics remain observable until next sweep/epoch start. **[Documented/Confirmed by contracts/tests]** (`tools/remote_proto.py`, `tests/test_ets_diag.c`, `app/link_cmd.h`)
- **Reset reason values (`GETV_ETS_RESET_REASON`, 39):** `ETS_RESET_TB_CHANGE=0x0001`, `ETS_RESET_TRIG_MODE_CHANGE=0x0002`, `ETS_RESET_TRIG_SLOPE_CHANGE=0x0004`, `ETS_RESET_TRIG_LEVEL_CHANGE=0x0008`, `ETS_RESET_READY=0x0010`, `ETS_RESET_INIT=0x0020`, `ETS_RESET_INCOMPLETE=0x0040`. Context-change reasons may co-occur as bitmask; completion outcomes (`READY`, `INCOMPLETE`) are distinct outcome reasons per defined semantics. **[Confirmed]** (`app/link_cmd.h`, `tests/test_ets_diag.c`, `tools/remote_proto.py`)
- **Tooling note:** `tools/remote_proto.py:read_ets_diag_snapshot` snapshots `34,35,36,37,38,39,41,42,43,44` under the seq-40 stability check. Exact atomicity guarantees for every included field relative to every update event beyond the stated seq contract are **[Assumption/undocumented]** except where implied.

---

## 7. Command classification and execution/deferral behavior

### 7.1 Read-only vs mutating
- **Read-only (defer-exempt):** `NOP` (0x00), `GET_STATUS` (0x06), `GET_VALUE` (0x09). **[Confirmed]** (`app/main.c:link_cmd_is_readonly`)
- **Mutating/side-effecting:** all other implemented opcodes (0x01–0x05, 0x07–0x08, 0x0A–0x11). **[Confirmed]** (classification by exclusion in `link_cmd_is_readonly`; side effects in handlers)

### 7.2 Acquisition-time servicing and single-slot deferral
- While an acquisition cycle is active (`s_acq_cycle_active != 0`): read-only commands are executed immediately; mutating commands are **deferred** by storing the command in a **single pending slot** (`s_link_pending_cmd`, `s_link_pending_valid`). A newly deferred mutating command overwrites any previously pending one. **[Confirmed]** (`app/main.c:link_dispatch_cmd`)
- Pending command is flushed after the cycle completes (`link_flush_pending` dispatches once and clears the slot). **[Confirmed]**
- RX is polled in normal operation and serviced cooperatively during acquisition waits via `link_service_hook` → `link_poll` to keep protocol responsive without mutating acquisition state during waits. **[Confirmed/Documented]** (`app/main.c:link_poll/link_service_hook`, `docs/TESTING.md`)

### 7.3 Timing implications
- A deferred mutating command’s reply can arrive later than a typical host timeout; this must be distinguished from a lost command or unresponsive link. Read-only queries are intended to be serviceable during waits but may still experience delays under sustained blocking conditions. **[Documented/Observed in tested contexts]** (`docs/TESTING.md`)
- Observed on B4.51 validated candidate (specific instrument/build/evidence): zero lost replies in extended-timeout validation runs; some replies exceeded 4 s with max observed ~12.5 s; single-slot deferred overwrite not exercised by sequential host client. **[Observed/tested, not a universal guarantee]** (`docs/TESTING.md` §6)

---

## 8. Streaming behavior

- **Enable/disable:** `STREAM` opcode (0 stop, 1 start). **[Confirmed]**
- **Data:** device→host sample frames (type `0x01`) with sequence and CRC as defined in §2.2. **[Confirmed]**
- **Console interaction:** console text suppressed while streaming for binary cleanliness. **[Documented]**
- **Current harness-observed behavior:** preconditioned stream suite captures 3 contiguous frames with `n==320` and samples in 0..4095 within its timeout/window. **[Observed/tested]** (`tools/remote_tests.py:suite_stream`)
- **Undocumented/assumption:** frame count per time/trigger condition, exact `n` across all configurations, and long-run continuity guarantees beyond what is implemented/tested. **[Assumption/undocumented]**

---

## 9. Compatibility and versioning conventions (current state)

- **No explicit wire protocol version field/negotiation** observed. “Protocol v1” appears as a labeling convention in code/tool comments. **[Confirmed/Documented]**
- **Evolution observed in implementation/tests:** append-only extension pattern for `GET_VALUE` indices (now 0..50) and contiguous opcode set 0x00–0x11 with validation/tests updated accordingly. **[Confirmed]**
- **Compatibility policy (recommended, not a wire-enforced guarantee):** treat existing opcodes/fields/semantics as stable unless explicitly versioned/changed; prefer additive, backward-compatible extensions (new indices/opcodes with documented behavior) and keep host tooling/tests in sync. **[Documented intent / convention]** (`docs/ROADMAP.md` context, issue #6 intent)
- **Unknown handling:** unknown opcodes at parse are rejected (no reply); argument out-of-range for known opcodes yields `ST_BAD_ARG`. Forward-compatibility behavior for future packet variations is **[Assumption/undocumented]**.

---

## 10. Host tools and test references

- **Protocol codec/helpers:** `tools/remote_proto.py` (framing, CRC, encode/parse, arg policy, GETV/op/status constants, ETS snapshot helper, offline self-checks via `main()`/`offline_failures()`).
- **Serial client/transport:** `tools/remote_client.py` (UART I/O, RX scan for replies/frames, sequential command/response, latency tracking).
- **Remote test suites:** `tools/remote_tests.py` (offline codec/control policy incl. `FakeClient`; live serial/control/stream suites), `tools/remote_run.py` (runner entrypoint).
- **Validation/campaign tooling:** `tools/validate_b436.py` and related `tools/remote_*` scripts (usage as applicable to bench campaigns).
- **Host tests (protocol/dispatch/diagnostics):** `tests/test_link.c`, `tests/test_link_cmd.c`, `tests/test_link_host.c`, `tests/test_link_e2e.c`, `tests/test_ets_diag.c` (GETV/ETS contract checks).
- **Operational/testing guidance:** `docs/TESTING.md` (includes protocol servicing/deferral/late-reply interpretation and campaign notes).

---

## 11. Known uncertainties / non-guarantees (summary)

- `ST_BUSY` emission: **[Assumption/undocumented]** in current dispatch.
- Universal streaming frame length/timing and cross-mode guarantees: **[Assumption/undocumented]** beyond codec limits and observed/tested cases.
- Full 32-bit value of `GETV_MEM_MSP_LOW_WATER` not available over protocol (only low 16 bits returned). **[Confirmed]**
- Some B4.x latency/timeliness figures are **[Observed/tested]** for specific builds/instruments, not global guarantees.
- Future compatibility/negotiation semantics: **[Assumption/undocumented]** beyond current append-only/convention practice.
