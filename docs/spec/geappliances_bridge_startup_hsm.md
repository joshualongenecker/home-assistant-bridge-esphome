# Geappliances Bridge Startup HSM — Specification

## 1. Overview

### 1.1 Purpose

The startup HSM drives the ordered startup sequence of the GE Appliances bridge from protocol initialization to steady-state running. It uses `tiny_hsm` to manage a flat hierarchy of phase states, each of which waits for specific signals or gate conditions before transitioning to the next phase.

### 1.2 Responsibilities

- Own the `tiny_hsm` state machine for all startup phases
- Transition between phases when each manager signals completion
- Call `IBridgeServices` to trigger bridge actions at phase boundaries
- Enforce per-phase timeout guards

### 1.3 Not Responsible For

- Implementing any phase's work (delegates to managers via `IBridgeServices`)
- Owning component instances or configuration state
- Any steady-state work beyond the `running` phase entry

---

## 2. Public API

### 2.1 Initialize HSM Wrapper

```cpp
void startup_hsm_wrapper_init(startup_hsm_wrapper_t* self, IBridgeServices* services,
  tiny_hsm_state_t initial);
```

Initializes the `startup_hsm_wrapper_t` struct with the given `IBridgeServices` pointer and initial state. Uses the `container_of` pattern (same as `erd_write_bridge_t`, `erd_bridge_poll_t`) to embed the HSM and services pointer in one struct, making the HSM reentrant and testable. Called once during bridge `loop()` before the HSM is started.

### 2.2 Retrieve Services from HSM

```cpp
IBridgeServices* services_from_hsm(tiny_hsm_t* hsm);
```

Recovers the `IBridgeServices` pointer from an HSM pointer using `container_of`. The `hsm` parameter is the embedded `tiny_hsm_t` within a `startup_hsm_wrapper_t`; `container_of` computes the wrapper address and returns its `services` member.

### 2.3 Destroy HSM Wrapper

```cpp
void startup_hsm_wrapper_destroy(startup_hsm_wrapper_t* self);
```

Nulls the `services` pointer in the wrapper. Called during bridge teardown.

### 2.4 HSM Configuration

```cpp
extern const tiny_hsm_configuration_t startup_hsm_configuration;
```

Defines the state descriptors and parent hierarchy for the startup HSM. All states have `startup_state_top` as their parent.

---

## 3. Signals

| Signal | Description |
|--------|-------------|
| `signal_run_loop` | Sent every `loop()` call to drive ongoing work in the current state. The primary mechanism for polling completion flags and advancing phases. |
| `signal_autodiscovery_complete` | Fired when the AutodiscoveryManager finds (or gives up on) an appliance. |
| `signal_device_id_complete` | Fired when the DeviceIdentityManager has read or pre-configured the device ID. |
| `signal_mqtt_connected` | Fired when the MQTT broker connection is established. |
| `signal_feature_bits_complete` | Fired when all feature bit ERDs are read and parsed. |
| `signal_bridge_ready` | Fired when the ERD bridge (poll/subscribe) finishes initialization. |
| `signal_subscription_fallback` | Fired in AUTO mode when the subscription watchdog times out and falls back to polling. |

---

## 4. State Machine

Flat hierarchy — all states are children of `startup_state_top`. Any signal not consumed by a child state bubbles to the top and is deferred (ignored).

### 4.1 Parent State: `startup_state_top`

Handles `entry` and `exit` signals with no action. All other signals return `tiny_hsm_result_signal_consumed`. This is the root of the hierarchy — unhandled signals from any child state end here and are consumed.

### 4.2 Phase States

#### `startup_state_startup_delay`

Waits for the appliance board to stabilize before starting autodiscovery. Duration is the configured `startup_delay` (5 seconds by default).

- **On entry:** Calls `svc->record_startup_delay_start()` to record the start time.
- **On `signal_run_loop`:** Checks `svc->is_startup_delay_elapsed()`. If elapsed, transitions to `startup_state_autodiscovery`.
- **On exit:** No action.

