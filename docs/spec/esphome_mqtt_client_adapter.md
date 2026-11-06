# esphome_mqtt_client_adapter — Specification

## 1. Overview

### 1.1 Purpose

Bridge ESPHome's C++ MQTT client to the component's C `i_mqtt_client_t` interface: publish ERD values and write results, receive write commands on a single wildcard topic, and republish MQTT connect/disconnect state as `tiny_event_t` events for the bridges and the ERD cache publisher.

### 1.2 Responsibilities

- Implement the `i_mqtt_client_t` vtable interface (`i_mqtt_client_api_t`)
- Publish write results to `geappliances/{device_id}/erd/0x{ERD}/write_result` topics (address-prefixed for secondary boards)
- Provide MQTT connect/disconnect events (`tiny_event_t`) for publisher coordination
- Accept write commands through a single wildcard subscription topic rather than per-ERD subscriptions
- Decode incoming hex write payloads before dispatching the write-request event

### 1.3 Not Responsible For

- Deciding which ERDs are valid (the subscription bridge applies the `ErdRegistry` filter at cache ingestion)
- Managing bridge lifecycle or startup phases
- ERD value serialization or string conversion
- MQTT connection management (connect, disconnect, reconnection — handled by ESPHome)

---

## 2. Data Structures

### 2.1 Adapter struct

```c
typedef struct {
  i_mqtt_client_t interface;
  const char* device_id;
  tiny_event_t on_write_request_event;
  tiny_event_t on_mqtt_disconnect_event;
  tiny_event_t on_mqtt_connect_event;
  // Tracked write topic for unsubscribe on destroy.
  char write_topic_[128];
} esphome_mqtt_client_adapter_t;
```

| Field | Type | Description |
|-------|------|-------------|
| `interface` | `i_mqtt_client_t` | Handle wrapping the vtable pointer; set during `init()` |
| `device_id` | `const char*` | Raw pointer to caller-owned string; used in topic construction. Not heap-allocated by the adapter. |
| `on_write_request_event` | `tiny_event_t` | Event published when a write command arrives via MQTT |
| `on_mqtt_disconnect_event` | `tiny_event_t` | Event published when the MQTT client disconnects |
| `on_mqtt_connect_event` | `tiny_event_t` | Event published when the MQTT client connects |
| `write_topic_` | `char[128]` | Tracked write topic string, populated by `subscribe_write_topic()`. Used for unsubscribe during `destroy()`. |

### 2.2 Static vtable

A single `static const i_mqtt_client_api_t api` instance is defined at file scope and shared across all adapter instances. The vtable is assigned to `self->interface.api` during `init()` and never modified at runtime.

---

## 3. Public API

### 3.1 `esphome_mqtt_client_adapter_init`

```c
void esphome_mqtt_client_adapter_init(
  esphome_mqtt_client_adapter_t* self,
  const char* device_id);
```

Store the device ID pointer, initialize the three events, and wire `esphome::mqtt::global_mqtt_client`'s `set_on_connect`/`set_on_disconnect` callbacks to `notify_connected()`/`notify_disconnected()`. If the client is already connected at init time, fire the connect event immediately. A null global client (unit tests, early boot) leaves the events initialized but unwired.

### 3.2 `esphome_mqtt_client_adapter_notify_disconnected`

```c
void esphome_mqtt_client_adapter_notify_disconnected(esphome_mqtt_client_adapter_t* self);
```

Publishes `on_mqtt_disconnect_event`. Called from the ESPHome MQTT disconnect callback.

### 3.3 `esphome_mqtt_client_adapter_notify_connected`

```c
void esphome_mqtt_client_adapter_notify_connected(esphome_mqtt_client_adapter_t* self);
```

Publishes `on_mqtt_connect_event`. Called from the ESPHome MQTT connect callback (or during `init()` when already connected).

### 3.4 `esphome_mqtt_client_adapter_subscribe_write_topic`

```c
void esphome_mqtt_client_adapter_subscribe_write_topic(esphome_mqtt_client_adapter_t* self);
```

Subscribe to the wildcard write topic `geappliances/{device_id}/erd/+/write` (QoS 0), record the topic string in `write_topic_`. No-op if the global MQTT client is null. See §6 for message routing.

### 3.5 `esphome_mqtt_client_adapter_destroy`

```c
void esphome_mqtt_client_adapter_destroy(esphome_mqtt_client_adapter_t* self);
```

Clear the global client's connect/disconnect callbacks, unsubscribe from the tracked write topic, null `device_id`, and clear `write_topic_`. Prevents dangling lambda captures from firing after teardown.

