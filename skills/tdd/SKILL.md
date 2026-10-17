---
name: tdd
description: "Model-invoked. Test-driven development with a red-green-refactor loop against the CppUTest suite and simulation doubles. Use when building features or fixing bugs test-first."
---

# Test-Driven Development

TDD is the red → green loop. This skill is the reference that makes that loop produce tests worth keeping: what a good test is, where tests go, the anti-patterns, and the rules of the loop. Every section applies on every cycle.

When exploring the codebase, read `docs/GLOSSARY.md` so test names and interface vocabulary match the project's domain language, and respect ADRs in the area you're touching.

## What a good test is

Tests verify behavior through public interfaces, not implementation details. Code can change entirely; tests shouldn't. A good test reads like a specification: "polling bridge publishes ERD value to the value topic on read completion" tells you exactly what capability exists, and it survives refactors.

## Seams: where tests go

A **seam** is the public boundary you test at. This repo's seams:

- **Module vtables** — the public C interfaces (`i_mqtt_client_t`, `i_tiny_gea3_erd_client_t`, `i_tiny_uart_t`, cache/bridge/publisher APIs) in `test/tests/`.
- **Simulation** — `test/simulation/` drives complete workflows (startup, mode fallback, write forwarding) through the `tiny_gea3_erd_client_double`, `mqtt_client_double`, and `tiny_timer_group_double`.
- **Pipeline** — `scripts/test_*.py` for the HA discovery pipeline.

**Test only at pre-agreed seams.** Before writing any test, write down the seams under test and confirm them with the user (the spec's Testing Decisions section is the standing agreement). Give each proposed seam a one-line note on what it catches and what it misses.

## CppUTest specifics

- **The `mock().clear()` trap:** CppUTest's teardown `mock().clear()` silently discards unmet expectations — a test whose `expectOneCall` is never matched still passes. For paths that bypass the mock (e.g. the real adapter publishing through the ESPHome MQTT client double), assert on the double's recorded state (`last_published_topic_`, `last_published_payload_`) instead of mock expectations. When a double routes calls via `mock().actualCall(...)` with no stored state, call `mock().checkExpectations()` at the end of the test body. Verify a test can fail: break the expected value and confirm the test goes red.
- **Single-flight write bridge:** `erd_write_bridge` drops new write requests while a write is in flight (state_writing). A write-path regression test does ONE write per test — a second write before the first completes is dropped by design and reads as a false failure.
- **Run:** `make test -j4` (unit + simulation), `make integration-test -j4` (full suite; integration tests are excluded from CI due to a known CppUTest TestMemoryAllocator false-positive on GCC/Ubuntu — always verify locally).

## Anti-patterns

- **Implementation-coupled**: mocks internal collaborators, tests private methods, or verifies through a side channel. The tell: the test breaks when you refactor but behavior hasn't changed.
- **Tautological**: the assertion recomputes the expected value the way the code does, so it passes by construction. Expected values come from an independent source of truth: a known-good literal, a worked example, the spec.
- **Wiring tests**: never test copies, forwarding, mock echoes, source text, or incidental defaults. Use a throwaway script to check those; they don't belong in the suite.
- **Horizontal slicing**: writing all tests first, then all implementation. Bulk tests verify _imagined_ behavior. Work in **vertical slices**: one test → one implementation → repeat, each test a **tracer bullet** that responds to what the last cycle taught you.

## Rules of the loop

- **Red before green.** Write the failing test first, then only enough code to pass it. Don't anticipate future tests or add speculative features.
- **One slice at a time.** One seam, one test, one minimal implementation per cycle.
- **Refactoring is not part of the loop.** It belongs to the review stage (`code-review`), not the red → green cycle.
