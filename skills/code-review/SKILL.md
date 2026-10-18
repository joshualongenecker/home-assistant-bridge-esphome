---
name: code-review
description: "Model-invoked. Review the diff since a fixed point along five axes — Standards, Security, Spec, Invariants, Performance — run as parallel sub-agents, aggregated side by side. Use when the user asks to review a branch, PR, or work-in-progress changes."
---

Five-axis review of the diff between `HEAD` and a fixed point the user supplies:

- **Standards**: does the code conform to this repo's documented coding standards?
- **Security**: evidence-backed security findings.
- **Spec**: does the code faithfully implement the originating issue / spec?
- **Invariants**: does the code respect the repo's load-bearing invariants?
- **Performance**: does the code risk loop time, heap, or publish-rate regressions, and (firmware-facing) does the PR carry the canary metrics against the develop baseline?

Run the five axes as **parallel sub-agents** (one batch, isolated contexts) so they don't pollute each other. If your agent has no sub-agents, run them as **five sequential passes**, each starting from a fresh read of the diff. Either way, this skill aggregates the findings.

## Process

### 1. Pin the fixed point

Whatever the user said (a commit SHA, branch name, tag, `develop`, `HEAD~5`). If they didn't specify one, ask. Capture the diff command once: `git diff <fixed-point>...HEAD` (three-dot, against the merge-base) and the commit list via `git log <fixed-point>..HEAD --oneline`. Confirm the fixed point resolves and the diff is non-empty before spawning anything.

### 2. Identify the spec source

In this order:

1. Issue references in the commit messages (`#123`, `Closes #45`), fetched via `gh issue view`.
2. A path the user passed.
3. `.scratch/<feature>/SPEC.md` matching the branch or feature.
4. If nothing is found, ask. If there isn't one, the Spec axis reports "no spec available" and is skipped.

### 3. Identify the standards sources

`CONTRIBUTING.md` (Code Style section), `docs/style-guide.md`, and
`.github/copilot-instructions.md` (Global Protocol Rules section). On top of
whatever the repo documents, the Standards axis always carries the **smell
baseline** below (Fowler, _Refactoring_ ch.3). Two rules bind it: **the repo
overrides** (a documented standard wins; suppress the smell where it endorses
what the baseline flags) and **always a judgement call** (labelled heuristic,
never a hard violation; skip anything tooling enforces).

- **Mysterious Name**: a name that doesn't reveal what it does or holds. → rename; if no honest name comes, the design's murky.
- **Duplicated Code**: the same logic shape in more than one hunk or file. → extract the shared shape.
- **Feature Envy**: a method that reaches into another object's data more than its own. → move it onto the data it envies.
- **Data Clumps**: the same few fields/params travelling together. → bundle into one type.
- **Primitive Obsession**: a primitive standing in for a domain concept. → give the concept its own small type.
- **Repeated Switches**: the same switch/if-cascade on the same type recurs. → polymorphism or one shared map.
- **Shotgun Surgery**: one logical change forces scattered edits. → gather what changes together.
- **Divergent Change**: one module edited for several unrelated reasons. → split it.
- **Speculative Generality**: abstraction added for needs the spec doesn't have. → delete it.
- **Message Chains**: long `a.b().c().d()` navigation. → hide the walk behind one method.
- **Middle Man**: a function that mostly delegates onward. → cut it.
- **Refused Bequest**: an implementer that ignores most of what it inherits. → composition.

### 4. Run all five axes

One sub-agent per axis (parallel batch when available; sequential passes otherwise). Each gets: the diff command and commit list, plus the axis-specific material below.

**Standards** — a code-standards reviewer. Include: the standards-source files; the smell baseline pasted in full. Brief: "Report, per file/hunk where relevant, (a) every place the diff violates a documented standard — cite the standard (file + rule); and (b) any baseline smell — name it and quote the hunk. Distinguish hard violations from judgement calls; a documented repo standard overrides the baseline. Skip anything tooling enforces. Under 400 words."

**Security** — a security reviewer. Brief: "Evidence-backed security findings only: buffer handling (the ERD data path memcpy's into a fixed arena — check bounds), integer scaling/overflow in value conversion, injection via MQTT payload parsing, secrets in logs or discovery payloads. Quote the hunk for each finding. Under 300 words."

**Spec** — a general-purpose reviewer. Include: the spec contents. Brief: "Report: (a) requirements the spec asked for that are missing or partial; (b) behaviour in the diff that wasn't asked for (scope creep); (c) requirements that look implemented but where the implementation looks wrong. Quote the spec line for each finding. Under 400 words."

**Invariants** — a general-purpose reviewer. Include: the **Critical Invariants** and **Global Protocol Rules** sections of `.github/copilot-instructions.md` pasted in full; the list of ADRs in `docs/adr/` (accepted) relevant to the touched area. Brief: "Check the diff against each invariant and protocol rule: (erd, board_address) keying everywhere a cache key appears; 0xFF never stored as a physical board address; no mutex held across an MQTT publish; no data_size above 248; generated files (erd_lists, appliance_api_feature_lists, appliance_type_map.h, ha_discovery_data, ha_discovery/*.jsonl, processed JSON) not hand-edited; no file-scope .cpp variables referenced from headers. Report each violation with the invariant cited and the hunk quoted. Under 300 words."

**Performance** — a general-purpose reviewer. Include: the canary-metrics
definition from `docs/GLOSSARY.md` (loop time, min heap, ERDs in cache, ERD
publish rate, MQTT publish rate) and the develop-baseline rule from
`docs/factory.md`. Brief: "A diff is firmware-facing iff it touches
`components/` or `lib/`. Report: (a) new heap allocations on the loop or
publish path (the repo rule is no avoidable allocation); (b) work added to the
main loop or publisher task that can lengthen loop time or pressure the
watchdog; (c) changes to poll rates, publish rates, or rate limits that move a
canary metric; (d) for firmware-facing diffs, whether device verification is
marked required in the spec/tickets and the canary benchmark against the
develop baseline is planned — the benchmark table itself is produced at the
device-verify stage, which runs after review, so report its absence as
expected-pending, not a finding; the table is enforced by the definition of
done and the PR body. Quote the hunk for each finding. Under 300 words."

### 5. Aggregate

Present the five reports under `## Standards`, `## Security`, `## Spec`, `## Invariants`, `## Performance` headings, verbatim or lightly cleaned. Do **not** merge or rerank findings — the axes are deliberately separate. End with a one-line summary: total findings per axis and the worst issue within each axis (if any).

## Why five axes

A change can pass four and fail the fifth:

- Code that follows every standard, implements the spec, is secure, and respects every invariant — but allocates on the publish path and the canary table is missing from the PR → **Performance fail.**
- Code that respects every invariant but implements the wrong thing → **Spec fail.**
- Code that follows every standard, implements the spec, and is secure, but holds the cache mutex across a publish → **Invariants fail.**

Reporting them separately stops one axis from masking the others. Nothing commits until the findings are resolved or explicitly accepted by the user.