### 3.6 `esphome_mqtt_client_adapter_publish_raw`

```c
bool esphome_mqtt_client_adapter_publish_raw(
  i_mqtt_client_t* self,
  const char* topic,
  const char* payload,
  size_t payload_len,
  bool retain);
```

Forward to `esphome::mqtt::global_mqtt_client->publish()` at QoS 0. Returns `false` if the client is null or disconnected without attempting the publish.

### 3.7 `esphome_mqtt_client_adapter_subscribe`

```c
void esphome_mqtt_client_adapter_subscribe(
  i_mqtt_client_t* self,
  const char* topic,
  void (*callback)(const char* topic, const char* payload, size_t payload_len, void* arg),
  void* arg);
```

Wrap the C callback in a lambda and register it with the global MQTT client (QoS 0). No-op if the client is null.

### 3.8 `esphome_mqtt_client_adapter_unsubscribe`

```c
void esphome_mqtt_client_adapter_unsubscribe(i_mqtt_client_t* self, const char* topic);
```

Remove a subscription from the global MQTT client. No-op if the client is null.

---

## 4. i_mqtt_client API Implementation

The adapter implements all seven vtable slots of `i_mqtt_client_api_t`:

### 4.1 `update_erd_write_result`

```c
void update_erd_write_result(
  i_mqtt_client_t* self,
  tiny_erd_t erd,
  uint8_t board_address,
  bool success,
  tiny_gea3_erd_client_write_failure_reason_t failure_reason);
```

Publish the result of a write request to `geappliances/{device_id}/erd/0x{ERD}/write_result`, or `geappliances/{device_id}/erd/0x{ADDR}_0x{ERD}/write_result` when `board_address` is not the broadcast sentinel `0xFF` (secondary board). On success, the payload is `"ok"`. On failure, the payload is a JSON object: `{"error":"<reason>"}` where `<reason>` is one of `retries_exhausted`, `not_supported`, `incorrect_size`, `queue_full`, or `unknown`. The message is published with retain flag set. Guards against null or disconnected MQTT client.

### 4.2 `on_write_request`

```c
i_tiny_event_t* on_write_request(i_mqtt_client_t* self);
```

Return a pointer to the `on_write_request_event` interface. The `erd_write_bridge` subscribes to this event to handle incoming write commands.

### 4.3 `on_mqtt_disconnect`

```c
i_tiny_event_t* on_mqtt_disconnect(i_mqtt_client_t* self);
```

Return a pointer to the `on_mqtt_disconnect_event` interface. Bridges subscribe to this to pause or defer operations until reconnection.

### 4.4 `on_mqtt_connect`

```c
i_tiny_event_t* on_mqtt_connect(i_mqtt_client_t* self);
```

Return a pointer to the `on_mqtt_connect_event` interface. Bridges subscribe to this to resume operations or re-publish state.

### 4.5 `publish_raw`

```c
bool publish_raw(
  i_mqtt_client_t* self,
  const char* topic,
  const char* payload,
  size_t payload_len,
  bool retain);
```

Forward to `esphome_mqtt_client_adapter_publish_raw()`. Guards against null or disconnected MQTT client; returns the publish result.

### 4.6 `subscribe`

```c
void subscribe(
  i_mqtt_client_t* self,
  const char* topic,
  void (*callback)(const char* topic, const char* payload, size_t payload_len, void* arg),
  void* arg);
```

Register a callback for an MQTT topic. The adapter wraps the C function pointer in a C++ lambda and forwards it to `esphome::mqtt::global_mqtt_client->subscribe()`, passing QoS 0. When a message arrives on the topic, ESPHome invokes the lambda, which converts the `std::string` parameters to C strings and calls the original callback with the `arg` pointer. The callback and `arg` are captured by value in the lambda closure, which is heap-allocated by the ESPHome MQTT client and retained for the lifetime of the subscription.

Guards against null MQTT client: if `global_mqtt_client` is `nullptr` at the time of the call, the function returns immediately without registering the callback.

### 4.7 `unsubscribe`

```c
void unsubscribe(i_mqtt_client_t* self, const char* topic);
```

Remove a previously registered subscription for a topic. Forwards to `esphome::mqtt::global_mqtt_client->unsubscribe()`, which releases the lambda closure and tells the broker to stop delivering messages for that topic. Guards against null MQTT client: if `global_mqtt_client` is `nullptr`, the function returns immediately.

---

## 5. Subscribe/Unsubscribe Callback Lifecycle

### 5.1 Callback Storage

