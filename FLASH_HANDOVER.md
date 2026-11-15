# Flash hand-over — DSO183 firmware (B4.40 candidate, build 58810)

Use a **safe, page-wise, read-back-verified** flash path. **Never use `st-flash write`** on STM32F1 (mass-erase risk can destroy calibration/settings and the vendor device record at `0x0800FC00`).

## 1. Latest deployed/validated candidate (B4.40, uncommitted/unreleased working-tree state)

> **Note:** B4.40 is an audited B4.37/B4.38 RAM-optimisation candidate deployed and hardware-validated on the tested instrument (2026-11-15). It is **not** committed, tagged, or released; the repository working tree also contains uncommitted production/source changes for this candidate. See `B4_40_DEPLOYMENT_AND_VALIDATION_REPORT.md`, `B4_41_POST_DEPLOYMENT_AUDIT_REPORT.md`, `B4_42_RELEASE_READINESS_REPORT.md`, and `evidence/B4_40_DEPLOY_AND_VALIDATION_20261115T202159Z/EVIDENCE_INDEX.md`. B4.36 (build 41320) remains the prior validated reference.

| Item | Value |
|---|---|
| Build sequence | **58810** (`0xE5BA`) |
| Image | `build/dso183.bin` (40,904 B) / `build/dso183.elf` (candidate, as built for B4.40) |
| SHA-256 (bin) | `00a1e11da6a97e727125ebda40c62fbc9c638ce04ca0b652ebdb616c23afabba` |
| SHA-256 (elf) | `d5b21402b0fd99b36c6e47e3d146b8f50472e08f2b6ee53c3eb31694d2aeea7b` |
| Flash use | 40,900 / 61,440 B (66%) — ends below reserved/calibration region (`0x08009FC8 < 0x0800FC00`) |
| RAM use | 16,184 / 20,480 B (79%) — data 136, bss 16,048 |
| Host tests | Preserved `make test` log (`evidence/B4_40_DEPLOY_AND_VALIDATION_20261115T202159Z/test.log`) records suite pass lines with no recorded failures in the preserved output; no aggregate summary line (suite/check totals) is explicitly present in that log. |
| Instrument status | **Deployed and hardware-validated** on the tested instrument (2026-11-15) via `tools/flash_cycle.py` (page-wise, page 0 last, per-page + whole-image readback verified, identity confirmed): UART `BUILD_SEQ 58810`, `STATUS 1`; trigger campaign 12×50 = **600/600 coherent**, `no_frame=0`, `diag_not_ok=0`, requested crossings within ±2 of index 96 (incl. 50 µs rising AUTO/NORM), comparable to B4.36 41320 on equivalent criteria. ETS sampled (TB0–TB4+) with no critical regression indicated in collected evidence (range/waveform-dependent; TB0/TB2 zero frames in short sampling window but non-zero ETS diag activity) — **not** full ETS HW validation/equivalence. Display rendering/smoothing/differential redraw not visually verified remotely. |

Preserved candidate/evidence: `evidence/B4_40_DEPLOY_AND_VALIDATION_20261115T202159Z/` (see `EVIDENCE_INDEX.md`). B4.36 preserved copies remain in `evidence/B4_36_TRIGGER_FIX_AND_VALIDATION_20261114/candidate/` and B4.36 validation in `evidence/B4_36_HW_VALIDATION_20261115T053222Z/`.

## 2. Reserved / protected flash regions (never erased by image writes)

| Range | Owner | Notes |
|---|---|---|
| 0x0800E800..0x0800EBFF | waveform save slot 0 | CRC-protected |
| 0x0800EC00..0x0800EFFF | waveform save slot 1 | CRC-protected |
| 0x0800F000..0x0800F3FF | settings slot A | alternating, CRC/sequence |
| 0x0800F400..0x0800F7FF | settings slot B | alternating, CRC/sequence |
| 0x0800F800..0x0800FBFF | calibration page | CRC record |
| 0x0800FC00..0x0800FFFF | vendor device record | **NEVER written** |

The application image (40,960 B) occupies low flash and must not reach protected pages.

## 3. Safe flashing (authorised only)

**Authorisation is required before any device programming.** Use a page-wise, read-back-verified tool that writes **page 0 last** and verifies identity.

### Full controlled image flash (recommended when authorised)
```bash
python3 tools/flash_cycle.py --image build/dso183.bin --elf build/dso183.elf
```
`tools/flash_cycle.py` erases/programs pages, verifies each page and the full image readback, queues reset/releases the core, and checks build identity in RAM.

### Page-wise writer (alternative, when applicable)
```bash
make flash-dry
AUTHORIZED=1 python3 tools/flash/write_image.py --image build/dso183.bin
```
Follow the tool’s safeguards (per-page erase/program/verify, IWDG frozen during erase, reset only after the final/page-0-last sequence as implemented). Do not improvise.

## 4. Post-flash verification

Confirm at minimum:
- Flash readback/image verification succeeds (tool-reported).
- UART identity: `GETV_BUILD_SEQ` matches the intended candidate (e.g. **58810** for B4.40, or **41320** for B4.36), `STATUS == 1` (acquisition healthy).
- Basic responsiveness: stream toggle / GET_VALUE replies OK.
- On first bench use: confirm expected display/trace behaviour for your signal and settings.

## 5. Recovery

- **Preserved recovery image (build 14139):** `evidence/stage1_ets_campaign_20261113T033944Z/B4_29_CONTROLLED_DEPLOYMENT/preflash_recovery/installed_14139_flash_0x08000000_len0x10000.bin` (65,536 B, SHA-256 `e2bd129fd044f44246e9c5a85547d92f2559f6b25ee72ced4d7fe0eff09949f8`). Do not overwrite.
- Restore only via the **safe page-wise path** (never `st-flash write`/mass erase). After restore, independently verify readback hash and UART identity (`BUILD_SEQ 14139`, `STATUS 1`).
- If behaviour is abnormal (no splash, frozen UI, verify/identity mismatch, instability): stop, power cycle, preserve evidence, and recover via the safe path.

## 6. Notes (B4.40 candidate and B4.36 reference)

- **B4.40 (58810, validated candidate, uncommitted/unreleased):** RAM optimisations (B4.37/B4.38) deployed and hardware-validated on the tested instrument; trigger campaign 600/600 coherent with requested crossings within ±2 of index 96 (incl. 50 µs rising AUTO/NORM), comparable to B4.36 on equivalent criteria. ETS sampled only (TB0–TB4+; TB0/TB2 zero frames in short sampling window but non-zero diag activity) — not full HW validation/equivalence. Display visual inspection not performed remotely. See `B4_40_DEPLOYMENT_AND_VALIDATION_REPORT.md` and `evidence/B4_40_DEPLOY_AND_VALIDATION_20261115T202159Z/EVIDENCE_INDEX.md`.
- **B4.36 (41320, prior validated reference):** 50 µs/div trigger span-bounded edge anchoring hardware-validated (600/600 coherent, tb5 rising 50/50 AUTO & 50/50 NORM within ±2 of index 96). ETS/TB0–TB4 remain equivalent-time ranges with range/waveform-dependent proven behaviour per prior B4.32 evidence (not re-campaigned on 41320). See `B4_36_TRIGGER_FIX_AND_VALIDATION_REPORT.md`, `README.md`, and `docs/FEATURES.md`.
