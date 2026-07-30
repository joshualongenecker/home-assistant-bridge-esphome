# YAML Configuration Reference

Complete reference for the `geappliances_bridge` ESPHome component configuration.

## Required Parameters

| Parameter | Type | Description |
|---|---|---|
| `gea3_uart_id` | `id` | ESPHome UART component ID for GEA3 communication. |

## Optional Parameters

### `gea2_uart_id`

| Property | Value |
|---|---|
| **Type** | `id` |
| **Default** | None |
| **Description** | ESPHome UART component ID for GEA2 (older appliances). When configured, autodiscovery tries GEA3 first, then falls back to GEA2. |

### `device_id`

| Property | Value |
|---|---|
| **Type** | `string` |
| **Default** | Auto-generated from appliance ERDs |
| **Description** | Custom device ID used in MQTT topics. If omitted, the bridge reads appliance type (ERD 0x0008), model number (ERD 0x0001), and serial number (ERD 0x0002) to generate `ApplianceType_Model_Serial`. |
| **Example** | `"MyDishwasher"` |

### `mode`

| Property | Value |
|---|---|
| **Type** | `enum` |
| **Default** | `auto` |
| **Options** | `auto`, `subscribe`, `poll` |
| **Description** | Operating mode. `auto` starts with subscription and falls back to polling if the subscription bridge enters the failed state. `subscribe` uses appliance-pushed updates only. `poll` actively reads ERDs at the polling interval. |

### `polling_interval`

| Property | Value |
|---|---|
| **Type** | `int` (milliseconds) |
| **Default** | `10000` (10 seconds) |
| **Description** | Interval between poll cycles in polling mode. Must be positive. |

### `appliance_api_parsing`

| Property | Value |
|---|---|
| **Type** | `bool` |
| **Default** | `true` |
| **Description** | When `true`, reads feature bit ERDs (0x0092–0x010D) to determine which ERDs the appliance supports. In polling mode, restricts polling to only supported ERDs. When `false`, polls all known ERDs regardless of appliance support. |

### `throttle_rate_seconds`

| Property | Value |
|---|---|
| **Type** | `int` (seconds) |
| **Default** | `1` |
| **Range** | 0–255 |
| **Description** | Minimum interval in seconds between MQTT publishes for any individual ERD. Set to `0` to disable throttling (publish on every update). Useful for reducing MQTT traffic when the appliance generates frequent updates. |

### `startup_delay`

| Property | Value |
|---|---|
| **Type** | time period |
| **Default** | `10s` |
| **Description** | Defers appliance discovery/communication and the ESPHome MQTT client's first connection attempt after boot. This prevents simultaneous Wi-Fi, MQTT, and GEA traffic during boot. |

For example, to wait 30 seconds before starting bridge and MQTT activity:

```yaml
geappliances_bridge:
  gea3_uart_id: gea3_uart
  startup_delay: 30s
```

The bridge suppresses MQTT's normal `enable_on_boot` behavior while it is present, then enables MQTT once the delay elapses.

### `generate_device_config`

| **Type** | `bool` |
| **Default** | `true` |
| **Description** | When `true` (default), the bridge runs HA discovery on discovery data changes (hash or device ID) and on fresh install. The Discovery Refresh button always works regardless of this flag. Normal boots skip discovery (topics retained by MQTT broker). |

### `filter_config_topics`

| Property | Value |
|---|---|
| **Type** | `bool` |
| **Default** | `true` |
| **Description** | When `true`, filters out internal/diagnostic entities (firmware metadata, commissioning state, usage profiles, cycle definitions, fault data) from Home Assistant MQTT discovery. Reduces entity count by approximately 19%. |


## Diagnostic Sensors

The following diagnostic entities are auto-created by default with `discovery: false` (visible only via ESPHome API, not MQTT discovery). Set to `false` to disable, or provide a dict to customize.

| Parameter | Type | Description |
|---|---|---|
| `erd_publish_rate_sensor` | `sensor` or `bool` | ERD cache update rate (updates/second). |
| `erd_cache_entries_sensor` | `sensor` or `bool` | Number of entries in the ERD cache. |
| `erd_cache_updates_sensor` | `sensor` or `bool` | Total ERD cache updates since startup. |
| `mqtt_publish_rate_sensor` | `sensor` or `bool` | MQTT publish rate (messages/second). |

### Customizing Diagnostic Sensors

```yaml
geappliances_bridge:
  gea3_uart_id: gea3_uart
  erd_publish_rate_sensor:
    name: "ERD Publish Rate"
    id: erd_publish_rate
  erd_cache_entries_sensor: false  # disable
```

## Discovery Refresh Button

| Parameter | Type | Default |
|---|---|---|
| `discovery_refresh_button` | `button` or `bool` | Auto-created with `discovery: false` |

Exposes an ESPHome button entity that triggers a Home Assistant MQTT discovery cleanup when pressed. This shares the same cleanup → publish → reboot path as automatic discovery change detection (hash or device ID change). Useful for clearing stale discovery topics after firmware updates or configuration changes.

```yaml
geappliances_bridge:
  gea3_uart_id: gea3_uart
  discovery_refresh_button:
    name: "Discovery Refresh"
```

Set to `false` to disable.

## Complete Example

```yaml
geappliances_bridge:
  gea3_uart_id: gea3_uart
  gea2_uart_id: gea2_uart          # optional, for GEA2 fallback
  device_id: "MyDishwasher"        # optional, auto-generated if omitted
  mode: auto                       # auto | subscribe | poll
  polling_interval: 10000          # milliseconds
  appliance_api_parsing: true      # restrict to supported ERDs
  throttle_rate_seconds: 1         # 0-255, 0=disabled
  filter_config_topics: true       # filter diagnostic entities
  erd_publish_rate_sensor:
    name: "ERD Publish Rate"
  erd_cache_entries_sensor: true
  erd_cache_updates_sensor: true
  mqtt_publish_rate_sensor: true
  discovery_refresh_button:
    name: "Discovery Refresh"
```
