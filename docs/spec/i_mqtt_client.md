# i_mqtt_client — Specification

## 1. Overview

### 1.1 Purpose

Define the abstract MQTT client interface through which the bridge modules (`erd_bridge_subscribe`, `erd_bridge_poll`, `erd_write_bridge`) report ERD values and receive write commands, keeping bridge implementations independent of ESPHome or any concrete MQTT transport.

### 1.2 Responsibilities

- Declare the `i_mqtt_client_t` vtable interface for MQTT operations
- Provide `static inline` wrappers for each vtable method
- Define the event argument struct for write requests (`mqtt_client_on_write_request_args_t`)

### 1.3 Not Responsible For

- Any implementation (see `EsphomeMqttClientAdapter`)
- MQTT connection management

---

## 2. Interface

### 2.1 Handle

```c
typedef struct {
  const struct i_mqtt_client_api_t* api;
} i_mqtt_client_t;
```

The handle wraps a pointer to the vtable. Callers interact with the interface through `static inline` wrappers that dereference `self->api` and invoke the function pointer.

### 2.2 Vtable

```c
typedef struct i_mqtt_client_api_t {
  void (*update_erd_write_result)(i_mqtt_client_t* self, tiny_erd_t erd, uint8_t board_address, bool success, tiny_gea3_erd_client_write_failure_reason_t failure_reason);

  i_tiny_event_t* (*on_write_request)(i_mqtt_client_t* self);

  i_tiny_event_t* (*on_mqtt_disconnect)(i_mqtt_client_t* self);

  i_tiny_event_t* (*on_mqtt_connect)(i_mqtt_client_t* self);

  void (*publish_raw)(i_mqtt_client_t* self, const char* topic, const char* payload, size_t payload_len, bool retain);

  void (*subscribe)(i_mqtt_client_t* self, const char* topic, void (*callback)(const char* topic, const char* payload, size_t payload_len, void* arg), void* arg);

  void (*unsubscribe)(i_mqtt_client_t* self, const char* topic);
} i_mqtt_client_api_t;
```

---

## 3. Methods

### 3.1 `mqtt_client_update_erd_write_result`

```c
void mqtt_client_update_erd_write_result(
    i_mqtt_client_t* self,
    tiny_erd_t erd,
    uint8_t board_address,
    bool success,
    tiny_gea3_erd_client_write_failure_reason_t failure_reason);
```

Provide the result for the most recently completed write request to an ERD. `board_address` is 0xFF (broadcast) for the primary board (legacy no-prefix result topic) or the secondary board address (address-prefixed result topic).

### 3.2 `mqtt_client_on_write_request`

```c
i_tiny_event_t* mqtt_client_on_write_request(i_mqtt_client_t* self);
```

Event raised when a write request is received from the MQTT broker.
### 3.3 `mqtt_client_on_mqtt_disconnect`

```c
i_tiny_event_t* mqtt_client_on_mqtt_disconnect(i_mqtt_client_t* self);
```

Event raised when the client disconnects from the MQTT broker.
### 3.4 `mqtt_client_on_mqtt_connect`

```c
i_tiny_event_t* mqtt_client_on_mqtt_connect(i_mqtt_client_t* self);
```

Event raised when the client connects to the MQTT broker.
### 3.5 `mqtt_client_publish_raw`

```c
void mqtt_client_publish_raw(
    i_mqtt_client_t* self,
    const char* topic,
    const char* payload,
    size_t payload_len,
    bool retain);
```

Publish a raw MQTT message (C-string topic and payload).
### 3.6 `mqtt_client_subscribe`

```c
void mqtt_client_subscribe(
    i_mqtt_client_t* self,
    const char* topic,
    void (*callback)(const char* topic, const char* payload, size_t payload_len, void* arg),
    void* arg);
```

Subscribe to a topic with a raw C callback.
### 3.7 `mqtt_client_unsubscribe`