#### `startup_state_autodiscovery`

Runs the AutodiscoveryManager, which is fully self-driving (owns its own timers and event subscriptions). The HSM only checks for completion.

- **On entry:** Logs phase entry. Calls `svc->run_autodiscovery()` to start the manager.
- **On `signal_run_loop`:** Checks `svc->is_autodiscovery_complete()`. If true, logs the discovered host address and protocol, transitions to `startup_state_device_id`.
- **On `signal_autodiscovery_complete`:** Checks `svc->is_autodiscovery_complete()` and transitions to `startup_state_device_id` if true. This provides an immediate transition path without waiting for the next loop iteration.
- **On exit:** No action.

**Note:** The AutodiscoveryManager retries indefinitely if no board responds. This state will not transition until a valid board address is found.

#### `startup_state_device_id`

Runs the DeviceIdentityManager to read the appliance identity ERDs. Supports both runtime reading and pre-configured device IDs.

- **On entry:** Calls `svc->init_device_id_reading()`. If a device ID is pre-configured and the manager completes synchronously during init, transitions immediately to `startup_state_mqtt_client_init`.
- **On `signal_run_loop`:** Checks `svc->is_device_id_complete()`. If true, transitions to `startup_state_mqtt_client_init`.
- **On `signal_device_id_complete`:** Transitions to `startup_state_mqtt_client_init`.
- **On exit:** No action.

#### `startup_state_mqtt_client_init`

Initializes the MQTT client adapter with the device ID and starts feature bit reading. This phase is fast — it does not wait for MQTT connection.

- **On entry:**
  - If MQTT client is not initialized, calls `svc->initialize_mqtt_client()`.
  - If ERD cache publisher is not initialized, calls `svc->initialize_erd_cache_publisher()`.
  - Calls `svc->start_feature_bit_reading()`.
  - Transitions to `startup_state_feature_bits`.
- **On exit:** No action.

#### `startup_state_feature_bits`

Waits for feature bit ERDs to be read and parsed. Transitions when `is_feature_bits_complete()` returns true (either `FEATURE_BIT_STATE_COMPLETE` or `FEATURE_BIT_STATE_FAILED`).

- **On entry:** Logs phase entry.
- **On `signal_run_loop`:** Checks `svc->is_feature_bits_complete()`. If true, transitions to `startup_state_bridge_init`.
- **On `signal_mqtt_connected`:** Checks `svc->is_feature_bits_complete()`. If true, transitions to `startup_state_bridge_init`.
- **On `signal_feature_bits_complete`:** Transitions to `startup_state_bridge_init`.
- **On exit:** No action.

**Note:** The implementation transitions as soon as feature bits are complete or failed, regardless of MQTT connection status. `is_feature_bits_complete()` returns true for both `FEATURE_BIT_STATE_COMPLETE` and `FEATURE_BIT_STATE_FAILED` (the latter triggers fallback to full polling). The `signal_mqtt_connected` handler provides an additional transition path if MQTT connects before feature bits finish.

#### `startup_state_bridge_init`

Initializes the ERD bridge (poll or subscribe). Waits for autodiscovery to be complete before starting bridge initialization.

- **On entry:** Logs phase entry.
- **On `signal_run_loop`:** If the bridge is not initialized and autodiscovery is complete, calls `svc->initialize_erd_bridge()`. Does NOT transition — waits for `signal_bridge_ready` from the polling bridge when ERD discovery is complete.
- **On `signal_bridge_ready`:** Transitions to `startup_state_subscription_watch`.
- **On exit:** No action.

#### `startup_state_subscription_watch`

In AUTO mode, monitors subscription activity and falls back to polling if no activity is detected. In poll/subscribe modes, this is a pass-through phase.

