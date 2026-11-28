# IBridgeServices — Specification

## 1. Overview

### 1.1 Purpose

`IBridgeServices` defines the contract between the startup state machine (`GeappliancesBridgeStartupHSM`) and the bridge implementation (`GeappliancesBridge`). It allows the HSM to drive startup phases — autodiscovery, device-ID reading, feature-bit reading, MQTT client initialization, and bridge initialization — without a compile-time dependency on `GeappliancesBridge`'s internals.

### 1.2 Responsibilities

- Declare every operation the startup HSM is allowed to invoke on the bridge
Provide phase-transition queries (`is_X_complete`, `is_X_failed`) so the HSM can check completion or failure without internal state knowledge
Provide phase-timing helpers (`record_*_phase_start`, `is_*_timed_out`) for delay-based transitions
- Group recurring-work dispatch methods so the running state stays clean
- Expose the current operating mode (`get_mode`) and subscription bridge state (`get_subscription_state`)

### 1.3 Not Responsible For

- Implementing any phase logic — `GeappliancesBridge` does that
- Owning any state — the interface is purely a contract for method invocation
- Anything not called by `geappliances_bridge_startup_hsm.cpp`

---

## 2. Interface

`IBridgeServices` is a pure abstract class. Every method is declared `virtual` with `= 0`. The single concrete implementation is `GeappliancesBridge`.

```cpp
class IBridgeServices {
 public:
  virtual ~IBridgeServices() = default;
  // ... all methods virtual = 0
};
```

The startup HSM holds a pointer to `IBridgeServices` and calls methods through the interface. The HSM wrapper (`startup_hsm_wrapper_t`) embeds a `tiny_hsm_t` and an `IBridgeServices*` pointer; the `services_from_hsm()` function uses `container_of` to recover the services pointer from the HSM pointer in state functions.
---

## 3. Method Groups

### 3.1 Autodiscovery

| Method | Description |
|--------|-------------|
| `run_autodiscovery()` | Run one tick of the autodiscovery manager. |
| `is_autodiscovery_complete()` | Returns `true` once autodiscovery has found (or given up on) an appliance. |
| `get_discovered_host_address()` | Host address of the discovered appliance. Valid when `is_autodiscovery_complete()` returns `true`. |
| `is_discovered_gea2_protocol()` | Returns `true` if the discovered appliance uses the GEA2 protocol. |

### 3.2 Device ID

| Method | Description |
|--------|-------------|
| `init_device_id_reading()` | Begin reading device-identification ERDs. Idempotent — safe to call multiple times. |
| `is_device_id_complete()` | Returns `true` once device-ID reading is complete. |

### 3.3 MQTT Client

| Method | Description |
|--------|-------------|
| `is_mqtt_client_initialized()` | Returns `true` if the MQTT client has been initialized. |
| `initialize_mqtt_client()` | Initialize the MQTT client. Idempotent — safe to call multiple times. |

### 3.4 Feature Bits

| Method | Description |
|--------|-------------|
| `start_feature_bit_reading()` | Start the feature-bit ERD read sequence. |
| `is_feature_bits_complete()` | Returns `true` once all feature bits have been read and parsed. |

### 3.5 Bridge Init

| Method | Description |
|--------|-------------|
| `is_bridge_initialized()` | Returns `true` if the ERD bridge (polling or subscription) has been initialized. |
| `initialize_erd_bridge()` | Initialize the ERD bridge (polling or subscription mode). |

### 3.6 Operating Mode

| `get_mode()` | Returns the current `BridgeMode` (POLL, SUBSCRIBE, or AUTO). |
| `get_subscription_state()` | Returns the current subscription bridge state as a `subscription_state_t` enum: `subscription_state_none`, `subscription_state_subscribing`, `subscription_state_subscribed`, `subscription_state_steady`, or `subscription_state_failed`. Callers derive `is_subscription_mode_active` from this: active when the value is not `subscription_state_none` and not `subscription_state_failed`. |
| `get_polling_state()` | Returns the current polling bridge state as a `polling_state_t` enum: `polling_state_none`, `polling_state_probing`, `polling_state_polling`, or `polling_state_failed`. Returns `polling_state_none` when the polling bridge has not been initialized. |
### 3.7 Startup Delay

| Method | Description |
|--------|-------------|
| `record_startup_delay_start()` | Record the timestamp when the startup delay phase begins. |
| `is_startup_delay_elapsed()` | Returns `true` if the configured startup delay has elapsed. |

### 3.8 Recurring Tasks

| Method | Description |
|--------|-------------|
| `handle_subscription_failed()` | Called when the subscription bridge enters the failed state; triggers fallback to polling mode in AUTO mode. |
| `handle_polling_failed()` | Called when the polling bridge enters the failed state; cleans up the polling bridge in dual-bridge mode or logs in POLL-only mode. |
| `maybe_start_custom_erd_polling()` | Start custom-ERD polling bridge if conditions are met. Idempotent. |
| `log_poll_state_transitions()` | Log any pending polling-bridge state-name transitions. |
| `check_steady_state()` | Check if the appliance-side data path has reached steady-state operation. Non-const: sets the steady-state flag and logs on first transition. Returns `true` only on the first call that detects steady state. |
### 3.9 ERD Cache Publisher

