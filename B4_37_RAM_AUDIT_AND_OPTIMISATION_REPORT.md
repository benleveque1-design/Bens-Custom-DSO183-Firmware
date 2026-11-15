# B4.37 — RAM Audit and Optimisation Report (Issue #4)

**Date:** 2026-11-15 (UTC)  
**Scope:** Read-only investigation and safe source changes to reduce static RAM usage (no flash/reset/program/erase; no hardware writes).  
**Target baseline:** build 41320 (hardware-validated, currently installed on the instrument).  
**Workspace HEAD at audit start:** `834b419` (docs: rewrite README for B4.36 status, clarity, and first impressions), clean working tree before changes.  
**Report/evidence:** additive (`evidence/B4_37_RAM_AUDIT_20261115/`, this report). **Not committed or pushed.**

---

## 1. Objective and constraints

- Investigate build 41320 RAM consumption and identify safe, measurable reductions without changing firmware behaviour.
- Preserve B4.36 trigger/ETS behaviour, acquisition timing/DMA requirements, UART protocol/streaming, and protected settings storage.
- No weakened tests/acceptance criteria; avoid large refactors; small independently reviewable changes only.
- No hardware validation claimed. Live instrument interaction limited to **read-only UART diagnostics** (with permission) to observe stack/heap on the installed 41320 image.

---

## 2. Phase 1 — Baseline (unchanged source)

### 2.1 Build identity and hashes (baseline)

Reproduced from clean build of unchanged source (`make clean && make`):

| Item | Value |
|---|---|
| Git HEAD | `834b419c2c640aa30bd6d93ce7a84ca793b63ebe` |
| Build sequence (`build/build_seq.h`) | `41320` (`0xA168`) |
| `build/dso183.bin` | 40,960 B, SHA-256 `bfa6e7bf8bf681d202dba6267b5b8443d6ccd7fd2a2e234d9f1b0b7cc40e7efd` |
| `build/dso183.elf` | SHA-256 `7d2f6d1eddd82f59505c0a55e1345bdf10b2dd584230e01829ca084be3166d87` |
| `arm-none-eabi-size` | `text 40820`, `data 136`, `bss 18544`, `dec 59500` |
| `tools/size_report.sh` | Flash `40956 / 61440 B (66%)`; RAM `18680 / 20480 B (91%)` (data+bss) |

Artifacts: `evidence/B4_37_RAM_AUDIT_20261115/{dso183.{elf,bin,map},baseline_*.txt,baseline_build.log}`.

### 2.2 Memory limits and linker layout (STM32F103C8)

- **SRAM:** 20 KiB total at `0x20000000–0x20004FFF` (`RAM LENGTH = 20K`, `_estack = 0x20005000`).
- **Linker reservations (`linker/STM32F103C8_64k_20k.ld`):** `_min_heap_size = 0x400` (1 KiB), `_min_stack_size = 0x800` (2 KiB) placed in `._user_heap_stack` after `.noinit`.
- **Baseline map (from `build/dso183.map`):** `_sbss=0x20000080`, `_ebss=0x20003CC8`, `_snoinit=0x20003CC8`, `_enoinit=0x20003CF0`, `._user_heap_stack` ends at `0x200048F0`.
- **Free gap above reserved stack:** `0x20005000 - 0x200048F0 = 0x110` (**272 B**) — significantly tighter than the 91% headline implies.
- **Section breakdown (`size -A`, baseline):** `.data 128`, `.bss 15432`, `.noinit 40`, `._user_heap_stack 3072` (sum 18672 B in RAM sections; `size` reports 18680 B data+bss total).

### 2.3 Largest static RAM consumers (baseline, `nm --size-sort -S`)

| Size (B) | Sym | Notes |
|---:|---|---|
| 2048 | `s_snap` | ADC frozen snapshot (`app/acq.c`) |
| 2048 | `s_buf` | DMA circular ADC ring (`app/acq.c`) |
| 1948 | `s_ets` | ETS epoch accumulators/state (`app/ets_epoch`/`app/main.c`) |
| 1280 | `s_avg_acc` | Averaging accumulator (`app/main.c`) |
| 1024 | `s_acq_scratch` | Union: ETS transient / stream TX scratch (`app/main.c`) |
| 656 | `s_scratch` | Wave store save/load scratch (`app/wave_store.c`) |
| 640 | `s_frame` | Working frame buffer (`app/main.c`) |
| 600×6 | `s_prev_lo/hi`, `s_cur_lo/hi`, `s_means`, `s_disp` | Scope column buffers (`app/main.c`) |
| 312 | `__sf` | newlib stdio FILE table (pulled in via stdio usage) |
| 270 | `s_diag_rows_prev` | Diagnostics UI cache (`app/main.c`) |
| 76 | `_impure_data` | newlib reentrancy data |

