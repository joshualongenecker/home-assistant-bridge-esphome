# GeappliancesBridge — Specification

## 1. Overview

### 1.1 Purpose

The main ESPHome component class that orchestrates the entire GE Appliances bridge. It manages UART interfaces for GEA2/GEA3 protocols, drives the startup state machine, handles MQTT connection lifecycle, and coordinates all sub-managers (autodiscovery, device identity, feature bits).

### 1.2 Responsibilities

- Own and construct all component instances (adapters, managers, bridges)
- Wire components together during `setup()`
- Drive the GEA2 tight-loop and delegate ongoing work in `loop()`
- Own the wire-level protocol machinery via the embedded `ProtocolStack` (UART adapters, GEA2/GEA3 interfaces, ERD clients, shared timer group) and drive it once per `loop()` iteration
- Expose configuration setters called by the ESPHome code generator
- Implement `IBridgeServices` so the startup HSM can request bridge actions without depending on this concrete class

### 1.3 Not Responsible For

- Assembling the device ID (`DeviceIdentityManager`)
- Determining which ERDs are valid (`FeatureBitManager` / `ErdRegistry`)
- MQTT connection lifecycle (`EsphomeMqttClientAdapter`)
- Startup phase sequencing (`StartupHsm`)
- Wire-level protocol driving (`ProtocolStack` — adapters, interfaces, ERD clients, the GEA2 tight loop, the shared timer group)

---

## 2. Public API

### 2.1 ESPHome Component Lifecycle

| Method | Description |
|--------|-------------|
| `setup()` | Initialize timer group, UART adapters, ERD clients, GEA interfaces, managers |
| `loop()` | Drive protocol stack and startup HSM |
| `dump_config()` | Log current configuration and state |
| `get_setup_priority()` | Returns `setup_priority::DATA` (600) — after MQTT (50), same as UART |
| `teardown()` | Clean up bridges and MQTT adapter |

### 2.2 Configuration Setters (called from `__init__.py` code generation)

| Setter | Description |
|--------|-------------|
| `set_gea3_uart(uart)` / `set_gea2_uart(uart)` | Configure UART interfaces |
| `set_client_address(address)` | Set the bridge's bus address (default `0xE4`) |
| `set_device_id(id)` | Pre-configure a static device ID |
| `set_mode(mode)` | Set bridge mode: POLL (0), SUBSCRIBE (1), or AUTO (2) |
| `set_polling_interval(ms)` | Set polling interval (default 10000 ms) |
| `set_appliance_api_parsing(bool)` | Enable feature bit-based ERD filtering (default true) |
| `set_generate_device_config(bool)` | Enable HA discovery payload generation (default true) |
| `set_filter_config_topics(bool)` | Filter diagnostic entities during discovery publishing (default true) |
| `add_custom_erd(erd)` | Add a custom ERD to poll |
| `set_erd_publish_rate_sensor(sensor)` | Sensor for ERD publish rate |
| `set_erd_cache_entries_sensor(sensor)` | Sensor for ERD cache entry count |
| `set_erd_cache_updates_sensor(sensor)` | Sensor for ERD cache update count |
| `set_mqtt_publish_rate_sensor(sensor)` | Sensor for MQTT publish rate |
| `set_throttle_rate_seconds(uint8_t)` | Set ERD cache throttle rate in seconds (default 1) |

---

## 3. Protected Methods

| Method | Description |
|--------|-------------|
| `handle_erd_client_activity_(args)` | Route ERD activity to appropriate manager (autodiscovery, device ID, feature bits) |
| `should_route_to_feature_bits_(erd)` | Decide whether an ERD read goes to FeatureBitManager or DeviceIdentityManager |
| `initialize_mqtt_client_()` | Create and configure the MQTT client adapter |
| `initialize_erd_bridge_()` | Initialize subscription or polling bridge based on mode |
| `loop()` → `protocol_stack_.run(gea2_pending)` | Drive GEA2/GEA3 hardware via the embedded `ProtocolStack` (see `protocol_stack.md`); the bridge computes `gea2_pending` from autodiscovery state |
| `start_feature_bit_reading_()` | Start the feature bit read sequence |
| `handle_subscription_failed()` | Handle subscription bridge failure; trigger polling fallback in AUTO mode |
| `handle_polling_failed()` | Handle polling bridge failure; clean up or recover |
| `start_custom_erd_polling_()` | Initialize polling for user-configured custom ERDs |
| `maybe_start_custom_erd_polling_()` | Guarded entry point for custom ERD polling (prevents re-initialization) |
| `log_poll_state_transitions_()` | Debug: log polling HSM state changes |

---

## 4. Startup Sequence
The bridge progresses through a linear sequence of phases via the `startup_hsm_wrapper_`:

```
startup_delay → autodiscovery → device_id → mqtt_client_init
             → feature_bits → bridge_init → subscription_watch
             → running
```

Each phase is driven by the startup HSM, which invokes `IBridgeServices` methods on the bridge to perform work and check completion.

---

## 5. Bridge Initialization Flow

During `startup_state_bridge_init`, `initialize_erd_bridge_()` runs:

1. **Apply ERD filter:** If appliance API parsing is enabled and complete, sets the valid-ERD filter on the registry.
2. **Select mode:** Determines polling vs. subscription based on mode setting and GEA2/GEA3 protocol.
3. **Build probe list:** Calls `build_poll_list_()` (which delegates to `erd_poll_list_builder`) to build the list of ERDs to probe.
4. **Initialize bridges:**
   - **Polling mode:** Initializes `erd_bridge_poll_` with the probe list, known host address, and appliance type.
   - **Subscription mode:** Initializes `erd_bridge_subscribe_` with the known host address.
   - **Write bridge:** Always initialized with the real host address from `autodiscovery_manager_.get_host_address()` (autodiscovery completes before bridge init).

---

## 6. Protocol Stack Driving

