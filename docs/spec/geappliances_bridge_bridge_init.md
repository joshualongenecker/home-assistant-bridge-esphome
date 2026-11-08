# geappliances_bridge_bridge_init — Specification

## 1. Overview

### 1.1 Purpose

Own every one-time initialization step for the MQTT client adapter and the bridge HSMs (polling, subscription, write), plus feature-bit reading startup and the AUTO-mode subscription-activity watchdog. This module implements the bridge initialization phases driven by the startup HSM through the `IBridgeServices` interface.

### 1.2 Responsibilities

- **Phase 4 — MQTT client adapter initialization** (`initialize_mqtt_client_`): bind the adapter to the device ID, wire the ERD registry, and detect GEA2-only configurations.
- **Feature-bit reading startup** (`start_feature_bit_reading_`): kick off the feature-bit ERD reads once the ERD client is available.
- **Phase 6 — ERD bridge initialization** (`initialize_erd_bridge_`): apply the valid-ERD filter, select the operating mode (poll / subscribe / auto), and initialize the appropriate bridge HSMs (polling, subscription, write).
- **Custom ERD polling** (`start_custom_erd_polling_`, `maybe_start_custom_erd_polling_`): start a secondary polling bridge for custom ERDs alongside the subscription bridge in AUTO mode.
- **Failure handling** (`handle_subscription_failed`, `handle_polling_failed`): handle bridge failures and trigger fallback to polling in AUTO mode.
- **Poll list construction** (`build_poll_list_`, `init_polling_bridge_`): build the ERD poll list and initialize the polling bridge with the correct probe list and callbacks.

### 1.3 Not Responsible For

- Autodiscovery (owned by `AutodiscoveryManager`).
- Device ID assembly (owned by `DeviceIdentityManager`).
- Feature-bit parsing and valid-ERD computation (owned by `FeatureBitManager`).
- Startup phase sequencing (owned by `StartupHsm` via `IBridgeServices`).
- MQTT connection lifecycle (owned by `EsphomeMqttClientAdapter`).
- GEA2/GEA3 protocol stack driving (owned by `run_protocol_stack_` in the main bridge file).

---

## 2. Interface

### 2.1 Class Membership

All methods are members of `GeappliancesBridge` (defined in `geappliances_bridge.h`). The public `IBridgeServices` interface delegates to the private implementations in this file.

### 2.2 IBridgeServices Delegation

| IBridgeServices Method | Implementation in this file |
|------------------------|-----------------------------|
| `initialize_mqtt_client()` | `initialize_mqtt_client_()` |
| `start_feature_bit_reading()` | `start_feature_bit_reading_()` |
| `initialize_erd_bridge()` | `initialize_erd_bridge_()` |
| `handle_subscription_failed()` | `handle_subscription_failed()` |
| `handle_polling_failed()` | `handle_polling_failed()` |
| `maybe_start_custom_erd_polling()` | `maybe_start_custom_erd_polling_()` |

**Note:** Most `IBridgeServices` methods delegate to `_`-suffixed private implementations (e.g., `initialize_mqtt_client` → `initialize_mqtt_client_`). The two failure handlers (`handle_subscription_failed`, `handle_polling_failed`) share the same name in both the interface and implementation, as they are called directly by the startup HSM without an intermediate private wrapper.
### 2.3 Internal Methods

| Method | Description |
|--------|-------------|
| `on_poll_discovery_complete_()` | Callback fired by the polling bridge when ERD discovery finishes; sends `signal_bridge_ready` to the startup HSM. |
| `init_polling_bridge_(bool log_as_info)` | Shared polling bridge initialization used by all three init paths (POLL, GEA2, AUTO fallback). |
| `start_custom_erd_polling_()` | Start the custom-ERD polling bridge alongside the subscription bridge. |
| `maybe_start_custom_erd_polling_()` | Guarded entry: starts custom ERD polling only after the subscription bridge reaches steady state. |

### 2.4 Free Function

| Function | Description |
|----------|-------------|
| `build_poll_list_(GeappliancesBridge*)` | Builds the poll list by delegating to `erd_poll_list_builder::build_erd_poll_list()`. |

---

## 3. Behavior

### 3.1 Poll List Construction

#### Requirement 3.1.1: Poll List Delegation