Heap/stack reservation: **3072 B** total (1 KiB + 2 KiB). `malloc/free/_sbrk` symbols are linked (nano libc/newlib paths observed in map); runtime heap usage required measurement (see §2.4).

### 2.4 Live read-only instrument observations (installed 41320, UART GETV, no writes)

Queried read-only via PC link (`GET_VALUE`) over `/dev/ttyUSB0` on `benabus@192.168.1.201` (permission granted; no flash/reset/halt):

| Field | GETV | Value |
|---|---:|---:|
| `BUILD_SEQ` | 11 | **41320** |
| `STATUS` | 4 | **1** |
| `MEM_STACK_FREE_NOW` | 28 | **2848 B** |
| `MEM_STACK_FREE_MIN` (watermark scan) | 29 | **2352 B** |
| `MEM_HEAP_USED` | 30 | **0 B** |
| `MEM_HEAP_HIGHWATER` | 31 | **0 B** |
| `MEM_MSP_LOW_WATER` | 32 | **19008** (`0x4A40`) |
| `MEM_MSP_FREE_MIN` (MSP low-water derived) | 33 | **2384 B** |

Interpretation (against 41320 layout): stack bottom at heap limit `0x200040F0`; MSP low-water `0x4A40` implies **2384 B** minimum headroom above stack bottom during observed runtime. Heap highwater **0** indicates no heap growth observed. These are measurements of the **installed 41320 firmware**, not of the modified post-audit build.

---

## 3. Phase 2 — Investigation (opportunities considered)

| Area | Finding | Proposed? | Rationale / risk |
|---|---|---:|---|
| `s_acq_scratch.trig_search` (1024 B) in `app/main.c` | Comment/usage show trig anchor scans ring in place; union lane used as ETS transient buffer requiring `BENCH_WINDOW` (320) samples, not 512 | **Yes** | Safe, behaviour-preserving: `TRIG_SEARCH_LEN 512→320` (==`BENCH_WINDOW`) keeps assert `>=BENCH_WINDOW`; union shrinks to TX-sized lane (644 B). Actual saving **380 B** (with alignment effects). |
| Scope differential-render y arrays (`s_prev_lo/hi`,`s_cur_lo/hi`, trig/ground y) | Stored plot y coordinates (0–239, plot 18–209) as `uint16_t` | **Yes** | Safe: values bounded to screen; sentinel `0xFFFF/0` only used when branch excluded by `i>=ncols` (logic preserved). Convert to `uint8_t` for **~1202 B** saved. |
| `s_scratch` (656 B) in `app/wave_store.c` | Synchronous save/load/valid scratch; could share/stack in theory | No | Lifetime/stack-depth proof non-trivial; stack buffer risks overflow given limited headroom; sharing needs careful audit. Avoids risky refactor. |
| `s_ets` (1948 B), `s_avg_acc` (1280 B) | Large mode-specific/stateful buffers | No | Sharing/lifetime coupling complex; high regression risk to ETS/averaging/acquisition paths. |
| `s_buf`/`s_snap` (2×2048 B) | DMA circular target + immutable snapshot | No | Fundamental to acquisition architecture/timing; reduction requires major redesign. |
| Reserved heap/stack (`1 KiB + 2 KiB`) | Heap highwater observed 0 on 41320 runtime; malloc symbols linked | No (this change set) | Evidence suggests headroom, but reducing linker reservation affects runtime guarantees and any future libc allocations; needs broader validation. |
| newlib `__sf` (312 B), `_impure_data` (76 B) | Pulled by stdio usage (`snprintf`/formatting paths) | No | Removing requires replacing formatting/stdio usage (large refactor, risk). |
| Struct packing/alignment/misc globals | Possible micro-savings | No | Low value and/or DMA/peripheral/alignment risk. |

