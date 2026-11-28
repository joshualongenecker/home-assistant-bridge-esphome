---
name: delete-comment
description: Use to remove a tranche of code comments under the repo's comment policy — audit, adjudicate every comment, delete tautologies/stale/dead code, file issues for excuses, move decision records into ADRs/spec Notes, then retighten the ratchet baseline.
---

# Delete Comment

Repeatable tranche of the comment ban defined in `docs/comment-policy.md`. The
ban is a ratchet: each run deletes a tranche and lowers the per-file baseline.
Never try to delete everything in one pass.

## 0. Preflight

```bash
git status --porcelain          # must be clean; a dirty tree corrupts the diff review
git submodule status            # lib/ submodules must be at their recorded commits
make check-comments             # current debt; if it fails, the baseline is stale
```

Work on a branch off `develop`. Never merge a PR; push the branch only.

## 1. Audit

```bash
python3 scripts/comment_policy.py audit --out /tmp/comments.jsonl
```

One JSON line per in-scope comment: `file`, `start`, `end`, `kind`, `text`,
`category`, `why`. The categories are regex priors. **Trust only** `separator or
structural banner`, `empty comment marker`, `docstring: API documentation`, and
`tool directive`. Everything else — especially the `stale` and `excuse` priors,
which fire on ordinary English ("later", "wrong", "no longer") — must be
adjudicated against the code.

## 2. Slice

Group the audit into worklists of ~180 comments that keep each file whole, one
file per slice line group, big files split by 600-line chunks:

```bash
python3 scripts/comment_slice.py --audit /tmp/comments.jsonl --out-dir /tmp/slices
```

Skip slices for the four trusted priors; they need no judgment.

## 3. Adjudicate

Fan out one subagent per slice, **3-5 at a time** — the model serving the
subagents is the same one serving this session, so a 20-agent fan-out queues and
each agent slows to a crawl. Test-tree slices (mostly narration and test intent)
are faster adjudicated here with a reviewed rule pass than by agents. Each agent:

- reads `docs/comment-policy.md` (the rubric is authoritative),
- for every worklist line reads the source file around the comment,
- writes one JSON line per comment to `/tmp/verdicts/sNN.jsonl`:
  `{"id": <audit line index>, "category": "...", "adr": "<slug>", "issue": "<title>"}`,
- edits nothing in the repository and runs no builds.

Give agents the fixed ADR slug list from `docs/adr/README.md` plus
`new:<kebab-slug>` for anything else. The rubric tie-breaks are the ones that
matter in practice: history carrying a present constraint is a decision record,
not stale; a test comment that names what it pins is a decision record, one that
narrates the next call is a tautology.

Tie-break rule that only shows up at merge time: **adjudicate contiguous audit
rows as one block.** Adjacent rows in one file whose line ranges touch are pieces
of a single comment block, and `delete` removes a block whole or not at all, so
every row of a block must get the same category — per-row verdicts leave mixed
blocks intact (one tranche lost 195 blocks that way). Group by
`(file, start == previous end + 1, same kind)` and classify the joined text.
`merge` validates `category` against `CATEGORY` in `scripts/comment_policy.py`;
free-form labels (`separator`) are rejected.

Merge and check coverage before applying anything:

```bash
python3 scripts/comment_policy.py merge --audit /tmp/comments.jsonl \
    --verdicts '/tmp/verdicts/*.jsonl' --out /tmp/decisions.jsonl
```

`merge` fails on a missing id, a duplicate id, an unknown category, a
decision_record without `adr`, or an excuse without `issue`.

## 4. Delete — exactly one pass per audit

`delete` resolves audit line numbers against the file as it is on disk, so the
moment one comment is removed every later line number in that file is stale.
**Run `delete` once per audit, with everything you intend to remove in that one
run.** A second pass over the same audit deletes the wrong lines.

Build one id list, then delete with it:

```bash
python3 scripts/comment_policy.py delete --audit /tmp/decisions.jsonl \
    --categories tautology,stale,dead_code,excuse,decision_record \
    --ids /tmp/to_delete.ids
```

`/tmp/to_delete.ids` = every adjudicated `tautology`, `stale`, `dead_code` and
`excuse` id, plus the `decision_record` ids whose rationale is already written
(the union of `/tmp/covered/*.ids`). Unadjudicated rows are `unresolved` after
`merge`, so a heuristic can never delete anything on its own.

Delete excuses only in the same change that files their issues (step 5), so no
hack loses its marker. Decision records are deleted only after their ADR or spec
Note is written (step 6); leftovers are what the ratchet carries to the next run.
The next tranche re-runs `audit` from scratch and gets fresh ids.

## 5. Excuses become issues

One issue per distinct hack, not per comment. Issues go to the canonical repo:

```bash
gh issue create --repo joshualongenecker/home-assistant-bridge-esphome \
  --title "<one-line fix>" --body-file /tmp/issue.md
```

Body: the file and symbol, what the shortcut is, why it is a hazard, what the
proper fix looks like, and the tests that would catch a regression. Record every
issue number in the PR description.

## 6. Decision records become ADRs or spec Notes

Cluster by `adr` slug. One ADR per cluster, never one ADR per comment:

```bash
python3 scripts/comment_policy.py clusters --audit /tmp/decisions.jsonl \
    --out-dir /tmp/clusters
```

Cluster by `adr` slug, then **consolidate**: agents invent `new:` slugs freely (one
tranche produced 73 slugs for 750 comments), and most of them are the same
decision seen from adjacent files. Fold each `new:` slug into an existing ADR
topic when it is the same decision — check the Code anchors of the existing ADRs
first. Open a new ADR only when no existing topic covers it, and keep the total
ADR set near one per durable decision (20-ish for this repo), not one per slug.

Write `docs/adr/NNNN-<slug>.md` using the format in `docs/adr/README.md`:
Status, Context, Decision, Consequences, and **Code anchors** listing the files
and symbols the decision governs. Module-local decisions go into
`docs/spec/<module>.md` § Notes instead — do not duplicate a spec note into an
ADR. Protocol and wire facts go to `docs/reference/erd-protocol.md` or
`docs/reference/mqtt-topics.md`.

Then delete those comments:

```bash
python3 scripts/comment_policy.py delete --audit /tmp/decisions.jsonl \
    --categories decision_record --ids /tmp/covered.txt
```

`--ids` restricts the delete to comments whose decision is actually captured.
Anything not captured stays and is carried by the baseline.

## 7. Verify

Comment-only edits must not change code. Prove it, then compile:

```bash
python3 scripts/comment_policy.py verify-code-identical --only $(git diff --name-only -- components test scripts | grep -v Makefile)
make check-protocol-variants                              # component sources compile
make test -j8 && make integration-test -j8                # repo gate before push
```

If `verify-code-identical` reports a file, the deletion tool ate code — revert
that file and investigate; do not hand-patch it.

## 8. Retighten the ratchet

```bash
make comments-baseline
make check-comments
```

Commit the baseline in the same commit as the deletions. Update
`docs/adr/README.md` with any new ADR. Never raise a baseline value.

## What not to do

- Never touch generated files or `miniz*`; their comments come from generators
  and vendored upstream.
- Never delete a tool directive (`NOLINT`, `LCOV_EXCL_*`, `clang-format`,
  `type: ignore`, `noqa`).
- Never delete a decision record before its rationale is written down somewhere.
- Never claim a tranche is complete: report the remaining counted comments from
  `make check-comments`.