`build_poll_list_()` MUST construct an `ErdPollListConfig` from the bridge's current state and delegate to `build_erd_poll_list()`. The config MUST include the bridge mode, subscription active state, appliance API parsing flag, feature-bit valid ERDs, custom ERDs, and appliance type.

**Rationale:** Decoupling poll-list construction from bridge initialization allows the same logic to be used in three contexts: primary polling mode, custom ERD polling alongside subscription, and AUTO-mode fallback.

**Implementation:** `geappliances_bridge_bridge_init.cpp` lines 56–72. The function populates `ErdPollListConfig` from bridge member state and calls `build_erd_poll_list(config)`.

**Verification:** The poll list builder is tested independently; this function is verified by confirming the config fields are populated from the correct bridge members.


### 3.2 Polling Bridge Initialization

#### Requirement 3.2.1: Discovery-Complete Callback Wiring

The discovery-complete callback MUST be wired *before* the polling bridge is initialized, to prevent a race condition where discovery completes synchronously on first entry and fires the callback before it is set.

**Rationale:** If the probe list is small or the appliance responds immediately, discovery can complete during `erd_bridge_poll_init()`. Without the callback pre-wired, the signal to the startup HSM would be lost.

**Implementation:** `geappliances_bridge_bridge_init.cpp` lines 83–86. The lambda captures `ctx` (the bridge pointer) and calls `on_poll_discovery_complete_()`.

#### Requirement 3.2.2: Poll Probe List Copy

The poll list returned by `build_poll_list_()` MUST be copied into the bridge's fixed-capacity `poll_probe_list_` array, and the count stored in `poll_probe_list_count_`.

**Implementation:** `geappliances_bridge_bridge_init.cpp` lines 88–90. Uses `std::memcpy` to copy `result.erds_count` entries.

#### Requirement 3.2.3: Bridge Initialization Parameters

`erd_bridge_poll_init()` MUST receive: the polling bridge struct, the shared timer group, the active ERD client, the configured polling interval, the host address, the probe list, and the ERD cache.

**Implementation:** `geappliances_bridge_bridge_init.cpp` lines 98–107.

#### Requirement 3.2.4: Initialization Flag

`polling_bridge_initialized_` MUST be set to `true` after successful initialization.

**Implementation:** `geappliances_bridge_bridge_init.cpp` line 108.

### 3.3 Feature-Bit Reading Startup

#### Requirement 3.3.1: Guard Against Re-Initialization

`start_feature_bit_reading_()` MUST return immediately if the feature bit manager is not in `FEATURE_BIT_STATE_READING_0092`, or if `feature_bit_reading_started_` is already `true`.

**Rationale:** The startup HSM may call this method multiple times per loop iteration. The state check ensures the manager is at the correct phase; the flag prevents re-initialization while the first ERD read is in-flight (the manager stays in `READING_0092` during this time).

**Implementation:** `geappliances_bridge_bridge_init.cpp` lines 121–132.

#### Requirement 3.3.2: ERD Client Availability Guard

The method MUST return immediately (without setting the started flag) if `get_active_erd_client()` returns `nullptr`. The flag remains unset so the next `loop()` call retries.

**Rationale:** Autodiscovery may not yet have produced an ERD client. Setting the flag on a transient null would permanently block retry.

**Implementation:** `geappliances_bridge_bridge_init.cpp` lines 137–140.

#### Requirement 3.3.3: Feature Bit Manager Initialization

Once guards pass, the method MUST set `feature_bit_reading_started_` to `true`, then call `feature_bit_manager_.init()` with the ERD client, host address, and timer group, followed by `feature_bit_manager_.start()`.

**Implementation:** `geappliances_bridge_bridge_init.cpp` lines 144–150.

### 3.4 MQTT Client Adapter Initialization (Phase 4)

#### Requirement 3.4.1: Idempotency

`initialize_mqtt_client_()` MUST return immediately if `mqtt_client_adapter_initialized_` is already `true`.

**Implementation:** `geappliances_bridge_bridge_init.cpp` lines 159–161.

#### Requirement 3.4.2: GEA2 Protocol Detection for Manual Configs

When no active ERD client is available (autodiscovery skipped) and no GEA3 UART is configured (`uart_` is `nullptr`), the method MUST set `gea2_protocol_active_` to `true`.