- **On entry:** If the mode is not `BRIDGE_MODE_AUTO`, calls `svc->maybe_start_custom_erd_polling()` and transitions immediately to `startup_state_running`.
- **On `signal_run_loop`:**
  - Checks `svc->get_subscription_state()`. If `subscription_state_failed`, calls `svc->handle_subscription_failed()` and transitions to `startup_state_running`.
  - Calls `svc->log_poll_state_transitions()`, `svc->handle_polling_failed()`, and `svc->maybe_start_custom_erd_polling()`.
  - If not AUTO mode or subscription is not active, transitions to `startup_state_running`.
- **On `signal_subscription_fallback`:** Transitions to `startup_state_running`.
- **On exit:** No action.


#### `startup_state_running` (terminal)

Steady-state operation. All recurring tasks run every loop iteration.

- **On entry:** Logs that the bridge is in steady-state operation.
- **On `signal_run_loop`:**
  - Checks `svc->get_subscription_state()`. If `subscription_state_failed`, calls `svc->handle_subscription_failed()`.
  - Calls `svc->handle_polling_failed()`, `svc->log_poll_state_transitions()`, and `svc->maybe_start_custom_erd_polling()`.
- **On exit:** No action.

### 4.3 State Diagram

```
startup_state_top (root — consumes all unhandled signals)
  │
  ├─ startup_state_startup_delay
  │    ├─ entry: record_startup_delay_start()
  │    ├─ run_loop: if delay elapsed → startup_state_autodiscovery
  │    └─ exit: —
  │
  ├─ startup_state_autodiscovery
  │    ├─ entry: run_autodiscovery()
  │    ├─ run_loop: if autodiscovery complete → startup_state_device_id
  │    ├─ autodiscovery_complete: if complete → startup_state_device_id
  │    └─ exit: —
  │
  ├─ startup_state_device_id
  │    ├─ entry: init_device_id_reading(); if complete → startup_state_mqtt_client_init
  │    ├─ run_loop: if device ID complete → startup_state_mqtt_client_init
  │    ├─ device_id_complete: → startup_state_mqtt_client_init
  │    └─ exit: —
  │
  ├─ startup_state_mqtt_client_init
  │    ├─ entry: initialize_mqtt_client(); initialize_erd_cache_publisher();
  │    │         start_feature_bit_reading() → startup_state_feature_bits
  │    └─ exit: —
  │
  ├─ startup_state_feature_bits
  │    ├─ entry: —
  │    ├─ run_loop: if feature bits complete/failed → startup_state_bridge_init
  │    ├─ mqtt_connected: if feature bits complete/failed → startup_state_bridge_init
  │    ├─ feature_bits_complete: → startup_state_bridge_init
  │    └─ exit: —
  │
  ├─ startup_state_bridge_init
  │    ├─ entry: —
  │    ├─ run_loop: if not initialized + autodiscovery complete → initialize_erd_bridge()
  │    ├─ bridge_ready: → startup_state_subscription_watch
  │    └─ exit: —
  │
  ├─ startup_state_subscription_watch
  │    ├─ entry: if not AUTO → maybe_start_custom_erd_polling(), → startup_state_running
  │    ├─ run_loop: if failed → handle_subscription_failed() → running;
  │    │            log_poll_state_transitions(), handle_polling_failed(),
  │    │            maybe_start_custom_erd_polling(); if not active → running
  │    ├─ subscription_fallback: → startup_state_running
  │    └─ exit: —
  │
  └─ startup_state_running (terminal)
       ├─ entry: —
       ├─ run_loop: if failed → handle_subscription_failed();
       │             handle_polling_failed(), log_poll_state_transitions(),
       │             maybe_start_custom_erd_polling()
       └─ exit: —
```

---

## 5. Data Structures

The startup HSM uses a wrapper struct to hold both the HSM and the bridge services pointer:

