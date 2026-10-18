# 0001. Adopt the software-factory workflow

## Status

`accepted`

## Date

2026-10-17

## Context

Features arrived as informal chat requests and ad-hoc branches; design decisions lived in conversation history; there was no shared vocabulary document, no ADR trail, no structured review, and no repeatable loop for proving firmware changes on a physical device. The repo already had strong assets — `docs/spec/` module contracts, a 537-test CppUTest suite with simulation doubles, CI, and a weekly auto-regenerate workflow — but no process wiring them into a feature lifecycle.

## Decision

Adopt a staged, skill-driven factory (see `docs/factory.md`):

1. **Intake** via GitHub issue templates (feature/bug) on the canonical repo.
2. **Triage** through a six-label state machine (`needs-triage`, `needs-info`, `accepted`, `ready-for-agent`, `in-progress`, `needs-device-verify`).
3. **Design** via grilling sessions (`grill-with-docs`) that write ADRs to `docs/adr/` and maintain `docs/GLOSSARY.md` as the shared language.
4. **Spec/tickets** as local working documents under `.scratch/<feature>/` (the session's fork account has pull-only access to the canonical tracker, so tickets are branch-local files rather than tracker issues).
5. **Implementation** test-first (`tdd`) at pre-agreed seams, with the repo's verification gauntlet.
6. **Review** by four parallel sub-agents: Standards, Security, Spec conformance, and Invariants (the Critical Invariants from `.github/copilot-instructions.md`).
7. **Device verification** on the physical test bench for firmware-facing changes (diff touches `components/` or `lib/`), with the loop codified in a workstation-local managed skill because the bench doesn't travel with the repo.
8. **PR** with a fixed body shape (summary, evidence, merge danger, device verification).

Stage skills live in `skills/` (repo-local, versioned, agent-agnostic), adapted from mattpocock/skills and rebound to repo conventions.

## Consequences

- Every feature leaves behind a durable trail: issue → ADRs → spec → tickets → tests → review findings → device evidence → PR. A fresh agent session can resume any feature from the branch alone.
- The user is the decision-maker at every stage boundary; the agent never merges.
- Cost: more process per feature. Mitigation: stages are skippable by explicit user decision, and small fixes can bypass the full lifecycle (a bug fix with a regression test needs no grill session).
- Invariant: when `docs/factory.md` and the skills disagree, the skill is wrong — fix the skill.
- Follow-up: the test bench must be configured in the `device-verify` skill before the first firmware-facing feature reaches stage 7.
