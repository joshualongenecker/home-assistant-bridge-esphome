# ADR-0009: A discovery publish run is gated on the largest free heap block, not on total free heap

Status: accepted (2026-11-27)
Related: ADR-0002, ADR-0003, issue #203, issue #209

## Context

A publish run needs roughly 53 KB from `ha_discovery_manager_start()` as four
separate allocations. On a long-lived device the heap is often far below that.
On `gea-esphome-refer` a Discovery Refresh had 30 388 B free when its cleanup
started and 15 172 B when the cleanup finished: the run died with every retained
config already removed, and the appliance stayed entity-less (issue #203).

Total free heap is the wrong predictor. Measured on hardware, the largest free
block after an aborted run stays at 102 400–143 360 B for the rest of the boot
against 237 568 B after a clean one, and a run has completed with 2 120 B of
internal free while mid-run largest block sat at 53 248–69 632 B against a
53 420 B allocation. A floor sampled from `loop()` is structurally blind to the
failure: the consumption that matters happens inside the publish call, and
`heap_min_free` is monotone since boot, so a rebooted device never reports a
run's trough.

## Decision

`begin_publish_()` refuses to start a run whose buffers cannot fit, using the
**largest contiguous free block** read through the `read_largest_free_block`
hook, compared against `ha_discovery_manager_run_allocation_size()` — the run's
actual allocation requirement, not a guessed constant.

- The hook reads `MALLOC_CAP_DEFAULT`, which is what the run's plain `malloc()`
  calls draw from. On the fleet's targets (C3, C6, S3 — no SPIRAM heap) that
  equals `MALLOC_CAP_INTERNAL`, which `ha_discovery_manager_start()` logs.
  Revisit if a PSRAM target ever runs discovery.
- The gate is deliberately conservative: the four allocations are separate, so a
  heap that could satisfy them out of several blocks is still refused. A false
  refusal costs one reboot; a false acceptance starts a run already known to be
  doomed, which burns the attempt and its 75–210 s and then takes the same
  bounded reboot through the graceful FAILED path.
- A refusal goes through the abort path (ADR-0002): the pending marker makes the
  next boot republish on a clean heap, and the bounded budget keeps it from
  becoming a reboot loop. A device with no stored record has to respect the same
  budget, because the initial path never arms the marker (issue #205).

## Consequences

- The appliance never ends up with its configs removed and nothing to put back.
- The gate is measured in the same units as the failure, so it can be reasoned
  about from the fleet's `heap_max_block` samples; `heap_free` and `heap_min_free`
  cannot validate it.
- Any change to the run's buffers changes the gate automatically, because the
  threshold is derived from the allocation sizes rather than written twice.
- Adding a heap floor sampled from the main loop would not add protection and is
  not wanted; the metric with margin pressure is the largest block inside the
  publish path.

## Code anchors

- `discovery_lifecycle_manager.cpp` — `begin_publish_()` gate,
  `read_largest_free_block()`, `abort_publish_run_()`.
- `discovery_lifecycle_manager.h` — the `read_largest_free_block` hook.
- `ha_discovery_manager.cpp` — `ha_discovery_manager_run_allocation_size()`,
  `ha_discovery_manager_start()` and its four run buffers.
- `test/tests/discovery_lifecycle_manager_test.cpp` — refusal and budget tests
  driven through the hook.
