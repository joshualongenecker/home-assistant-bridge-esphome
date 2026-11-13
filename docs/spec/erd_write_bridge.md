# ERD Write Bridge — Specification

## 1. Overview

### 1.1 Purpose

The ERD write bridge relays write requests from MQTT to the GEA3 ERD client and reports write results back to MQTT. It is the sole component with direct MQTT write request handling, decoupling the polling and subscription bridges from the MQTT client.

### 1.2 Responsibilities

- Subscribe to `mqtt_client_on_write_request` events
- Forward write requests to the ERD client via `tiny_gea3_erd_client_write()`
- Queue requests that arrive while a write is in flight, up to `erd_write_bridge_queue_capacity` entries, and drain them in arrival order
- Gate writes on appliance identification (host address must not be the broadcast address)
- Correlate completion/failure events by `request_id`, rejecting stale responses
- Report write results back to MQTT via `mqtt_client_update_erd_write_result()` — including a failure result for every request the bridge refuses, so no caller is left waiting

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

Unsubscribes all event handlers and frees state. Guards against being called on a never-initialized struct (checks `timer_group == NULL`) and against partial init where `mqtt_client` or `erd_client` may be null. The queue is embedded in the struct, so a destroy with entries still queued discards them without publishing results (the bridge is going away).

---

## 3. State Machine

The write bridge uses a hierarchical state machine (`tiny_hsm`) with a parent state (`write_state_top`) and two child states. The states track the one write that may be outstanding on the bus; the queue is a separate structure that only `state_ready` drains.

### 3.1 Parent State: `write_state_top`

Defers all signals to child states.

### 3.2 Child States

#### `state_ready`

Initial state. No write is in flight, so the queue is empty.

