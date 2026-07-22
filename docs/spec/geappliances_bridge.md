# GeappliancesBridge — Specification

## 1. Overview

### 1.1 Purpose

The main ESPHome component class that orchestrates the entire GE Appliances bridge. It manages UART interfaces for GEA2/GEA3 protocols, drives the startup state machine, handles MQTT connection lifecycle, and coordinates all sub-managers (autodiscovery, device identity, feature bits).

### 1.2 Responsibilities

- Own and construct all component instances (adapters, managers, bridges)
- Wire components together during `setup()`
- Drive the GEA2 tight-loop and delegate ongoing work in `loop()`
- Expose configuration setters called by the ESPHome code generator
- Implement `IBridgeServices` so the startup HSM can request bridge actions without depending on this concrete class

### 1.3 Not Responsible For

- Assembling the device ID (`DeviceIdentityManager`)
- Determining which ERDs are valid (`FeatureBitManager` / `ErdRegistry`)
- MQTT connection lifecycle (`EsphomeMqttClientAdapter`)
- Startup phase sequencing (`StartupHsm`)

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
| `run_protocol_stack_()` | Drive GEA2/GEA3 hardware (includes GEA2 tight loop) |
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

## 6. GEA2 Tight Loop

When GEA2 is active, `run_protocol_stack_()` executes a 100 ms wall-clock busy loop to ensure the full TX→RX cycle at 19200 baud completes within a single `loop()` call. A manual millisecond counter (`gea2_msec_interrupt_`) drives the GEA2 interface's internal timers without starving the shared `timer_group_`.

| Constant | Value | Description |
|----------|-------|-------------|
| `GEA2_LOOP_DURATION_MS` | 100 ms | Wall-clock duration for GEA2 tight loop |
| `GEA3_LOOP_DURATION_MS` | 10 ms | Wall-clock duration for GEA3 protocol tick |

---

## 7. Data Structures

Key member variables:

| Member | Type | Description |
| `startup_hsm_wrapper_` | `startup_hsm_wrapper_t` | Startup state machine wrapper (embeds `tiny_hsm_t` and `IBridgeServices*`) |
| `uart_` / `gea2_uart_` | `UARTComponent*` | UART interfaces |
| `erd_cache_` | `erd_cache_t` | Shared ERD cache |
| `erd_cache_publisher_` | `erd_cache_mqtt_publisher_t` | MQTT publisher |
| `erd_registry_` | `ErdRegistry` | Valid/registered ERD tracking |
| `mqtt_client_adapter_` | `esphome_mqtt_client_adapter_t` | MQTT adapter |
| `uart_adapter_` / `gea2_uart_adapter_` | `esphome_uart_adapter_t` | UART adapters |
| `erd_client_` / `gea2_erd_client_` | ERD client | GEA3/GEA2 ERD clients |
| `gea2_erd_client_adapter_` | `gea2_erd_client_adapter_t` | GEA2→GEA3 adapter |
| `erd_bridge_subscribe_` | `erd_bridge_subscribe_t` | Subscription bridge |
| `erd_bridge_poll_` | `erd_bridge_poll_t` | Polling bridge |
| `erd_write_bridge_` | `erd_write_bridge_t` | Write bridge |
| `autodiscovery_manager_` | `AutodiscoveryManager` | Appliance discovery |
| `device_identity_manager_` | `DeviceIdentityManager` | Device ID generation |
| `feature_bit_manager_` | `FeatureBitManager` | Feature bit reading |
| `timer_group_` | `tiny_timer_group_t` | Shared timer group |
| `custom_erds_[CUSTOM_ERDS_MAX]` | `tiny_erd_t[128]` | User-configured custom ERDs |
| `poll_probe_list_[POLLING_LIST_MAX_SIZE]` | `uint16_t[649]` | Pre-built probe list |

---

## 8. Invariants

1. **IBridgeServices implementation:** `GeappliancesBridge` implements `IBridgeServices`, the abstract contract consumed by the startup HSM. This eliminates `friend` declarations and lets the HSM be unit-tested with a mock.
2. **Probe list ownership:** The `poll_probe_list_` member stores the built probe list so the pointer passed to `erd_bridge_poll_init()` remains valid across the probe phase.
3. **ERD cache publisher delegation:** The `erd_cache_mqtt_publisher_` drains `update_required` entries from the shared cache and publishes them to MQTT each `loop()`, decoupling the bridges from direct MQTT interaction.
4. **Single appliance:** The bridge operates with a single discovered appliance address.
5. **Fixed capacity arrays throughout:** All data structures use fixed-capacity arrays to avoid heap allocation (custom ERDs: 64, probe list: 649, ERD cache: 200).

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
2. **Fixed capacity arrays:** All data structures use fixed-capacity arrays. If the appliance supports more ERDs than the cache can hold (200), updates are silently dropped.
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