**Rationale:** Manual `device_id` configurations with only a GEA2 UART bypass autodiscovery. This flag ensures `run_protocol_stack_()` enables the GEA2 tight loop before autodiscovery runs.

**Implementation:** `geappliances_bridge_bridge_init.cpp` lines 166–170.

#### Requirement 3.4.3: Adapter Binding

The method MUST:
1. Call `esphome_mqtt_client_adapter_init()` with the device ID from `device_identity_manager_`.
2. Set `mqtt_client_adapter_initialized_` to `true`.

**Rationale:** The adapter is the write path's MQTT handle; the flag gates `initialize_erd_bridge_()`, which needs it for the write bridge and the write-topic subscription.

**Implementation:** `geappliances_bridge_bridge_init.cpp`, `initialize_mqtt_client_()`.

### 3.5 ERD Bridge Initialization (Phase 6)

#### Requirement 3.5.1: Precondition Guards

`initialize_erd_bridge_()` MUST return immediately if `mqtt_client_adapter_initialized_` is `false` or `erd_bridge_initialized_` is already `true`.

**Rationale:** The MQTT adapter must be ready (it is used by the write bridge), and the method must not run twice.

**Implementation:** `geappliances_bridge_bridge_init.cpp` lines 192–194.

#### Requirement 3.5.2: Valid-ERD Filter Application

When `appliance_api_parsing_` is `true`, the feature bit manager is in `FEATURE_BIT_STATE_COMPLETE`, and the valid ERD count is greater than zero, the method MUST call `erd_registry_.set_valid_erds()` with the feature bit manager's valid ERD list.

**Rationale:** The valid-ERD filter restricts publishing to only the ERDs the appliance actually supports, reducing noise and avoiding publishing undefined values. An empty set is intentionally ignored by the registry (all ERDs are published).

**Implementation:** `geappliances_bridge_bridge_init.cpp` lines 201–206.

#### Requirement 3.5.3: Mode Selection

The method MUST determine the operating mode as follows:
- If `is_gea2_protocol()` is `true`: use polling (GEA2 does not support subscriptions).
- If `mode_` is `BRIDGE_MODE_POLL`: use polling.
- If `mode_` is `BRIDGE_MODE_SUBSCRIBE`: use subscription.
- If `mode_` is `BRIDGE_MODE_AUTO`: use subscription with custom ERD polling.

**Implementation:** `geappliances_bridge_bridge_init.cpp` lines 209–224.

#### Requirement 3.5.4: Polling Bridge Path

When `use_polling` is `true`, the method MUST:
1. Call `init_polling_bridge_(false)`.
2. Set `erd_bridge_initialized_` to `true` after `init_polling_bridge_()` returns.

**Rationale:** The synchronous completion race is real — if the probe list is small and the appliance responds immediately, the discovery-complete callback fires during `erd_bridge_poll_init()`. The flag is set after the init call completes; the startup HSM checks it via `check_steady_state()` on the next loop iteration after the signal is processed.

**Implementation:** `geappliances_bridge_bridge_init.cpp` lines 229–234.

#### Requirement 3.5.5: Subscription Bridge Path

When `use_polling` is `false`, the method MUST:
1. Call `erd_bridge_subscribe_init()` with the subscription bridge struct, timer group, active ERD client, host address, and ERD cache.
2. Set `subscription_bridge_initialized_` and `erd_bridge_initialized_` to `true`.
3. Send `signal_bridge_ready` to the startup HSM immediately (the subscription bridge has no discovery phase).

**Implementation:** `geappliances_bridge_bridge_init.cpp` lines 240–253.

#### Requirement 3.5.6: Write Bridge Initialization

Regardless of mode, the method MUST:
1. Get the host address from the autodiscovery manager.
2. Call `erd_write_bridge_init()` with the write bridge struct, timer group, active ERD client, the MQTT adapter's interface, and the host address.
3. Set `write_bridge_initialized_` to `true`.
4. Subscribe to the wildcard write topic via `esphome_mqtt_client_adapter_subscribe_write_topic()`.

**Rationale:** The write bridge is always needed (it handles Home Assistant write commands). Autodiscovery is complete by this point, so the real host address is available.

**Implementation:** `geappliances_bridge_bridge_init.cpp` lines 255–268.

### 3.6 Custom ERD Polling