The wire-level protocol machinery lives in the embedded `ProtocolStack` (see `protocol_stack.md`). Each `loop()` iteration computes `gea2_pending` from autodiscovery state and calls `protocol_stack_.run(gea2_pending)`, which executes the GEA2 tight loop (100 ms wall-clock, 200 ms hard cap) when GEA2 work is in flight, the GEA3 iteration (10 ms) otherwise, or a timer-only iteration with no UART configured. A manual millisecond counter inside the stack drives the GEA2 interface's internal timers without starving the shared timer group.

| Constant | Value | Description |
|----------|-------|-------------|
| `kGea2LoopDurationMs` | 100 ms | Wall-clock duration for GEA2 tight loop |
| `kGea3LoopDurationMs` | 10 ms | Wall-clock duration for GEA3 protocol tick |

---

## 7. Data Structures

Key member variables:

| Member | Type | Description |
| `startup_hsm_wrapper_` | `startup_hsm_wrapper_t` | Startup state machine wrapper (embeds `tiny_hsm_t` and `IBridgeServices*`) |
| `protocol_stack_` | `ProtocolStack` | Wire-level protocol machinery: UART adapters, GEA2/GEA3 interfaces, ERD clients, GEA2→GEA3 adapter, buffers, GEA2 tick source, shared timer group (see `protocol_stack.md`) |
| `uart_` / `gea2_uart_` | `UARTComponent*` | UART interfaces |
| `erd_cache_` | `erd_cache_t` | Shared ERD cache |
| `erd_cache_publisher_` | `erd_cache_mqtt_publisher_t` | MQTT publisher |
| `erd_registry_` | `ErdRegistry` | Valid/registered ERD tracking |
| `mqtt_client_adapter_` | `esphome_mqtt_client_adapter_t` | MQTT adapter (`.initialized` replaces the old `mqtt_client_adapter_initialized_` flag) |
| `erd_bridge_subscribe_` | `erd_bridge_subscribe_t` | Subscription bridge |
| `erd_bridge_poll_` | `erd_bridge_poll_t` | Polling bridge |
| `erd_write_bridge_` | `erd_write_bridge_t` | Write bridge |
| `autodiscovery_manager_` | `AutodiscoveryManager` | Appliance discovery |
| `device_identity_manager_` | `DeviceIdentityManager` | Device ID generation |
| `feature_bit_manager_` | `FeatureBitManager` | Feature bit reading (`is_initialized()` replaces the old `feature_bit_reading_started_` flag) |
| `custom_erds_[CUSTOM_ERDS_MAX]` | `tiny_erd_t[128]` | User-configured custom ERDs |
| `poll_probe_list_[POLLING_LIST_MAX_SIZE]` | `uint16_t[649]` | Pre-built probe list |

### 7.1 Data-Plane Phase Machine

Eleven independent booleans (`erd_bridge_initialized_`, `subscription_bridge_initialized_`, `polling_bridge_initialized_`, `write_bridge_initialized_`, `custom_erd_polling_started_`, `steady_state_reached_`, `mqtt_client_adapter_initialized_`, `feature_bit_reading_started_`, `gea2_protocol_active_`, `erd_cache_publisher_paused_`, `discovery_just_resumed_`) were only valid in the handful of combinations the previous `loop()` source order happened to produce. They are replaced by three small machines:

| Machine | States | Replaces |
|---------|--------|----------|
| `DataPlanePhase data_plane_phase_` | `kIdle`, `kPolling`, `kSubscription`, `kSubscriptionCustomPolling`, `kSubscriptionSettled` | `erd_bridge_initialized_`, `subscription_bridge_initialized_`, `polling_bridge_initialized_`, `write_bridge_initialized_`, `custom_erd_polling_started_` |
| `DataPlaneReadiness data_plane_readiness_` | `kNotSteady`, `kSteady` (one-way latch) | `steady_state_reached_` |
| `PublisherPausePhase publisher_pause_phase_` | `kIdle`, `kPaused`, `kResuming` | `erd_cache_publisher_paused_`, `discovery_just_resumed_` |

Two more booleans became owner-side state instead of bridge-side copies: `mqtt_client_adapter_initialized_` → `mqtt_client_adapter_.initialized` (set by `esphome_mqtt_client_adapter_init()`, cleared by `destroy()`), and `feature_bit_reading_started_` → `feature_bit_manager_.is_initialized()` (true once `init()` installed an ERD client; `cleanup()` resets it). `gea2_protocol_active_` became `protocol_stack_.set_gea2_active()`.

Phase transitions (all inside `initialize_erd_bridge_()`, `start_custom_erd_polling_()`, `handle_polling_failed()`, `handle_subscription_failed()`, and `teardown()`):

```text
kIdle --initialize_erd_bridge_()--> kPolling | kSubscription
kSubscription --start_custom_erd_polling_()--> kSubscriptionCustomPolling | kSubscriptionSettled
kSubscriptionCustomPolling --handle_polling_failed()--> kSubscription
kSubscription* --handle_subscription_failed()--> kPolling   (AUTO-mode fallback rebuild)
any phase --teardown()--> kIdle
```

The phase never returns to `kIdle` before teardown: the write bridge is initialized at the end of `initialize_erd_bridge_()` on every path and destroyed only in `teardown()`. `is_bridge_initialized()` is `data_plane_phase_ != kIdle`; the getters `get_subscription_state()` / `get_polling_state()` return `*_state_none` when the phase does not imply a live bridge of that kind (`phase_has_subscription_bridge()` / `phase_has_polling_bridge()`), so a stale bridge struct can never be read through the public interface.

---

## 8. Invariants

