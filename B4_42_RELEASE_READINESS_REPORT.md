# B4.42 — Release Hygiene and Documentation Preparation Report (additive)

**Date (UTC):** 2026-11-15  
**Scope:** Repository preparation for a public release of B4.40 candidate (build 58810), strictly without changing production firmware code or touching hardware.  
**Constraints respected:** no production code changes, no firmware rebuilds or deployment, no hardware interaction, no commit/tag/push. All raw evidence and historical reports preserved.

---

## 1. Tasks completed

### 1.1 Corrected missing `candidate_test.log` reference (actual preserved log; no invented aggregates)
- **`B4_40_DEPLOYMENT_AND_VALIDATION_REPORT.md` §1.2:** replaced `make test: **36 suites, 1318 checks, 0 failures**` and reference to missing `candidate_test.log` with wording that cites the actual preserved artifact `evidence/B4_40_DEPLOY_AND_VALIDATION_20261115T202159Z/test.log`, states that suite pass lines are recorded with no recorded failures in the preserved output, and explicitly notes that no aggregate summary line (suite/check totals) is present in that log (no invented/unsupported aggregate claim).
- **`FLASH_HANDOVER.md`** and **`README.md`** B4.40 notes use the same factual characterization of preserved host-test evidence (no invented aggregate).

### 1.2 Resolved 0-byte duplicate report under `evidence/`
- **Deleted:** `evidence/B4_40_DEPLOYMENT_AND_VALIDATION_REPORT.md` (confirmed 0-byte duplicate; canonical B4.40 report remains `B4_40_DEPLOYMENT_AND_VALIDATION_REPORT.md` at repo root).

### 1.3 Clarified aborted campaign preservation and location
- **`B4_40_DEPLOYMENT_AND_VALIDATION_REPORT.md` §8:** clarified that the aborted first campaign run (`/tmp/b440_hw_validation_20261115T202735Z/` on Pi, serial timeout during characterization, not primary) is preserved in evidence as `evidence/B4_40_DEPLOY_AND_VALIDATION_20261115T202159Z/pi_campaign_run_attempt1_aborted/` (device remained responsive).

### 1.4 Empty timestamp-placeholder evidence directory
- **Removed:** `evidence/B4_40_DEPLOY_AND_VALIDATION_20261115T000000Z/` (confirmed empty directory; no meaningful historical evidence). All meaningful historical evidence preserved.

### 1.5 B4.40 evidence index created
- **Added:** `evidence/B4_40_DEPLOY_AND_VALIDATION_20261115T202159Z/EVIDENCE_INDEX.md` containing: build sequence **58810**; exact binary/ELF SHA-256 hashes; flash/RAM figures; deployment verification evidence; host test evidence; hardware campaign evidence (primary + aborted attempt location); B4.36 comparison reference; ETS validation scope and TB0/TB2 zero-frame caveat (zero frames in short 6-attempt window but non-zero ETS diagnostic/counter activity); display visual-inspection limitation; recovery image provenance; related report links.

### 1.6 Root README and release documentation reviewed for accuracy (minimal, factual)
- **`README.md`:** updated status text to reflect newer validated work including B4.40 candidate (not tagged/released); added "B4.40 candidate (validated on tested instrument, not tagged/released)" subsection with identity/resources, deployment/trigger validation summary, host-test evidence characterization, explicit ETS/display limitations, and report/index links. Unrelated sections not rewritten.
- **`FLASH_HANDOVER.md`:** updated to reflect B4.40 (build 58810) as latest deployed/validated candidate with hashes/sizes/validation summary, limitations, and evidence/report references; preserved B4.36/14139 recovery context and safe-flashing rules; generalized post-flash BUILD_SEQ check. (Documentation correction for accuracy relative to B4.40 evidence.)
- **`docs/RELEASE_READINESS_v0.1.0.md` and `RELEASE_NOTES_v0.1.0.md`:** reviewed; remain clearly marked historical/superseded for v0.1.0 with no unsupported claims requiring change.

### 1.7 Evidence/hash/file reference verification
- Verified existence of key referenced artifacts (candidate hash/size/seq/build/test/deploy/campaign/postcheck logs & summaries, recovery bin+sha, B4.36 reference summaries, reports/docs). Recovery SHA-256 recomputed and matches recorded value: `e2bd129fd044f44246e9c5a85547d92f2559f6b25ee72ced4d7fe0eff09949f8`. Candidate bin/elf hashes as recorded in `candidate_sha256.txt`: `00a1e11da6a97e727125ebda40c62fbc9c638ce04ca0b652ebdb616c23afabba` / `d5b21402b0fd99b36c6e47e3d146b8f50472e08f2b6ee53c3eb31694d2aeea7b`. Flash_cycle deployment evidence shows whole-image read-back OK (40904 bytes identical) and identity seq `0x0000E5BA` (58810).
- **Note:** `evidence/` is gitignored in this repository; evidence files are preserved locally but not tracked by git. `B4_42_RELEASE_READINESS_REPORT.md` did not exist at start of this task and is added as part of B4.42 (see §3).

