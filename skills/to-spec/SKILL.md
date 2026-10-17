---
name: to-spec
description: "User-invoked. Turn the current conversation into a feature spec: no interview, just synthesis of what you've already discussed. Writes .scratch/<feature>/SPEC.md and identifies which docs/spec/ module contracts it changes. Use after a grilling session settles a design."
---

This skill takes the current conversation context and codebase understanding and produces a feature spec. Do NOT interview the user; just synthesize what you already knows. (If the design isn't settled, run `grill-with-docs` first.)

## Process

1. Explore the repo to understand the current state of the codebase, if you haven't already. Use `docs/GLOSSARY.md` vocabulary throughout the spec, and respect the ADRs in `docs/adr/` in the area you're touching.

2. Sketch the seams at which you'll test the feature. Existing seams first, highest seam possible, fewest seams — the ideal is one. This repo's seams, in descending order:

   - **Simulation tests** (`test/simulation/`) — full application workflows through the GEA/MQTT/timer doubles.
   - **Unit tests** (`test/tests/`) — module-level, at the public C vtable interfaces (`i_mqtt_client_t`, `i_tiny_gea3_erd_client_t`, …).
   - **Pipeline pytest** (`scripts/test_*.py`) — for HA discovery pipeline changes.
   - **Device verification** — the physical test bench; the final seam for firmware-facing behavior (see the `device-verify` skill).

   Check with the user that these seams match their expectations.

3. Write the spec to `.scratch/<feature-slug>/SPEC.md` using the template below. If the session's `gh` account can write to the tracker, also post the spec as a comment on the originating issue; otherwise note that for the user. (The `ready-for-agent` label is applied after the tickets stage, per the `factory` skill's label discipline.)

4. List the **affected module specs** in `docs/spec/` (the spec is the single source of truth for module contracts; CONTRIBUTING requires them updated before committing a behavioral change). The implement stage updates them.

<spec-template>

# <Feature name> — Spec

**Issue:** #<N> · **Branch:** <branch> · **Date:** YYYY-MM-DD

## Problem Statement

The problem, from the user's perspective.

## Solution

The solution, from the user's perspective.

## User Stories

A long, numbered list. Format: `As a <actor>, I want a <feature>, so that <benefit>.` Cover all aspects of the feature, including failure modes.

## Acceptance Requirements

Numbered, testable, in SHALL form: "The system SHALL publish a write_result for every admitted write." Each requirement maps to at least one test or device-verification check; ticket acceptance criteria cite these numbers.

## Implementation Decisions

Modules built/modified, interfaces changed, architectural decisions, schema/API contracts, specific interactions. No file paths or code snippets — they go stale. Exception: if a prototype produced a snippet that encodes a decision more precisely than prose can (state machine, schema, type shape), inline it and note that it came from a prototype.

## Testing Decisions

- What makes a good test here (external behavior at a seam, not implementation details).
- Which seams from step 2 are used, and what each catches.
- Prior art: similar existing tests in `test/tests/`, `test/simulation/`, or `scripts/`.
- Device verification: required (firmware-facing) or exempt (and why).

## Affected Module Specs

- `docs/spec/<module>.md` — <what contract changes>

## Out of Scope

What this spec deliberately does not cover.

## Further Notes

Anything else a fresh agent session needs.

</spec-template>
