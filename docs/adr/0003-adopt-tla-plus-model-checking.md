# 0003. Adopt TLA+ model checking for the concurrency core

## Status

`accepted`

## Date

2026-10-17

## Context

The concurrency core — the mutex-protected ERD cache, the snapshot publisher task, the single-flight write HSM, and the startup HSM — has a bug history of interleaving and lifecycle defects (write clobbering #156, subscribe-bridge use-after-free heap corruption, poll-blocks-loop → publisher burst → MQTT queue drops) that deterministic tests structurally under-cover. The module specs in `docs/spec/` already state the invariants in prose; nothing checks them exhaustively. TLA+ with the TLC model checker explores interleavings exhaustively at the protocol level, and the repo's specs are deep enough to transcribe.

## Decision

1. **Scope: the concurrency core only.** One *composed* system model of the ERD cache, MQTT publisher, write bridge, and startup HSM, in `formal/`. Never the Python pipeline (low concurrency risk) and never memory-level C properties (TLA+ does not model bytes; that stays with tests and the compiler).
2. **Pilot first.** The write bridge HSM alone (smallest model, richest bug history — #156). Success criterion: it finds a property the tests don't cover, or it is cheap enough to trust. Only then expand to cache + publisher, then the startup HSM.
3. **CI gate.** TLC runs in CI; the gauntlet row is conditional — "when a concurrency-core `docs/spec/` module changes". Sync rule mirrors the generated-file rule: model and spec commit together, atomically.
4. **Sequencing.** The pilot is a factory feature (issue → spec → tickets → implement → review → PR), device-verify exempt (non-firmware-facing), run through the lifecycle after the factory branch merges — the first feature through the new process.

## Consequences

- A third artifact to keep in sync (model ↔ spec ↔ code). The chain is model → spec → code, each link checked by a different mechanism: TLC, the review's Spec axis, the test suite. A green TLC run proves the model, not the C.
- Abstraction choice is the hard part: a model that misses an aspect proves the wrong thing. The pilot includes a short "how to read a counterexample" note so findings are actionable.
- Real-time and performance properties (watchdog budgeting, loop time, heap) remain out of TLA+ scope; the canary metrics (ADR-0002) cover them.
- Rejected alternatives: no formal verification (tests + review only — accepted risk of unexplored interleavings); the full composed core model up front (≈3× the upfront work, no cheap abort point); modeling the discovery pipeline (low concurrency risk, low value).
