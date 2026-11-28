# Architecture Decision Records

One decision per file, numbered `NNNN-<slug>.md`. An ADR records a decision that
is expensive to reverse or easy to get wrong again — not every code detail.

## Format

```markdown
# ADR-NNNN: <decision as a sentence>

Status: accepted (<date>)
Related: issue #NNN, PR #NNN, ADR-NNNN

## Context
What the code had to deal with, and the constraints that were actually in force.

## Decision
What we do now, stated so it can be checked.

## Consequences
What becomes easy, what becomes harder, and what is now forbidden.

## Code anchors
Files and symbols this decision governs. This is how an ADR stays findable from
the code, since the code itself carries no comments.
```

Keep an ADR to one screen. Module-local decisions belong in the module's
`docs/spec/<module>.md` § Notes, not here; do not duplicate a spec note into an
ADR. Protocol and wire facts belong in `docs/reference/`.

## Index

|ADR|Decision|Status|
|---|---|---|
|[0001](./0001-source-comments-banned.md)|First-party source carries no prose comments; rationale lives in ADRs and specs, enforced by a per-file ratchet.|accepted|
|[0002](./0002-aborted-discovery-run-reboots.md)|An aborted discovery run frees its buffers, reboots to recover the fragmentation, and bounds the retries with a persisted NVS budget.|accepted|
|[0003](./0003-discovery-lifecycle-phase-machine.md)|Discovery runs are driven by one phase machine with host effects injected through hooks, and stalls are detected by lost progress rather than wall clock.|accepted|
|[0004](./0004-erd-cache-fixed-arena.md)|The ERD cache is a fixed 300-slot arena with an immutable entry size, and its lock is never held across the MQTT publish.|accepted|
|[0005](./0005-erd-publisher-pinned-task.md)|ERD publishing runs in a FreeRTOS task pinned to core 0 at priority 1 that publishes from an atomic cache snapshot.|accepted|
|[0007](./0007-startup-is-an-explicit-hsm.md)|Startup is an explicit HSM whose phases gate the data plane, with no timeout on the phases that wait on the appliance.|accepted|
|[0008](./0008-valid-erd-filter-borrows-one-array.md)|The valid-ERD filter borrows and sorts the feature-bit array in place instead of copying it, and the valid-ERD fingerprint drives discovery republishes.|accepted|
|[0009](./0009-discovery-run-gated-on-largest-block.md)|A discovery publish run is gated on the largest contiguous free heap block, sized from the run's own allocations, not on total free heap.|accepted|
|[0010](./0010-publisher-drain-policy.md)|The ERD publisher paces only backlogs above a threshold, backs off 250 ms after a dropped publish, and re-drains everything after a 60 s outage.|accepted|
|[0006](./0006-bridge-data-plane-phase-machine.md)|The bridge's data plane and publisher pause are explicit phase machines, and teardown/restart order follows from the phase.|accepted|
|[0011](./0011-board-address-is-explicit.md)|Board addresses are carried explicitly through cache, topics, writes and polling, and 0xFF is a sentinel that can never be a physical address.|accepted|
|[0012](./0012-one-owner-for-the-mqtt-topic-grammar.md)|One module owns the ERD MQTT topic grammar; the primary board keeps the legacy no-prefix form and parsing is strict.|accepted|
|[0013](./0013-sanitizer-and-variant-gates-get-their-own-builds.md)|Every sanitizer and build-variant gate gets its own build directory and flags, and leak checking is Linux-only.|accepted|
|[0014](./0014-discovery-record-layout-is-versioned-and-pinned.md)|The NVS discovery record layout is pinned by static asserts with a versioned prefix loader, and a flapping valid-ERD fingerprint is bounded to two republishes.|accepted|
|[0015](./0015-write-path-is-single-flight.md)|The ERD write path keeps one write on the bus, queues accepted requests with an accept-time copy, and rejects what it cannot honor with a write_result.|accepted|
|[0016](./0016-value-init-and-guarded-destroy.md)|Embedded structs that own heap are value-initialized at the declaration, and every destroy path is guarded, idempotent, and ordered stop-timers then unsubscribe then free.|accepted|
|[0017](./0017-autodiscovery-is-self-driving.md)|Autodiscovery assembles packets from the byte stream, ignores the Wi-Fi module's answers, and never stalls a probe attempt on a failed send.|accepted|
|[0018](./0018-esphome-layer-is-a-thin-seam.md)|C structs and vtables carry the data path, ESPHome's MQTT client is reached through one adapter, diagnostic entities need codegen wrappers, and libraries are pinned by SHA.|accepted|
|[0019](./0019-silent-board-keeps-its-entities.md)|Discovery takes its board set from the configuration, not the cache, so a board that is silent at one boot keeps its Home Assistant entities.|accepted|
|[0020](./0020-generated-files-are-generated.md)|Generated sources are produced by two scripts, CI diffs them against the generators, and the check itself mutates the working tree.|accepted|