```c
void mqtt_client_unsubscribe(i_mqtt_client_t* self, const char* topic);
```

Unsubscribe from a topic.
---

## 4. Inline Wrappers

Each vtable method has a corresponding `static inline` wrapper in the header:

| Wrapper | Vtable Call |
|---------|-------------|
| `mqtt_client_update_erd_write_result(self, erd, board_address, success, failure_reason)` | `self->api->update_erd_write_result(self, erd, board_address, success, failure_reason)` |
| `mqtt_client_on_write_request(self)` | `self->api->on_write_request(self)` |
| `mqtt_client_on_mqtt_disconnect(self)` | `self->api->on_mqtt_disconnect(self)` |
| `mqtt_client_on_mqtt_connect(self)` | `self->api->on_mqtt_connect(self)` |
| `mqtt_client_publish_raw(self, topic, payload, payload_len, retain)` | `self->api->publish_raw(self, topic, payload, payload_len, retain)` |
| `mqtt_client_subscribe(self, topic, callback, arg)` | `self->api->subscribe(self, topic, callback, arg)` |
| `mqtt_client_unsubscribe(self, topic)` | `self->api->unsubscribe(self, topic)` |

This allows callers to use the interface with normal function-call syntax without knowing the vtable structure.


---

## 5. Event Types

### 5.1 Write Request

```c
typedef struct {
  tiny_erd_t erd;
  uint8_t board_address;
  uint8_t size;
  const void* value;
} mqtt_client_on_write_request_args_t;
```

| Field | Type | Description |
|-------|------|-------------|
| `erd` | `tiny_erd_t` | The target ERD to write |
| `board_address` | `uint8_t` | 0xFF (broadcast) = primary board (bridge resolves to its host address); any other value = secondary board address parsed from the `0x{addr}_0x{erd}` topic form |
| `size` | `uint8_t` | Payload length in bytes |
| `value` | `const void*` | The write payload |

Carried by the event returned from `mqtt_client_on_write_request`. The `erd_write_bridge` subscribes to this event and forwards the write to the ERD client.

---

## 6. Invariants

1. **Abstract interface only:** This module declares no implementation — no state, no connection logic, no topic construction. It is a pure interface definition.
2. **Event-driven, not callback-driven:** Write requests and connection state changes are communicated via the `tiny_event` pub/sub system, not direct function callbacks. This allows multiple subscribers (e.g., both bridges and the cache publisher).
3. **No connection management:** The interface does not include connect, disconnect, or reconnection methods. Connection lifecycle is managed entirely by the concrete adapter.
4. **Vtable immutability:** The `api` pointer in `i_mqtt_client_t` points to a `const` vtable. Implementations set the vtable once during adapter construction; it is never modified at runtime.
5. **No allocation in wrappers:** The inline wrappers perform a single pointer dereference and function call. They do not allocate, copy, or validate arguments.

---

## 7. Dependencies

| Dependency | Purpose |
|------------|---------|
| `i_tiny_event.h` | Event pub/sub system for write requests and connection events |
| `i_tiny_gea3_erd_client.h` | GEA3 ERD client types (`tiny_gea3_erd_client_write_failure_reason_t`) |
| `tiny_erd.h` | `tiny_erd_t` type for ERD identifiers |

---

## 8. Known Limitations

1. **Interface only:** No concrete implementation is provided. The sole implementation is `EsphomeMqttClientAdapter` in the ESPHome component.
2. **No error return on publish:** `mqtt_client_publish_raw` and `mqtt_client_update_erd_write_result` are fire-and-forget — they do not return success/failure. The adapter is expected to queue or drop messages internally.
3. **Single write request event:** There is one `on_write_request` event shared across all ERDs. The bridge must filter by `erd` in the event args if it needs to handle specific ERDs differently.
4. **No QoS or acknowledgment:** The interface does not expose MQTT QoS levels or publish acknowledgments. These are handled by the adapter.