C callbacks are wrapped in lambdas registered with the ESPHome MQTT client. The closure is owned by the ESPHome client for the subscription's lifetime.

### 5.2 When Callbacks Fire

Connect/disconnect callbacks fire from the ESPHome MQTT loop and immediately publish the corresponding `tiny_event_t`. The write-topic callback parses and dispatches the write request (see §6.3).

### 5.3 Null Safety

Every entry point checks `global_mqtt_client` before use; `destroy()` also clears the global client's connect/disconnect callbacks to prevent dangling captures.

### 5.4 Multiple Subscriptions

Each `subscribe()` call registers an independent lambda with the ESPHome client; the adapter keeps no subscription list of its own except the tracked write topic.

---

## 6. Wildcard Subscription

### 6.1 Design

One subscription to `geappliances/{device_id}/erd/+/write` replaces per-ERD subscriptions. Incoming topics are parsed: the `/erd/0x` segment is either `0x{erd}` (primary board) or `0x{addr}_0x{erd}` (secondary board), distinguished by the `_` separator.

### 6.2 Benefits

A single wildcard topic eliminates per-ERD heap-allocated closures and MQTT outbox entries, and lets Home Assistant publish to any ERD topic without the bridge re-subscribing.

### 6.3 Write Command Routing

For each message on the write topic:

1. Parse the ERD (and optional board address) from the topic path; malformed hex is dropped.
2. Primary-board topics (no address prefix) map to the broadcast sentinel `0xFF` — it can never be a physical board address, so a secondary board at `0x00` stays unambiguous. The write bridge resolves the sentinel to the detected host address.
3. Decode the hex payload into a local stack buffer (`ERD_CACHE_MAX_DATA_SIZE` = 248 bytes max). The local buffer avoids a race: `tiny_event_publish()` is synchronous, so the buffer is fully consumed before the callback returns even if another message arrives later.
4. Publish `on_write_request_event` with `{erd, board_address, size, value}`.

---

## 7. Invariants

1. **Single vtable instance:** The `i_mqtt_client_api_t` vtable is a file-scope `static const` variable. It is assigned once during `init()` and never modified.
2. **Wildcard subscription eliminates per-ERD overhead:** A single wildcard topic replaces per-ERD subscriptions, eliminating heap-allocated closures and MQTT outbox entries.
3. **Incoming hex decoding by adapter:** Incoming write payloads are received as hex strings and decoded to raw bytes inside the subscribe lambda before being passed to the write bridge. Outgoing hex encoding of ERD values is performed by the caller (e.g., `erd_bridge_subscribe`), not by the adapter.
4. **Fire-and-forget publishes:** `publish_raw()` guards against null/disconnected MQTT client and reports success/failure, but callers do not wait for broker acknowledgment.
5. **Event-driven architecture:** Write requests and connection state changes are communicated via `tiny_event_t` pub/sub, not direct callbacks. This allows multiple subscribers.
6. **Caller-owned struct:** The adapter struct is allocated by the caller (stack or static). `device_id` is a raw pointer to caller-owned memory (not heap-allocated by the adapter). `destroy()` nulls the pointer but does not free it.
7. **Null-safe operations:** All public methods guard against a null or disconnected global MQTT client.

---

## 8. Dependencies

| Dependency | Purpose |
|------------|---------|
| `esphome::mqtt::global_mqtt_client` | ESPHome MQTT client singleton for publish and connection callbacks |
| `i_mqtt_client.h` | Abstract interface definition from the component |
| `tiny_event.h` | Event pub/sub system for write requests and connection events |
| `tiny_erd.h` | `tiny_erd_t` type for ERD identifiers |
| `tiny_gea_constants.h` | Broadcast address sentinel (`tiny_gea_broadcast_address`) |
| `esphome::core::log` | ESPHome logging framework (`ESP_LOGD`) |

---

## 9. Known Limitations

1. **No individual ERD write topics:** The wildcard subscription approach means write commands are received on a single topic and routed by parsing the ERD from the topic path. Individual ERD write topics are not subscribed to.
2. **No publish acknowledgment:** All publish operations are fire-and-forget beyond the local queue-acceptance result. There is no QoS or acknowledgment mechanism exposed to the caller.
3. **Single write request event:** There is one `on_write_request` event shared across all ERDs. The bridge must filter by `erd` in the event args if it needs to handle specific ERDs differently.
4. **ESPHome MQTT client singleton dependency:** The adapter depends on `esphome::mqtt::global_mqtt_client` being non-null and properly initialized before any publish operation. There is no fallback or alternative transport.
