# ADR-0006: The appliance data plane is one phase machine, and teardown and restart follow its phases

Status: accepted (2026-11-27)
Related: ADR-0003, ADR-0004, ADR-0005

## Context

The bridge tracked the appliance-side data path with a cluster of `*_initialized_` booleans. Any combination was reachable, so "which bridges exist right now" had no single answer: destruction could double-free a bridge that was never created, or leak one that was, and rebuild, teardown, and the Reconnect Appliance restart each re-derived the answer by hand. Two more clusters hang off the same path: the ERD cache publisher, which must stop while HA discovery publishes, and the readiness question the discovery lifecycle asks before a run.

## Decision

**One `DataPlanePhase` enum names which bridges exist**, read only through
`phase_has_subscription_bridge()` / `phase_has_polling_bridge()`:

- `kIdle` — no data-plane bridge exists.
- `kPolling` — polling bridge only (poll mode, or fallback after a subscription failure).
- `kSubscription` — subscription bridge only; custom-ERD polling undecided.
- `kSubscriptionCustomPolling` — subscription bridge + custom-ERD polling bridge.
- `kSubscriptionSettled` — subscription bridge only; every custom ERD is covered by subscription, so no polling bridge is started.

The subscription bridge survives a custom-polling failure and the settled state; the
polling bridge exists only in the two polling phases.

**Steady state is a separate one-way latch (`DataPlaneReadiness`)**, because a failure
rebuild (`handle_subscription_failed()`) returns the phase to `kPolling` while the latch
stays set. Only an appliance restart clears it.

**Destruction follows the phase.** `destroy_data_plane_()` destroys exactly the bridges the
phase says exist — no double-free, no missed cleanup — and is the only path back to `kIdle`
(teardown, or a restart). Bridge `destroy()` stops its timers and unsubscribes before
freeing heap state: `tiny_timer_stop()` is idempotent, a subscription left registered fires
the bridge HSM against freed state (use-after-free, heap corruption), and `destroy()` guards
on `timer_group` so it is safe on a never-initialized struct. Teardown order is fixed — HA
discovery manager → ERD cache publisher → ERD cache → MQTT adapter — because the discovery
manager holds borrowed pointers to the cache and the adapter.

**Restart is deferred and partial.** `restart_appliance_connection()` only sets
`restart_requested_`; `loop()` runs it when the discovery lifecycle phase is `kIdle`, and it
is a no-op if startup has not begun (`hsm.current == nullptr`). It drops the data path and
forgets what the boards reported (cache contents, valid-ERD filter, feature bits) — they may
now run different firmware — then re-creates the autodiscovery manager and re-enters the
startup HSM at `startup_state_startup_delay`. It keeps the MQTT client, the device ID, the
cache object and its publisher, and the HA discovery state, so the MQTT topics and the HA
device stay the same and the device does not reboot.

**The publisher pause machine (`PublisherPausePhase`) is separate from the data plane.**
Paused while `ha_discovery_manager_is_processing()`, resumed afterwards, with `kResuming`
tracking the post-resume drain so "Device is in steady state" logs exactly once.
`update_publisher_state_()` reads `first_round_done()` *before* its own `signal_work()`: the
background task sets that flag during its drain, and the read wants the value from before the
give that can wake it again. Pacing may defer a wake; it can never advance one past that read.

**The throttle is applied before the publisher task exists**
(`erd_cache_set_throttle_rate_seconds()` before `erd_cache_mqtt_publisher_start()`), because
the task drains the cache as soon as it is created. Readiness is a predicate, not a
reference: the lifecycle manager calls `discovery_is_ready_(context)` rather than aliasing
the bridge's steady-state and MQTT members.

## Consequences

- Invalid bridge combinations are unrepresentable; adding a bridge means adding a phase and
  updating both predicates plus `destroy_data_plane_()`, and "does a bridge exist" is never
  answered by null-checking the bridge struct.
- A restart is cheap: no reboot, no topic churn, no HA entity loss, safe to request from a
  button callback. Discovery publishes and ERD publishes never interleave, and the
  steady-state line logs once per pause cycle.
- Reordering teardown, or starting the publisher before the throttle is set, silently
  reintroduces a use-after-free or an unthrottled first drain.

## Code anchors

- `geappliances_bridge.h` — `DataPlanePhase`, `DataPlaneReadiness`, `PublisherPausePhase`, `phase_has_subscription_bridge()`, `phase_has_polling_bridge()`, `restart_appliance_connection()`, `restart_requested_`, `ReconnectApplianceButton`.
- `geappliances_bridge.cpp` — `loop()`, `teardown()`, `destroy_data_plane_()`, `restart_appliance_connection_now_()`, `update_publisher_state_()`, `check_steady_state()`, `init_erd_cache_publisher_()`, `discovery_is_ready_()`.
- `geappliances_bridge_bridge_init.cpp` — `initialize_erd_bridge_()`, `maybe_start_custom_erd_polling_()`, `handle_subscription_failed()`, `handle_polling_failed()`.
- `erd_bridge_poll.cpp` / `erd_bridge_subscribe.cpp` — `erd_bridge_poll_destroy()`, `erd_bridge_subscribe_destroy()`; `erd_cache_mqtt_publisher.cpp` — `erd_cache_mqtt_publisher_start()`, `_pause()`, `_resume()`, `_first_round_done()`.
- `test/tests/startup_integration_test.cpp` — restart and phase-transition tests.