---

## 4. Phase 3 — Changes implemented (safe, small)

### 4.1 `app/main.c` — reduce ETS transient scratch capacity
- **Before:** `TRIG_SEARCH_LEN 512U` (comment: "software edge-refinement search window"); union member `uint16_t trig_search[TRIG_SEARCH_LEN]` (1024 B) made `s_acq_scratch` 1024 B.
- **After:** `TRIG_SEARCH_LEN` set to `BENCH_WINDOW` (320) with documentation that the lane is now ETS-only transient storage (anchor scans ring in place). `_Static_assert(TRIG_SEARCH_LEN >= BENCH_WINDOW, ...)` retained.
- **Expected/measured:** union max becomes 644 B (TX lane) vs 1024 B; **380 B** reduction in `s_acq_scratch` contribution.
- **Behavioural risk:** none intended — ETS code only requires `BENCH_WINDOW` samples and already asserted this; no DMA/acquisition timing/protocol changes.

### 4.2 `app/main.c` — narrow scope differential-render y tracking to `uint8_t`
- **Before:** `s_prev_lo/hi`, `s_cur_lo/hi` (`UI_PLOT_W` each), `s_prev_trig_y`, `s_prev_ground_y` as `uint16_t`.
- **After:** stored as `uint8_t` (screen y coordinates). Sentinel logic adjusted to remain safe when `i >= ncols` (branch condition unchanged).
- **Expected/measured:** **1202 B** reduction (4×300 + 2).
- **Behavioural risk:** low — values constrained to display coordinates; render paths unchanged apart from storage width/casts.

### 4.3 `tools/size_report.sh` — RAM budget warning (build-time)
- Added `RAM_WARN` (default **18432 B**, 18 KiB = 90% of 20 KiB) with explanatory comments; warns when `data+bss > RAM_WARN` while keeping hard fail at `RAM_MAX`.
- **Behavioural risk:** none to firmware; build now surfaces earlier pressure.

**No other source/functional logic changes.** Flash/protocol/ETS/trigger/acquisition/storage semantics unchanged by design.

---

## 5. Phase 4 — Validation (host/build only)

| Check | Result |
|---|---|
| `make` (after changes) | **Pass** — `text 40820`, `data 136`, `bss 16968`, RAM `17104 / 20480 B (83%)`, Flash `40956 / 61440 B (66%)` |
| `make test` | **Pass** — all host suites passed (36 suites, 1,318 checks, 0 failures); existing warnings in `tests/test_link_host.c` unchanged |
| Protocol/logic tests | Covered by `make test` (includes link/protocol/trigger/ETS/diag suites) |
| `tools/size_report.sh` | **Pass** (no warning at 17104 B < 18432 B) |
| Hardware validation | **Not performed** (no instrument interaction beyond read-only GETV diagnostics on installed 41320) |

Artifacts: `evidence/B4_37_RAM_AUDIT_20261115/after/{dso183.{elf,bin,map},size*.txt,nm_size.txt,after_build.log,test.log}`.

### 5.1 After-change build identity

| Item | Value |
|---|---|
| Build sequence | `27803` |
| `build/dso183.bin` | 40,960 B, SHA-256 `7c1c23e92520c53178c32cb428028b920d1fead448aeed951c794f2785d4008e` |
| `build/dso183.elf` | SHA-256 `0283c8a2649efe8ea8ddda28a2f2407f06e230e4ad39427ae1567249c57a1246` |
| `size -A` | `.data 128`, `.bss 13852`, `.noinit 40`, `._user_heap_stack 3076` |
| Map | `_ebss/_enoinit 0x2000369C/0x200036C4`, heap+stack ends `0x200042C8`, **3384 B** free above reserved stack to `_estack` |

Build identity changed because `app/main.c` (and generated `build_seq`) changed; behaviour-preserving intent only.

---

## 6. Before/after comparison (actual, measured)