| Method | Description |
|--------|-------------|
| `initialize_erd_cache_publisher()` | Initialize the ERD cache MQTT publisher. Idempotent. |
| `is_erd_cache_publisher_initialized()` | Returns `true` if the ERD cache publisher has been initialized. |

---

## 4. State Machine

The interface itself has no state machine. It is a passive contract — the startup HSM drives state transitions by calling methods on the interface and checking completion queries. The state machine lives in `GeappliancesBridgeStartupHSM`, which uses `IBridgeServices` as its dependency.

The HSM startup sequence is:

```
startup_delay
  └─ autodiscovery
      └─ device_id
          └─ mqtt_client_init
              └─ feature_bits
                  └─ bridge_init
                      └─ erd_cache_publisher_init
                          └─ running (recurring tasks)
```

Each phase completes when the corresponding `is_X_complete()` or `is_X_initialized()` returns `true`.

---

## 5. Data Structures

The interface declares no data members. All state is owned by `GeappliancesBridge`.

The HSM wrapper struct embeds the services pointer:

```cpp
typedef struct {
  tiny_hsm_t hsm;
  IBridgeServices* services;
} startup_hsm_wrapper_t;
```

The `services_from_hsm()` function uses `container_of` to recover the `IBridgeServices*` from the `tiny_hsm_t*` passed to state functions.
---

## 6. Invariants

1. **No state ownership:** `IBridgeServices` owns no state. It is a pure interface — all data lives in `GeappliancesBridge`.
2. **Idempotent init methods:** `init_device_id_reading()`, `initialize_mqtt_client()`, and `initialize_erd_cache_publisher()` are safe to call multiple times. They guard against re-initialization internally.
3. **Back-pointer via `container_of`:** The HSM recovers the `IBridgeServices*` from the embedded `tiny_hsm_t*` using `services_from_hsm()`, which uses the `container_of` pattern on `startup_hsm_wrapper_t`. No global state is needed.
4. **Const correctness:** All query methods (`is_*`, `get_*`) are `const` — they do not mutate state.
5. **Single implementation:** Only `GeappliancesBridge` implements `IBridgeServices`. The interface is not designed for multiple implementations.

---

## 7. Dependencies
| Dependency | Role |
|------------|------|
| `bridge_mode.h` | `BridgeMode` enum (POLL, SUBSCRIBE, AUTO). Shared type to avoid circular dependency between `IBridgeServices` and `GeappliancesBridge`. |
| `erd_bridge_common.h` | `subscription_state_t` and `polling_state_t` enums used by `get_subscription_state()` and `get_polling_state()`. |

---

## 8. Known Limitations

1. **Single implementation:** The interface is designed for exactly one implementation. Adding a second implementation would require adjusting the `startup_hsm_wrapper_t` pattern, though the `container_of` approach already supports any type embedding the HSM.
2. **No compile-time enforcement of phase order:** The HSM enforces phase ordering at runtime. The interface itself does not prevent calling methods out of order (e.g., calling `initialize_erd_bridge()` before `is_autodiscovery_complete()`).
3. **Non-const `check_steady_state()`:** This is the only non-const method in the recurring tasks group; it sets the steady-state flag and logs on first transition.

---

## 9. Notes

What an implementation MUST guarantee when a `virtual … = 0` declaration cannot say it. Verified against the
only implementation, `GeappliancesBridge` (`geappliances_bridge.cpp`, `geappliances_bridge_bridge_init.cpp`);
the number in parentheses is the comment-audit id.

### 9.1 Idempotent starters — and what the guard actually is

- `init_device_id_reading()` guards on `device_identity_manager_.get_state() != DEVICE_ID_STATE_COMPLETE`.
  It is idempotent **only once the read sequence has completed**: a repeat call while reads are in flight
  re-inits `DeviceIdentityManager` and restarts them. §3.2 and §6.2 state the flat "safe to call multiple
  times" without that condition. (1557)
- `initialize_mqtt_client()` guards on `mqtt_client_adapter_.initialized`. It is not a pure initializer: when
  no ERD client is active and no GEA3 UART exists (manual `device_id` on a GEA2-only bridge) it also calls
  `protocol_stack_.set_gea2_active(true)` so the GEA2 tight loop runs before autodiscovery would. (1558)
- `initialize_erd_cache_publisher()` uses `erd_cache_publisher_.cache != nullptr` as its "already
  initialized" flag — the same field `teardown()` dispatches on (ADR-0016). A second call must be a no-op:
  the first one starts the Core 0 publisher task (ADR-0005) and then calls `ha_discovery_manager_init()`,
  which frees the buffers a live discovery run holds. Ordering inside the first call is load-bearing:
  `erd_cache_set_throttle_rate_seconds()` is applied before `erd_cache_mqtt_publisher_init()` because the
  ESP-IDF publisher task starts inside init and could otherwise drain cache entries before the rate limit
  takes effect. (1568)
