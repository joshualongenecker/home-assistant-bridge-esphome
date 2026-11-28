# ADR-0001: First-party source carries no prose comments

Status: accepted (2026-11-27)
Related: docs/comment-policy.md, ADR-0002

## Context

Measured on `develop` at 2026-11-27 across the 140 tracked first-party source
files (component, tests, pipeline scripts, Makefile): 41,237 lines contain 4,480
comments occupying 7,330 lines. Deleting every one of them sheds 6,469 lines —
15.7% of the source — and the code is byte-identical afterwards apart from
whitespace.

Auditing that population produced three failure modes, all of which cost more
than the comments return:

- **Duplication.** The specs in `docs/spec/` are already the single source of
  truth for module contracts, and they carry the same rationale the headers do.
  `erd_cache.h` and `docs/spec/erd_cache.md` both state the arena design, the
  248-byte cap, and the lock discipline. Two copies of a fact, only one of which
  is updated by a spec change.
- **Rot.** Comments name identifiers, files and revisions that no longer exist
  ("the `polling_timer_armed` flag", "replaces the seven independent booleans").
  Nothing checks them, so they keep asserting things the code no longer does.
- **Excuses.** `// hardcoded, update in lockstep`, `// simplification`, `// for
  now` record a shortcut instead of fixing it, and normalize it for the next
  reader. A comment that says a constant must be hand-synced is a missing
  `static_assert` or missing test with a paper bandage on it.

The counter-argument is real: embedded code carries hard-won constraints —
measured heap figures, wire-format facts, hazards where an innocent-looking
cleanup breaks a dual-core race. Deleting those without writing them down loses
them. That is why the ban is paired with a home for the rationale rather than a
bare "no comments" rule.

## Decision

First-party source carries no prose comments.

- Rationale for a cross-cutting or hard-won decision goes into an ADR here, with
  a **Code anchors** section naming the files and symbols it governs, so the
  decision stays findable from the code that implements it.
- Module-local rationale goes into `docs/spec/<module>.md` § Notes.
- Protocol and wire facts go into `docs/reference/`.
- A comment that excuses a live hack is deleted and replaced by an issue that
  owns the hack.
- Exemptions: submodules, vendored `miniz*`, generated files (their comments come
  from the generators), Python docstrings, and tool directives (`NOLINT`,
  `LCOV_EXCL_*`, `clang-format`, `type: ignore`, `noqa`).

Enforcement is a ratchet, not a switch: `scripts/comment_policy.py` counts
comments per file, `scripts/comment_baseline.json` is the committed ceiling, and
`make check-comments` fails any file that exceeds its baseline. Removing comments
lowers the ceiling in the same commit; raising it is not accepted.

## Consequences

- New comments fail CI unless the baseline is raised, which is not accepted. A
  contributor who needs to explain something writes an ADR or a spec Note.
- Locality is lost: a reader standing at `begin_publish_()` no longer sees the
  measured heap numbers next to the gate. They are recovered by grepping the
  symbol name across `docs/adr/`, which the Code anchors section exists to make
  reliable.
- Comment debt is now visible and per-file, so it can be burned down in tranches
  (`.omp/skills/delete-comment/SKILL.md`) instead of argued about case by case.
- Test files are in scope. A test that needs a scenario label states it in the
  test name; the CppUTest name is the readable form.
- Generated files keep their header banners because they are exempt; changing
  that means changing the generators, not the output.

## Rejected alternatives

- **Keep only "why" comments.** No measurable boundary: the same file grows
  tautology and rot again within a few months, and nothing detects it.
- **Reference ADRs from the code (`// see ADR-0007`).** That is a comment, and it
  rots the same way. Code anchors in the ADR give the same link in one place.
- **One big sweep.** 4,480 judgments in one change is unreviewable and would have
  deleted decision records before they had anywhere to go. The ratchet makes each
  tranche reviewable and keeps CI green throughout.

## Code anchors

- `scripts/comment_policy.py` — scope, exemptions, audit, ratchet (`check`,
  `baseline --write`), comment removal, `verify-code-identical`.
- `scripts/comment_slice.py` — adjudication worklists for a tranche.
- `scripts/comment_baseline.json` — the per-file ceiling.
- `Makefile` — `check-comments`, `comments-baseline`.
- `.github/workflows/test.yml` — the `Check comment ratchet` step in the `pytest` job.
- `docs/comment-policy.md` — the rubric and tie-breaks.
- `.omp/skills/delete-comment/SKILL.md` — the repeatable tranche procedure.
