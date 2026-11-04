# ESPHome MQTT Client Adapter

## Purpose

Adapts ESPHome's MQTT client to the `i_mqtt_client` interface expected by the tiny-gea-api bridge libraries. Handles write-result publishing, write command subscriptions, and MQTT connection event notifications. ERD value publishing is handled by the ERD cache MQTT publisher.

## Public API

| Function | Description |
|----------|-------------|
| `esphome_mqtt_client_adapter_init(self, device_id)` | Initialize adapter with device ID string. Wires ESPHome MQTT connect/disconnect callbacks to tiny events. |
| `esphome_mqtt_client_adapter_subscribe_write_topic(self)` | Subscribe to the wildcard write topic `geappliances/{device_id}/erd/+/write`. No-op if MQTT client is not available. |
| `esphome_mqtt_client_adapter_notify_disconnected(self)` | Publish disconnect event to bridge HSMs. |
| `esphome_mqtt_client_adapter_notify_connected(self)` | Publish connect event to bridge HSMs. |
| `esphome_mqtt_client_adapter_destroy(self)` | Clear global MQTT client callbacks, unsubscribe from wildcard write topic, null the device ID pointer. |
| `esphome_mqtt_client_adapter_publish_raw(self, topic, payload, payload_len, retain)` | Publish raw MQTT message (C-string topic and payload). Implements the i_mqtt_client_t publish_raw vtable slot. |
| `esphome_mqtt_client_adapter_subscribe(self, topic, callback, arg)` | Subscribe to a topic with a raw C callback. Implements the i_mqtt_client_t subscribe vtable slot. |
| `esphome_mqtt_client_adapter_unsubscribe(self, topic)` | Unsubscribe from a topic. Implements the i_mqtt_client_t unsubscribe vtable slot. |

## Internal i_mqtt_client API Implementation

The adapter implements the `i_mqtt_client_api_t` interface:

| Callback | Description |
|----------|-------------|
| `update_erd_write_result(erd, board_address, success, reason)` | Publish write result to MQTT (address-prefixed topic for secondary boards). |
| `on_write_request()` | Return event pointer for write request notifications. |
| `on_mqtt_disconnect()` | Return event pointer for disconnect notifications. |
| `on_mqtt_connect()` | Return event pointer for connect notifications. |
| `publish_raw(topic, payload, payload_len, retain)` | Publish raw MQTT message. |
| `subscribe(topic, callback, arg)` | Subscribe to a topic with a C callback. |
| `unsubscribe(topic)` | Unsubscribe from a topic. |

## Dependencies

- `esphome::mqtt::global_mqtt_client` — ESPHome MQTT client singleton
- `i_mqtt_client` — interface definition
- `tiny_event` — event pub/sub system

## Key Design Decisions

- **Wildcard subscription**: Instead of subscribing to 100+ individual write topics (one per ERD), a single wildcard topic `geappliances/{device_id}/erd/+/write` is used. This eliminates:
  - 108 heap-allocated lambda closures
  - 108 IDF MQTT outbox entries (SUBSCRIBE packets)
  - A 3-second stall on MQTT reconnect from synchronous re-subscriptions
- **No pending update queue**: ERD updates are published directly to MQTT via the ERD cache MQTT publisher. There is no queue for pending updates during disconnects.
- **No settle delay**: There is no `mqtt_connected_at_ms` timestamp or settle delay mechanism. The adapter simply fires connect/disconnect events.
- **Hex payloads**: All ERD values are published as lowercase hex strings (e.g., `%02x`). String conversion is handled at the application level, not in the MQTT adapter. Incoming write payloads are hex-decoded into a stack buffer before the synchronous write-request event fires.
- **No ERD filtering**: The adapter does not consult `ErdRegistry`; valid-ERD filtering happens in the subscription bridge at cache ingestion.

## Testing

Covered by integration tests in `test/tests/`. The wildcard subscription and write-command routing are tested through the full bridge initialization flow.
