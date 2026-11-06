# ERD Write Bridge — Specification

## 1. Overview

### 1.1 Purpose

The ERD write bridge relays write requests from MQTT to the GEA3 ERD client and reports write results back to MQTT. It is the sole component with direct MQTT write request handling, decoupling the polling and subscription bridges from the MQTT client.

### 1.2 Responsibilities

- Subscribe to `mqtt_client_on_write_request` events
- Forward write requests to the ERD client via `tiny_gea3_erd_client_write()`
- Gate writes on appliance identification (host address must not be the broadcast address)
- Correlate completion/failure events by `request_id`, rejecting stale responses
- Report write results back to MQTT via `mqtt_client_update_erd_write_result()`

### 1.3 Not Responsible For

- ERD discovery or polling
- Subscription management
- ERD value publishing
- Bridge startup or lifecycle management

---

## 2. Initialization

### 2.1 Primary Init

```c
void erd_write_bridge_init(
    erd_write_bridge_t* self,
    tiny_timer_group_t* timer_group,
    i_tiny_gea3_erd_client_t* erd_client,
    i_mqtt_client_t* mqtt_client,
    uint8_t host_address);
```

| Parameter | Description |
|-----------|-------------|
| `timer_group` | Shared timer group (reserved for future retry timers). |
| `erd_client` | GEA3 ERD client interface. |
| `mqtt_client` | MQTT client interface (subscribe to write requests, publish results). |
| `host_address` | The appliance's GEA bus address. Writes are dropped if this is `tiny_gea_broadcast_address` (0xFF), indicating the appliance has not been identified. |

### 2.2 Destroy

```c
void erd_write_bridge_destroy(erd_write_bridge_t* self);
```

Unsubscribes all event handlers and frees state. Guards against being called on a never-initialized struct (checks `timer_group == NULL`) and against partial init where `mqtt_client` or `erd_client` may be null.

---

## 3. State Machine

The write bridge uses a hierarchical state machine (`tiny_hsm`) with a parent state (`write_state_top`) and two child states.

### 3.1 Parent State: `write_state_top`

Defers all signals to child states.

### 3.2 Child States

#### `state_ready`

Initial state. Accepts new write requests.

**On `signal_write_requested`:**
- If `erd_host_address == tiny_gea_broadcast_address`: logs a warning, publishes a failure result via `mqtt_client_update_erd_write_result(erd, board_address, false, not_supported)`, stays in `state_ready`.
- Otherwise: resolves the target address — `board_address != 0xFF` uses the parsed secondary address, `board_address == 0xFF` uses `erd_host_address` (primary). Calls `tiny_gea3_erd_client_write(erd_client, &request_id, target_address, erd, value, size)`.
  - If write fails to queue (returns false): logs a warning, publishes a failure result via `mqtt_client_update_erd_write_result(erd, board_address, false, retries_exhausted)`, stays in `state_ready`.
  - If write succeeds: stores `request_id` in `pending_request_id`, `erd` in `pending_erd`, and `board_address` in `pending_board_address`, transitions to `state_writing`.

#### `state_writing`

One write is in progress. New write requests are dropped.

**On `signal_write_requested`:**
- Logs a warning. The request is dropped. The bridge does not queue multiple writes.

**On `signal_write_completed`:**
- Validates `args->write_completed.request_id` against `pending_request_id`.
- If request IDs do not match: logs a warning and ignores the stale event.
- If request IDs match: publishes a success result via `mqtt_client_update_erd_write_result(pending_erd, pending_board_address, true, 0)`, transitions to `state_ready`.

**On `signal_write_failed`:**
- Validates `args->write_failed.request_id` against `pending_request_id`.
- If request IDs do not match: logs a warning and ignores the stale event.
- If request IDs match: publishes a failure result via `mqtt_client_update_erd_write_result(pending_erd, pending_board_address, false, args->write_failed.reason)`, transitions to `state_ready`.

### 3.3 State Diagram

```
write_state_top (parent — defers all signals)
  ├─ state_ready (initial)
  │    ├─ write_requested + broadcast address → drop, publish failure
  │    ├─ write_requested + queue full → drop, publish failure
  │    └─ write_requested + success → store request_id/erd, → state_writing
  │
  └─ state_writing
       ├─ write_requested → drop with warning
       ├─ write_completed (request_id matches) → publish success, → state_ready
       ├─ write_completed (request_id mismatch) → ignore with warning
       ├─ write_failed (request_id matches) → publish failure, → state_ready
       └─ write_failed (request_id mismatch) → ignore with warning
```

---

## 4. Write Flow