| Metric | Baseline (41320, unchanged) | After (27803, this audit) | Delta |
|---|---:|---:|---:|
| `text` | 40820 B | 40820 B | 0 |
| `data` | 136 B | 136 B | 0 |
| `bss` (`size`) | 18544 B | 16968 B | **-1576 B** |
| RAM static total (`data+bss`) | 18680 B (91%) | 17104 B (83%) | **-1576 B** |
| `.bss` section (`size -A`) | 15432 B | 13852 B | **-1580 B** |
| `._user_heap_stack` section | 3072 B | 3076 B | +4 B (alignment/layout accounting) |
| Free above reserved stack (`_estack - heap_stack_end`) | 272 B | 3384 B | **+3112 B** (includes static savings shifted layout) |
| Flash image | 40956 B (66%) | 40956 B (66%) | 0 |

**Actual reclaimed static RAM:** **1576 bytes** (reported), attributed to: ETS scratch union sizing (−380 B effective) and y-tracking storage narrowing (−~1202 B) with minor alignment effects.

---

## 7. RAM budget recommendation (Phase 5)

**Platform facts:** STM32F103C8 provides **20,480 B** SRAM. Linker currently reserves **3,072 B** for heap+stack (`1 KiB + 2 KiB`) inside `._user_heap_stack`; `arm-none-eabi-size` RAM = `data+bss` includes this reservation. Post-change static total is **17,104 B**, leaving **3,376 B** to the 20 KiB hard limit and **3,384 B** free above the reserved stack top.

**Recommended budget (with rationale):**
- **Warning threshold:** **18,432 B** (18 KiB, 90% of SRAM) — implemented as default `RAM_WARN` in `tools/size_report.sh`. Rationale: preserves ~2 KiB margin beyond reported static usage for stack-depth variation, interrupt/ISR nesting, and safe future growth while heap/stack reservation remains 3 KiB.
- **Hard limit:** **20,480 B** (existing) — must not be exceeded.
- **Operational note:** keep **≥1.5–2 KiB** free above the reserved stack in practice; live 41320 MSP low-water showed ~2.3 KiB minimum headroom above stack bottom, so static/layout reductions materially improve margin but stack-heavy changes still require care (and, when authorized, runtime measurement).
- **Future consideration (not implemented):** heap highwater was **0** on 41320 read-only observation, suggesting the 1 KiB heap reservation *may* be reducible; any change to `_min_heap_size`/stack layout should be validated for libc/runtime usage and worst-case stack depth before adoption.

**Build-time check:** `tools/size_report.sh` now warns above `RAM_WARN` (overridable via env) and fails above `RAM_MAX`.

---

## 8. Risks, limitations, and non-claims

- **No hardware validation:** modified build (27803) was not flashed/run on the instrument. Live UART readings reflect installed **41320** only.
- **Behaviour preserved by inspection and host tests:** trigger/ETS/acquisition/DMA/protocol/streaming/storage logic not intentionally changed; host suites all pass.
- **Stack depth not re-measured on modified build:** static headroom increased, but actual worst-case stack depends on runtime paths; MSP watermark diagnostics remain available via GETV on-device.
- **Heap reservation unchanged:** 1 KiB still reserved despite observed 0 highwater (conservative).
- **Large buffers remain:** `s_buf`, `s_snap`, `s_ets`, `s_avg_acc`, `s_frame`, etc. are required/high-risk to restructure; further savings need careful, case-specific design.

---

## 9. Deliverables and evidence index

- Report: `B4_37_RAM_AUDIT_AND_OPTIMISATION_REPORT.md`
- Baseline artifacts: `evidence/B4_37_RAM_AUDIT_20261115/{dso183.{elf,bin,map},baseline_*.txt,baseline_build.log,baseline_nm_size.txt}`
- After-change artifacts: `evidence/B4_37_RAM_AUDIT_20261115/after/{dso183.{elf,bin,map},size*.txt,nm_size.txt}` plus `after_build.log`, `test.log`
- Code changes (not committed): `app/main.c`, `tools/size_report.sh`

**Outcome:** **1576 bytes** of static RAM reclaimed safely (17104 B / 83% vs 18680 B / 91%), with build-time budget warning added and live read-only stack/heap measurements recorded for the installed 41320 image. No regressions observed in host build/tests; no instrument programming performed.

---

## 10. B4.38 addendum — continued RAM-only investigation (2026-11-15)

**Scope:** RAM reduction only, continuing from the B4.37 working tree (current baseline). No acquisition, DMA, trigger, ETS, UART, persistence, flash-size, or performance refactoring. No instrument flash/reset/program/erase. No commit/push. B4.37 RAM warning threshold (18,432 B) and hard limit (20,480 B) preserved in `tools/size_report.sh`.

