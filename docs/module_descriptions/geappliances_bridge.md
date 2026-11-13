# GeappliancesBridge

## Purpose

The main ESPHome component class that orchestrates the entire GE Appliances bridge. It manages UART interfaces for GEA2/GEA3 protocols, drives the startup state machine, handles MQTT connection lifecycle, and coordinates all sub-managers (autodiscovery, device identity, feature bits, discovery change detection/cleanup, diagnostic sensor publishing).

## Public API

| Method | Description |
|--------|-------------|
| `setup()` | Initialize timer group, UART adapters, ERD clients, GEA interfaces, managers |
| `loop()` | Drive protocol stack and startup HSM |
| `dump_config()` | Log current configuration and state |
| `get_setup_priority()` | Returns `setup_priority::DATA` (600) — after MQTT (50), same as UART |
| `teardown()` | Clean up bridges and MQTT adapter |

### Configuration Setters (called from `__init__.py` code generation)

| Setter | Description |
|--------|-------------|
| `set_gea3_uart(uart)` / `set_gea2_uart(uart)` | Configure UART interfaces |
| `set_client_address(address)` | Set the bridge's bus address (default 0xE4) |
| `set_device_id(id)` | Pre-configure a static device ID |
| `set_mode(mode)` | Set bridge mode: POLL (0), SUBSCRIBE (1), or AUTO (2) |
| `set_polling_interval(ms)` | Set polling interval (default 10000 ms) |
| `set_appliance_api_parsing(bool)` | Enable feature bit-based ERD filtering (default true) |
| `set_generate_device_config(bool)` | Enable/disable HA device config generation (default true). When enabled, the bridge checks for discovery changes (hash or device ID) at steady state and runs cleanup + republish if needed; also enables initial discovery publish on fresh installs. The Discovery Refresh button always works regardless of this flag. Normal boots skip discovery (topics retained by MQTT broker). |
| `set_erd_publish_rate_sensor(sensor)` | Set sensor for ERD publish rate monitoring |
| `set_erd_cache_entries_sensor(sensor)` | Set sensor for ERD cache entries count |
| `set_erd_cache_updates_sensor(sensor)` | Set sensor for ERD cache updates count |
| `set_mqtt_publish_rate_sensor(sensor)` | Set sensor for MQTT publish rate monitoring |
| `set_throttle_rate_seconds(rate)` | Set minimum interval (seconds) between publishes per ERD (default 1, range 0-255) |
| `add_custom_erd(erd)` | Add a custom ERD to poll |
## Protected Methods

| `handle_erd_client_activity_(args)` | Route ERD activity to appropriate manager (device identity, feature bits, subscription ERD tracking) |
| `should_route_to_feature_bits_(erd)` | Decide whether an ERD read goes to FeatureBitManager or DeviceIdentityManager |
| `initialize_mqtt_client_()` | Create and configure the MQTT client adapter |
| `initialize_erd_bridge_()` | Initialize subscription or polling bridge based on mode |
| `init_erd_cache_publisher_()` | Initialize the ERD cache MQTT publisher and HA discovery manager |
| `init_polling_bridge_(log_as_info)` | Initialize the polling bridge with the probe list |
| `run_protocol_stack_()` | Drive GEA2/GEA3 hardware (includes GEA2 tight loop) |
| `update_publisher_state_()` | Publisher pause/resume during HA discovery + discovery_just_resumed_ tracking |
| `start_feature_bit_reading_()` | Start the feature bit read sequence |
| `start_custom_erd_polling_()` | Initialize polling for user-configured custom ERDs |
| `maybe_start_custom_erd_polling_()` | Guarded entry point for custom ERD polling (prevents re-initialization) |
| `log_poll_state_transitions_()` | Debug: log polling HSM state changes |
| `on_poll_discovery_complete_()` | Callback from polling bridge when probe phase completes |
| `trigger_discovery_refresh()` | Queue a discovery cleanup + republish + reboot. Public method (was protected). Delegates to `discovery_lifecycle_manager_.trigger_discovery_refresh()`. If pressed before steady state/MQTT/device ID are ready, the request is queued and executes once the bridge is ready. |

