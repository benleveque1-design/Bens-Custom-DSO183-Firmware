# AI-assisted development statement

**DSO183 custom firmware — AI methodology**

This project uses AI-assisted coding and research tools during development. This
document states, factually, how those tools are used and what they are *not*
used for.

---

## What is human-directed

All of the following are human-directed:

- Architecture decisions (module boundaries, build identity, flash map,
  watchdog policy, trigger band design)
- Engineering direction (what to build, what to leave out of scope)
- Hardware experimentation (bench validation, pin mapping, display ID reading)
- Validation (what counts as "bench-verified" vs "host-tested" vs "theoretical")
- Project management (release timing, prioritization, community engagement)
- Final review of every change before it is committed

AI tools may draft text, suggest code, summarize documentation, or help
investigate reverse-engineering findings, but a human reviews, accepts,
rejects, or rewrites everything before it lands in the repository.

---

## What AI tools are used for

- Drafting documentation and issue/PR templates
- Summarizing reverse-engineering findings from the Ghidra workspace
- Suggesting code structure or test cases
- Investigating error messages and build failures

---

## What AI tools are NOT used for

- Making architecture or engineering decisions autonomously
- Flashing hardware or interacting with the instrument directly
- Deciding release timing or version numbers
- Determining what is "bench-verified" — that is determined by human observation
  on the bench, recorded in `docs/FEATURES.md`

---

## Honesty about verification

Where this repository makes claims about what works, those claims are based on
human-directed bench verification and host testing, not on AI assertion. See
`docs/FEATURES.md` for the breakdown of verified / host-tested / experimental /
known limitations. Nothing in this document should be read as an AI system
vouching for the correctness of the firmware.