#### Requirement 3.6.1: Deferred Start

`maybe_start_custom_erd_polling_()` MUST return immediately if there are no custom ERDs, custom ERD polling has already started, or the subscription bridge is not in `subscription_state_steady`.

**Rationale:** Waiting for the subscription bridge to reach steady state gives it time to publish its ERDs first, so the custom ERD polling bridge can avoid redundant polling of ERDs already covered by subscription.

**Implementation:** `geappliances_bridge_bridge_init.cpp` lines 296–313.

#### Requirement 3.6.2: Non-Destructive Start

`start_custom_erd_polling_()` MUST NOT destroy the subscription bridge. The polling bridge runs alongside it, polling only custom ERDs. Both bridges subscribe to the same ERD client activity event but handle different event types.

**Implementation:** `geappliances_bridge_bridge_init.cpp` lines 281–294.

### 3.7 Failure Handling

#### Requirement 3.7.1: Polling Bridge Failure

`handle_polling_failed()` MUST:
- Return immediately if the polling state is not `polling_state_failed`.
- If the polling bridge is running alongside a subscription bridge (custom ERD polling): destroy the polling bridge, reset `polling_bridge_initialized_` and `custom_erd_polling_started_`, and continue with subscription only.
- If the polling bridge is the primary data path (POLL mode or GEA2): log an error and leave the bridge in a failed state. The appliance-lost handler in the startup HSM's `state_failed` state will re-probe if the appliance comes back.
- In both cases, reset `last_logged_poll_state_` to `polling_state_none`.

**Implementation:** `geappliances_bridge_bridge_init.cpp` lines 320–341.

#### Requirement 3.7.2: Subscription Failure Fallback

`handle_subscription_failed()` MUST:
- Return immediately if the mode is not `BRIDGE_MODE_AUTO` (POLL and SUBSCRIBE modes have no fallback).
- Destroy the subscription bridge and reset `subscription_bridge_initialized_`.
- Reset `last_logged_poll_state_` to `polling_state_none` and `last_logged_subscribe_state_` to `subscription_state_none`.
- Destroy any existing polling bridge (e.g., from custom ERD polling) to avoid leaking heap allocations, resetting `custom_erd_polling_started_` and `polling_bridge_initialized_`.
- Call `init_polling_bridge_(true)` to re-initialize the polling bridge as the full data path.
- Leave the state transition to the caller: `startup_state_subscription_watch` invokes this handler from its `signal_run_loop` branch and transitions to `startup_state_running` itself.

**Rationale:** In AUTO mode, subscription failure is expected (the appliance may not support it). Falling back to polling ensures the bridge still functions. Destroying the old polling bridge before re-initialization prevents memory leaks from the `erd_bridge_poll_init()` heap allocations.

**Implementation:** `geappliances_bridge_bridge_init.cpp` lines 306–331.

---

## 4. Notes

1. **Phase ordering.** The startup HSM drives the sequence: protocol stack → autodiscovery → device ID → MQTT client init (Phase 4) → feature bits → bridge init (Phase 6) → subscription watch → running. This file implements the Phase 4 and Phase 6 actions, plus the feature-bit kick-off that occurs between them.

2. **Three polling bridge init paths.** `init_polling_bridge_()` is called from three contexts: (a) primary polling mode (POLL or GEA2), (b) custom ERD polling alongside subscription (AUTO mode), and (c) full fallback from subscription failure (AUTO mode). The `log_as_info` parameter controls log level — `true` for fallback (user-visible), `false` for normal polling init.
3. **Synchronous discovery race.** The polling bridge's discovery phase can complete synchronously during `erd_bridge_poll_init()` if the probe list is small. The discovery-complete callback is wired before init. `erd_bridge_initialized_` is set after `init_polling_bridge_()` returns; the startup HSM checks it via `check_steady_state()` on the next loop iteration after the signal is processed.

4. **Write bridge is always initialized.** Unlike the data-path bridges (poll/subscribe), the write bridge is initialized regardless of mode. It is needed to relay Home Assistant write commands to the appliance, and it depends on the MQTT adapter's interface being available.

5. **No destroy for normal paths.** Bridges are only destroyed on failure (subscription fallback destroys the subscription bridge; polling failure alongside subscription destroys the polling bridge). In normal operation, bridges live for the lifetime of the component.