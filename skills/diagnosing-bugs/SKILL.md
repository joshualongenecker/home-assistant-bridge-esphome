---
name: diagnosing-bugs
description: "Model-invoked. Diagnosis loop for hard bugs and performance regressions: build a red feedback loop, minimise, hypothesise, instrument, fix, regression-test. Use when the user says 'diagnose'/'debug this' or reports something broken, throwing, failing, or slow."
---

# Diagnosing Bugs

A discipline for hard bugs. Skip phases only when explicitly justified.

When exploring the codebase, read `docs/GLOSSARY.md` for a clear mental model of the relevant modules, and check ADRs in the area you're touching.

## Redact

This skill has you show commands, outputs, and captured artifacts. **Redact every secret first**: write `<REDACTED>` in its place. Captured artifacts carry auth headers: quote only the lines that carry the signal. If the redacted output isn't enough to diagnose, say so and ask.

## Phase 1: Build a feedback loop

**This is the skill.** Everything else is mechanical. If you have a **tight** pass/fail signal for the bug (one that goes red on _this_ bug), you will find the cause; bisection, hypothesis-testing, and instrumentation all just consume it. If you don't have one, no amount of staring at code will save you.

Spend disproportionate effort here. **Be aggressive. Be creative. Refuse to give up.**

### Ways to construct one, in roughly this order

1. **Failing CppUTest test** at whatever seam reaches the bug: unit (`test/tests/`) or simulation (`test/simulation/`).
2. **Pipeline pytest** for HA discovery pipeline bugs.
3. **Replay a captured trace.** Save a real GEA packet stream or MQTT payload set to disk; replay it through the code path in isolation (the simulation doubles exist for exactly this).
4. **Throwaway harness.** A minimal `TEST()` that drives the bug code path with one call.
5. **Property / fuzz loop.** If the bug is "sometimes wrong output", run 1000 random inputs (ERD payloads, write sequences) and look for the failure mode.
6. **Bisection harness.** If the bug appeared between two commits, automate "build at state X, run the check" so you can `git bisect run` it.
7. **Differential loop.** Same input through old vs new firmware (or two configs), diff the MQTT output.
8. **Device loop.** Last resort for hardware-only bugs: OTA the build to the test bench, watch the MQTT topics and logs (see the `device-verify` skill). Slow, but real.
9. **HITL script.** If a human must act, drive them with a structured script; captured output feeds back to you.

Build the right feedback loop, and the bug is 90% fixed.

### Tighten the loop

Treat the loop as a product. Once you have _a_ loop, **tighten** it: faster (skip unrelated init, narrow the test scope), sharper (assert on the specific symptom, not "didn't crash"), more deterministic (pin time via the timer double, seed RNG, isolate fixtures).

A 30-second flaky loop is barely better than no loop; a 2-second deterministic one is a debugging superpower.

### Non-deterministic bugs

The goal is not a clean repro but a **higher reproduction rate**. Loop the trigger 100×, add stress, narrow timing windows, inject sleeps via the timer double. A 50%-flake bug is debuggable; 1% is not.

### When you genuinely cannot build a loop

Stop and say so explicitly. List what you tried. Ask the user for: (a) access to whatever environment reproduces it, (b) a redacted captured artifact (log dump, MQTT capture, core dump), or (c) permission to add temporary instrumentation. Do **not** proceed to hypothesise without a loop.

### Completion criterion: a tight loop that goes red

Phase 1 is done when you can name **one command** (a test invocation, a script path) that you have **already run at least once** (show the invocation and its output, redacted), and that is:

- [ ] **Red-capable**: drives the actual bug code path and asserts the **user's exact symptom**.
- [ ] **Deterministic**: same verdict every run (flaky bugs: a pinned, high reproduction rate).
- [ ] **Fast**: seconds, not minutes.
- [ ] **Agent-runnable**: you can run it unattended.

If you catch yourself reading code to build a theory before this command exists, **stop: jumping straight to a hypothesis is the exact failure this skill prevents.** No red-capable command, no Phase 2.

## Phase 2: Reproduce + minimise

Run the loop. Watch it go red.

Confirm:

- [ ] The loop produces the failure mode the **user** described, not a different failure that happens to be nearby. Wrong bug = wrong fix.
- [ ] The failure is reproducible across multiple runs (or at a high enough rate for flaky bugs).
- [ ] You have captured the exact symptom (error message, wrong value, missing topic, slow timing) so later phases can verify the fix addresses it.

### Minimise

Shrink the repro to the **smallest scenario that still goes red**. Cut inputs, callers, config, and steps **one at a time**, re-running after each cut, keeping only what's load-bearing. A minimal repro shrinks the hypothesis space and becomes the clean regression test in Phase 5.

Done when **every remaining element is load-bearing**: removing any one makes the loop go green. Do not proceed until you have reproduced **and** minimised.

## Phase 3: Hypothesise

Generate **3–5 ranked hypotheses** before testing any of them. Single-hypothesis generation anchors on the first plausible idea.

Each hypothesis must be **falsifiable**: "If <X> is the cause, then <changing Y> will make the bug disappear / <changing Z> will make it worse." If you cannot state the prediction, the hypothesis is a vibe: discard or sharpen it.

**Show the ranked list to the user before testing.** They often have domain knowledge that re-ranks instantly. Don't block on it; proceed with your ranking if the user is AFK.

## Phase 4: Instrument

Each probe must map to a specific prediction from Phase 3. **Change one variable at a time.**

Tool preference:

1. **Debugger / REPL inspection** if the env supports it (GDB on ESP-IDF for on-device bugs).
2. **Targeted logs** at the boundaries that distinguish hypotheses.
3. Never "log everything and grep".

**Tag every debug log** with a unique prefix, e.g. `[DEBUG-a4f2]`. Cleanup becomes a single grep. Untagged logs survive; tagged logs die.

**Perf branch.** For performance regressions (MQTT publish rate, poll latency, watchdog pressure), logs are usually wrong. Establish a baseline measurement first (timing harness, benchmark on the test bench), then bisect. Measure first, fix second.

## Phase 5: Fix + regression test

Write the regression test **before the fix**, but only if there is a **correct seam** for it — one where the test exercises the **real bug pattern** as it occurs at the call site. If the only available seam is too shallow (single-caller test when the bug needs multiple callers, unit test that can't replicate the chain), a regression test there gives false confidence.

**If no correct seam exists, that itself is the finding.** Note it: the architecture is preventing the bug from being locked down. Flag it for the review stage.

If a correct seam exists:

1. Turn the minimised repro into a failing test at that seam.
2. Watch it fail. If you forced the red by mutating code or a fixture, `diff` against a pristine copy to prove the mutation landed.
3. Apply the fix.
4. Watch it pass.
5. Re-run the Phase 1 feedback loop against the original (un-minimised) scenario.

## Phase 6: Cleanup

Required before declaring done:

- [ ] Original repro no longer reproduces (re-run the Phase 1 loop)
- [ ] Regression test passes (or absence of seam is documented)
- [ ] All `[DEBUG-...]` instrumentation removed (`grep` the prefix)
- [ ] Throwaway prototypes deleted
- [ ] The hypothesis that turned out correct is stated in the commit / PR message, so the next debugger learns