- **`startup_hsm_wrapper_t`**: Struct with `tiny_hsm_t hsm` and `IBridgeServices* services`, using `container_of` in `services_from_hsm()` to recover the wrapper from the HSM pointer. Same pattern as `erd_write_bridge_t` and `erd_bridge_poll_t`.
- **`startup_hsm_configuration`**: Static configuration struct with state descriptors and parent hierarchy.
- **`startup_hsm_state_descriptors[]`**: Array of 10 `tiny_hsm_state_descriptor_t` entries, each mapping a state function to its parent (`startup_state_top`).

Each state function receives `tiny_hsm_t* hsm`, `tiny_hsm_signal_t signal`, and `const void* data`. The `hsm` pointer is used for transitions (`tiny_hsm_transition()`) and for retrieving services via `services_from_hsm()`, which uses `container_of` to recover the wrapper and its `services` member.

1. **Flat hierarchy:** All states are direct children of `startup_state_top`. No intermediate parent states exist. This simplifies signal routing — any unhandled signal bubbles to the top and is deferred.

2. **Signal-driven transitions:** Each state handles specific signals for transition triggers. The `signal_run_loop` signal drives ongoing work and polls completion flags. Manager-specific signals (`signal_autodiscovery_complete`, `signal_device_id_complete`, etc.) provide immediate transition paths without waiting for the next loop iteration.

3. **container_of for IBridgeServices:** The HSM accesses bridge operations through `services_from_hsm()`, which uses `container_of` to recover the `IBridgeServices` pointer from the embedded HSM pointer. This decouples the HSM from the concrete `GeappliancesBridge` class at compile time and makes the HSM reentrant — each wrapper instance carries its own services pointer.

4. **Feature bits completion gate:** The `feature_bits` state accepts three transition triggers — `signal_run_loop` (polls feature bits completion), `signal_mqtt_connected` (checks feature bits completion), and `signal_feature_bits_complete` (unconditional transition). `is_feature_bits_complete()` returns true for both COMPLETE and FAILED states.

5. **Linear progression:** States transition forward only. There is no mechanism to go back to a previous phase. Once a phase completes, it cannot be re-entered.

6. **Terminal state:** `startup_state_running` is the terminal state. Once entered, the HSM remains there for the lifetime of the bridge.

---

## 6. Dependencies

| Dependency | Role |
|------------|------|
| `tiny_hsm` | Hierarchical state machine framework (state transitions, signal dispatch, parent hierarchy) |
| `IBridgeServices` | Abstract contract implemented by `GeappliancesBridge`; the HSM invokes bridge operations through this interface without compile-time dependency on the concrete class |
| ESPHome `mqtt::global_mqtt_client` | MQTT connection state check (used by `IBridgeServices` implementations in feature_bits / bridge_init states) |
| `geappliances_bridge_constants.h` | Default startup-delay constant and bridge mode enum (`BRIDGE_MODE_AUTO`) |

---

## 7. Known Limitations

1. **Linear sequence with no rollback:** Once a phase transitions to the next, there is no mechanism to go back. If a later phase discovers an issue (e.g., MQTT disconnects after bridge init), the HSM does not re-enter earlier phases. Recovery is handled by individual managers within the running state.

2. **Per-instance services via container_of:** Each `startup_hsm_wrapper_t` carries its own `IBridgeServices` pointer. This makes the HSM reentrant — multiple bridge instances can each have their own wrapper. The `container_of` pattern is the same one used by `erd_write_bridge_t` and `erd_bridge_poll_t`.

3. **No phase timeout for autodiscovery:** The autodiscovery phase has no timeout — it retries indefinitely until a board is found. This is intentional (the bridge cannot function without an identified appliance), but it means the HSM will stall here if no appliance is present.

4. **No timeout for startup delay:** The 10-second startup delay is fixed and not configurable. If the appliance board needs more or less time, this value must be changed at compile time.

5. **No phase timeout for feature bits:** The feature bits phase has no timeout. If ERD 0x0092 (Common Feature API) fails, the manager transitions to `FEATURE_BIT_STATE_FAILED` and the bridge falls back to full polling. If other ERDs fail, they are skipped and the manager continues to the next one.