**On `signal_write_requested`:**
- Validate and queue the request (see [3.4](#34-accepting-a-request)).
- Pump the queue: the request just queued is the only entry, so it goes onto the bus immediately.
  - If the ERD client refuses it (returns false): logs a warning, publishes a failure result via `mqtt_client_update_erd_write_result(erd, board_address, false, retries_exhausted)`, stays in `state_ready`.
  - If it is accepted: stores `request_id` in `pending_request_id`, `erd` in `pending_erd`, and `board_address` in `pending_board_address`, transitions to `state_writing`.

#### `state_writing`

One write is in progress. New write requests are queued, not dropped.

**On `signal_write_requested`:**
- Validate and queue the request (see [3.4](#34-accepting-a-request)). The queue is never pumped here: the new request waits for the in-flight write to settle.

**On `signal_write_completed`:**
- Validates `args->write_completed.request_id` against `pending_request_id`.
- If request IDs do not match: logs a warning and ignores the stale event. The in-flight slot and the queue are left untouched.
- If request IDs match: publishes a success result via `mqtt_client_update_erd_write_result(pending_erd, pending_board_address, true, 0)`, transitions to `state_ready`, then pumps the queue.

**On `signal_write_failed`:**
- Validates `args->write_failed.request_id` against `pending_request_id`.
- If request IDs do not match: logs a warning and ignores the stale event.
- If request IDs match: publishes a failure result via `mqtt_client_update_erd_write_result(pending_erd, pending_board_address, false, args->write_failed.reason)`, transitions to `state_ready`, then pumps the queue.

### 3.3 Pumping the queue

`start_next_write()` is called only when no write is in flight — from `state_ready` after a request is queued, and from `state_writing` after a result has been published and the machine has transitioned back to `state_ready`. It pops entries from the head of the ring and puts them on the bus:

- First accepted entry becomes the in-flight write (`pending_*` is set, transition to `state_writing`) and the pump returns.
- An entry the ERD client refuses never becomes in-flight: it is logged, reported with `retries_exhausted`, and the pump continues with the next entry, so one bad request cannot stall the queue.
- With an empty queue the pump does nothing and the bridge stays in `state_ready`.

### 3.4 Accepting a request

`enqueue_write_request()` validates a request and either refuses it — publishing a `write_result` — or copies it into the next free slot:

| Condition | Result |
|---|---|
| `erd_host_address == tiny_gea_broadcast_address` | `not_supported` |
| `size > ERD_WRITE_MAX_DATA_SIZE` (248) | `incorrect_size` |
| Queue already holds `erd_write_bridge_queue_capacity` entries | `retries_exhausted` |

The payload is copied into the slot at accept time. The write-request event carries a pointer into the MQTT adapter's stack buffer, which the next MQTT message overwrites; a queued request that kept the pointer would send whatever the next command carried.

### 3.5 State Diagram

```
write_state_top (parent — defers all signals)
  ├─ state_ready (initial, queue empty)
  │    ├─ write_requested + appliance not identified → publish failure, stay
  │    ├─ write_requested + oversized payload → publish failure, stay
  │    ├─ write_requested + client refuses the write → publish failure, stay
  │    └─ write_requested + accepted → store request_id/erd/board → state_writing
  │
  └─ state_writing (one write in flight)
       ├─ write_requested + queue has room → copy into queue, stay
       ├─ write_requested + queue full → publish failure, stay
       ├─ write_completed (request_id matches) → publish success → state_ready → pump
       ├─ write_completed (request_id mismatch) → ignore with warning
       ├─ write_failed (request_id matches) → publish failure → state_ready → pump
       └─ write_failed (request_id mismatch) → ignore with warning
```

---

## 4. Write Flow

1. MQTT client receives a write request (e.g., from Home Assistant on `geappliances/{deviceId}/erd/0x{ERD}/write`)
2. `mqtt_client_on_write_request` fires with `mqtt_client_on_write_request_args_t` (erd, board_address, value, size)
3. Write bridge receives `signal_write_requested`
   - In `state_ready`: the request is queued and immediately pumped onto the bus
   - In `state_writing`: the request is copied into the queue and waits
4. ERD client fires `write_completed` or `write_failed` activity event
5. Write bridge validates `request_id` against `pending_request_id`
6. If valid: publishes the result to MQTT, transitions back to `state_ready`, and pumps the next queued request onto the bus
7. If stale: logs warning, ignores the event

Home Assistant automations commonly publish two commands back to back (set a mode, then a setpoint). Up to `erd_write_bridge_queue_capacity` of those follow-up commands are buffered and executed in order; only when the queue is full is a command refused, and it is refused with a `write_result`.

---

## 5. Data Structures

```c
#define ERD_WRITE_MAX_DATA_SIZE 248  /* same GEA3 on-wire limit as ERD_CACHE_MAX_DATA_SIZE */

enum { erd_write_bridge_queue_capacity = 4 };

typedef struct {
    tiny_erd_t erd;
    uint8_t board_address;   /* 0xFF = primary-board sentinel */
    uint8_t size;
    uint8_t value[ERD_WRITE_MAX_DATA_SIZE];
} erd_write_request_t;

typedef struct {
    tiny_timer_group_t* timer_group;
    i_tiny_gea3_erd_client_t* erd_client;
    i_mqtt_client_t* mqtt_client;
    uint8_t erd_host_address;
    tiny_hsm_t hsm;
    tiny_event_subscription_t mqtt_write_request_subscription;
    tiny_event_subscription_t erd_client_activity_subscription;
    // Accepted writes waiting for the single in-flight slot, in arrival order
    erd_write_request_t queue[erd_write_bridge_queue_capacity];
    uint8_t queue_head;
    uint8_t queue_count;
    // The one write outstanding on the bus (valid in state_writing)
    tiny_gea3_erd_client_request_id_t pending_request_id;
    tiny_erd_t pending_erd;
    uint8_t pending_board_address;
} erd_write_bridge_t;
```

The ring is embedded in the bridge, so it costs `erd_write_bridge_queue_capacity * (ERD_WRITE_MAX_DATA_SIZE + 4)` bytes (just over 1 KB) with no allocation and no allocation failure to handle. `ERD_WRITE_MAX_DATA_SIZE` is pinned to `ERD_CACHE_MAX_DATA_SIZE` — the buffer size the MQTT adapter decodes into — by a `static_assert` in `geappliances_bridge_bridge_init.cpp`.

---

## 6. Concurrency

Only one write is on the bus at a time, matching the single-outstanding-request design of the GEA3 ERD client. Requests that arrive while that write is in flight are buffered in the ring (FIFO) and sent one at a time as each result arrives. All of this runs on the ESPHome main loop: the MQTT write-request event and the ERD client activity events are both delivered synchronously on the same task, so the queue needs no locking.

---

## 7. Error Handling

| Scenario | Behavior |
|----------|----------|
| Appliance not identified (broadcast address) | Drop write, publish failure result with `not_supported` |
| Payload larger than `ERD_WRITE_MAX_DATA_SIZE` | Drop write, publish failure result with `incorrect_size` |
| Write request while a write is in progress and the queue has room | Copy into the queue; sent after the in-flight write settles |
| Write request while the queue is full | Drop with warning log, publish failure result with `retries_exhausted` |
| ERD client refuses the write (queue full) | Publish failure result with `retries_exhausted`, continue with the next queued entry |
| ERD client reports `not_supported` | Publish failure result with reason |
| ERD client reports `retries_exhausted` | Publish failure result with reason |
| ERD client reports `incorrect_size` | Publish failure result with reason |
| Stale completion/failure (request_id mismatch) | Log warning, ignore event; the in-flight slot and the queue are unchanged |
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

| Result | Topic | Payload |
|--------|-------|---------|
| Success | `geappliances/{deviceId}/erd/0x{ERD}/write_result` | `ok` |
| Failure | `geappliances/{deviceId}/erd/0x{ERD}/write_result` | `{"error":"{reason}"}` |

Secondary-board writes report on the `0x{addr}_0x{erd}` topic form; each queued entry keeps the board address it was accepted with.

---

## 10. Invariants

1. **One write on the bus at a time:** A second write is never handed to the ERD client while one is in flight. Additional requests wait in the queue.
2. **Only the idle state pumps:** `start_next_write()` runs only while the machine is in `state_ready`, so the queue is empty whenever the bridge is idle.
3. **Requests are copied at accept time:** A queued request never references the caller's payload buffer after the write-request event returns.
4. **FIFO order:** Queued writes are sent in arrival order, and each result is reported with the `board_address` the request was accepted with.
5. **Nothing is dropped silently:** Every request the bridge does not put on the bus publishes a `write_result` failure.
6. **Request ID correlation:** Completion and failure events are only processed if their `request_id` matches `pending_request_id`. Stale events are logged and ignored, and never advance the queue.
7. **Write gated on identification:** Writes are never forwarded when `erd_host_address == tiny_gea_broadcast_address`.
8. **Success has no failure reason:** A successful write reports `failure_reason = 0` (no error), not a failure enum value.
9. **Clean destroy:** All event subscriptions are removed before freeing state. Null guards prevent crashes on partial init.

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

1. **No write timeout:** If the ERD client never responds to a write, the bridge remains in `state_writing` indefinitely and the queue never drains. A timeout timer could transition back to `state_ready` and publish a timeout failure.
2. **Fixed queue capacity:** The queue is sized at compile time (`erd_write_bridge_queue_capacity`). A burst larger than the capacity is refused with `retries_exhausted` rather than waiting for room.
3. **Single appliance:** The write bridge targets a single appliance address. Supporting multiple appliances would require multiple write bridge instances or address routing logic.
4. **No batch writes:** Each write is sent individually. If the ERD client supports batch writes, the bridge could accumulate writes and send them together.
