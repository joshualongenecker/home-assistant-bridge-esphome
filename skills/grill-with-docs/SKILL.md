---
name: grill-with-docs
description: "User-invoked. A relentless interview (grilling) about a plan or design that also builds the project's domain model: updates docs/GLOSSARY.md and writes ADRs in docs/adr/ as decisions land. Use when starting a feature or working through a hard design decision."
---

Run a `grilling` session for the whole session (read and follow `skills/grilling/SKILL.md`), with `domain-modeling` discipline active (read and follow `skills/domain-modeling/SKILL.md`).

The difference from plain grilling: as decisions land, write them down immediately, in the same turn.

- **Term resolved or sharpened** → update `docs/GLOSSARY.md`.
- **Decision worth recording** (any of: hard to reverse, rejects at least one viable alternative, constrains future work — per `docs/adr/README.md`) → write an ADR in `docs/adr/` per `docs/adr/README.md` and `docs/adr/TEMPLATE.md`, numbered sequentially after the highest existing ADR.
- **No ADR** for obvious bug fixes, style choices, refactors with no behavioral change, or decisions the existing specs already cover.

The session ends when the grilling frontier is empty **and** the user confirms shared understanding. Only then may implementation planning (`to-spec`, `to-tickets`) begin.
