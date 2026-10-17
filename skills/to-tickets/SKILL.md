---
name: to-tickets
description: "User-invoked. Break a plan, spec, or conversation into tracer-bullet tickets with blocking edges, written as local files under .scratch/<feature>/issues/. Use after to-spec, before implement."
---

# To Tickets

Break a plan, spec, or conversation into a set of **tickets**: tracer-bullet vertical slices, each declaring the tickets that **block** it.

**Tracker:** tickets are local files under `.scratch/<feature-slug>/issues/`, committed on the feature branch. (The session's `gh` account may lack write access to the GitHub tracker; local files work regardless and travel with the branch. If the account can write to the tracker and the user prefers issues, publish one issue per ticket instead, in dependency order, as sub-issues of the parent.)

## Process

### 1. Gather context

Work from whatever is already in the conversation context. If the user passes a reference (a spec path, an issue number), fetch it and read its full body and comments.

### 2. Explore the codebase (optional)

If you haven't already explored the codebase, do so. Ticket titles and descriptions use `docs/GLOSSARY.md` vocabulary and respect ADRs in the area. Look for opportunities to prefactor first: "make the change easy, then make the easy change."

### 3. Draft vertical slices

<vertical-slice-rules>

- Each slice cuts a narrow but COMPLETE path through every layer (component, cache, MQTT, tests): vertical, NOT a horizontal slice of one layer.
- A completed slice is demoable or verifiable on its own (a test that runs, a topic that publishes, a scenario that passes).
- Each slice is sized to fit in a single fresh context window.
- Any prefactoring is its own ticket, first.
- A slice that changes ERD definitions, overrides, or pipeline scripts must include the pipeline rerun and all generated files in its scope (they commit together).

</vertical-slice-rules>

Give each ticket its **blocking edges**: the other tickets that must complete before it can start. A ticket with no blockers can start immediately.

**Wide refactors are the exception to vertical slicing.** A wide refactor is one mechanical change (rename a symbol, retype a shared field) whose blast radius fans across the codebase so no vertical slice lands green. Sequence it as **expand–contract**: expand (add the new form beside the old), migrate call sites in batches sized by blast radius (each batch its own ticket, CI green batch to batch), contract (delete the old form, blocked by every migrate batch).

### 4. Quiz the user

Present the proposed breakdown as a numbered list. For each ticket: **Title**, **Blocked by**, **What it delivers** (the end-to-end behaviour). Ask: is the granularity right? Are the blocking edges correct? Should any tickets be merged or split? Iterate until the user approves.

### 5. Write the tickets

One file per ticket under `.scratch/<feature-slug>/issues/<NN>-<slug>.md`, numbered from `01` in dependency order (blockers first). Never a single combined file.

<ticket-template>

# <NN>: <Ticket title>

**What to build:** the end-to-end behaviour this ticket makes work, from the user's perspective, not a layer-by-layer implementation list.

**Blocked by:** the numbers/titles of the tickets that gate this one, or "None (can start immediately)".

**Acceptance criteria:**

- [ ] Criterion 1
- [ ] Criterion 2

**Verification:** <which test targets / device-verify apply>

</ticket-template>

Avoid specific file paths or code snippets: they go stale fast. Exception: a prototype snippet that encodes a decision more precisely than prose can — inline it, trimmed to the decision-rich parts.

Work the **frontier**: any ticket whose blockers are all done. For a purely linear chain that means top to bottom. Do NOT close or modify the parent issue.