1. MQTT client receives a write request (e.g., from Home Assistant on `geappliances/{deviceId}/erd/0x{ERD}/set`)
2. `mqtt_client_on_write_request` fires with `mqtt_client_on_write_request_args_t` (erd, value, size)
3. Write bridge receives `signal_write_requested` in `state_ready`
4. If `erd_host_address == tiny_gea_broadcast_address`: log warning, publish failure result, stay in `state_ready`
5. Otherwise: call `tiny_gea3_erd_client_write()`, transition to `state_writing`
6. ERD client fires `write_completed` or `write_failed` activity event
7. Write bridge validates `request_id` against `pending_request_id`
8. If valid: publishes result to MQTT, transitions back to `state_ready`
9. If stale: logs warning, ignores the event

---

## 5. Data Structures

```c
typedef struct {
    tiny_timer_group_t* timer_group;
    i_tiny_gea3_erd_client_t* erd_client;
    i_mqtt_client_t* mqtt_client;
    uint8_t erd_host_address;
    tiny_hsm_t hsm;
    tiny_event_subscription_t mqtt_write_request_subscription;
    tiny_event_subscription_t erd_client_activity_subscription;
    // Pending write state (one write at a time)
    tiny_gea3_erd_client_request_id_t pending_request_id;
    tiny_erd_t pending_erd;
    uint8_t pending_board_address;
} erd_write_bridge_t;
```

---

## 6. Concurrency

Only one write is processed at a time. If a write request arrives while a previous write is in progress (`state_writing`), the new request is dropped with a warning log. The ERD client itself may queue the write internally, but the bridge does not track multiple pending writes.

---

## 7. Error Handling

| Scenario | Behavior |
|----------|----------|
| Appliance not identified (broadcast address) | Drop write, publish failure result with `not_supported` |
| Write request during in-progress write | Drop with warning log |
| ERD client write fails (queue full) | Publish failure result with `retries_exhausted`, stay in `state_ready` |
| ERD client reports `not_supported` | Publish failure result with reason |
| ERD client reports `retries_exhausted` | Publish failure result with reason |
| ERD client reports `incorrect_size` | Publish failure result with reason |
| Stale completion/failure (request_id mismatch) | Log warning, ignore event |
| MQTT disconnected | Continue processing; write results are queued by the MQTT adapter |

---

## 8. Integration with GeappliancesBridge

### 8.1 Initialization

```cpp
// In GeappliancesBridge::initialize_erd_bridge_() or similar:
// Autodiscovery completes before bridge init, so the real host address is available.
erd_write_bridge_init(
    &this->erd_write_bridge_,
    &this->timer_group_,
    this->autodiscovery_manager_.get_active_erd_client(),
    &this->mqtt_client_adapter_.interface,
    this->autodiscovery_manager_.get_host_address());
```

### 8.2 Host Address

The host address is set once during `erd_write_bridge_init()`, from the address detected by autodiscovery.

### 8.3 Teardown

```cpp
// In GeappliancesBridge::teardown():
erd_write_bridge_destroy(&this->erd_write_bridge_);
```

---

## 9. MQTT Topics

The write bridge publishes results via `mqtt_client_update_erd_write_result()`. The exact topic and payload format is determined by the `i_mqtt_client_t::update_erd_write_result` implementation in the MQTT adapter; the table below reflects the format produced by the default ESPHome adapter.

| Result | Topic |
|--------|-------|
| Success | `geappliances/{deviceId}/erd/0x{ERD}/set` with payload `"ok"` |
| Failure | `geappliances/{deviceId}/erd/0x{ERD}/set` with payload `"error:{reason}"` |

---

## 10. Invariants

1. **One write at a time:** A new write is never accepted while a previous write is in progress.
2. **Request ID correlation:** Completion and failure events are only processed if their `request_id` matches `pending_request_id`. Stale events are logged and ignored.
3. **Write gated on identification:** Writes are never forwarded when `erd_host_address == tiny_gea_broadcast_address`.
4. **Success has no failure reason:** A successful write reports `failure_reason = 0` (no error), not a failure enum value.
5. **Clean destroy:** All event subscriptions are removed before freeing state. Null guards prevent crashes on partial init.

---

## 11. Dependencies

| Dependency | Role |
|------------|------|
| `i_tiny_gea3_erd_client` | GEA3 ERD client interface (write, activity events) |
| `i_mqtt_client` | MQTT client interface (write request events, result publishing) |
| `tiny_hsm` | Hierarchical state machine |
| `tiny_timer` | Timer group (reserved for future retry timers) |
| `tiny_gea_constants.h` | Broadcast address constant (`tiny_gea_broadcast_address`) |

---

## 12. Known Limitations

1. **No write queue:** If multiple writes arrive rapidly, only the first is processed; subsequent writes are dropped. A write queue could be added to buffer requests.
2. **No write timeout:** If the ERD client never responds to a write, the bridge remains in `state_writing` indefinitely. A timeout timer could transition back to `state_ready` and publish a timeout failure.
3. **Single appliance:** The write bridge targets a single appliance address. Supporting multiple appliances would require multiple write bridge instances or address routing logic.
4. **No batch writes:** Each write is sent individually. If the ERD client supports batch writes, the bridge could accumulate writes and send them together.