1. **IBridgeServices implementation:** `GeappliancesBridge` implements `IBridgeServices`, the abstract contract consumed by the startup HSM. This eliminates `friend` declarations and lets the HSM be unit-tested with a mock.
2. **Probe list ownership:** The `poll_probe_list_` member stores the built probe list so the pointer passed to `erd_bridge_poll_init()` remains valid across the probe phase.
3. **ERD cache publisher delegation:** The `erd_cache_mqtt_publisher_` drains `update_required` entries from the shared cache and publishes them to MQTT from its own background task — one entry per wake, and paced while the backlog is large (publisher spec §2.5) — decoupling the bridges from direct MQTT interaction.
4. **Single appliance:** The bridge operates with a single discovered appliance address.
5. **Fixed capacity arrays throughout:** All data structures use fixed-capacity arrays to avoid heap allocation (custom ERDs: `CUSTOM_ERDS_MAX` = 128, probe list: `POLLING_LIST_MAX_SIZE` = 659, ERD cache: `ERD_CACHE_CAPACITY` = 300).

---

## 9. Dependencies

| Dependency | Role |
|------------|------|
| ESPHome `Component` base class | Lifecycle (setup, loop, teardown) |
| ESPHome `uart::UARTComponent` | UART interfaces |
| ESPHome `mqtt::MQTTClientComponent` | MQTT client |
| `tiny_gea3_interface`, `tiny_gea3_erd_client` | GEA3 protocol stack |
| `tiny_gea2_interface`, `tiny_gea2_erd_client` | GEA2 protocol stack |
| `AutodiscoveryManager` | Appliance discovery |
| `DeviceIdentityManager` | Device ID generation |
| `FeatureBitManager` | Feature bit reading |
| `esphome_uart_adapter` | UART → i_tiny_uart |
| `esphome_mqtt_client_adapter` | ESPHome MQTT → i_mqtt_client |
| `gea2_erd_client_adapter` | GEA2 → GEA3 interface |
| `erd_bridge_subscribe`, `erd_bridge_poll`, `erd_write_bridge` | ERD bridges |
| `erd_poll_list_builder` | Probe list construction |
| `erd_registry` | Valid/registered ERD tracking |
| `erd_cache_mqtt_publisher` | Cache → MQTT publishing |
| `erd_bridge_common.h` | Shared signals, timing, utilities |
| `tiny_hsm`, `tiny_timer` | State machine and timer infrastructure |

---

## 10. Known Limitations

1. **Single appliance:** The bridge operates with a single discovered appliance address. Multi-appliance support would require significant architectural changes.
2. **Fixed capacity arrays:** All data structures use fixed-capacity arrays. If the appliance supports more ERDs than the cache can hold (`ERD_CACHE_CAPACITY` = 300 entries, or its 4096-byte arena), the entry is not cached and a warning is logged (`erd_cache_update_locked()` in `erd_cache.cpp`, once per condition) — the drop is reported, not silent.
3. **No rollback:** The startup sequence is linear — once a phase completes, it does not re-run. If a phase fails, the bridge continues with fallback values.
4. **GEA2 tight loop blocks the main loop:** During the 100 ms GEA2 tight loop, the ESPHome main loop is blocked. This is necessary for correct GEA2 half-duplex operation but limits the responsiveness of other ESPHome components during that window.
5. **Startup HSM bridge access:** The startup HSM uses the `container_of` pattern via `services_from_hsm()` to recover the `IBridgeServices` pointer from the embedded `startup_hsm_wrapper_t`. This avoids a static global back-pointer and keeps the HSM self-contained.

## 11. HA Discovery Integration

The bridge integrates Home Assistant MQTT discovery via the `ha_discovery_manager_` and coordinates with the `erd_cache_publisher_` to avoid MQTT queue contention during discovery payload generation and cleanup.

**Discovery does not run on normal boots.** HA discovery topics are retained on the MQTT broker, so the bridge skips discovery on regular reboots. Discovery runs in four scenarios:

1. **Fresh install** (no stored state in NVS): initial discovery publish → reboot
2. **Discovery data hash changed** (firmware update changed discovery definitions): cleanup → publish → reboot
3. **Device ID changed** (different appliance or config change): cleanup → publish → reboot
4. **Discovery Refresh button pressed**: cleanup → publish → reboot

Change detection runs in `check_steady_state()` (not `setup()`), since device identity is not available until steady state is reached. After each successful publish, a `DiscoveryRecord {version, hash, device_id, appliance_api_parsing, filter_config_topics, valid_erds_republishes, valid_erds_stable_checks, valid_erds_fingerprint}` struct is stored in NVS for comparison on the next boot.

### 11.1 Configuration Flags

Two boolean configuration flags control discovery behavior. Both are set via the ESPHome code generator during `setup()` and read by `loop()` to gate discovery actions.

