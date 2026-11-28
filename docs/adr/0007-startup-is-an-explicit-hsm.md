# ADR-0007: Startup sequencing is an explicit hierarchical state machine

Status: accepted (2026-11-27)
Related: ADR-0001, commit `f97d544`, [startup HSM spec](../spec/geappliances_bridge_startup_hsm.md), [startup sequence](../architecture/startup-sequence.md), [i_bridge_services](../spec/i_bridge_services.md)

## Context

Startup is eight ordered stages, each owned by a different manager: board stabilization delay, GEA bus autodiscovery, device-identity ERD reads, MQTT client + cache-publisher init, feature-bit parsing, ERD bridge init, subscription watch, running. Completion is observable only through a predicate on each owner, several owners can complete synchronously inside their own `init()`, and all of it runs in ESPHome's single-threaded `loop()`. A cluster of booleans across `GeappliancesBridge` cannot answer "which stage am I in" without every stage re-testing the flags of the stages before it, and it lets one transition be triggered from two places.

## Decision

1. One `tiny_hsm` state per startup phase in a flat hierarchy under `startup_state_top`, declared by `startup_hsm_state_descriptors` and exported as `startup_hsm_configuration`.
2. Transitions are predicate-polled: `GeappliancesBridge::loop()` sends `signal_run_loop` once per iteration and each state's `run_loop` handler polls the `IBridgeServices` completion predicate that owns its transition. There is deliberately **no** per-phase completion signal — a duplicate event path lets the same transition be triggered two ways.
3. `signal_bridge_ready` is the single exception. The `bridge_init` → `subscription_watch` transition fires when ERD discovery finishes, and no predicate describes "discovery finished": `is_bridge_initialized()` goes true before the probe phase completes in polling mode. `startup_state_bridge_init`'s `run_loop` handler MUST NOT transition after calling `initialize_erd_bridge()`; it waits for `signal_bridge_ready`, fired by `on_poll_discovery_complete_()` in polling mode and directly by `initialize_erd_bridge_()` in subscription mode, which has no discovery phase.
4. The HSM never reaches into `GeappliancesBridge` internals: the `IBridgeServices` wrappers are its only entry points, each delegating to an existing internal method or member. `startup_hsm_wrapper_t` embeds the `tiny_hsm_t` plus the services pointer and `services_from_hsm()` recovers it with `container_of`, as `erd_write_bridge_t` and `erd_bridge_poll_t` do.
5. `startup_state_top` consumes every signal its children leave unconsumed; a signal deferred at the root is lost, not queued.
6. Phase rules: `startup_state_startup_delay` waits `AUTODISCOVERY_STARTUP_DELAY_MS` (10 000 ms) so the appliance board can boot and answer broadcast requests; `startup_state_autodiscovery` has no timeout — `AutodiscoveryManager` retries broadcast probes indefinitely and the state leaves only on `is_autodiscovery_complete()`; `startup_state_device_id` transitions in its entry handler when a pre-configured `device_id` completed synchronously inside `init_device_id_reading()`; `startup_state_mqtt_client_init` MUST NOT wait for an MQTT connection; `startup_state_running`'s entry calls `check_steady_state()` immediately so the steady-state log fires even when the next `loop()` is delayed by the long probe phase that triggered the transition.
7. Data-plane ownership: `initialize_erd_bridge_()` sets `data_plane_phase_` (`kPolling`) **before** the probe starts, so `check_steady_state()` in the running entry sees the phase when the probe completes synchronously. The `subscription_watch` → `running` transition belongs to the HSM, so `handle_subscription_failed()` only rebuilds the data path.
8. Failure recovery is split: a failed custom-ERD polling bridge alongside a subscription bridge is destroyed and the phase falls back to `DataPlanePhase::kSubscription`; a failed primary polling bridge is left failed, because re-probing is the polling bridge's own job — `state_failed` in `erd_bridge_poll.cpp` handles `signal_appliance_lost` (from the `appliance_lost_timeout` = 60 000 ms timer), restores `known_host_address`, clears `polling_failure_count` and transitions to `state_probe_list`.

## Consequences

- Adding a phase means a state plus an `IBridgeServices` predicate, never a new signal; a second completion path for an existing phase is a bug.
- Startup has no overall timeout by design: a silent appliance parks the machine in `startup_state_autodiscovery` and keeps retrying.
- Tests drive the machine by sending `signal_run_loop` against fake predicates; nothing may depend on a phase-completion event.
- Nothing in `GeappliancesBridge` may re-derive "did startup finish" from member flags: phase state lives in the HSM plus `data_plane_phase_`.

## Code anchors

`geappliances_bridge_startup_hsm.h` (`signal_run_loop`, `signal_bridge_ready`, `startup_hsm_wrapper_t`, `services_from_hsm`, `startup_hsm_configuration`); `geappliances_bridge_startup_hsm.cpp` (`startup_state_top`, `startup_state_startup_delay`, `startup_state_autodiscovery`, `startup_state_device_id`, `startup_state_mqtt_client_init`, `startup_state_feature_bits`, `startup_state_bridge_init`, `startup_state_subscription_watch`, `startup_state_running`, `startup_hsm_state_descriptors`); `geappliances_bridge.cpp` (`loop()`, the `IBridgeServices` wrapper block, `check_steady_state()`); `geappliances_bridge_bridge_init.cpp` (`initialize_erd_bridge_()`, `on_poll_discovery_complete_()`, `handle_polling_failed()`, `handle_subscription_failed()`, `data_plane_phase_`); `geappliances_bridge_constants.h` (`AUTODISCOVERY_STARTUP_DELAY_MS`); `erd_bridge_common.h` (`appliance_lost_timeout`); `erd_bridge_poll.cpp` (`state_failed`); `test/tests/startup_hsm_test.cpp`.
