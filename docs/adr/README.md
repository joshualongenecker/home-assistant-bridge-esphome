# Architecture Decision Records

ADRs capture decisions that are hard to reverse, or that trade off viable alternatives: protocol and wire-format choices, concurrency and memory strategy, public API shape, tooling and process adoption.

## When to write one

Write an ADR when a decision:

- Is hard to reverse (wire format, on-device memory strategy, MQTT topic shape), or
- Rejects at least one viable alternative (the rejected option and the reason must be recorded), or
- Constrains future work (an invariant other code must respect).

Do not write one for: obvious bug fixes, style choices, refactors with no behavioral change, or decisions the existing specs already cover.

## When they get written

- `grill-with-docs` writes ADRs inline when a grilling session settles a hard decision.
- The factory lifecycle (`docs/factory.md`) requires an ADR for any feature whose design rejected a viable alternative.
- Anyone may add one at any time. Accepted ADRs are amended only by supersession, never edited in place.

## Format and numbering

- File: `NNNN-short-imperative-title.md` (e.g. `0004-key-cache-by-board-address.md`).
- Numbers are sequential and never reused. A superseded ADR keeps its number and records `superseded by NNNN` in its status.
- Template: [TEMPLATE.md](./TEMPLATE.md).
- Keep ADRs short: context, decision, consequences. If a decision needs diagrams, link to `docs/architecture/` instead of duplicating them.
