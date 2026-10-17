# 0002. Add the Performance review axis and canary metrics

## Status

`accepted`

## Date

2026-10-17

## Context

ADR-0001's four-axis review (Standards, Security, Spec, Invariants) had no performance dimension. This project's failure history is performance-adjacent: watchdog pressure from unbounded loop work, heap exhaustion during discovery, and publish-rate bursts that saturated the MQTT queue. The first indicator that a change broke something is a movement in loop time, heap, or publish rates — but nothing in the lifecycle required measuring or reviewing them. The firmware already exposes most of the needed metrics with zero component code: the ESPHome `debug` sensor platform (loop time, min free heap) in the device configs' `common-debug.yaml` package, and `DiagnosticSensorPublisher` (ERD cache entries, ERD/MQTT publish rates).

## Decision

1. **Fifth review axis — Performance.** `code-review` runs five axes. The Performance axis reports: new heap allocations on loop/publish paths, work added to the main loop or publisher task, changes to poll/publish rates or rate limits, and (for firmware-facing diffs) whether the PR's Device Verification section carries the canary metrics against the develop baseline.
2. **Five canary metrics:** loop time, min heap, ERDs in cache, ERD publish rate, MQTT publish rate.
3. **`develop` is the standing baseline.** Every firmware build is benchmarked against a build of current `develop` — not the previous feature branch — so each PR is accountable to the mainline, not to its own predecessor.
4. **No ERD-read counter.** Appliance-side read completions would need new component code; the existing ERD-publish vs MQTT-publish rate divergence already catches most read-path breakage. The counter is deferred until a real regression slips past the five metrics.

## Consequences

- Every firmware-facing PR carries a five-metric table vs `develop`; the `device-verify` skill's benchmark step enforces it.
- Reviews run five sub-agents (or five sequential passes), not four.
- ADR-0001 point 6 (four axes) is refined by this ADR; the other points of ADR-0001 stand.
- Rejected alternatives: adding the axis only after a performance incident (rejected — the canary must exist before the regression it catches); the ERD-read counter now (rejected — component cost for marginal signal over the existing rates).
