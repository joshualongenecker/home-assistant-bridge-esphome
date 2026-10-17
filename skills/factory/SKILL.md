---
name: factory
description: "User-invoked. The software-factory orchestrator: takes an issue number (or a raw idea) and drives it through triage → grill-with-docs → to-spec → to-tickets → implement → code-review → device-verify → pr, with a user confirmation gate at every stage boundary. Use when the user says 'run the factory on #N' or 'start feature #N'."
---

# Factory

Drive one feature or bug from submission to merge-ready PR. The lifecycle source of truth is `docs/factory.md`; this skill is the driver.

**Input:** an issue number on `joshualongenecker/home-assistant-bridge-esphome`, or a raw idea (draft the issue first, with the user, using the feature template shape).

## Stages

Run in order. **Gate:** after each stage, summarize what was produced and wait for the user's confirmation before starting the next. The user can skip a stage ("skip the grill, we already decided") — record the skip in the stage summary.

1. **Triage** (`skills/triage/SKILL.md`) — if the issue is unlabeled or `needs-triage`. Ends at `accepted` (or `needs-info` / terminal — stop the factory and say why).
2. **Grill** (`skills/grill-with-docs/SKILL.md`) — the design tree. Produces ADRs in `docs/adr/` and `docs/GLOSSARY.md` updates as decisions land. Skip only if the user confirms the design is already settled.
3. **Spec** (`skills/to-spec/SKILL.md`) — `.scratch/<feature>/SPEC.md` + affected `docs/spec/` module contracts identified.
4. **Tickets** (`skills/to-tickets/SKILL.md`) — `.scratch/<feature>/issues/NN-*.md` with blocking edges.
5. **Implement** (`skills/implement/SKILL.md`) — work the ticket frontier, `skills/tdd/SKILL.md` at the agreed seams, full verification gauntlet per ticket.
6. **Review** (`skills/code-review/SKILL.md`) — five parallel sub-agent axes against the branch fixed point. Resolve findings (or the user explicitly accepts them) before proceeding.
7. **Device verify** — **required iff the diff touches `components/` or `lib/`** (firmware-behavior changes). Run the `device-verify` skill on the test bench; post the evidence to the issue. Otherwise record the exemption with its reason in the PR body.
8. **PR** (`skills/pr/SKILL.md`) — open the PR against `develop` on the canonical repo with the full body (summary, evidence, merge danger, device verification, `Closes #N`).

**Bug path:** for bugs, run the shortened path from `docs/factory.md` — triage (claim verification is the red loop) → `skills/diagnosing-bugs/SKILL.md` (only if no tight loop exists) → implement (regression test first, no ticket files for small fixes) → review → device verify → PR. Stages 2–4 above become mandatory only when the fix is itself a design decision — the user decides at the triage gate.

## Label discipline

Update the tracker labels as the feature moves (when the session's `gh` account has permission; otherwise present the changes for the user): `accepted` → `ready-for-agent` (after stage 4) → `in-progress` (stage 5) → `needs-device-verify` (stage 7) → labels cleared when the PR merges.

## Definition of done

A feature is done when ALL hold:

- [ ] Every acceptance criterion in the spec and tickets is met.
- [ ] Verification gauntlet green: `make test -j4`, `make integration-test -j4`, `make pytest` (if pipeline touched), `make check-generated` (if generated files touched), `esphome compile docs/test-compile.yaml` (if component touched), TLC over `formal/` (if a concurrency-core spec changed).
- [ ] Code review clean on all five axes (or findings explicitly accepted by the user).
- [ ] Device verification evidence posted (firmware-facing) or exemption recorded (and why).
- [ ] Affected `docs/spec/` module specs updated.
- [ ] ADRs written for hard decisions; `docs/GLOSSARY.md` current.
- [ ] PR open against `develop` with the full `pr`-skill body.

The factory stops at the open PR. Merging is the user's action, never the agent's.