- `maybe_start_custom_erd_polling()` starts only when `custom_erds_count_ != 0`,
  `data_plane_phase_ == DataPlanePhase::kSubscription`, and `get_subscription_state() ==
  subscription_state_steady`. Its idempotency comes from the phase: starting moves it to
  `kSubscriptionCustomPolling`, so the guard rejects every later call. Waiting for subscription steady state
  is deliberate — the subscription bridge publishes its ERDs first, so already-covered ERDs are not polled
  redundantly. (1560)

### 9.2 Self-driving sequences: the HSM polls a completion query, it does not drive reads

- `start_feature_bit_reading()` must tolerate repeated calls, and two guards make that safe: it returns when
  `feature_bit_manager_.is_initialized()` (the manager holds an ERD client; `cleanup()` clears that pointer,
  which re-arms the guard for a later re-init), and it returns when
  `autodiscovery_manager_.get_active_erd_client()` is null so the next `loop()` retries instead of sticking
  permanently. `FeatureBitManager::start()` then queues the first read only at `FEATURE_BIT_STATE_READING`
  with `reading_idx_ == 0` and `read_queued_ == false`; after that the manager advances on
  `tiny_gea3_erd_client_on_activity`, so no further call is needed. (1559)
- `run_autodiscovery()` is called once, on entry to the autodiscovery state, and
  `AutodiscoveryManager::start()` returns unless the state is `AUTODISCOVERY_IDLE` and at least one UART is
  configured. The broadcast/wait cycle then runs on the manager's own timer callback. With
  `board_address_configured_` the call sets a target address first (targeted discovery, ADR-0011).
- `initialize_erd_bridge()` is called from the HSM's `run_loop` only when `!is_bridge_initialized() &&
  is_autodiscovery_complete()`, and the phase does not transition there — it waits for `signal_bridge_ready`
  from the polling bridge.

### 9.3 Completion queries are terminal; one of them reports failure

- `is_autodiscovery_complete()` is `get_state() == AUTODISCOVERY_COMPLETE`, and that state is reached only
  from `timer_callback_()` with a non-null `active_erd_client_`. `AutodiscoveryState` has no failure state and
  `schedule_next_broadcast_()` re-arms the GEA3/GEA2 broadcasts indefinitely, so the "(or given up on)"
  wording in §3.1 is wrong: the phase has no timeout and its only exit is a board answering. (1555)
- `get_discovered_host_address()` returns `host_address_`, which `init()`/reset set to `0` and which stays 0
  until a board answers — an unusable value, not a sentinel. Callers MUST gate on
  `is_autodiscovery_complete()`; the HSM reads it only in that branch. (1556)
- `is_feature_bits_complete()` returns true for `FEATURE_BIT_STATE_COMPLETE` **or**
  `FEATURE_BIT_STATE_FAILED`: a parse failure is terminal and the bridge falls back to full polling. It is
  declared `const` but logs the failure once through `mutable bool feature_bit_failure_logged_`, so §6.4's
  "all query methods are const" does not mean side-effect free. (no comment carried this)

### 9.4 Failure handlers are edge-triggered and mode-gated

- `handle_subscription_failed()` returns unless `mode_ == BRIDGE_MODE_AUTO` — in POLL mode there is no
  subscription bridge to fall back from. It destroys the subscription bridge, destroys any existing polling
  bridge before re-initializing (otherwise the polling bridge's heap allocations leak), then
  `init_polling_bridge_(true)` and `data_plane_phase_ = DataPlanePhase::kPolling`. It must not perform the
  state transition: the startup HSM's `subscription_watch` handler owns the transition to running right after
  calling this. (1561, 1562)
- `handle_polling_failed()` returns unless `get_polling_state() == polling_state_failed`. With a subscription
  bridge in the current phase it destroys the polling bridge and drops back to `kSubscription`; in POLL-only
  mode it leaves the bridge failed, where the `appliance_lost` handler in `state_failed` re-probes. Both
  handlers reset `last_logged_poll_state_` so the state-name logging does not compare against a stale value.
  (1563, 1564)

### 9.5 `check_steady_state()` is a one-shot edge, not a level

- Non-const by design: `data_plane_readiness_ == DataPlaneReadiness::kNotSteady` is part of the predicate and
  the method sets it to `kSteady`, so the first detecting call returns `true` and every later call returns
  `false`. Callers use that edge to print the steady-state summary exactly once; treating the return as a
  level would print it on every loop. Steady means every bridge present in `data_plane_phase_` is in its own
  steady state (`subscription_state_steady`, `polling_state_polling`) and the phase is not `kIdle`.
  (1565, 1566, 1567)
