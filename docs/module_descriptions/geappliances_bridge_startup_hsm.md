# GeappliancesBridge Startup HSM

## Purpose

Hierarchical state machine that drives the linear startup sequence of the GE Appliances bridge. Replaces the previous manual switch-based phase progression with a proper `tiny_hsm`-based state machine where each phase handles its own entry/exit logic and waits for signals from managers before transitioning.

## Public API

| Function | Description |
|----------|-------------|
| `startup_hsm_wrapper_init(wrapper, services, initial)` | Initialize the wrapper struct with bridge services and initial state |
| `services_from_hsm(hsm)` | Retrieve the `IBridgeServices` pointer from an HSM pointer via `container_of` |
| `startup_hsm_wrapper_destroy(wrapper)` | Null the services pointer in the wrapper |

## Signals

| Signal | Description |
|--------|-------------|
| `signal_run_loop` | Drive ongoing work in the current state (sent every `loop()`); the only driving signal — each state polls its own completion predicate here |
| `signal_bridge_ready` | ERD discovery finished (polling bridge) or subscription bridge initialized; the one event-driven transition |

## State Machine

Flat hierarchy — all states defer unhandled signals to `startup_state_top`, which consumes them.

```
startup_state_top (root — handles entry/exit, defers all other signals)
  │
  ├─ startup_state_startup_delay
  │    ├─ entry: record_startup_delay_start()
  │    └─ run_loop: if delay elapsed → autodiscovery
  │
  ├─ startup_state_autodiscovery
  │    └─ run_loop: run AutodiscoveryManager (retries indefinitely); if complete → device_id
  │
  ├─ startup_state_device_id
  │    ├─ entry: init DeviceIdentityManager
  │    └─ run_loop: check device ID complete → mqtt_client_init
  │
  ├─ startup_state_mqtt_client_init
  │    └─ entry: init MQTT adapter, init ERD cache publisher, start feature bit reading → feature_bits
  │
  ├─ startup_state_feature_bits
  │    ├─ run_loop: check feature bits complete (COMPLETE or FAILED) → bridge_init
  │
  ├─ startup_state_bridge_init
  │    ├─ run_loop: if autodiscovery complete, init bridge
  │    └─ bridge_ready → subscription_watch
  │
  ├─ startup_state_subscription_watch
  ├─ non-AUTO modes: skip to running
  ├─ AUTO mode: monitor subscription activity, fall back to polling
  └─ fallback or non-AUTO → running
  │
  └─ startup_state_running (steady state)
       └─ run_loop: run all managers (autodiscovery, device identity,
           feature bits), check subscription activity
           (AUTO mode), maybe start custom ERD polling, log poll state
           transitions
```

## Dependencies

- `tiny_hsm` — hierarchical state machine framework
- `IBridgeServices` — abstract contract implemented by `GeappliancesBridge`; the HSM is isolated from the concrete class (accessed via `services_from_hsm()`)

## Key Design Decisions

- **container_of instead of global back-pointer**: The HSM is embedded in `startup_hsm_wrapper_t` alongside the `IBridgeServices*` pointer. `services_from_hsm()` uses `container_of` to recover the wrapper from the HSM pointer, making the HSM reentrant and testable. Same pattern as `erd_write_bridge_t` and `erd_bridge_poll_t`.
- **Flat hierarchy**: All states are children of `startup_state_top` with no intermediate parent states. This simplifies signal routing — any unhandled signal bubbles to the top and is deferred.
- **Polled transitions**: `signal_run_loop` is the only driving signal — each state polls its `IBridgeServices` completion predicate, so every transition has exactly one trigger. The sole exception is `signal_bridge_ready` (bridge_init → subscription_watch).
- **Feature bits completion gate**: The `feature_bits` state transitions on `is_feature_bits_complete()` returning true, which covers both `FEATURE_BIT_STATE_COMPLETE` (feature filtering succeeded) and `FEATURE_BIT_STATE_FAILED` (falls back to full polling).

## Testing

Covered by integration tests in `test/tests/` through the full startup sequence. State transitions are tested implicitly as the bridge progresses through each phase.