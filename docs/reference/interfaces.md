# Interface Reference

Key C interfaces used within the bridge component. For full vtable definitions, see the individual module specs.

## i_mqtt_client

Abstract MQTT client interface. Decouples bridge modules from ESPHome's MQTT implementation.

**File:** `components/geappliances_bridge/i_mqtt_client.h`
**Spec:** [i_mqtt_client spec](../spec/i_mqtt_client.md)

| Method | Signature | Description |
|---|---|---|
| `mqtt_client_update_erd_write_result()` | `void(i_mqtt_client_t* self, tiny_erd_t erd, uint8_t board_address, bool success, tiny_gea3_erd_client_write_failure_reason_t failure_reason)` | Report write success/failure to MQTT. `board_address` is 0xFF (broadcast) for the primary board (legacy topic) or the secondary board address (address-prefixed topic). |
| `mqtt_client_on_write_request()` | `i_tiny_event_t*(i_mqtt_client_t* self)` | Event fired when a write command arrives on a `*/write` topic |
| `mqtt_client_on_mqtt_disconnect()` | `i_tiny_event_t*(i_mqtt_client_t* self)` | Event fired when the client disconnects from the MQTT broker |
| `mqtt_client_on_mqtt_connect()` | `i_tiny_event_t*(i_mqtt_client_t* self)` | Event fired when the client connects to the MQTT broker |
| `mqtt_client_publish_raw()` | `void(i_mqtt_client_t* self, const char* topic, const char* payload, size_t payload_len, bool retain)` | Publish a raw MQTT message |
| `mqtt_client_subscribe()` | `void(i_mqtt_client_t* self, const char* topic, callback, void* arg)` | Subscribe to a topic with a raw C callback |
| `mqtt_client_unsubscribe()` | `void(i_mqtt_client_t* self, const char* topic)` | Unsubscribe from a topic |

**Implementation:** `EsphomeMqttClientAdapter` bridges to ESPHome's `global_mqtt_client`.

## IBridgeServices

Abstract interface between the startup HSM and the bridge. The HSM calls methods on this interface to drive startup phases without depending on the concrete `GeappliancesBridge` class.

**File:** `components/geappliances_bridge/i_bridge_services.h`
**Spec:** [IBridgeServices spec](../spec/i_bridge_services.md)

| Method | Description |
|---|---|
| `run_autodiscovery()` | Run one tick of the autodiscovery manager |
| `init_device_id_reading()` | Initialize device-ID reading |
| `start_feature_bit_reading()` | Begin the feature-bit reading sequence |
| `initialize_mqtt_client()` | Initialize the MQTT client adapter |
| `initialize_erd_bridge()` | Initialize the ERD bridge in the configured mode |
| `is_autodiscovery_complete()` | Query if autodiscovery finished |
| `is_device_id_complete()` | Query if device ID is ready |
| `is_feature_bits_complete()` | Query if feature bits are parsed |
| `is_bridge_initialized()` | Query if the ERD bridge is initialized |
| `maybe_start_custom_erd_polling()` | Start custom-ERD polling if conditions are met |
| `handle_subscription_failed()` | Handle subscription bridge failure (AUTO fallback) |
| `handle_polling_failed()` | Handle polling bridge failure |
| `check_steady_state()` | Check if appliance-side data path reached steady state |
| `initialize_erd_cache_publisher()` | Initialize the ERD cache MQTT publisher |
| `loop()` | Drive ongoing work in main loop |

**Implementation:** `GeappliancesBridge` implements this interface.

## i_tiny_gea3_erd_client_t

GEA3 ERD client interface from the `tiny-gea-api` submodule.

| Method | Description |
|---|---|
| `read()` | Read an ERD from the appliance |
| `write()` | Write data to an ERD |
| `subscribe()` | Subscribe to ERD publications |
| `retain_subscription()` | Retain active subscriptions |

**Event:** `on_activity` — fired on read/write/subscribe completion.

## i_tiny_gea2_erd_client_t

GEA2 ERD client interface from the `tiny-gea-api` submodule. Same methods as GEA3, but `subscribe()` and `retain_subscription()` are not supported.

## i_tiny_uart_t

UART HAL interface from the `tiny` submodule. The `EsphomeUartAdapter` implements this to bridge ESPHome's `UARTComponent` to the GEA protocol stack.

| Method | Description |
|---|---|
| `send()` | Transmit bytes |
| `on_receive()` | Event callback for received data |

## i_tiny_time_source_t

Monotonic clock interface. The `EsphomeTimeSource` implements this using ESPHome's `millis()`.

| Method | Description |
|---|---|
| `now()` | Current time in milliseconds |