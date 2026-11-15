# 50 µs/div Trigger Diagnostics (B4.36 update)

## Current state
- **Coherent per-capture diagnostics implemented (B4.36):** `GETV_DIAG_LATCH` (46) copies a complete per-capture snapshot; fields read from the latched copy include sequence, gate/hw/anchor indices, found/fallback/anchor-kind, span validity/post span, hw count, timebase/slope/level/mode context (see `app/link_cmd.h`, `tools/remote_proto.py`).
- **Protocol:** `GETV_COUNT = 51` (0–50 valid). Use `tools/remote_proto.py` and `tools/validate_b436.py` for bracketed/coherent reads.

## Interpreting B4.36 diagnostics
- `fallback`: 0 verified edge published, 1 HW index path (as applicable), 2 tail/untriggered, 3 span-invalid (rejected), 4 no-edge (rejected).
- `anchor_kind`: 0 local pass, 1 recovery pass, 2 HW, 3 none.
- `anchor_offset` can be derived as `(anchor_index - hw_index)` mod 1024 mapped to signed (±512); `effective_span = post_span - offset` (with span validity checks in firmware).

## 41320 hardware campaign observations (2026-11-15)
- 600/600 captures coherent; `refined_found=1`, `fallback=0`, `anchor_kind=0`, offsets −1…−2, publishable spans throughout tb5/tb6/tb9 matrix. Recovery path not exercised on this run.

## Historical (pre-B4.36)
Prior attempts to diagnose 50 µs rising with torn/non-latched field reads or failed instrumented flashes were inconclusive; those notes are preserved in git history and B4.33–B4.35 materials.