### 10.1 B4.38 baseline (current working tree, rebuilt)

| Item | Value |
|---|---|
| Source state | B4.37 modifications preserved (`app/main.c`, `tools/size_report.sh`, this report) + `app/ets_epoch.{c,h}` changes below |
| Baseline build (`evidence/B4_38_RAM_AUDIT_20261115/`) | `STATUS_BUILD_SEQ 27803` |
| `dso183.bin` / `dso183.elf` (B4.38 baseline) | SHA-256 `7c1c23e9…008e` / `0283c8a2…1246` |
| `arm-none-eabi-size` | `text 40820`, `data 136`, `bss 16968` → **RAM 17104 / 20480 B (83%)** |
| Sections (`size -A`) | `.data 128`, `.bss 13852`, `.noinit 40`, `._user_heap_stack 3076` |
| Top consumers (B4.38 baseline) | `s_snap` 2048, `s_buf` 2048, `s_ets` 1948, `s_avg_acc` 1280, `s_scratch`(wave_store) 656, `s_acq_scratch` 648, `s_frame` 640, `s_means` 600, `s_disp` 600, `s_prev/cur lo/hi` 300×4, `__sf` 312, `s_diag_rows_prev` 270 |

### 10.2 Candidates investigated

| Candidate | Exact storage / current size | Proposed change | Expected saving | Behavioural/concurrency risks | Evidence / decision |
|---|---|---:|---:|---|---|
| ETS per-bin sample count | `struct ets_epoch.cnt[ETS_EPOCH_WINDOW]` = `uint16_t[320]` (**640 B**) inside `s_ets` (1948 B), `app/ets_epoch.h/.c` | Narrow `cnt` to `uint8_t[320]` and saturate at `0xFF` | **320 B** | Low: wrong narrowing could overflow counts/fill tracking and corrupt ETS reconstruction | **Implemented.** Evidence: `ETS_EPOCH_SWEEPS_TARGET == 16` and epoch reconstructs/resets at target; accepted sweeps per epoch ≤16, so each bin receives ≤16 increments total (`ets_epoch_add` per mapped sample, bounded by sweep/epoch lifecycle). `filled` tracks nonzero bins; reconstruction divides `sum/bin_count`. No API exposes per-bin counts; host `ets_epoch`/`ets_*` tests pass. |
| Scope column-means buffer | `static uint16_t s_means[UI_PLOT_W]` = `uint16_t[300]` (**600 B**) in `app/main.c` | Remove `s_means`; compute each column mean inline in `scope_draw` and apply identical shift/smoothing directly into `s_disp` | **600 B** | Low-medium (UI rendering path): arithmetic/order change could alter smoothing/trace; extra per-column recomputation in smoothing path (no acquisition/DMA/trigger/ETS/UART/persistence logic touched) | **Implemented.** Evidence: `s_means` was recomputed every draw via `wave_column_means` and consumed only in the same `scope_draw` pass to initialise/update `s_disp` (`s_disp` remains persistent for temporal smoothing). Inlined logic replicates `wave_column_means` slicing/rounding (`(sum + count/2)/count`, same `start/end` bounds) and preserves existing smoothing thresholds/alpha. `wave_column_means` API/tests unchanged. Symbol absent from final link; host tests pass. |
| Wave-store flash scratch | `static uint8_t s_scratch[REC_MAX]` = **656 B** in `app/wave_store.c` (save/load/valid) | Share with `s_acq_scratch` / `s_avg_acc` / `s_frame`, or move to stack, or stream CRC/write | 656 B max (if shared) | High: `save` needs source `s_frame` and scratch concurrently; `valid` is called from menu/link paths and would corrupt `s_avg_acc` averaging state if aliased; `s_acq_scratch.tx` is 644 B (12 B short) and has different lifetimes; stack buffer conflicts with “do not move large static buffers onto stack”; split flash writes unsafe without erase-aware streaming API guarantees | **Rejected.** Lifetime/ownership and flash-programming constraints make sharing/streaming speculative without deeper refactor. |
| Averaging accumulator vs ETS | `s_avg_acc` `uint32_t[320]` (**1280 B**) vs `s_ets` (**1628 B** after narrowing) | Union if mutually exclusive | up to 1280 B | High: incorrect exclusivity assumption would corrupt ETS or averaging | **Rejected.** ETS reconstruction publishes into `s_frame` and subsequent averaging logic may process frames when `s_avg_n > 1`/aligned; mutual exclusion not evidenced. |
| Display/trace state arrays | `s_disp` 600 B, `s_prev/cur lo/hi` 300 B×4, `s_frame` 640 B | Further narrowing/sharing | varies | Medium-high: required for differential render, smoothing, and frame ownership across draw/acquisition paths | **Rejected.** No clearly safe reduction found without rendering/behaviour changes. |
| Diag render caches | `s_diag_rows_prev` **270 B**, static `s_diag_cal_prev[9][16]` 144 B, `s_diag_status_prev[3][48]` 144 B in `app/main.c` | Shrink/share/remove diff caches | up to ~558 B | Medium: would change SYSTEM DIAG rendering/update behaviour (flicker/readability) or require mode-lifetime proof | **Rejected.** Purposeful UI diff caches; not unnecessary/oversized with clear safety argument. |
| newlib stdio state | `__sf` 312 B, `_impure_data` 76 B | Remove/reconfigure stdio usage | ~388 B | High: requires widespread `snprintf`/formatting refactor | **Rejected.** Out of scope / not a small safe change. |
| Linker heap/stack reservation | `._user_heap_stack` **3076 B** (`1 KiB + 2 KiB`) | Reduce `_min_heap_size`/`_min_stack_size` | up to 3076 B (reservation) | High: runtime stack/heap guarantees; prior zero heap high-water is insufficient evidence | **Rejected (per scope).** Not reduced. |
| Acquisition/DMA/ETS/UART buffers | `s_buf`/`s_snap` 2048 B each, `s_acq_scratch` 648 B | Redesign/shrink/share | large but unknown safe amount | Critical: timing/DMA/trigger/ETS/protocol/persistence architecture | **Rejected.** Would require critical architecture/timing changes. |

