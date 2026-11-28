# ADR-0002: An aborted discovery run reboots the device, bounded by a persisted budget

Status: accepted (2026-11-27)
Related: issue #203, issue #205, issue #209, ADR-0004, ADR-0006

## Context

A Home Assistant discovery publish run holds roughly 52 KB of run-local
allocations (the four decompression/payload buffers plus the `tinfl` state) for
the 75–210 s the run takes, while the ERD publisher churns about 1 500
allocations per second. When a run is refused or dies, that memory is released —
but the heap does not come back. Measured on hardware, the largest free block
after an aborted run stays at 102 400–143 360 B for the rest of the boot,
against 237 568 B after a clean one: the freed region is bounded by surviving
allocations, so nothing re-coalesces it short of a reset. Consolidating the four
allocations into one block was tried and measured *worse* (102 400 B versus
108 544–143 360 B).

A failed run also never saves its `DiscoveryRecord`, which is what makes the next
boot's change check retry discovery. Retry without memory is a reboot loop: a
permanently failing payload (a corrupt discovery data blob, for example) would
otherwise reboot on every boot forever.

## Decision

`DiscoveryLifecycleManager::abort_publish_run_()` releases the run's
allocations, saves no record, and arms a reboot while a persisted budget lasts.

- The budget is `kMaxAbortReboots` (3) attempts, counted in a dedicated NVS
  preference `kAbortRebootNvsKey` (`"drab"`), cleared by a successful run. It is
  persisted because the reboot itself erases the only in-RAM evidence that the
  run failed.
- If the counter cannot be written, the abort path **stays up** rather than
  rebooting: a reboot that is not remembered is an unbounded retry loop with no
  way to stop it.
- The firmware is always confirmed (`esp_ota_mark_app_valid_cancel_rollback`)
  regardless of the discovery outcome, so a good OTA is never rolled back because
  a discovery run was refused. `kSafeModeRtcKey` is not reused for this counter:
  that is firmware-validation state, and this counts discovery runs.
- A separate NVS preference `kPublishPendingNvsKey` (`"drpp"`) marks "a cleanup
  has not been republished yet". It is a separate preference rather than a
  `DiscoveryRecord` field because a record field would force `kRecordVersion`
  up, and a version bump makes every bridge clean up and republish once at the
  next OTA.
- The marker is armed in `begin_cleanup_()` **before** any removal publish,
  because the first removal publish is the point of no return for Home
  Assistant's retained configs. If the marker cannot be written, nothing has
  been removed and the allocated queue is released again.
- Both the marker retrigger and the no-record initial trigger are gated on the
  abort budget. When the budget is spent, the machine **holds** instead of
  retrying, and the hold takes precedence over the hash, device-id and version
  change branches below it. Do not reorder that.
- The marker is cleared only after the record is saved. Clearing it first risks
  a boot with no configs and no retry; clearing it after costs at most one extra
  republish.

## Consequences

- A failed discovery run costs one reboot, up to three, then the bridge stays up
  with discovery held until an operator presses Discovery Refresh. A power cycle
  does not lift the hold: the budget lives in NVS and only a successful publish
  clears it.
- Heap headroom, not run count, is the thing to watch. The reboot is the
  recovery mechanism for fragmentation, so any change that removes the reboot
  needs a replacement that actually re-coalesces.
- A device that has never completed a publish run (no record) spends the budget
  without ever storing a record, and the initial path goes straight to
  `begin_publish_()` so it never arms the pending marker either. That path was
  reboot-looping one full publish attempt per boot (issue #205) before it was
  gated.
- With the marker set, a concurrent discovery-data change is swallowed until the
  hold lifts. That is intentional: the configs are already gone, so a second
  teardown could only lose more.
- Fleet operators see the effect as a reboot plus a delayed entity restore, not
  as a permanently empty appliance — which is what issue #203 was.

## Code anchors

- `discovery_lifecycle_manager.cpp` — `abort_publish_run_()`,
  `begin_cleanup_()`, `check_discovery_changes()`, `handle_failed_run_()` paths,
  `kAbortRebootNvsKey`, `kPublishPendingNvsKey`.
- `discovery_lifecycle_manager.h` — `kMaxAbortReboots`,
  `read_abort_reboot_count()`, `write_abort_reboot_count()`,
  `abort_publish_run_()` contract.
- `ha_discovery_manager.cpp` / `ha_discovery_manager_start()` — the ~52 KB of
  run allocations this decision recovers.
- `test/tests/discovery_lifecycle_manager_test.cpp` — budget, marker ordering,
  and hold-precedence tests.
