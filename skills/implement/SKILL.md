---
name: implement
description: "User-invoked. Implement a spec or set of tickets: work the ticket frontier, drive tdd at pre-agreed seams, run the repo's test gauntlet, close out with code-review before committing. Use when starting implementation of a specified feature."
---

Implement the work described by the spec or tickets.

If the user passes a ticket reference, read it (`.scratch/<feature>/issues/<NN>-<slug>.md` or a tracker issue) and state its title before starting. If the reference is ambiguous, ask.

Work the **frontier**: any ticket whose blockers are all done. One ticket at a time; mark each done (checkboxes in the ticket file) before starting the next.

Drive `tdd` (read and follow `skills/tdd/SKILL.md`) at the seams agreed in the spec.

## Verification gauntlet

Run as each applies, per ticket:

- `make test -j4` — unit + simulation (440 tests)
- `make integration-test -j4` — full suite incl. startup integration (537 tests)
- `make pytest` — pipeline tests, when `scripts/ha_discovery/` changed
- `python3 scripts/ha_discovery/run_pipeline.py` + `make check-generated` — when ERD definitions, overrides, or pipeline scripts changed; commit all generated files together
- `esphome compile docs/test-compile.yaml` — when `components/geappliances_bridge/` changed (the test harness uses mocked ESP-IDF stubs and misses real-build header issues)
- TLC model check over `formal/` — when a concurrency-core `docs/spec/` module changed (cache, publisher, write bridge, startup HSM); model and spec commit together (ADR-0003)

Run single test files regularly while iterating; the full gauntlet once at the end of each ticket.

Once all tickets are done, run `code-review` (read and follow `skills/code-review/SKILL.md`) against the branch's fixed point and resolve findings before committing.

Commit your work to the current branch (branch from `develop`, one branch per feature/issue).