### 10.3 Changes implemented (small, independently measured)

1. **`app/ets_epoch.h`, `app/ets_epoch.c` — narrow ETS per-bin counters**
   - `uint16_t cnt[ETS_EPOCH_WINDOW]` → `uint8_t cnt[ETS_EPOCH_WINDOW]` (saturating), 640 B → 320 B in struct.
   - Rationale: per-epoch accepted sweeps ≤ `ETS_EPOCH_SWEEPS_TARGET` (16), so per-bin sample count cannot exceed 16 under existing accumulation/reconstruction lifecycle.
   - Measured step (after clean build+tests): `s_ets` 1948 → **1628 B** (`nm`), RAM 17104 → **16784 B** (`text 40804`, `bss 16648`). Artifacts: `evidence/B4_38_RAM_AUDIT_20261115/after_ets_cnt/` (seq 9155, hashes `3a31804a…02ef` / `5edb0392…007b`).

2. **`app/main.c` — remove `s_means` scope-render scratch**
   - Eliminated `static uint16_t s_means[UI_PLOT_W]` (600 B) and inlined equivalent per-column mean computation into `scope_draw`, feeding the existing `s_disp` initialisation/smoothing logic unchanged.
   - Rationale: means were transient within a single draw pass and fully derivable from `s_frame` with the same slicing/rounding used by `wave_column_means`.
   - Measured step (after clean build+tests): `s_means` absent from link map, RAM 16784 → **16184 B** (`text 40764`, `bss 16048`). Artifacts: `evidence/B4_38_RAM_AUDIT_20261115/after_s_means/` (seq 58810, hashes `00a1e11d…abba` / `d5b21402…ea7b`).

### 10.4 Validation

| Check | Result |
|---|---|
| `make` after each change | **Pass** (clean rebuilds) |
| `make test` after each change and final | **Pass** — 36 suites, **1,318 checks, 0 failures** (unchanged total) |
| `tools/size_report.sh` | **Pass** — `RAM 16184 / 20480 B (79%)`, warning threshold 18432 B preserved, hard limit 20480 B preserved |
| Hardware validation | **Not performed** (no instrument interaction; modified builds not flashed) |

### 10.5 Before/after comparison (actual, measured)

