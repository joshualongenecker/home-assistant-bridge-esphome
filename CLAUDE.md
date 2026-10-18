# Project Instructions

This repo runs the **software factory**: features and bugs flow issue → triage → grill → spec → tickets → implement → review (five sub-agent axes) → device verify → PR.

- Lifecycle source of truth: `docs/factory.md`
- Stage skills: `skills/<stage>/SKILL.md` — read the skill for the stage you're in and follow it
- Shared language: `docs/GLOSSARY.md` (use its terms; when code and glossary disagree, the code wins)
- Decisions: `docs/adr/` (accepted ADRs bind; never edited in place, only superseded)
- Module contracts: `docs/spec/` (update before committing a behavioral change)

## Hard rules

- NEVER merge a PR or force-push; only the user merges. No `Co-Authored-By:` trailers.
- PRs target `develop` on `joshualongenecker/home-assistant-bridge-esphome` (use `gh ... --repo`).
- Generated files are never hand-edited; rerun the pipeline and commit all generated files together.
- Firmware-facing changes (diff touches `components/` or `lib/`) require test-bench device verification before the PR.
- After changing component sources, verify with `esphome compile docs/test-compile.yaml`.
- Before committing: `make test -j4` and `make integration-test -j4`.
