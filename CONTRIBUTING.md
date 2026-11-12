# Contributing to the DSO183 custom firmware

**DSO183 custom firmware — how to build, test, and submit pull requests**

This is an open-source community project, not a competition to produce the most
sophisticated firmware. The objective is to improve the original JYE Tech
DSO183, make its capabilities and internals accessible, and create a foundation
that other people can build upon. Bug reports, suggestions, fixes, and
contributions are welcome at every experience level.

---

## Ground rules (read first)

- **Do not modify firmware functionality without discussion.** Open an issue
  describing the change and why before submitting a non-trivial pull request.
- **Do not flash hardware on behalf of others.** The project never flashes
  your instrument for you. Flashing is user-controlled only, through the safe
  page-wise writer (`tools/flash/write_image.py`, `AUTHORIZED=1`).
- **Do not push to a public remote or create a release without explicit
  approval** from the maintainer.
- **Be technically honest.** Distinguish bench-verified behaviour from
  theoretical or host-tested-only behaviour. See `docs/FEATURES.md`.
- **Do not delete existing engineering documentation.**

---

## 1. Before you contribute

1. **Check for existing issues/PRs.** Someone may already be working on it.
2. **Open an issue first** for anything non-trivial (a new feature, a behavior
   change, a refactor). Discuss the approach before writing code.
3. **Fork the repo** and create a feature branch from `main`.

---

## 2. Build and test your change

From the repository root:

```bash
make            # firmware image
make test       # host unit tests (native compiler, no HAL needed)
make guard      # static safety checks (expect 0 issues)
make check      # build + tests + size + documentation/artefact cross-check
```

- `make check` is the gate: it verifies `FLASH_HANDOVER.md` against the
  built image, so any doc/artefact drift fails the build.
- Host tests live under `tests/` and build with the native compiler.
- Two clean builds from the same tree are byte-identical (deterministic).

If your change touches documentation, update the relevant doc under `docs/`
and confirm `make check` still passes.

---

## 3. Submitting a pull request

1. Push your feature branch to your fork.
2. Open a pull request against `main`.
3. Use the pull request template (`.github/PULL_REQUEST_TEMPLATE.md`) — it
   asks for: what changed, how you tested it, whether hardware was involved,
   and any docs affected.
4. A maintainer will review. Be prepared to re-run `make check` after review
   feedback.

### What we look for

- Code compiles cleanly (`-Wall -Wextra`, no new warnings).
- All host tests pass.
- `make guard` reports 0 issues.
- `make check` passes (including the docs ↔ image cross-check).
- Any behaviour claim is labelled as bench-verified, host-tested, or
  theoretical — never overstated.
- New modules are host-testable where possible (no HAL dependency in pure
  logic).

---

## 4. Code style

- C11, `-std=c11`, clang-format style not enforced but keep it consistent
  with surrounding code.
- Pure-logic modules contain no HAL dependency; hardware glue lives in
  `main()` and `board_*.c` / `stm32f1xx_it.c`.
- No unbounded waits; every busy wait has an explicit deadline.
- Watchdog is fed once per main-loop iteration, never from SysTick.

---

## 5. Documentation

Docs live under `docs/`:

| Doc | Covers |
|-----|--------|
| `ARCHITECTURE.md` | design and data paths |
| `HARDWARE.md` | pin map, display caveat, tested revision |
| `FEATURES.md` | verified / host-tested / experimental / known limitations |
| `ROADMAP.md` | near / mid / long term plans |
| `development.md` | toolchain, clone/build/test, guard, dry-run flash |
| `LICENSING.md` | third-party licenses and uncertainties |
| `AI_METHODOLOGY.md` | AI-assisted development transparency statement |

When you change user-visible behaviour, update the matching doc and confirm
`make check` passes.

---

## 6. Labels

Issues and PRs use these labels:

- `firmware` — core firmware code
- `acquisition/DSP` — ADC, trigger, timebase, measurements
- `hardware` — pin map, board bring-up, display
- `UI` — menus, display rendering, front panel
- `documentation` — docs, README, templates
- `tooling` — build, flash, test tools
- `good first issue` — suitable for new contributors

---

## 7. Community

- `CODE_OF_CONDUCT.md` — community standards; please read and follow it.
- `SECURITY.md` — security disclosure policy.
- Be respectful, patient, and welcoming to contributors at every experience
  level.

Thank you for helping make the DSO183 better.