| Metric | B4.37 after (tree baseline) | B4.38 final | Delta (B4.38) | Delta vs original 41320 baseline (18680 B) |
|---|---:|---:|---:|---:|
| `text` | 40820 B | 40764 B | -56 B | -56 B |
| `data` | 136 B | 136 B | 0 | 0 |
| `bss` (`size`) | 16968 B | 16048 B | **-920 B** | **-2496 B** |
| RAM static (`data+bss`) | **17104 B (83%)** | **16184 B (79%)** | **-920 B** | **-2496 B** |
| `.bss` section (`size -A`) | 13852 B | 12932 B | -920 B | -2500 B |
| `._user_heap_stack` | 3076 B | 3076 B | 0 | +4 B (layout/alignment accounting) |
| Flash image | 40956 B (66%) | 40900 B (66%) | -56 B | -56 B |
| Key symbols | `s_ets` 1948 B, `s_means` 600 B | `s_ets` 1628 B, `s_means` removed | -320 B, -600 B | — |

**Actual reclaimed in B4.38:** **920 bytes** static RAM (17104 → 16184 B). **Cumulative reclaimed from original 41320 baseline:** **2496 bytes** (18680 → 16184 B).

### 10.6 Remaining risks and limitations

- **No hardware validation:** final build (seq 58810) not flashed/run; behaviour preservation argued by invariants, localized changes, and passing host tests only.
- **Scope-render inlining:** mean computation duplicated inline in `scope_draw` (logic equivalent to `wave_column_means`); rendering performance/structure changed only insofar as an intermediate buffer was removed (not a goal-directed perf refactor).
- **Limited further safe headroom:** largest remaining buffers (`s_buf`/`s_snap` 2048 B each, `s_ets` 1628 B, `s_avg_acc` 1280 B, wave-store scratch 656 B, frame/trace/display arrays) are either architecturally required, mode-critical, or lack clear evidence for safe lifetime sharing/narrowing. `__sf`/`_impure_data` reduction would require broad stdio refactoring.
- **Heap/stack reservations unchanged** (3076 B) per constraint; live 41320 runtime showed heap highwater 0 but reservation not reduced.

### 10.7 B4.38 evidence index

- Baseline: `evidence/B4_38_RAM_AUDIT_20261115/{dso183.{elf,bin,map},build_seq.h,size*.txt,nm_size.txt,build.log}`
- Step 1 (ETS cnt): `evidence/B4_38_RAM_AUDIT_20261115/after_ets_cnt/`
- Step 2 (s_means): `evidence/B4_38_RAM_AUDIT_20261115/after_s_means/`
- Modified source (uncommitted): `app/ets_epoch.h`, `app/ets_epoch.c`, `app/main.c` (plus B4.37 `tools/size_report.sh` preserved)

**B4.38 outcome:** **920 B** additional safe, measured static RAM reclaimed (16184 B / 79% of 20 KiB), with full build+tests passing after each step and B4.37 RAM warning/hard limits preserved. Further savings appear to require changes to critical acquisition/timing/UI-state architecture or larger refactors; investigation stopped per scope.

## B4.40 deployment and validation (additive)

- **2026-11-15 (UTC):** Reviewed B4.38 candidate (build seq **58810**, bin 40904 B `00a1e11d…abba`, elf `d5b21402…ea7b`, RAM **16184/20480 B, 79%**) deployed to hardware via `tools/flash_cycle.py` only (page-wise, page 0 last, per-page/full readback verification, reset/release, identity confirmed; `0x0800FC00` not written; 14139 recovery SHA256 verified). Pre-deployment checks (diff/build/test/hashes/recovery) passed; `make test` 36 suites/1318 checks.
- **Hardware validation:** UART/streaming and runtime heap/stack diagnostics verified (heap 0). B4.36 trigger campaign (12 configs × 50) completed **600/600 coherent**, `no_frame=0`, `diag_not_ok=0`, requested crossings within ±2 of index 96 incl. 50 µs rising AUTO/NORM, comparable to B4.36 41320. ETS TB0–TB4 sampled with no critical regression indicated (waveform/range-dependent; see report). Display rendering/smoothing/differential redraw not remotely observable.
- **Evidence/report:** `evidence/B4_40_DEPLOY_AND_VALIDATION_20261115T202159Z/` and `B4_40_DEPLOYMENT_AND_VALIDATION_REPORT.md`. No commit/push; no production code changes.
