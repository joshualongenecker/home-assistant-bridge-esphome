# Documentation Quality Checklist

Use this checklist before committing documentation changes.

## Structure

- [ ] File is in the correct directory per [style-guide.md](./style-guide.md) taxonomy.
- [ ] Filename matches the module/concept name (lowercase, underscores).
- [ ] Document title (`#`) matches the filename.
- [ ] Heading depth does not exceed `###` (level 3).

## Formatting

- [ ] All code blocks have language tags (`cpp`, `c`, `yaml`, `bash`, `python`, `json`).
- [ ] No trailing whitespace on any line.
- [ ] Prose lines do not exceed 120 characters (code blocks exempt).
- [ ] No emojis.
- [ ] No marketing language ("powerful", "revolutionary", "best-in-class").
- [ ] Tables have a brief description before them explaining what they show.

## Content

- [ ] Every claim is grounded in source code or existing documentation.
- [ ] Module names match the actual source file names.
- [ ] Terminology matches the [style guide](./style-guide.md) table (ERD, GEA2/GEA3, Bridge, Appliance, etc.).
- [ ] No vague references ("the system does X") — name the specific module.
- [ ] Mermaid diagrams use `graph TB`, `graph LR`, or `sequenceDiagram` syntax.
- [ ] Mermaid diagrams have a caption describing what they show.

## Specs (docs/spec/)

- [ ] Follows the [spec template](./spec/spec-template.md) exactly.
- [ ] **Overview** has Purpose, Responsibilities, Not Responsible For.
- [ ] **Interface** documents handle struct, vtable, and public API.
- [ ] **Behavior** has numbered requirements with Rationale, Implementation, Verification.
- [ ] **Notes** documents design decisions and tradeoffs.
- [ ] Source file references are accurate (filename matches actual file).

## Cross-References

- [ ] Links to other docs use relative paths.
- [ ] Links resolve (target file exists).
- [ ] Links to source files use correct paths from repo root.

## Guides

- [ ] Uses imperative voice ("Run the pipeline", "Configure the UART").
- [ ] Code blocks contain real, tested examples.
- [ ] Steps are ordered and complete (no gaps in the procedure).

## Reference

- [ ] Tables are complete (no missing options or topics).
- [ ] Default values match the actual code defaults.
- [ ] Examples are consistent with the configuration reference.

## Before Commit

- [ ] If ERD definitions, overrides, or pipeline scripts changed, the pipeline was rerun.
- [ ] The [docs/README.md](./README.md) index is updated if new docs were added.
- [ ] No generated files (`.pyc`, build artifacts) are included.

## Source Comments

- [ ] `make check-comments` passes (no file exceeds its comment baseline).
- [ ] Any rationale a removed comment carried is now in an ADR or the module spec's Notes.
- [ ] New ADRs are listed in [adr/README.md](./adr/README.md) with Code anchors.
- [ ] Excuse comments that were deleted have a filed issue for the underlying hack.