Change detection runs in `check_steady_state()` (not `setup()`), since device identity is not available until steady state is reached. After each successful publish, a `DiscoveryNVS {hash, device_id, appliance_api_parsing, filter_config_topics}` struct is stored in NVS for comparison on the next boot.

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
| `erd_cache_publisher_paused_` | `bool` | Tracks whether the ERD cache publisher was paused during discovery activity. Set to `true` when `pause()` is called, and `false` when `resume()` is called. Used to avoid redundant pause/resume calls across `loop()` iterations. Initialized to `false`. |
| `discovery_just_resumed_` | `bool` | Set to `true` when the publisher is resumed after discovery activity ends. Cleared to `false` once `erd_cache_mqtt_publisher_first_round_done()` returns `true`, indicating the publisher has completed a full cache round after resuming. Initialized to `false`. |

The following state variables were previously members of `GeappliancesBridge` but have been extracted into their respective managers:

| Variable | Owner |
|----------|-------|
| `discovery_refresh_in_progress_` | `OtaCleanupManager` |
| `ota_cleanup_needed_` | `OtaCleanupManager` |
| `ota_cleanup_in_progress_` | `OtaCleanupManager` |
| `ota_discovery_publishing_` | `OtaCleanupManager` |
| `ota_reboot_pending_` | `OtaCleanupManager` |
| `ota_reboot_start_ms_` | `OtaCleanupManager` |
| `initial_discovery_needed_` | `OtaCleanupManager` |
| `initial_discovery_done_` | `OtaCleanupManager` |
| `cleanup_trigger_` | `OtaCleanupManager` |
| `last_erd_publish_rate_publish_` | `DiagnosticSensorPublisher` |
| `last_erd_cache_stats_publish_` | `DiagnosticSensorPublisher` |
| `last_mqtt_disconnect_stats_publish_` | `DiagnosticSensorPublisher` |

### 11.3 Discovery Change Detection (check_steady_state())

When steady state is first reached, `check_steady_state()` calls `ota_cleanup_manager_.check_discovery_changes()` with the current device ID. Under `#ifdef USE_ESP_IDF`, the method loads a `DiscoveryNVS {hash, device_id, appliance_api_parsing, filter_config_topics}` struct from NVS preferences (key `0x64697363u` / "disc").

Four outcomes are possible:

- **No stored state (fresh install):** `trigger_initial_discovery()` is called. The bridge will publish discovery topics without cleanup, then reboot.
- **Hash, device ID, appliance_api_parsing, or filter_config_topics changed:** `trigger_ota_cleanup()` is called. The bridge will clean old topics, publish fresh discovery, and reboot.
- **No changes detected:** Nothing happens. The bridge proceeds normally with retained topics on the broker.

The hash is compared against `HA_DISCOVERY_DATA_HASH`, a compile-time constant generated by the discovery pipeline. The device ID is compared against the current value from `DeviceIdentityManager`. The config flags are compared against the current YAML values.

### 11.4 loop() Delegation

Each `loop()` iteration delegates discovery and diagnostic work to the extracted managers:

1. **Update publisher state:** Call `update_publisher_state_()` to manage the publisher pause/resume state machine and signal or run the publisher.

2. **Discovery cleanup and refresh:** Delegates to `ota_cleanup_manager_.loop()`, which drives the state machine for all discovery paths: initial publish (fresh install), cleanup + republish (hash/device ID change or Discovery Refresh), and the final reboot. The state machine gates on steady state, MQTT initialization, and device ID completion, and handles the full lifecycle: optional cleanup, discovery publishing, safe-mode counter clearing, OTA rollback cancellation, NVS state storage, and the final reboot delay.

3. **Diagnostic sensor publishing:** Delegates to `diagnostic_sensor_publisher_.loop()`, which publishes ERD publish rate, MQTT publish rate, ERD cache stats, and MQTT disconnect stats at their configured intervals (~60 seconds).

### 11.5 trigger_discovery_refresh() Flow

`trigger_discovery_refresh()` is now a **public** method (previously protected with `friend DiscoveryRefreshButton`). It delegates to `ota_cleanup_manager_.trigger_discovery_refresh()`.

The actual behavior is implemented in `OtaCleanupManager`:

1. **Idempotency guard:** If any discovery operation is already in progress (`discovery_refresh_in_progress_`, `ota_cleanup_needed_`, `ota_cleanup_in_progress_`, `ota_discovery_publishing_`, or `ota_reboot_pending_`), log a warning ("Discovery refresh already in progress, ignoring") and return.
2. **Queue the request:** Set `discovery_refresh_in_progress_` to `true`, set `cleanup_trigger_` to `DISCOVERY_REFRESH`, and log "Discovery refresh queued, will execute when appliance is ready".

