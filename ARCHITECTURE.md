# Architecture

An ESPHome custom component (`components/geappliances_bridge/`) that bridges the GE
Appliances ERD (Extended Read/Write Data) protocol to Home Assistant over MQTT, running
on ESP32-S3/C3/C6 (ESP-IDF 5.5.5).

It talks to a GE appliance over UART (GEA2 or GEA3 protocol), reads ERDs (sensors,
settings, diagnostics), and exposes them to Home Assistant via MQTT discovery. It also
accepts writes from Home Assistant (e.g. set a temperature) and forwards them to the
appliance.

This file is a quick map. For detail, see:
- `docs/architecture/overview.md` — C4 model (system context, container, component diagrams)
- `docs/architecture/startup-sequence.md` — startup HSM sequence
- `docs/architecture/data-flow.md` — read / write / discovery flows
- `docs/module_descriptions/` — per-module summaries
- `docs/spec/` — detailed behavioral contracts per module

## Data flow

```
UART (GEA2 / GEA3)
   |
   v
[Startup HSM] -> autodiscovery -> device ID -> feature bits -> valid ERD set
   |
   v
[ERD Bridge] -> poll / subscribe ERDs -> erd_cache (fixed array, in-place updates)
   |
   v
[MQTT publisher task] -> snapshot -> Home Assistant (discovery + telemetry)
   ^
   |
[Write Bridge] <- MQTT write topic <- Home Assistant (commands)
```

Startup is driven by a hierarchical state machine (`geappliances_bridge_startup_hsm`):
it sequences autodiscovery, device-ID reading, and feature-bit reading, then hands off
to the ERD bridge. The bridge runs in **poll** or **subscribe** mode (or **auto**, which
tries subscribe and falls back to poll).

## Module map

### Component entry
| File | Responsibility |
|------|----------------|
| `geappliances_bridge.h/.cpp` | Main `GeappliancesBridge` class; owns the managers and drives the main loop. |
| `geappliances_bridge_bridge_init.cpp` | One-time bridge initialization (wires up the managers). |
| `geappliances_bridge_startup_hsm.h/.cpp` | Startup state machine: sequences autodiscovery -> device ID -> feature bits -> bridge. |
| `geappliances_bridge_constants.h` | Shared constants (ERD IDs, timeouts, buffer sizes). |
| `geappliances_bridge_log.h` | Logging macros (`ESP_LOG*` wrappers with per-module tags). |
| `__init__.py` | ESPHome component registration: config schema and `MODE_*` constants. |

### Startup / discovery
| File | Responsibility |
|------|----------------|
| `autodiscovery_manager.h/.cpp` | Finds the appliance on the bus (broadcast) with GEA3<->GEA2 protocol fallback; retries indefinitely. |
| `device_identity_manager.h/.cpp` | Reads the appliance device ID (serial, model, appliance type). |
| `feature_bit_manager.h/.cpp` | Reads the feature-bit ERDs to determine which ERDs the appliance supports. |

### ERD bridge (data plane)
| File | Responsibility |
|------|----------------|
| `erd_bridge_common.h` | Shared bridge types (`subscription_state_t`, `polling_state_t`, etc.). |
| `erd_bridge_poll.h/.cpp` | Polling bridge: 3-phase polling lifecycle (init -> poll -> complete). |
| `erd_bridge_subscribe.h/.cpp` | Subscription bridge: subscribes to appliance ERD updates. |
| `erd_write_bridge.h/.cpp` | Write bridge: MQTT write topic -> ERD write to the appliance. |
| `erd_poll_list_builder.h/.cpp` | Builds the list of ERDs to poll from the feature-bit results. |

### ERD storage
| File | Responsibility |
|------|----------------|
| `erd_cache.h/.cpp` | ERD cache; mutex-protected fixed 300-slot array + 4096-byte bump arena, keyed by `(erd, board_address)`. Entries never removed; data updated in place; O(n) linear lookup. |
| `erd_registry.h/.cpp` | Holds the valid-ERD set (from feature bits) and filters cache entries against it (max 645). |

### MQTT
| File | Responsibility |
|------|----------------|
| `erd_cache_mqtt_publisher.h/.cpp` | Background publisher task (Core 0, priority 1); snapshots cache entries and publishes to MQTT. |
| `i_mqtt_client.h` | MQTT client interface (abstraction over ESPHome's MQTT). |
| `esphome_mqtt_client_adapter.h/.cpp` | ESPHome MQTT client adapter (implements `i_mqtt_client`). |

### Home Assistant discovery
| File | Responsibility |
|------|----------------|
| `ha_discovery_manager.h/.cpp` | Publishes HA discovery config; chunked + compressed, one entity per main-loop call. |
| `ha_discovery_cleanup.h/.cpp` | Cleans up stale discovery topics (per-instance `portMUX_TYPE` spinlock). |

### Lifecycle managers
| File | Responsibility |
|------|----------------|
| `discovery_lifecycle_manager.h/.cpp` | HA discovery lifecycle phase machine (initial publish, cleanup + republish, reboot). |
| `diagnostic_sensor_publisher.h/.cpp` | Publishes diagnostic/telemetry sensors (publish rates, cache entries/updates, MQTT disconnect stats). |

### Adapters / interfaces
| File | Responsibility |
|------|----------------|
| `esphome_uart_adapter.h/.cpp` | UART adapter (byte-level TX/RX for the GEA protocol). |
| `gea2_erd_client_adapter.h/.cpp` | GEA2 ERD client adapter (wraps the tiny-gea-api GEA2 client). |
| `esphome_time_source.h/.cpp` | Time source adapter (wraps ESPHome's time). |
| `i_bridge_services.h` | `IBridgeServices` interface: the contract between the startup HSM and the bridge. |
| `bridge_mode.h` | `BridgeMode` enum (POLL / SUBSCRIBE / AUTO); must match `__init__.py` `MODE_*` constants. |

### Generated files (do not edit)
| File | Generated by |
|------|--------------|
| `erd_lists.h/.cpp` | `scripts/generate_erd_lists.py` — ERD category lists. |
| `appliance_api_feature_lists.h/.cpp` | `scripts/generate_erd_lists.py` — feature-bit -> ERD lists. |
| `ha_discovery_data.h/.cpp` | `scripts/ha_discovery/run_pipeline.py` (compress step) — compressed HA discovery JSONL. |
| `appliance_type_map.h` | `scripts/generate_erd_lists.py` — appliance type (ERD 0x0008) -> display-name identifier. |

CI verifies the generated files are in sync with their generators via
`scripts/check_generated_sync.sh` (run `make check-generated` locally).

## Key invariants

See the **Critical Invariants** section in `.github/copilot-instructions.md` for the
load-bearing contracts: cache concurrency, publisher core/priority/snapshot, discovery
spinlock, the 248-byte ERD limit, multi-board keying, and the generated-files rule.
