---
name: Pull Request Template
about: Template for DSO183 custom firmware pull requests
title: "[PR] "
labels: []
assignees: ""
---

## What changed

A clear and concise description of what you changed and why.

## How you tested it

- `make` — build passes?
- `make test` — host tests pass?
- `make guard` — static safety checks (0 issues)?
- `make check` — full cross-check passes?
- Hardware tested? (bench verification, if applicable) [yes/no]
  - If yes, which instrument / MCU rev / display ID?

## Docs affected

List any docs under `docs/` that you added or updated. If none, say "none."

## Behavior claims

For each user-visible behavior change, state whether it is:
- bench-verified (observed on real hardware), or
- host-tested only (logic proven, hardware path not exercised), or
- theoretical / not yet verified

See `docs/FEATURES.md` for the honesty categories.

## Checklist

- [ ] `make check` passes
- [ ] No new compiler warnings
- [ ] Docs updated (if user-visible behaviour changed)
- [ ] No firmware functionality changed without prior issue discussion
- [ ] Not flashing hardware on behalf of others

## Related issue

Link the issue this PR addresses, if any.
