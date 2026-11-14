# DSO183 Custom Firmware Project — Agent Instructions

## Project Overview
This project contains the custom firmware for the JYE Tech DSO183 (18301K) oscilloscope. The firmware is a working scope core (acquisition, snapshot, trigger modes, display, front-panel/menu, PC link), host-tested and partly bench-verified; it is not yet a product. Current state, defects and remaining work are tracked in `docs/AUDIT_2026-11-05.md` and campaign evidence/reports. Critical BUG-001 rules (no status-byte gate, always arm/feed IWDG) remain mandatory.

## Current instrument / campaign status (B4.32, 2026-11-14 UTC)
- **Hardware host (bench):** Raspberry Pi `benabus@192.168.1.201` (Benabus). **Probe:** ST-LINK/V2 `12010C1C8418303030303032` (V2J37S7, STM32F1xx_MD, 0x410, 64 KiB). **UART:** `/dev/ttyUSB0` (FTDI FT232R A5069RR4, `0403:6001`). Do not treat workstation USB enumeration as evidence that hardware is absent.
- **Installed firmware (as of B4.32):** **build_seq 12066** (B4.27 `dual_b1_bound` ETS trigger-phase integration candidate), independently verified post-flash (SWD readback hash + UART `GETV_BUILD_SEQ==12066`, responsive/running).
  - Candidate image: `dso183.bin` 39,988 B, SHA-256 `983e6f6c9f0108663ffcf96abc832dc253c0f686f1e41c514bba0864de3c2c35`.
  - Deployed via `tools/flash_cycle.py` on Pi (page-wise, page 0 last, per-page + whole-image verify, identity check). No `st-flash write`.
- **Recovery (pre-deployment, preserved):** installed **build_seq 14139** full flash image captured read-only over SWD (attach, no halt/reset) and archived intact: `evidence/stage1_ets_campaign_20261113T033944Z/B4_29_CONTROLLED_DEPLOYMENT/preflash_recovery/installed_14139_flash_0x08000000_len0x10000.bin` (65,536 B, SHA-256 `e2bd129fd044f44246e9c5a85547d92f2559f6b25ee72ced4d7fe0eff09949f8`, UART identity 14139). Do not overwrite.
- **B4.32 hardware validation (evidence separate from host/sim):** `evidence/stage1_ets_campaign_20261113T033944Z/B4_32_DEPLOYMENT_VALIDATION/` (report: `B4_32_DEPLOYMENT_AND_HARDWARE_VALIDATION_REPORT.md`, raw `hw/20261114T025538Z/`). Applied B2 Amendment A (C1–C5) + B-REC.
  - **AC-6 (deployment/identity): PASS.** AC-5 (TB5/normal/stream/control, tested scope): no regression observed.
  - **TB4 fast undersampled square (AC-3): PASS** in captured coherent window (fill 128, ready 1, quantised placement, `reject_unbound==0`).
  - **TB2/TB3 fast undersampled:** 80-bin gate not demonstrated in captured windows (fills 32/64).
  - **TB0/TB1 fast aperture-unresolved square (AC-4): coverage-limited (structural/honest)** — 80-bin metric reported, **no PASS/FAIL**; no coverage fabricated, no integrity relaxed, no success claimed by gate exclusion.
  - **AC-2 (resolved sine/triangle, TB0–TB4): not tested** (no sine/triangle source available). Integrity/attribution diagnostics consistent in observed coherent cases; some ETS snapshots seq_mismatch-limited during active accumulation (reported).
- **Note:** `evidence/`, `build/`, `logs/` are gitignored (local/machine-specific). Campaign artifacts live locally under `evidence/`.

---

## Camera — Tunable Snapshot Interface
A live ESP32-CAM snapshot interface may be available on the **local bench LAN** at `http://<bench-camera-host>/`.

- **Read-only reference only.** Do not issue POST/control requests or modify configuration.
- Keep DSO183 development/flashing isolated from the camera network segment unless explicitly required.

## Strict guard rails — never brick the DSO183
**We have one instrument. Do not risk bricking it.** Mandatory rules:

### 1. Flashing policy
- **Never use `st-flash write`** (STM32F1 mass-erase risk; can destroy reserved `0x0800E800..0x08010000`).
- **Always `make flash-dry` / verify outputs before any device programming.**
- **Authorised write paths:** page-wise, read-back-verified tools only. `tools/flash/write_page.py` (pyocd, `AUTHORIZED=1`, one page per invocation) and, for full controlled deployments when explicitly authorised, `tools/flash_cycle.py` (page-wise, page 0 last, per-page + whole-image verify, identity check). Do not bypass safeguards. Do not proceed without explicit authorisation.
- Keep recovery/known-good images available; **never flash without a verified backup/readback plan**. Preserve recovery archives (e.g. B4.31 14139 image) and confirm candidate hashes/identity before and after programming.

### 2. JTAG/SWD pin protection
- Vendor disables JTAG (`AFIO->MAPR = 0x02000000u`) to free PB3/PB4/PA15 for display bus. **Never change MAPR** without verified hardware need. `board_free_jtag_pins()` must keep this value.

### 3. Power-on / reset safety
- Connect ST-Link first, then apply power; no hot-swap. After watchdog reset core may halt and need queued reset/release — verify splash/running state. If splash does not appear or behaviour is abnormal, **stop and seek guidance**.

### 4. No unbounded waits
- No unbounded waits in the superloop; every wait must have a deadline (IWDG recovers hangs). No unbounded auto-zero wait.

### 5. Status byte gate — absolute prohibition
- **`0x20000346` (ATSHA204A slot 4 byte 8) must never gate control flow.** Do not replicate vendor guard (`== 'G'`); use explicit firmware-owned state.

### 6. ATSHA204A dependency
- No runtime dependency on ATSHA204A for core functionality. Read calibration from local flash at boot; on absent/failing device start in safe defaults and warn, never freeze.

### 7. Page-aligned flash writes / reserved regions
- Reserved last 4 KiB `0x0800F000–0x0800FFFF`: `F000/F400` alternating settings (CRC+seq), `F800` calibration, `FC00` vendor device record (**never written**). Linker must reserve; writes page-aligned, journaled, read-back verified. Do not mass-erase reserved data.

### 8. Option bytes
- **Never write option bytes** without a hardware recovery path.

### 9. Superloop / IWDG feed
- IWDG always armed and fed every superloop iteration; a stopped feed must lead to reset (intentional recovery). Do not alter feed/timeout without understanding implications (`board_iwdg_init`).

### 10. Build & test before hardware
- Run host tests (`make test`), `make guard`, `make check`, and `make size` before hardware bring-up. Do not exceed flash/RAM budgets for the target.

## Escalation & recovery
If guard rails are violated or behaviour is abnormal (no splash, garbled display, frozen UI, verify/identity mismatch, unexpected reset/instability):
1. **Stop immediately.** No further flash/reset improvisation.
2. Power cycle and observe.
3. Restore a verified known-good/recovery image via **safe page-wise path only** (never `st-flash erase`/write). Device record `0x0800FC00` must not be written.
4. Report current flash/readback SHA-256, on-device identity/build_seq, and symptoms; preserve evidence.

## Repository layout (summary)
- `agent.md` — this file
- `Makefile`, `linker/`, `startup/` — build/link/startup (reserved flash regions preserved)
- `app/` — firmware (acquisition, trigger/ETS, board, UI/menu, link, storage)
- `drivers/` — CMSIS + STM32F1 HAL
- `tests/` — host unit tests
- `tools/` — flash/protocol/remote/campaign utilities
- `docs/` — hardware, features, audit, diagnostics, release notes

## GitHub / upload notes
- **Canonical repository:** https://github.com/benleveque1-design/Bens-Custom-DSO183-Firmware
- **`main` is published on GitHub** and contains firmware source, tests, docs, and tooling. `docs` branch remains available (historical docs-only branch). Keep the repository reflecting the actual project state.
- Before upload/push: ensure `build/`, `evidence/`, `logs/`, local configs/secrets are not committed (see `.gitignore`); review diffs; follow project-lead direction for releases/tags.

## Contact & authorization
- Project lead must grant explicit authorisation (e.g. `AUTHORIZED=1` / campaign approval) before any device programming.
- Known-good/recovery images and procedures are project references; preserve and verify hashes/identity.

**Remember: we have one instrument. Protect it as if it were the only one.**