No guard checks for steady state, MQTT readiness, or device ID are performed at press time. The request is queued and will execute in `loop()` once all prerequisites are met. This allows the user to press the button at any time, even during startup.

### 11.6 NVS State Storage and Reboot Preparation

After discovery publishing completes, `OtaCleanupManager.loop()` performs several steps before rebooting:

1. **Clear the safe mode boot loop counter:** Writes `0` to the ESPHome preferences key matching `SAFE_MODE_RTC_KEY` (233825507UL) and syncs immediately. This prevents the device from entering safe mode due to rapid reboots during the cleanup → publish → reboot cycle.
2. **Cancel OTA rollback:** Calls `esp_ota_mark_app_valid_cancel_rollback()` to mark the current OTA partition as valid, preventing the bootloader from rolling back to the previous partition on the next boot.
3. **Store discovery state in NVS:** Saves a `DiscoveryNVS {hash, device_id, appliance_api_parsing, filter_config_topics}` struct to NVS preferences (key `0x64697363u` / "disc"). The `hash` field is set to `HA_DISCOVERY_DATA_HASH`, `device_id` from `DeviceIdentityManager`, and both config flags from the current YAML configuration. This stored state is compared against on the next boot to detect changes requiring cleanup.

### 11.7 Integration with ha_discovery_manager

The bridge owns a `ha_discovery_manager_t` instance (`ha_discovery_manager_`). The manager is driven by `OtaCleanupManager` from `loop()` via delegation:

- **Configuration:** `ha_discovery_manager_configure()` is called when discovery starts (during any discovery path: initial publish, change-triggered cleanup, or Discovery Refresh), passing the device identity (ID, model, serial, appliance type), the `filter_config_topics_` flag, the ERD cache, and the MQTT client interface.
- **Start:** `ha_discovery_manager_start()` transitions the manager from `IDLE` to `BUILDING`.
- **Drive:** `ha_discovery_manager_run()` is called each `loop()` iteration while `ha_discovery_manager_is_processing()` returns `true`. This advances the manager through its state machine, decompressing JSONL chunks and publishing discovery payloads at a rate-limited interval (50 ms).
- **Completion:** When the manager reaches `COMPLETE` or `FAILED` state, `ha_discovery_manager_is_processing()` returns `false`, signaling that discovery publishing is done.

### 11.8 Integration with ha_discovery_cleanup

The cleanup module is accessed via `ha_discovery_manager_.cleanup` and driven by `OtaCleanupManager`. All cleanup paths (hash/device ID change, Discovery Refresh) use the same cleanup module; initial publish skips cleanup entirely.

`OtaCleanupManager` handles the full lifecycle:

- **Configuration:** `ha_discovery_cleanup_configure()` is called with the device ID, MQTT client interface, and time source.
- **Start:** `ha_discovery_cleanup_start()` begins the cleanup process.
- **Drive:** `ha_discovery_cleanup_run()` is called each `loop()` iteration within `OtaCleanupManager`.
- **Completion:** `ha_discovery_cleanup_is_done()` returns `true` when all phases are complete. `OtaCleanupManager` then destroys the cleanup module, resets the discovery manager, and proceeds to publish fresh discovery topics.

### 11.9 Reboot with safe_reboot()

After cleanup and discovery publishing complete, `OtaCleanupManager` reboots using `esphome::App.safe_reboot()` instead of `esphome::App.reboot()`. `safe_reboot()` performs a graceful MQTT disconnect before resetting, ensuring a clean session end on the broker. This prevents the broker from thinking the device is still connected, which could cause stale availability topics or delayed reconnection events.

A 5-second delay precedes the reboot to allow final discovery messages to transmit and the heap to stabilize. The reboot also defragments the heap after the memory-intensive cleanup and publish cycle.

### 11.10 Publisher Pause/Resume Rationale

The ERD cache publisher is paused during discovery activity to avoid competing for the ESP-IDF MQTT task's inbound/outbound queues. Without pausing, the background publisher task would continue draining cache updates while the discovery manager publishes config payloads, potentially causing:

- MQTT queue overflow, leading to dropped messages
- Retained-clear messages from cleanup being overwritten by late cache publishes
- Unpredictable ordering of discovery config vs. value messages on the broker

The pause/resume cycle ensures a clean separation: discovery messages are published without interference, then the cache publisher resumes and drains accumulated updates. The `first_round_done` mechanism confirms the publisher has completed a full cache round after resuming, clearing `discovery_just_resumed_`. The `steady_state_reached_` flag is set by `check_steady_state()` (called from the startup HSM), which also triggers discovery change detection at that point; this is the signal that gates HA discovery startup.