The bridge progresses through a linear sequence of phases via the `startup_hsm_`:

```
startup_delay → autodiscovery → device_id → mqtt_client_init
             → feature_bits → bridge_init → subscription_watch
             → running
```

## Bridge Initialization Flow

During `startup_state_bridge_init`, `initialize_erd_bridge_()` runs:

1. **Apply ERD filter**: If appliance API parsing is enabled and complete, sets the valid-ERD filter on the registry.
2. **Select mode**: Determines polling vs. subscription based on mode setting and GEA2/GEA3 protocol.
3. **Build probe list**: Calls `build_poll_list_()` (which delegates to `erd_poll_list_builder`) to build the list of ERDs to probe.
4. **Initialize bridges**:
   - **Polling mode**: Initializes `erd_bridge_poll_` with the probe list, known host address, and appliance type.
   - **Subscription mode**: Initializes `erd_bridge_subscribe_` with the known host address.
   - **Write bridge**: Always initialized with the real host address (updated after appliance identification).

## Dependencies

- ESPHome `Component` base class
- ESPHome `uart::UARTComponent` — UART interfaces
- ESPHome `mqtt::MQTTClientComponent` — MQTT client
- `tiny_gea3_interface`, `tiny_gea3_erd_client` — GEA3 protocol stack
- `tiny_gea2_interface`, `tiny_gea2_erd_client` — GEA2 protocol stack
- All sub-managers: `AutodiscoveryManager`, `DeviceIdentityManager`, `FeatureBitManager`
- Adapters: `esphome_uart_adapter`, `esphome_mqtt_client_adapter`, `gea2_erd_client_adapter`
- Bridges: `erd_bridge_subscribe`, `erd_bridge_poll`, `erd_write_bridge`
- `erd_poll_list_builder` — builds the probe list for the polling bridge
- `erd_registry` — single owner of valid-ERD filter, string-type set, and registered-ERD tracking
- `erd_cache_mqtt_publisher` — drains ERD cache updates to MQTT from its own background task, one entry per wake, paced while the backlog is large
- `erd_bridge_common.h` — shared signals, timing constants, and utility templates
- `DiscoveryLifecycleManager` — owns the discovery change detection, cleanup → republish → reboot phase machine, initial discovery publish, and the DiscoveryRefresh path
- `DiagnosticSensorPublisher` — owns periodic publishing of diagnostic sensor values (ERD/MQTT publish rate, cache stats, disconnect stats)
- `tiny_hsm`, `tiny_timer` — state machine and timer infrastructure

## Key Design Decisions

- **GEA2 tight loop**: When GEA2 is active, a 100 ms wall-clock busy loop ensures the full TX→RX cycle at 19200 baud completes within a single `loop()` call. A manual millisecond counter drives the GEA2 interface's internal timers without starving the shared timer group.
- **Bridge modes**: Three modes — POLL (always poll), SUBSCRIBE (always subscribe), AUTO (try subscribe, fall back to polling after 10 s if no activity).
- **IBridgeServices interface**: `GeappliancesBridge` implements `IBridgeServices`, the abstract contract consumed by the startup HSM. This eliminates `friend` declarations and lets the HSM be unit-tested with a mock.
- **Phase timeouts**: Device ID phase has a 30 s timeout, feature bits phase has a 60 s timeout — both prevent the startup HSM from stalling indefinitely.
- **Probe list ownership**: The `poll_probe_list_` member stores the built probe list so the pointer passed to `erd_bridge_poll_init()` remains valid across the probe phase.
- **loop() delegation:** `loop()` delegates ongoing work to `discovery_lifecycle_manager_.loop()` (discovery change detection, cleanup, republish, initial publish, reboot) and `diagnostic_sensor_publisher_.loop()` (periodic diagnostic sensor publishing) instead of driving them inline.

## Testing

Covered by integration tests in `test/tests/` that exercise the full startup sequence, mode switching, and error recovery paths.