### 1.8 Audit trail update
- **`B4_41_POST_DEPLOYMENT_AUDIT_REPORT.md`:** appended "B4.42 correction status" addendum documenting resolved hygiene issues and still-applicable validation-scope limitations/release-state constraints (additive; original audit findings preserved).

---

## 2. Release readiness assessment

### Blockers (must be addressed before a clean public release/tag)
1. **Uncommitted/unpushed/unreleased repository state:** audited B4.37/B4.38 production/source changes (`app/ets_epoch.c`, `app/ets_epoch.h`, `app/main.c`, `tools/size_report.sh`) and release/documentation updates remain uncommitted; no release commit/tag/push. A public release should be cut from a clearly identified, reviewed, committed/tagged revision with traceable artifacts.
2. **Evidence packaging/traceability for public consumers:** `evidence/` is gitignored (raw campaign/build/deploy artifacts preserved locally but not in git). A public release that cites B4.40 hardware validation must define how consumers access the referenced evidence (e.g. release bundle/archive/checksums, or explicitly scoped claims) without implying git-tracked availability.
3. **Validation-scope limitations must remain explicit in release materials:** ETS is **sampled only** (TB0–TB4+), **not** full hardware validation/equivalence; TB0/TB2 showed zero frames in a short 6-attempt sampling window but non-zero ETS diagnostic/counter activity. On-device display rendering/smoothing/differential redraw not visually verified remotely. These are evidenced limitations (not contradicted by collected data) and cannot be claimed as fully validated.

### Optional improvements (non-blocking, recommended)
1. Include an explicit host-test aggregate summary artifact (suites/check totals) or full `make test` output containing an explicit summary line, if such output is available from the validated build context (do not invent counts).
2. Perform on-device visual inspection of display/smoothing/differential redraw and/or a fuller ETS TB0–TB4 hardware campaign if stronger release claims are required.
3. Add a top-level note mapping release tag/build hashes to evidence bundle locations/checksums once packaged.
4. Consider whether `FLASH_HANDOVER.md` should distinguish "instrument state at validation time" from "recommended candidate" in future revisions; current text reflects B4.40 as latest deployed/validated candidate per preserved evidence.

---

## 3. Modified / deleted / added files (this B4.42 session)

### Modified
- `B4_40_DEPLOYMENT_AND_VALIDATION_REPORT.md` — corrected host-test evidence reference/wording (no invented aggregate; references preserved `test.log`); clarified aborted campaign preservation/location (`pi_campaign_run_attempt1_aborted/`).
- `B4_41_POST_DEPLOYMENT_AUDIT_REPORT.md` — appended B4.42 correction status addendum (additive).
- `README.md` — minimal accuracy updates: current-status wording and B4.40 candidate (validated, not tagged/released) subsection with evidence/report links and explicit limitations.
- `FLASH_HANDOVER.md` — updated to latest deployed/validated B4.40 candidate (58810) with hashes/size/validation summary, preserved context, and post-flash identity guidance; added B4.40/B4.36 notes.

### Deleted (confirmed empty/duplicate documentation artifacts only; raw/meaningful historical evidence preserved)
- `evidence/B4_40_DEPLOYMENT_AND_VALIDATION_REPORT.md` — 0-byte duplicate report.
- `evidence/B4_40_DEPLOY_AND_VALIDATION_20261115T000000Z/` — empty timestamp-placeholder directory.

### Added
- `evidence/B4_40_DEPLOY_AND_VALIDATION_20261115T202159Z/EVIDENCE_INDEX.md` — concise B4.40 evidence index.
- `B4_42_RELEASE_READINESS_REPORT.md` — this report.

### Not modified (intentional, per constraints)
- Production/source firmware and tooling code unchanged by B4.42: `app/ets_epoch.c`, `app/ets_epoch.h`, `app/main.c`, `tools/size_report.sh` remain in their pre-existing working-tree modified state (B4.37/B4.38 candidate) and were not altered.
- No firmware rebuilds, no deployment/hardware interaction, no commits/tags/pushes.

---

## 4. Remaining Git status (at completion)

```
 M app/ets_epoch.c
 M app/ets_epoch.h
 M app/main.c
 M tools/size_report.sh
 M B4_40_DEPLOYMENT_AND_VALIDATION_REPORT.md
 M B4_41_POST_DEPLOYMENT_AUDIT_REPORT.md
 M FLASH_HANDOVER.md
 M README.md
?? B4_37_RAM_AUDIT_AND_OPTIMISATION_REPORT.md
?? B4_42_RELEASE_READINESS_REPORT.md
```

Notes: deleted `evidence/B4_40_DEPLOYMENT_AND_VALIDATION_REPORT.md` and `evidence/B4_40_DEPLOY_AND_VALIDATION_20261115T000000Z/` were under gitignored `evidence/` and do not appear in git status. Added `evidence/B4_40_DEPLOY_AND_VALIDATION_20261115T202159Z/EVIDENCE_INDEX.md` is also under gitignored `evidence/` and does not appear in git status. Untracked `B4_37_RAM_AUDIT_AND_OPTIMISATION_REPORT.md` was pre-existing/unrelated to B4.42 edits except prior additive content; preserved as-is.

*End of B4.42 release hygiene and documentation preparation report (additive).*
