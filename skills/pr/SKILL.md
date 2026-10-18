---
name: pr
description: "Model-invoked. The shape a pull request body takes: a summary as the smallest visual that makes the change clear, before/after evidence, and a merge-danger call. Use when writing or updating a PR body."
---

Use this template for the PR body:

```markdown
## Summary

<diagram, diff-sketch, call tree, or file tree — the smallest view that makes the key point clear>

## Evidence

- **Before:** <failing test run / wrong MQTT output / log excerpt>
  **After:** <passing test run / correct output / device-verification evidence>

## Merge Danger

**Door:** <one-way or two-way>

**Blast Radius:** <one-word description>

## Device Verification

<evidence from the test bench: the five canary metrics (loop time, min heap, ERDs in cache, ERD publish rate, MQTT publish rate) vs the develop baseline, topics observed, values, log excerpt — per the `device-verify` skill. Or: "Exempt — <reason: change touches only scripts/, docs/, or CI.">>

Closes #<issue>
```

## Sections

Skip all preambles and keep prose brief. Use the domain language from `docs/GLOSSARY.md`.

### Summary

Pick the smallest view that makes the key point clear:

- Logic or algorithm → pseudocode.
- Runtime control flow → a call tree.
- Component interaction or data flow → Mermaid (sequence or flowchart).
- File responsibility or a broad refactor → a shallow file tree.
- What changes where the surrounding shape already exists → a `diff` sketch, matched to the topic.
- Most of it new, or the user needs a copyable target shape → the whole block.

Place each visual next to the short text it supports. Keep only the calls, files, states, and boundaries needed to make the point.

### Evidence

Concrete evidence that the change works, before and after. Execution-based evidence is A-tier: the exact test that now fails and passes, MQTT topic captures, log excerpts. For firmware-facing changes, the **Device Verification** section carries the test-bench evidence (see the `device-verify` skill); the exemption must state its reason.

### Merge Danger

**Door:** one-way or two-way. You can walk back through two-way doors, but not one-way doors. Wire-format changes, discovery data schema changes, and MQTT topic shape changes are one-way doors (retained topics and existing HA installations). Cheap-to-revert logic changes are two-way.

**Blast Radius:** the potential impact of the merge. Consider: retained MQTT topics on existing installations, generated-file consumers, the weekly auto-regenerate workflow, other open PRs touching the same modules.

## Repo conventions (apply when creating/updating the PR)

- PRs target `develop`. Create against the canonical repo: `gh pr create --repo joshualongenecker/home-assistant-bridge-esphome ...` from the fork branch.
- NEVER merge the PR yourself; only the user merges.
- NEVER add a `Co-Authored-By:` trailer.
- When a PR is updated with a new commit, update the PR title and description to reflect **all** changes in the PR, not just the latest commit.
