---
name: domain-modeling
description: "Model-invoked. Build and sharpen the project's domain model: challenge terms against docs/GLOSSARY.md, stress-test with edge-case scenarios, update the glossary and write ADRs in docs/adr/ as decisions crystallise. Use when terminology is being discussed or settled."
---

Actively build and sharpen the project's domain model as you design. This is the *active* discipline: challenging terms, inventing edge-case scenarios, and writing the glossary and decisions down the moment they crystallise. (Merely *reading* `docs/GLOSSARY.md` for vocabulary is not this skill: that's a one-line habit any skill can do. This skill is for when you're changing the model, not just consuming it.)

## File structure

This repo has a single context:

```
/
├── docs/
│   ├── GLOSSARY.md
│   └── adr/
│       ├── README.md
│       ├── TEMPLATE.md
│       └── 0001-<slug>.md
```

Update files as content is resolved; never create an empty placeholder.

## During the session

### Challenge against the glossary

When the user uses a term that conflicts with the existing language in `docs/GLOSSARY.md`, call it out immediately. "The glossary defines 'probe entry' as X, but you seem to mean Y. Which is it?"

### Sharpen fuzzy language

When the user uses vague or overloaded terms, propose a precise canonical term. "You're saying 'board': do you mean the primary board or a secondary board? Those key the cache differently."

### Discuss concrete scenarios

When domain relationships are being discussed, stress-test them with specific scenarios. Invent scenarios that probe edge cases — a `0x00` secondary board, a 248-byte ERD payload, a broker restart mid-publish, a GEA2 appliance in AUTO mode — and force precision about the boundaries between concepts.

### Cross-reference with code

When the user states how something works, check whether the code agrees. If you find a contradiction, surface it: "The write bridge drops writes when its host address is the broadcast sentinel, but you just said writes queue until discovery completes. Which is right?"

### Update the glossary inline

When a term is resolved, update `docs/GLOSSARY.md` right there. Don't batch these up: capture them as they happen. The glossary is totally devoid of implementation details — it is a glossary and nothing else.

### Offer ADRs sparingly

Only offer to create an ADR when any of these holds (per `docs/adr/README.md`):

1. **Hard to reverse**: the cost of changing your mind later is meaningful
2. **Rejects a viable alternative**: there were genuine alternatives and you picked one for specific reasons — the rejected option and the reason must be recorded
3. **Constrains future work**: an invariant other code must respect

Do not offer one for obvious bug fixes, style choices, refactors with no behavioral change, or decisions the existing specs already cover. Use `docs/adr/TEMPLATE.md`.
