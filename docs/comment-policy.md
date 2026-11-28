# Comment Policy

First-party source carries no prose comments. Rationale lives in `docs/adr/` and
`docs/spec/`; the code carries only code. The ban is enforced by a ratchet, so
existing comments are removed incrementally instead of in one big sweep.

## Scope

In scope: git-tracked sources under `components/geappliances_bridge/`, `test/`,
`scripts/`, and the `Makefile`.

Exempt, and never counted:

|Exemption|Why|
|---|---|
|`lib/` submodules|Upstream code, not ours.|
|`miniz*` in the component dir|Vendored third-party source.|
|Generated files (`erd_lists.*`, `appliance_api_feature_lists.*`, `appliance_type_map.h`, `ha_discovery_data.*`, `ha_discovery/*.jsonl`)|Comments there come from the generators; fix the generator, never the output.|
|Python docstrings|API documentation, part of the module contract, read by tooling.|
|Tool directives (`NOLINT`, `LCOV_EXCL_*`, `clang-format on/off`, `type: ignore`, `noqa`, `pragma: no cover`)|Functional markers, not prose.|

## Categories

Every in-scope comment is one of these. The action column is mandatory.

|Category|Definition|Action|
|---|---|---|
|**Tautology**|Restates what the code already says. Deleting it loses nothing a competent reader could not infer from the code: section banners, `// end of X`, `// init cache` above `init_cache()`, test-step labels that echo the call below them.|Delete.|
|**Stale**|Describes a revision that no longer exists: names an identifier, file, or behaviour that is gone, or narrates a past change with no present-day constraint.|Delete.|
|**Excuse**|Papers over a shortcut, defers work, or asks the reader to tolerate a live defect (`TODO`, `FIXME`, `HACK`, "for now", "these constants must be updated in lockstep", "simplification").|Delete **and** file an issue describing the underlying hack.|
|**Decision Record**|States why the code is this way: an invariant, a wire/protocol fact, a measured number, a tradeoff, or a hazard that breaks if someone "cleans up" the code.|Move to `docs/adr/NNNN-*.md` (cross-cutting) or `docs/spec/<module>.md` § Notes (module-local), then delete.|

## Tie-breaks

- Describes *and* constrains (`MUST be called after init`) → decision record.
- A test comment that names what is being pinned (`a 0x00 secondary board stays
  unambiguous against the 0xFF sentinel`) → decision record. One that narrates the
  next call (`advance the timer`) → tautology.
- History that carries a present constraint (`the cache never removes entries, so a
  reset leaves the old value behind`) → decision record, not stale.
- Unsure between tautology and decision record: would deleting it let a plausible
  future edit break something? Yes → decision record. No → tautology.
- A comment that is a decision record *and* an excuse (`workaround for ESP-IDF
  #1234`) is a decision record: the constraint is the durable part.

## Where rationale goes

- `docs/adr/NNNN-<slug>.md` — cross-cutting or hard-won decisions: context,
  decision, consequences, and a **Code anchors** list naming the files and symbols
  the decision governs, so the ADR stays findable from the code.
- `docs/spec/<module>.md` § Notes — module-local decisions, per the
  [style guide](./style-guide.md). Specs are already the single source of truth for
  module contracts; do not duplicate a spec note into an ADR.
- `docs/reference/`, `docs/architecture/` — protocol and wire facts.

An ADR must never be a comment transcription. It states the decision and its
consequences once; the code that implements it carries nothing.

## Ratchet

`scripts/comment_policy.py` counts in-scope comments per file. The committed
baseline `scripts/comment_baseline.json` is the ceiling.

```bash
make check-comments                      # CI gate: fails if any file is above baseline
scripts/comment_policy.py check          # same, directly
scripts/comment_policy.py baseline --write   # lower the ceiling after removing comments
scripts/comment_policy.py count          # current counts per file
```

The gate fails when a file exceeds its baseline. Lowering the baseline is part of
any comment-removal change; raising it is not accepted.

## Triage procedure

See the `delete-comment` skill (`.omp/skills/delete-comment/SKILL.md`) for the
repeatable procedure: audit, adjudicate, delete, file issues, move decision
records, retighten the baseline.