| Flag | Default | Description |
|------|---------|-------------|
| `generate_device_config_` | `true` | When `true`, the bridge checks for discovery changes (hash, device ID, appliance_api_parsing, or filter_config_topics) at steady state and runs cleanup + republish if needed. Also enables the initial discovery publish on fresh installs. When `false`, all automatic discovery change detection is skipped. The Discovery Refresh button always works regardless of this flag (it's a user-initiated manual action). Normal boots skip discovery regardless (topics retained by MQTT broker). |
| `filter_config_topics_` | `true` | When `true`, the discovery manager filters out internal/diagnostic entities during discovery publishing. Passed to `ha_discovery_manager_configure()`. |

**Configuration setters:**

```cpp
void set_generate_device_config(bool generate_device_config);
void set_filter_config_topics(bool filter_config_topics);
```

Both setters store their value directly into the corresponding member variable. They are called from `__init__.py` during ESPHome code generation based on the user's YAML configuration.

### 11.2 Discovery State Fields

| Field | Type | Description |
|-------|------|-------------|
| `publisher_pause_phase_` | `PublisherPausePhase` | Publisher pause machine (`kIdle` / `kPaused` / `kResuming`), replacing the old `erd_cache_publisher_paused_` / `discovery_just_resumed_` boolean pair. `kPaused` while `ha_discovery_manager_is_processing()` holds; on resume the phase becomes `kResuming` and returns to `kIdle` once `erd_cache_mqtt_publisher_first_round_done()` confirms the post-resume drain finished — so the "Device is in steady state" log fires exactly once per pause cycle. |

The following state variables were previously members of `GeappliancesBridge` but have been extracted into their respective managers:

| Variable | Owner |
|----------|-------|
| `phase_` | `DiscoveryLifecycleManager` |
| `trigger_` | `DiscoveryLifecycleManager` |
| `initial_publish_done_` | `DiscoveryLifecycleManager` |
| `reboot_start_ms_` | `DiscoveryLifecycleManager` |
| `phase_start_ms_` | `DiscoveryLifecycleManager` |
| `last_erd_publish_rate_publish_` | `DiagnosticSensorPublisher` |
| `last_erd_cache_stats_publish_` | `DiagnosticSensorPublisher` |
| `last_mqtt_disconnect_stats_publish_` | `DiagnosticSensorPublisher` |

`DiscoveryLifecycleManager` keeps its lifecycle in one `Phase` value instead of the cluster of independent booleans it used to carry, which were only valid in the handful of combinations the previous `loop()` source order happened to produce:

| Phase | Meaning |
|-------|---------|
| `kPhaseIdle` | Nothing running; accepts the next request |
| `kPhaseQueuedInitial` | Initial publish requested, waiting for readiness |
| `kPhaseQueuedCleanup` | Cleanup + republish requested, waiting for readiness |
| `kPhaseCleaning` | Deleting stale discovery topics |
| `kPhasePublishing` | A discovery run is in flight |
| `kPhaseRebootPending` | Published and recorded; rebooting after the delay |
| `kPhaseClaimed` | Transient: a trigger won the atomic claim from Idle and is recording `trigger_` before publishing the queued phase; `loop()` treats it as busy |

```text
Idle --trigger_initial_discovery()--> Claimed --(trigger recorded)--> QueuedInitial
Idle --trigger_ota_cleanup() / trigger_discovery_refresh()--> Claimed --(trigger recorded)--> QueuedCleanup
QueuedInitial --ready--> Publishing
QueuedCleanup --ready--> Cleaning
Cleaning --cleanup done--> Publishing
Cleaning --kPhaseTimeoutMs with no topic removed--> Idle  (aborted, no record)
Publishing --run failed--> RebootPending       (budget left) / Idle (spent)
Publishing --kPhaseTimeoutMs with no successful publish--> same as a failed run
Publishing --run complete--> RebootPending
RebootPending --5 s elapsed--> Idle
```

`trigger_` (`kTriggerNone` / `kTriggerOta` / `kTriggerDiscoveryRefresh` / `kTriggerInitial`) records why the current phase was entered. It is orthogonal to the phase because the same cleanup → publish → reboot sequence runs for every trigger. `initial_publish_done_` is the one piece of state outside the phase: a sticky latch that keeps a fresh install from publishing twice in the same boot if the NVS record never landed.

### 11.3 Discovery Change Detection (check_steady_state())

When steady state is first reached, `check_steady_state()` calls `discovery_lifecycle_manager_.check_discovery_changes()` with the current device ID. The method loads a `DiscoveryRecord {version, hash, device_id, appliance_api_parsing, filter_config_topics, valid_erds_republishes, valid_erds_stable_checks, valid_erds_fingerprint}` struct through the injected `load_discovery_record` hook (NVS preferences key `0x64697363u` / "disc" on the device).

The outcomes are:

- **No stored state (fresh install):** `trigger_initial_discovery()` is called. The bridge will publish discovery topics without cleanup, then reboot. This trigger is gated on the discovery-abort reboot budget (section 11.6): with `kMaxAbortReboots` (3) attempts already spent and no record stored, the boot logs "No stored discovery record, but the discovery-abort reboot budget (3) is spent; holding the initial publish" and stays idle. The initial path never arms the pending-publish marker, so an ungated trigger would re-run a doomed attempt on every boot forever. Only a Discovery Refresh press retries — a power cycle does not, because the count is in NVS and only a successful publish clears it.
- **Record version differs:** `trigger_ota_cleanup()`; the layout or payload format changed. A version 4 record (104 bytes, no fingerprint) is still read: the loader falls back to that size, so an upgraded device cleans up and republishes once instead of taking the fresh-install path.
- **Hash, device ID, appliance_api_parsing, or filter_config_topics changed:** `trigger_ota_cleanup()` is called. The bridge will clean old topics, publish fresh discovery, and reboot.
- **Valid-ERD fingerprint changed:** `trigger_ota_cleanup()` as above, within a bound (below).

Every publish that followed a cleanup logs, at INFO, how many configs it published against how many the cleanup removed (the previous run's retained set), naming the valid-ERD change when that was the trigger, e.g. `valid-ERD change republish: 155 configs published, 167 removed by the cleanup; 12 fewer than before`. A shortfall is entities Home Assistant is about to delete.
- **No changes detected:** Nothing happens. The bridge proceeds normally with retained topics on the broker.

The hash is compared against `HA_DISCOVERY_DATA_HASH`, a compile-time constant generated by the discovery pipeline. The device ID is compared against the current value from `DeviceIdentityManager`. The config flags are compared against the current YAML values.

The **valid-ERD fingerprint** is what discovery is filtered by: FNV-1a over the host address, the distinct secondary-board addresses the configuration names (custom ERDs with an explicit board address, ascending; the primary sentinel `0xFF` is skipped), and the sorted valid-ERD set in `ErdRegistry`. The bridge sets it (`set_valid_erds_fingerprint()`) just before the check. It changes when the bridge moves to another host board, a configuration change adds or removes a secondary board, or firmware changes the feature bits, which would otherwise leave the published entity list stale.

Board liveness is deliberately not an input (issue #212, #210 item 6). The fingerprint used to hash the board addresses present in the ERD cache, so one boot where a configured secondary board did not answer changed it, and the cleanup + republish that followed dropped that board's lines (the run filters by the cache) and Home Assistant deleted its entities: 12 of 167 on `gea-esphome-waterheaters3` when board `0x62` went silent. Now the configured boards are hashed, and a configured board ERD is published whether or not it is cached (`ha_discovery_manager_set_configured_board_erds()`, set in `setup()`), so a silent board leaves its entities stale instead of deleted, and nothing republishes when it comes back.

Changing the fingerprint's inputs changes every bridge's stored fingerprint once, so the first boot on firmware with this change normally does one fingerprint republish (cleanup, republish, reboot). That republish is subject to the usual gates, not guaranteed: a bridge whose record already holds `kMaxValidErdsRepublishes` (2) fingerprint republishes in a row logs "Valid-ERD set changed again after 2 republishes in a row; not republishing" and skips it, and the pending-publish and abort-budget branches above take precedence over every change check. A bridge that flip-flopped on the old cache-derived fingerprint is the likely case for the first. On such a bridge the published set stays as it was until a Discovery Refresh press. `kRecordVersion` was not bumped: the record layout is unchanged, and a version bump (ungated) would only replace that fingerprint republish with a version-change one. It is 0, and this trigger inert, whenever there is no valid-ERD filter: with `appliance_api_parsing: false`, or when the feature bits produced no valid ERDs (for example the common feature ERD 0x0092 could not be read). A 0 never triggers a republish.

Unlike the other inputs, the fingerprint is read live from the appliance, and feature-bit reads may fail partially, so it is bounded: after `kMaxValidErdsRepublishes` (2) republishes in a row caused only by the fingerprint, further fingerprint changes are logged and ignored until the fingerprint has been seen unchanged at `kValidErdsStableChecksToReset` (3) checks in a row. A set that flaps between boots keeps breaking that streak, so it cannot republish and reboot forever. Discovery Refresh, or any other discovery change, resets the count. The check runs at each steady-state transition, which is every boot and every Reconnect Appliance press.

### 11.4 loop() Delegation

Each `loop()` iteration delegates discovery and diagnostic work to the extracted managers:

1. **Update publisher state:** Call `update_publisher_state_()` to pause the publisher's background task while Home Assistant discovery is generating payloads, and to resume it otherwise. Both of an iteration's wakes of the publisher come from here: `erd_cache_mqtt_publisher_resume()` gives `work_semaphore` after clearing `paused`, and the function then calls `erd_cache_mqtt_publisher_signal_work()` directly when a cache is attached and discovery is not active.

2. **Discovery lifecycle:** Delegates to `discovery_lifecycle_manager_.loop()`, which advances the discovery phase machine by at most one transition per call: initial publish (fresh install), cleanup + republish (hash/device ID change or Discovery Refresh), and the final reboot. The machine gates on steady state, MQTT initialization, and device ID completion, and handles the full lifecycle: optional cleanup, discovery publishing, safe-mode counter clearing, OTA rollback cancellation, NVS state storage, and the final reboot delay.

3. **Diagnostic sensor publishing:** Delegates to `diagnostic_sensor_publisher_.loop()`, which publishes ERD publish rate, MQTT publish rate, ERD cache stats, and MQTT disconnect stats at their configured intervals (~60 seconds).

### 11.5 trigger_discovery_refresh() Flow

`trigger_discovery_refresh()` is now a **public** method (previously protected with `friend DiscoveryRefreshButton`). It delegates to `discovery_lifecycle_manager_.trigger_discovery_refresh()`.

The actual behavior is implemented in `DiscoveryLifecycleManager`:

1. **Atomic claim:** CAS `phase_` from `kPhaseIdle` to the transient `kPhaseClaimed`. If the CAS fails (any other phase, including a concurrent claim that won first), log a warning naming the request and the current phase and return. "One phase at a time" is the invariant the enum enforces, and the CAS makes it hold across cores.
2. **Queue the request:** Set `trigger_` to `kTriggerDiscoveryRefresh`, then publish it with a release store of `kPhaseQueuedCleanup`, and log "Discovery refresh queued, will execute when appliance is ready".

No guard checks for steady state, MQTT readiness, or device ID are performed at press time. The request is queued and will execute in `loop()` once all prerequisites are met. This allows the user to press the button at any time, even during startup.

### 11.6 NVS State Storage and Reboot Preparation

After a successful discovery run, `DiscoveryLifecycleManager::finish_publish_()` performs several steps before arming the reboot. Each host-environment step goes through an injected hook, so the phase machine runs identically on the device and in the unit-test harness:

1. **Confirm the firmware** (`confirm_firmware`): writes `0` to the ESPHome preferences key matching `SAFE_MODE_RTC_KEY` (233825507UL), syncs immediately, and calls `esp_ota_mark_app_valid_cancel_rollback()`. This prevents the device from entering safe mode due to rapid reboots during the cleanup → publish → reboot cycle, and marks the current OTA partition valid so the bootloader does not roll back. It runs for a failed run too, so a good OTA is never rolled back because discovery failed.
2. **Store the discovery record** (`save_discovery_record`): saves `DiscoveryRecord {version, hash, device_id, appliance_api_parsing, filter_config_topics, valid_erds_republishes, valid_erds_stable_checks, valid_erds_fingerprint}` to NVS preferences (key `0x64697363u` / "disc"). The `hash` field is `ha_discovery_manager_data_hash()`, `device_id` comes from `DeviceIdentityManager`, both config flags from the current YAML configuration, and `valid_erds_fingerprint` from the bridge (section 11.3), with the fingerprint-republish count and a zeroed stable-check streak. This stored state is compared against on the next boot to detect changes requiring cleanup. `version` is `kRecordVersion`, bumped whenever the record layout changes.

A **failed** run (or one refused by the largest-free-block gate in `begin_publish_()`) skips the record and goes through `abort_publish_run_()` instead: the run's allocations are released, `kMaxAbortReboots` (3) reboots in NVS (key `0x64726162u` / "drab") bound the recovery, and each reboot gives the clean heap a failed run cannot restore. Leaving the stored record stale or absent is what makes the next boot retry — which is why both automatic retriggers (the pending-publish marker and the absent-record initial publish) check the spent budget before starting, and why a spent budget ends in a logged hold rather than another attempt.

### 11.7 Integration with ha_discovery_manager

The bridge owns a `ha_discovery_manager_t` instance (`ha_discovery_manager_`). The manager is driven by `DiscoveryLifecycleManager` from `loop()` via delegation:

- **Configuration:** `ha_discovery_manager_configure()` is called when discovery starts (during any discovery path: initial publish, change-triggered cleanup, or Discovery Refresh), passing the device identity (ID, model, serial, appliance type), the `filter_config_topics_` flag, the ERD cache, and the MQTT client interface.
- **Start:** `ha_discovery_manager_start()` transitions the manager from `IDLE` to `BUILDING`.
- **Drive:** `ha_discovery_manager_run()` is called each `loop()` iteration while `ha_discovery_manager_is_processing()` returns `true`. This advances the manager through its state machine, decompressing JSONL chunks and publishing discovery payloads at a rate-limited interval (50 ms).
- **Completion:** When the manager reaches `COMPLETE` or `FAILED` state, `ha_discovery_manager_is_processing()` returns `false`, signaling that discovery publishing is done.

### 11.8 Integration with ha_discovery_cleanup

The cleanup module is accessed via `ha_discovery_manager_.cleanup` and driven by `DiscoveryLifecycleManager`. All cleanup paths (hash/device ID change, Discovery Refresh) use the same cleanup module; initial publish skips cleanup entirely.

`DiscoveryLifecycleManager` handles the full lifecycle:

- **Configuration:** `ha_discovery_cleanup_configure()` is called with the device ID, MQTT client interface, and time source.
- **Start:** `ha_discovery_cleanup_start()` begins the cleanup process.
- **Drive:** `ha_discovery_cleanup_run()` is called each `loop()` iteration while the phase is `kPhaseCleaning`.
- **Completion:** `ha_discovery_cleanup_is_done()` returns `true` when all phases are complete. `DiscoveryLifecycleManager` then destroys the cleanup module, resets the discovery manager, and proceeds to publish fresh discovery topics.

### 11.9 Reboot with safe_reboot()

After cleanup and discovery publishing complete, `DiscoveryLifecycleManager` reboots using `esphome::App.safe_reboot()` instead of `esphome::App.reboot()` (the `reboot` hook). `safe_reboot()` performs a graceful MQTT disconnect before resetting, ensuring a clean session end on the broker. This prevents the broker from thinking the device is still connected, which could cause stale availability topics or delayed reconnection events.

A 5-second delay precedes the reboot to allow final discovery messages to transmit and the heap to stabilize. The reboot also defragments the heap after the memory-intensive cleanup and publish cycle.

### 11.10 Publisher Pause/Resume Rationale

The ERD cache publisher is paused during discovery activity to avoid competing for the ESP-IDF MQTT task's inbound/outbound queues. Without pausing, the background publisher task would continue draining cache updates while the discovery manager publishes config payloads, potentially causing:

- MQTT queue overflow, leading to dropped messages
- Retained-clear messages from cleanup being overwritten by late cache publishes
- Unpredictable ordering of discovery config vs. value messages on the broker

The pause/resume cycle ensures a clean separation: discovery messages are published without interference, then the cache publisher resumes and drains accumulated updates. The `first_round_done` mechanism confirms the publisher has completed a full cache round after resuming, returning `publisher_pause_phase_` from `kResuming` to `kIdle`. The `data_plane_readiness_` latch (`kSteady`) is set by `check_steady_state()` (called from the startup HSM), which also triggers discovery change detection at that point; this is the signal that gates HA discovery startup.

---

## Notes

1. **Diagnostic entities are schema-defaulted, and only schema-defaulted entities reach the broker.** `components/geappliances_bridge/__init__.py` declares all eight component-created diagnostic entities as `cv.Optional(CONF_…, default={})` with a `cv.boolean_false` alternative, so each one is created unless the YAML option is explicitly set to `false`. The defaults live in `CONFIG_SCHEMA` on purpose: validation is what materializes the entity `id` and — because the component depends on `mqtt` — the `mqtt_id`, and codegen emits the `MQTTSensorComponent` / `MQTTButtonComponent` wrapper only for entities whose validated config carries that key. An entity assembled by hand in the codegen path is therefore registered, calls `publish_state()` every cycle, and never reaches the broker, with no compile error and no test to show it. `expand_diagnostic_entity_defaults()` — the first step of the `cv.All(…)` that builds `CONFIG_SCHEMA` — rewrites a literal `true` to `{}` so the option takes the schema branch rather than the boolean one, which is what produced an entity with no `mqtt_id`. See ADR-0018 for the cross-cutting rule; the module-local consequence is that a new diagnostic entity is added as a schema option (its display name, state class, `id` and `mqtt_id` all come from the default) and never built by hand. The eight options are `erd_publish_rate_sensor`, `erd_cache_entries_sensor`, `erd_cache_updates_sensor`, `mqtt_publish_rate_sensor`, `mqtt_disconnect_count_sensor`, `mqtt_disconnect_duration_sensor`, `discovery_refresh_button` and `reconnect_appliance_button` — `DIAGNOSTIC_ENTITY_KEYS`.
2. **The wrapper gate proves the listed entities have wrappers, not that the list is complete.** `make check-diagnostic-mqtt` copies `docs/test-secrets.yaml` to `docs/secrets.yaml`, regenerates the CI config with `esphome compile --only-generate docs/test-compile.yaml`, and runs `scripts/check_diagnostic_mqtt_wrappers.py` against the generated `main.cpp`. `--only-generate` is what makes the gate cheap: the check reads codegen output, it does not need the ESP-IDF build. The script pairs each `App.register_sensor` / `App.register_button` (the entity name may be wrapped in `ESPHOME_PSTR(…)` from ESPHome 2026.11 on) with a `new(…) mqtt::MQTT(Sensor|Button)Component(…)` wrapper and fails with a per-entity report — including the closest registered name, so a renamed display name is not mistaken for a missing entity. CI runs the same check in `.github/workflows/esphome-compile.yml`. **Known gap:** the check iterates its own `EXPECTED_SENSORS` (six display names) and `EXPECTED_BUTTONS` (two), which are hand-mirrored copies of the schema defaults' `name` values ("ERD Publish Rate", "ERD Cache Update Rate", "MQTT Last Disconnect Duration", "Discovery Refresh", …). A diagnostic entity present in `DIAGNOSTIC_ENTITY_KEYS` but absent from those tuples is never looked at, so it passes silently; a green gate is not evidence that every diagnostic entity is covered.
3. **Configuration reaches the bridge at `setup()`, and `init()` is re-runnable.** ESPHome constructs the component before it applies configuration, so the UART pointers are only available inside `GeappliancesBridge::setup()`, which is where `protocol_stack_.init(client_address_, uart_, gea2_uart_)` runs; every embedded struct the bridge owns must be constructible with no UART attached and value-initialized (`{}`) because `teardown()` frees/dispatches on them before `setup()` has ever run. `GeappliancesBridge::teardown()` — an override of `Component::teardown()`, also invoked from the destructor — resets per-run state so a later `setup()`/`init()` starts clean after an ESPHome re-setup (deep-sleep wake, YAML reconfiguration): `protocol_stack_.teardown()` clears the GEA2 tick tracking and the GEA2-active latch, `erd_set_clear()` clears `custom_erd_subscription_seen_erds_`, and `feature_bit_failure_logged_` is cleared so the feature-bit failure log is correct on the next attempt. The stack-side half of the same contract is requirement 3.4 of [the ProtocolStack spec](./protocol_stack.md).
4. **`loop()` drives the protocol stack before any MQTT work.** `GeappliancesBridge::loop()` calls `protocol_stack_.run(gea2_pending)` first, and only afterwards the startup HSM's `signal_run_loop` (which in steady state drains pending MQTT updates), `update_publisher_state_()`, `discovery_lifecycle_manager_.loop()` and `diagnostic_sensor_publisher_.loop()`. On single-core ESP32 variants a blocking MQTT call can delay UART response processing past the appliance's timeout window, so the tight loop must get the iteration's first slice; reordering `loop()` to favor MQTT latency is the change this forbids. `gea2_pending` is orchestration-owned and computed here from the autodiscovery manager (`is_gea2_protocol()`, `AUTODISCOVERY_GEA2_BROADCAST_PENDING`, `AUTODISCOVERY_GEA2_BROADCAST_WAITING`); the stack-side dispatch is requirement 3.1.1 of [the ProtocolStack spec](./protocol_stack.md) and the phase machine is ADR-0006.
5. **The task watchdog is fed at exactly two points in `loop()`, which bounds what one segment may do.** Under `#ifdef USE_ESP32`, `esp_task_wdt_reset()` runs once after `protocol_stack_.run()` — the GEA2 tight loop can consume its full wall-clock cap (`kGea2LoopDurationMs` = 100 ms, `kGea2LoopHardCapMs` = 2×) — and once after the HSM `signal_run_loop`, which in steady state drains pending MQTT updates, each acquiring the IDF MQTT client mutex and able to block for hundreds of milliseconds. The tight loops additionally reset the watchdog per iteration inside `run_tight_loop_()` (`protocol_stack.h`). Consequence: a new blocking segment in `loop()` must either sit between two existing feeds or get its own feed plus a measured bound; the `Long protocol stack run` and `Long HSM run_loop` warnings (both at ≥1000 ms elapsed) are the tripwires that show a segment outgrowing its budget. The ERD cache publisher does not count against this budget because it is a pinned task of its own (ADR-0005).
6. **Boot-log volume is a budget, not a style preference.** Anything that scales with ERD count logs at VERBOSE. The custom-ERD boot dump is one `ESP_LOGD` summary line carrying the per-board tally (`board_tally_init` / `board_tally_add` / `board_tally_format`), not one line per ERD: that per-ERD loop was the largest single block of the boot log (65–92 lines on real appliances) and the board distribution is the only part of it that matters. The steady-state summary prints one line per section at DEBUG, and the per-entry detail that used to accompany it (per-ERD config dump, per-cache-add, per-skipped feature ERD) was moved to VERBOSE because roughly 200 boot lines of it overflowed the 30-slot MQTT log buffer on the busier appliances — that overflow is what the `Dropped N messages` warning was reporting. In the custom-ERD path the tally is computed into locals before formatting so the `board_tally_*` helpers still execute when the log macros are stubbed out in tests.
7. **The MQTT write-topic subscription outlives the write bridge.** `write_topic_subscribed_` is a bridge-lifetime latch, not a data-plane one. `initialize_erd_bridge_()` (`geappliances_bridge_bridge_init.cpp`) calls `erd_write_bridge_init()` on every data-plane (re)start but registers the wildcard write filter with `esphome_mqtt_client_adapter_subscribe_write_topic()` only while the latch is clear, and the latch is cleared only in `GeappliancesBridge::teardown()`, where the adapter and its subscriptions are destroyed (`esphome_mqtt_client_adapter_destroy`). The subscription routes through the MQTT adapter's `on_write_request_event`, which every write bridge re-subscribes to, so a restart (`restart_requested_` → `restart_appliance_connection_now_()`) must not register the topic a second time: the write bridge queues rather than deduplicates (`enqueue_write_request`, `erd_write_bridge_queue_capacity` = 4, and `state_writing` enqueues while the bus is busy), so a duplicate registration would execute the same Home Assistant write command twice — two GEA writes and two `write_result` messages. Topic grammar and the `0xFF` primary-board sentinel: ADR-0012 and `docs/reference/mqtt-topics.md`.
8. **With both UARTs configured, only the active protocol's adapter is enabled.** `ProtocolStack::run()` calls `esphome_uart_adapter_set_enabled()` with `!need_gea2_loop` for the GEA3 adapter and `need_gea2_loop` for the GEA2 adapter, and only when both UARTs exist. Both adapters start a 0 ms periodic poll timer in the shared `timer_group_` inside `esphome_uart_adapter_init()` (which leaves `enabled` true), so `tiny_timer_group_run()` fires both poll callbacks on every call; a disabled adapter's `poll()` returns at the `enabled` guard without consuming bytes or publishing receive events into an interface that is not being driven. Bridge-level consequence: the `gea2_pending` value from note 4 selects which adapter is live, so a read or write issued while GEA2 autodiscovery is in flight travels the GEA2 adapter even on a dual-UART device. Stack-side statement: requirement 3.1.2 of [the ProtocolStack spec](./protocol_stack.md).

9. **The steady-state summary's per-board split is read from the running polling bridge, and its label is fixed.** `board_polled_summary_format()` (declared in `erd_bridge_common.h`, where the wording, the "the bridge already printed the count" suppression and the reason clauses live so the unit tests can see them) is fed `erd_bridge_poll_.erd_polling_addresses` and `erd_bridge_poll_.polling_list_count` into a `BOARD_POLLED_SUMMARY_BUF_SIZE` line — not `subscription_unseen_custom_erds_`, because the probe phase drops unresponsive ERDs, so the list as built is larger than the list actually polled and its split would not sum to the count printed beside it. The label stays "Polled ERDs" rather than "Poll list:" or "Polling bridge:" because `bridge_init` logs the list as built and `erd_bridge_poll` logs the list handed to the bridge, and those counts differ by design. (1208)

10. **`first_round_done` is read before this iteration's `signal_work()`.** In `loop()`, the `publisher_pause_phase_ == kResuming && erd_cache_mqtt_publisher_first_round_done(&erd_cache_publisher_)` check runs before the `erd_cache_mqtt_publisher_signal_work()` call at the end of the same function. `first_round_done` is set by the background publisher task during its drain, and this read must see the value from before the wake that this same iteration may issue: pacing can defer a wake, it can never advance one past this read. The read is deliberately NOT ordered against every wake — the resume path signals work on this same iteration whenever the cache is attached, whether or not discovery had paused the publisher. (1166)

11. **Appliance-side discovery is not the bridge's job, and it is not a data-plane bridge's job either.** The broadcast probe that identifies the appliance (the `0xFF` broadcast of ERD `0x0008`, yielding the host address and appliance type) belongs to `AutodiscoveryManager` and `DeviceIdentityManager`. Home Assistant discovery is a separate subsystem: payload building and publishing belong to `ha_discovery_manager`, stale-config deletion to `ha_discovery_cleanup`, and the decision of when either runs to `DiscoveryLifecycleManager` (ADR-0003). The bridge owns the storage and the destruction order of those structs (note 3) plus the wiring of the data plane; it must not run discovery of its own. The data-plane bridges inherit the same boundary: `erd_bridge_poll_init()` is always handed a real host address (`autodiscovery_manager_.get_host_address()`, `geappliances_bridge_bridge_init.cpp:75`) and keeps it as `erd_bridge_poll_t.known_host_address` (`erd_bridge_poll.cpp:552`), so `signal_appliance_lost` restores that address and re-probes from scratch (`erd_bridge_poll.cpp:334`, and the same restore at `:505`) rather than falling back to a broadcast. (633, 634, 677, 678, 679, 680)
12. **The polling cycle machine is deliberately quiet.** `set_cycle_phase()` assigns `poll_cycle_phase_t` without logging: a steady-state cycle changes phase three times per interval, which floods debug builds. The cycle is observable at startup through the `Entered steady-state polling: N ERDs, interval … ms` INFO line (`erd_bridge_poll.cpp:399`) and through the plain enum fields `current_state` (`polling_state_t`) and `cycle_phase` on `erd_bridge_poll_t`; the bridge re-exports the state as `GeappliancesBridge::get_polling_state()` and renders it through `polling_state_name()` in its own log lines, so a caller can watch transitions without the polling bridge coupling to ESP logging headers. Adding per-transition logging inside the bridge is the change this forbids. (642, 742)
13. **The polled-ERD summary is a formatter with a testable contract, not a log call.** `polled_erds_state_t` (`erd_bridge_common.h`) carries plain values (`polling_bridge_initialized`, `custom_erds_count`, `unseen_custom_count`) and `board_polled_summary_format()` renders into a `BOARD_POLLED_SUMMARY_BUF_SIZE` buffer, returning the characters stored or 0 with `out[0] == '\0'` when the line would add nothing; the bridge fills the struct, calls the formatter, and logs only a non-empty result. `ESP_LOGx` compiles to nothing in the unit build, so a branch that picks the wrong reason clause is invisible to the suite unless the rendered line itself is observable — the same reason the boot-log tally computes into locals before formatting (note 6). With a polling bridge running, the per-board split is suppressed unless the polled set spans two or more boards, because the count is already logged at INFO by the bridge on the same transition (`Entered steady-state polling: N ERDs`). Recorded bench figure behind the split rule, not re-measured since: `gea-esphome-refer` builds 60 ERDs and polls 53. (628, 631)
14. **Parameters that exist only for logging are marked, not deleted.** Values used solely inside `ESP_LOGx` are `(void)`-cast or declared `[[maybe_unused]]` (the component also defines `GEA_MAYBE_UNUSED` in `geappliances_bridge_log.h`) so the unit build, where those macros compile away, stays warning-free: `[[maybe_unused]] const void* data` on `poll_state_top()`, `(void)mode_name` in `geappliances_bridge_bridge_init.cpp`, the `[[maybe_unused]]` summary locals in `discovery_lifecycle_manager.cpp`. Recorded caveat while moving this: the `(void)appliance_type;` in `AutodiscoveryManager::on_broadcast_response()` claims the parameter is "used only in ESP_LOG calls that may be compiled out", but no log call in that function (`autodiscovery_manager.cpp:310-340`) references `appliance_type` — it is genuinely unused today, so the cast and the parameter are both candidates for removal. (132)
