# ADR-0003: Discovery runs are driven by one phase machine, with host effects behind hooks

Status: accepted (2026-11-27)
Related: issue #203, issue #205, issue #209, ADR-0002, ADR-0004

## Context

`GeappliancesBridge::loop()` decided what discovery to do from a cluster of
booleans and counters spread across the bridge (`discovery_in_progress_`,
`discovery_cleanup_pending_`, `discovery_reboot_pending_`, …). Any combination
of them was reachable, so a new state needed a new flag and the invalid
combinations were never excluded. The same code could not be exercised on the
host: it called NVS, ESPHome's MQTT availability config, `esp_ota_*`,
`esp_restart()`, and the watchdog directly.

## Decision

One `DiscoveryLifecycleManager` owns the sequence as an explicit phase
(`kIdle`, `kPhaseClaimed`, `kPhaseQueuedInitial`, `kPhaseQueuedCleanup`,
`kPhaseCleaning`, `kPhasePublishing`, `kPhaseReboot`, …) plus a
`reason` field that records *why* the phase was entered. The reason is
orthogonal to the phase, so the same phase can be reached from a button press,
an OTA version change, or a pending-marker retrigger without new states.

- **Triggers are claimed atomically.** `trigger_discovery_refresh()` runs off the
  main loop, so a check-then-act on `kIdle` would let two triggers both pass and
  overwrite each other. The claim moves the phase to `kPhaseClaimed` with a
  release store, so `loop()` never observes a queued phase whose `trigger_` is
  still stale.
- **Host effects go through `DiscoveryHooks`.** NVS access, OTA-rollback
  confirmation, watchdog feeding, rebooting, and reading ESPHome's MQTT
  availability config are injected, so the phase machine runs identically on the
  device and in the unit-test harness. Tests drive the real state fields instead
  of mocking them.
- **The bridge owns `ha_discovery_manager_t`.** The manager calls
  `init`/`configure`/`start`/`destroy` on it, but the struct's storage lives in
  the bridge.
- **A run ends in COMPLETE or FAILED, and both clear `is_processing()`.** Branch
  on the actual state: recording a FAILED run as a successful publish makes the
  next boot's change check see a matching hash and never retry.
- **Stall detection is an idle timeout, not a wall-clock bound.** The timeout
  measures silence from phase entry until the first topic is removed, or until
  the first entity is published, and the counter is seeded from the module's own
  count rather than assumed zero. A congested broker makes publishes drop and
  retry through the publish backoff, one topic per main-loop iteration, so the
  largest runs exceed any fixed entry-based bound while still working; only a
  run that *stops* making progress is wedged.
- **Cleanup allocates its topic queue before committing anything.** A cleanup
  that cannot track the topics it removes must not start; refusing at that point
  changes neither NVS nor the broker, so the next boot simply tries again.
- **An explicit cleanup proceeds even when `generate_device_config_` is off.**
  Stale retained topics from an earlier firmware still need removing; only the
  automatic initial publish honours the switch.
- **The availability topic is refreshed before the discovery-data hash is
  computed**, so a `topic_prefix` or node-name change triggers a republish.
- **A successful publish waits a grace period before rebooting**, so the last
  discovery packets leave through MQTT.

## Consequences

- Adding a state means adding a phase and its transitions; the invalid
  combinations are unrepresentable rather than merely untested.
- The machine is testable on the host without ESP-IDF, which is where the
  budget, marker-ordering, and hold-precedence tests live.
- A wedged run is detected by lost progress, so raising the timeout does not
  make a genuinely stuck run take longer to notice, and a slow broker does not
  cause false aborts.
- Aborting is the only way the machine accepts another request in the same boot;
  the record is never saved, so the next boot's change check retries too
  (see ADR-0002).

## Code anchors

- `discovery_lifecycle_manager.h` / `.cpp` — `Phase`, `TriggerReason`,
  `trigger_discovery_refresh()`, `begin_cleanup_()`, `begin_publish_()`,
  `finish_publish_()`, `abort_publish_run_()`, `check_discovery_changes()`,
  `DiscoveryHooks`.
- `geappliances_bridge.cpp` — `loop()` delegation, ownership of
  `ha_discovery_manager_`.
- `test/tests/discovery_lifecycle_manager_test.cpp`,
  `discovery_lifecycle_concurrency_test.cpp` — claim atomicity, idle-timeout,
  and FAILED-vs-COMPLETE tests.
