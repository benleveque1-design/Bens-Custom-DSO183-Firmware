# Flash hand-over — DSO183 firmware (B4.36, build 41320)

Use a **safe, page-wise, read-back-verified** flash path. **Never use `st-flash write`** on STM32F1 (mass-erase risk can destroy calibration/settings and the vendor device record at `0x0800FC00`).

## 1. Current candidate (B4.36)

| Item | Value |
|---|---|
| Build sequence | **41320** (`0xA168`) |
| Image | `build/dso183.bin` (40,960 B) / `build/dso183.elf` |
| SHA-256 (bin) | `bfa6e7bf8bf681d202dba6267b5b8443d6ccd7fd2a2e234d9f1b0b7cc40e7efd` |
| SHA-256 (elf) | `7d2f6d1eddd82f59505c0a55e1345bdf10b2dd584230e01829ca084be3166d87` |
| Flash use | 40,956 / 61,440 B (66%) — ends below reserved/calibration region |
| RAM use | 18,680 / 20,480 B (91%) — data 136, bss 18,544 |
| Host tests | 36 suites, 1,318 checks, 0 failures (`make test`, B4.36 tree) |
| Instrument status | **Deployed and hardware-validated** on the tested instrument (2026-11-15): UART `BUILD_SEQ 41320`, `STATUS 1`; 50 µs/div trigger campaign 12×50 = 600/600 coherent, primary and regressions pass |

Canonical preserved copies: `evidence/B4_36_TRIGGER_FIX_AND_VALIDATION_20261114/candidate/` and `build/` (gitignored locally).

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
- UART identity: `GETV_BUILD_SEQ == 41320`, `STATUS == 1` (acquisition healthy).
- Basic responsiveness: stream toggle / GET_VALUE replies OK.
- On first bench use: confirm expected display/trace behaviour for your signal and settings.

## 5. Recovery

- **Preserved recovery image (build 14139):** `evidence/stage1_ets_campaign_20261113T033944Z/B4_29_CONTROLLED_DEPLOYMENT/preflash_recovery/installed_14139_flash_0x08000000_len0x10000.bin` (65,536 B, SHA-256 `e2bd129fd044f44246e9c5a85547d92f2559f6b25ee72ced4d7fe0eff09949f8`). Do not overwrite.
- Restore only via the **safe page-wise path** (never `st-flash write`/mass erase). After restore, independently verify readback hash and UART identity (`BUILD_SEQ 14139`, `STATUS 1`).
- If behaviour is abnormal (no splash, frozen UI, verify/identity mismatch, instability): stop, power cycle, preserve evidence, and recover via the safe path.

## 6. B4.36 notes (current functionality)

- 50 µs/div trigger uses span-bounded edge anchoring with coherent per-capture diagnostics. Hardware-validated on 41320 (600/600 coherent, tb5 rising 50/50 AUTO & 50/50 NORM within ±2 of index 96; regressions clean).
- ETS/TB0–TB4 remain equivalent-time ranges with range/waveform-dependent proven behaviour per prior B4.32 evidence (not re-campaigned on 41320).
- See `B4_36_TRIGGER_FIX_AND_VALIDATION_REPORT.md`, `README.md`, and `docs/FEATURES.md` for details and limits.
