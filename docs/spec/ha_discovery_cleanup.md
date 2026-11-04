# HA Discovery Cleanup — Specification

## 1. Overview

### 1.1 Purpose

The HA Discovery Cleanup module discovers and removes stale Home Assistant MQTT discovery topics for a device. It is used by both the OTA reboot flow and the Discovery Refresh button. When the discovery configuration changes (e.g., entities are removed or renamed), old retained discovery messages remain on the MQTT broker, causing Home Assistant to show orphaned entities. The cleanup module uses a single wildcard subscription (`homeassistant/+/{device_id}/#`) to discover all existing discovery topics across all domains, then republishes each with an empty retained payload to clear them from the broker.

### 1.2 Responsibilities

- Subscribe to a wildcard topic to discover all retained discovery messages for a device
- Filter to only `/config` topics (discovery configuration payloads)
- Queue discovered topics in a fixed-size buffer for batch removal
- Republish each queued topic with an empty retained payload to clear the broker
- Perform multiple passes to ensure all topics are discovered and removed
- Track dropped topics when the internal buffer is full
- Log heap fragmentation before and after cleanup for diagnostic purposes

### 1.3 Not Responsible For

- Discovery topic creation or publishing (owned by `ha_discovery_manager`)
- MQTT connection lifecycle (owned by `EsphomeMqttClientAdapter`)
- Determining when cleanup should run (triggered by `GeappliancesBridge`)
- Device identity management (owned by `DeviceIdentityManager`)

---

## 2. Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `HA_CLEANUP_IDLE_TIMEOUT_MS` | 1000 | Idle timeout after last topic callback during cleanup. Must be long enough for the broker to finish delivering a batch and for the MQTT task to process its queue (~32 messages) before flush. |
| `HA_CLEANUP_MIN_SUBSCRIBE_MS` | 1000 | Minimum time subscribed before considering a pass complete. Ensures we wait for the initial retained message burst even if `cleanup_run()` isn't called frequently. |
| `HA_CLEANUP_DRAIN_WAIT_MS` | 1000 | Wait after unsubscribe for the inbound MQTT event queue to drain before re-subscribing. If no new topic callbacks fire during this window, the queue is empty and it's safe to re-subscribe. |
| `HA_CLEANUP_FLUSH_BATCH` | 1 | Flush one topic per batch call. Each publish allocates a `std::string` on the heap; processing one at a time minimizes peak heap pressure. |
| `HA_CLEANUP_TOPIC_BUF_SIZE` | 6144 | Size of the internal topic buffer. Overridable via `HA_CLEANUP_TEST_BUF_SIZE` for tests. |

---

## 3. Data Structures

### 3.1 State Enum

```c
typedef enum {
  ha_cleanup_state_idle,
  ha_cleanup_state_cleaning,
  ha_cleanup_state_done,
} ha_cleanup_state_t;
```

| State | Description |
|-------|-------------|
| `ha_cleanup_state_idle` | Initial state after `init()`. No cleanup in progress. |
| `ha_cleanup_state_cleaning` | Active cleanup in progress. Set by `start()`. |
| `ha_cleanup_state_done` | Cleanup completed successfully. Two consecutive clean passes with no topics found. |

### 3.2 Context Struct

```c
typedef struct {
  i_mqtt_client_t* mqtt_client;
  const char* device_id;
  ha_cleanup_state_t state;
  uint32_t (*get_time_ms)(void);

  /* Topic queue — ring buffer */
  char topic_buf[HA_CLEANUP_TOPIC_BUF_SIZE];
  uint16_t queue_write_pos;
  uint16_t queue_count;
  uint16_t dropped_count;

  /* Timing */
  uint32_t last_activity_ms;
  uint32_t subscribe_start_ms;
  bool subscribed;
  bool flushed_once;
  uint8_t clean_passes;
  bool pass_found_topics;
  uint16_t pass_received_count;
  uint16_t pass_removed_count;
  uint8_t pass_number;

  /* Drain */
  uint32_t drain_start_ms;
} ha_discovery_cleanup_t;
```

| Field | Type | Description |
|-------|------|-------------|
| `mqtt_client` | `i_mqtt_client_t*` | MQTT client interface for subscribe, unsubscribe, and publish. |
| `device_id` | `const char*` | Device ID used to build the wildcard subscription topic. |
| `state` | `ha_cleanup_state_t` | Current lifecycle state. |
| `get_time_ms` | `uint32_t (*)(void)` | Time source function (defaults to `esphome::millis`). Overridable for testing. |
| `topic_buf` | `char[HA_CLEANUP_TOPIC_BUF_SIZE]` | Fixed-size buffer for queuing discovered topic strings. Topics are appended linearly as null-terminated strings: `[topic:variable][null:1]`. |
| `queue_write_pos` | `uint16_t` | Current write position in `topic_buf`. |
| `queue_count` | `uint16_t` | Number of topics currently queued in `topic_buf`. |
| `dropped_count` | `uint16_t` | Cumulative count of topics dropped due to buffer full. |
| `last_activity_ms` | `uint32_t` | Timestamp of the last topic callback or subscription. Used for idle timeout detection. |
| `subscribe_start_ms` | `uint32_t` | Timestamp when the current subscription was established. Used for minimum subscription time enforcement. |
| `subscribed` | `bool` | Whether the wildcard subscription is currently active. |
| `flushed_once` | `bool` | Whether at least one flush has occurred after subscribing. Prevents declaring a pass empty before any topics are processed. |
| `clean_passes` | `uint8_t` | Count of consecutive passes that found no topics. Cleanup completes after 2 clean passes. |
| `pass_found_topics` | `bool` | Whether the current pass discovered any topics. |
| `pass_received_count` | `uint16_t` | Number of topic callbacks received during the current pass. |
| `pass_removed_count` | `uint16_t` | Number of topics removed (published with empty payload) during the current pass. |
| `pass_number` | `uint8_t` | Monotonically increasing pass counter, starting at 1. |
| `drain_start_ms` | `uint32_t` | Timestamp when drain wait started after unsubscribe. Used to determine when the inbound MQTT event queue is empty. |

---

## 4. Initialization

### 4.1 Init

```c
void ha_discovery_cleanup_init(ha_discovery_cleanup_t* self);
```

`init()` MUST:
- Zero the struct with `memset`
- Set `state = ha_cleanup_state_idle`

### 4.2 Configure

```c
void ha_discovery_cleanup_configure(ha_discovery_cleanup_t* self,
    const char* device_id, i_mqtt_client_t* mqtt_client, uint32_t (*get_time_ms)(void));
```

| Parameter | Description |
|-----------|-------------|
| `self` | Opaque cleanup struct (caller-allocated, embedded in `ha_discovery_manager_t`). |
| `device_id` | Device ID string used in wildcard subscription topic construction. |
| `mqtt_client` | MQTT client interface for subscribe, unsubscribe, and publish operations. |
| `get_time_ms` | Time source function pointer (defaults to `esphome::millis`). |

`configure()` stores all parameters. It is called separately from `init()` to allow deferred configuration after the discovery manager is constructed.

### 4.3 Destroy

```c
void ha_discovery_cleanup_destroy(ha_discovery_cleanup_t* self);
```

`destroy()` MUST:
- Null `get_time_ms` first to poison the callback, preventing it from firing on a partially-destroyed struct
- If `subscribed` is true and `mqtt_client` is not NULL:
  - Save `mqtt_client` to a local variable, then null `self->mqtt_client` (prevents a concurrent callback from using a stale pointer)
  - If `device_id` is not NULL, unsubscribe from the wildcard topic using the saved client
  - Set `subscribed = false`
- Zero the struct with `memset`

---

## 5. Public API

### 5.1 Start

```c
void ha_discovery_cleanup_start(ha_discovery_cleanup_t* self);
```

`start()` MUST:
- Set `state = ha_cleanup_state_cleaning`
- Reset all tracking fields via the internal `cleanup_start()` function:
  - `queue_write_pos = 0`, `queue_count = 0`, `dropped_count = 0`
  - `flushed_once = false`
  - `clean_passes = 0`, `pass_found_topics = false`
  - `pass_received_count = 0`, `pass_removed_count = 0`
  - `subscribe_start_ms = 0`, `pass_number = 1`
  - `drain_start_ms = 0`
- Log heap fragmentation baseline (free heap, largest free block, fragmentation percentage)
- Log info: "Starting HA discovery cleanup..."

### 5.2 Run

```c
void ha_discovery_cleanup_run(ha_discovery_cleanup_t* self);
```

**Precondition checks:**
- If `mqtt_client` is NULL: set `state = ha_cleanup_state_done`, log info, and return
- If `device_id` is NULL: set `state = ha_cleanup_state_done`, log warning, and return
- If `get_time_ms` is NULL: set `state = ha_cleanup_state_done`, log warning, and return

See Section 6 for the full state machine flow.

### 5.3 Get State

```c
ha_cleanup_state_t ha_discovery_cleanup_get_state(ha_discovery_cleanup_t* self);
```

Returns the current `state` field.

### 5.4 Is Done

```c
bool ha_discovery_cleanup_is_done(ha_discovery_cleanup_t* self);
```

Returns `true` if `state == ha_cleanup_state_done`.

---

## 6. Cleanup Flow

The cleanup process operates as a multi-pass state machine driven by repeated calls to `ha_discovery_cleanup_run()`. Each pass subscribes, discovers, flushes, and verifies.

### 6.1 Phase 1: Subscribe

If `subscribed` is false:

1. **Drain wait (if applicable):** If `drain_start_ms != 0` (we just unsubscribed from a previous pass):
   - If `last_activity_ms > drain_start_ms`: a callback still fired after drain started — reset `drain_start_ms` to the latest activity and return
   - If `now - last_activity_ms < HA_CLEANUP_DRAIN_WAIT_MS`: not enough time has passed — return
   - Otherwise: queue has drained — clear `drain_start_ms` and proceed to subscribe

2. **Subscribe:** Build the wildcard topic `homeassistant/+/{device_id}/#` and call `mqtt_client_subscribe()` with `cleanup_topic_callback`
   - Set `subscribed = true`
   - Record `subscribe_start_ms` and `last_activity_ms` from current time
   - Reset `pass_found_topics = false`, `pass_removed_count = 0`, `pass_received_count = 0`, `flushed_once = false`
   - Log info: "Pass N: subscribing to {topic}"
   - Return

### 6.2 Phase 2: Minimum Subscription Time

If `now - subscribe_start_ms < HA_CLEANUP_MIN_SUBSCRIBE_MS`:
- Flush one topic from the queue via `cleanup_flush_queue()`
- Return

This ensures we wait for the initial retained message burst even if `run()` isn't called frequently.

### 6.3 Phase 3: First Flush

If `flushed_once` is false:
- Flush one topic via `cleanup_flush_queue()`
- Set `flushed_once = true`
- Return

This prevents declaring a pass empty before any topics are processed.

### 6.4 Phase 4: Idle Detection

If `now - last_activity_ms >= HA_CLEANUP_IDLE_TIMEOUT_MS`:

1. Flush one remaining topic via `cleanup_flush_queue()`
2. Unsubscribe from the wildcard topic
3. Set `subscribed = false`
4. Start drain wait: `drain_start_ms = last_activity_ms`

**If topics were found this pass (`pass_found_topics` is true):**
- Log info: "Pass N: M received, K removed, D dropped — retrying"
- Increment `pass_number`
- Return (next `run()` call will go through drain wait, then re-subscribe)

**If no topics were found:**
- Log info: "Pass N: clean (M received, K removed)"
- Increment `clean_passes` and `pass_number`
- If `clean_passes < 2`: return (need one more verification pass)
- If `clean_passes >= 2`:
  - Log warning if `dropped_count > 0`
  - Log heap fragmentation after cleanup
  - Set `state = ha_cleanup_state_done`
  - Log info: "Cleanup complete"
  - Return

### 6.5 Phase 5: Still Receiving

If we're still within the idle timeout (callbacks are still firing):
- Flush one topic via `cleanup_flush_queue()`
- Return

This interleaves flush with discovery to keep heap pressure low.

---

## 7. Topic Callback

```c
void cleanup_topic_callback(const char* topic, const char* payload, size_t payload_len, void* arg);
```

Called by the MQTT client when a message arrives on the wildcard subscription. The callback is designed to be short — it stores the topic string and returns, deferring the actual publish to the main loop via `cleanup_flush_queue()`.

**Filtering:**
1. **NULL guard:** If `self` is NULL or `get_time_ms` is NULL, return immediately. Protects against callback firing after `destroy()` zeroes the struct.
2. **Config topics only:** Skip if topic length < 7 or doesn't end with `/config`. Non-config topics (e.g., `/availability`) are ignored.
3. **Echo detection:** Skip if `payload_len == 0`. Empty payloads are our own echo from a previous clear operation.
4. **Oversized topic guard:** If `topic_len >= HA_CLEANUP_TOPIC_BUF_SIZE`, increment `dropped_count` and return. Prevents `uint16_t` overflow when casting `topic_len + 1`.

**Queueing:**
- Enter critical section (`taskENTER_CRITICAL(&self->mux)`)
- Increment `pass_received_count`
- Calculate space needed: `topic_len + 1` (for null terminator)
- If `queue_write_pos + needed <= HA_CLEANUP_TOPIC_BUF_SIZE`:
  - Copy topic string into `topic_buf` at `queue_write_pos`
  - Advance `queue_write_pos` and increment `queue_count`
- Otherwise:
  - Increment `dropped_count` (buffer full)
- Set `pass_found_topics = true`
- Update `last_activity_ms` to current time
- Exit critical section

---

## 8. Queue Management

### 8.1 Buffer Layout

The topic buffer uses simple linear append — not a ring buffer. Topics are stored as consecutive null-terminated strings:

```
[topic1\0][topic2\0][topic3\0]...[queue_write_pos]
```

`queue_write_pos` tracks the current write position; `queue_count` tracks the number of queued topics.

### 8.2 Flush Behavior

```c
uint16_t cleanup_flush_queue(ha_discovery_cleanup_t* self);
```

`flush_queue()` processes one topic per call to minimize peak heap pressure (each `mqtt_client_publish_raw()` call allocates a `std::string` on the heap).

**Algorithm:**
1. If `self` is NULL: return 0
2. If `mqtt_client` is NULL: return `queue_count`
3. Enter critical section
4. If `queue_count == 0`: exit critical section, return 0
5. Copy the first topic string to a local 256-byte stack buffer via `strncpy` (before compacting, as `memmove` invalidates pointers into `topic_buf`)
6. **Truncation detection:** If `strlen(topic) != strlen(topic_buf)` (topic was longer than the stack buffer):
   - Log warning: "Topic truncated during flush, skipping"
   - Calculate `consumed` as `strlen(topic_buf) + 1`, clamped to `queue_write_pos`
   - Compact: `memmove` remaining data to front of `topic_buf`
   - Decrement `queue_write_pos` by `consumed`, decrement `queue_count`
   - Read `remaining = queue_count`, exit critical section
   - Return `remaining` (publish is skipped for truncated topics)
7. Calculate bytes consumed: `strlen(topic_buf) + 1`
8. Safety clamp: if `consumed > queue_write_pos`, clamp to `queue_write_pos`
9. Compact: `memmove` remaining data to front of `topic_buf`
10. Decrement `queue_write_pos` by `consumed`, decrement `queue_count`, increment `pass_removed_count`
11. Read `remaining = queue_count` while still in critical section
12. Exit critical section
13. Publish empty retained payload: `mqtt_client_publish_raw(mqtt_client, topic, "", 0, true)`
14. Log debug: "Removed old topic: {topic}"
15. Return `remaining` (number of topics still in queue)

**No delay:** The function does not call `vTaskDelay()` — it flushes one topic per call to keep the main loop responsive.

### 8.3 Buffer Capacity

The default `HA_CLEANUP_TOPIC_BUF_SIZE` of 6144 bytes can hold approximately 100–150 typical HA discovery topic strings (average ~50–60 bytes each including null terminator). When the buffer is full, topics are silently dropped and `dropped_count` is incremented. Dropped topics are not retried within the same pass, but may be discovered again in a subsequent pass.

---

## 9. Memory Management

### 9.1 No Heap Allocation

The cleanup module uses only stack-allocated and struct-embedded buffers:

| Buffer | Size | Location | Purpose |
|--------|------|----------|---------|
| `topic_buf` | 6144 bytes (default) | Embedded in struct | Queuing discovered topic strings |
| `topic` | 256 bytes | Stack in `flush_queue()` | Temporary copy before compact |
| `sub_topic` | 128 bytes | Stack in `run()` / `destroy()` | Wildcard subscription topic string |

No dynamic memory allocation is performed by the cleanup module itself. The only heap allocation occurs indirectly via `mqtt_client_publish_raw()`, which allocates a `std::string` internally.

### 9.2 Heap Fragmentation Monitoring

`cleanup_start()` and the completion path in `run()` log heap statistics:
- Free heap size (`heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`)
- Largest free block (`heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)`)
- Fragmentation percentage: `(1.0 - largest_free / free_heap) * 100.0`

This provides diagnostic visibility into heap pressure during the cleanup process.

### 9.3 Critical Sections

Both `cleanup_topic_callback()` and `cleanup_flush_queue()` use `taskENTER_CRITICAL(&self->mux)` / `taskEXIT_CRITICAL(&self->mux)` — a per-instance `portMUX_TYPE` spinlock — to protect shared access to `topic_buf`, `queue_write_pos`, `queue_count`, and related fields. The callback runs in the MQTT task context; `flush_queue()` runs in the main loop context. On single-core (C3/C6) the critical section disables interrupts; on dual-core (S3) the `portMUX_TYPE` is a spinlock, so concurrent access is prevented on both. The mux is initialized in `ha_discovery_cleanup_init()` and re-initialized in `ha_discovery_cleanup_destroy()`, where the `memset()` that zeroes the struct would otherwise leave the embedded spinlock in an invalid state for a late callback.

---

## 10. Integration with ha_discovery_manager

### 10.1 Embedding

The cleanup module is embedded as a member of `ha_discovery_manager_t`:

```c
typedef struct {
    /* ... other fields ... */
    ha_discovery_cleanup_t cleanup;
} ha_discovery_manager_t;
```

### 10.2 Lifecycle

| Event | Action |
|-------|--------|
| Discovery manager init | `ha_discovery_cleanup_init(&self->cleanup)` |
| Discovery manager destroy | `ha_discovery_cleanup_destroy(&self->cleanup)` |

### 10.3 Triggering

`GeappliancesBridge` triggers cleanup when a discovery refresh is requested:

1. `ha_discovery_cleanup_configure()` — pass device ID, MQTT client, and time source
2. `ha_discovery_cleanup_start()` — begin cleanup process
3. Each loop iteration: `ha_discovery_cleanup_run()` — advance the state machine
4. When `ha_discovery_cleanup_is_done()` returns true: destroy cleanup module, configure and start discovery manager for fresh publishing, drive publishing until complete, call mark_boot_successful_for_reboot(), wait 5 seconds, then call safe_reboot()

### 10.4 Independence

The cleanup module is independent of the discovery manager's own state and buffers. It has no knowledge of which entities are currently active or what discovery topics are "correct" — it simply discovers everything on the broker and removes it. The discovery manager then republishes the current configuration.

---

## 11. Invariants

1. **ESP-IDF only:** The entire module is guarded by `#ifdef USE_ESP_IDF`.
2. **No discovery state knowledge:** The cleanup module has no awareness of the discovery manager's internal state, buffers, or entity list. It operates purely on what the broker returns via the wildcard subscription.
3. **Two-pass verification:** Cleanup is not considered complete until two consecutive passes find no topics. This handles the race condition where topics discovered in one pass may not have been fully flushed before the next subscription fires.
4. **Drain between passes:** After unsubscribing, the module waits for the inbound MQTT event queue to drain before re-subscribing. This prevents stale callbacks from a previous pass contaminating the next pass's results.
5. **One topic per flush:** `cleanup_flush_queue()` processes exactly one topic per call, minimizing peak heap pressure from the `std::string` allocation inside `mqtt_client_publish_raw()`.
6. **Critical section safety:** Shared state between the MQTT callback task and the main loop is protected by `taskENTER_CRITICAL(&self->mux)` / `taskEXIT_CRITICAL(&self->mux)` using a per-instance `portMUX_TYPE`.
7. **Config topics only:** Only topics ending with `/config` are queued for removal. Availability and other non-config topics are ignored.
8. **Echo suppression:** Topics with empty payloads are skipped — they are the module's own echo from previous clear operations.
9. **Optimistic drain detection:** The drain wait uses `last_activity_ms` to detect when the inbound queue is truly empty. If a late callback fires after drain started, the timer is reset from the new activity time.
10. **Clean destroy:** `destroy()` unsubscribes if still subscribed, preventing dangling callbacks after the struct is zeroed.

---

## 12. Dependencies

| Dependency | Role |
|------------|------|
| `i_mqtt_client.h` | MQTT interface: `mqtt_client_subscribe`, `mqtt_client_unsubscribe`, `mqtt_client_publish_raw` |
| `geappliances_bridge_log.h` | Logging tag definition (`GEA_TAG`) |
| `esp_log.h` | Logging macros (`ESP_LOGI`, `ESP_LOGW`, `ESP_LOGD`, `ESP_LOGV`) |
| `esp_heap_caps.h` | Heap diagnostics (`heap_caps_get_free_size`, `heap_caps_get_largest_free_block`) |
| `freertos/FreeRTOS.h`, `freertos/task.h` | Critical section APIs (`taskENTER_CRITICAL`, `taskEXIT_CRITICAL` with a per-instance `portMUX_TYPE`), task delay (`vTaskDelay`) |
| `esphome/core/hal.h` | Time source (`esphome::millis`) |

---

## 13. Known Limitations

1. **Buffer overflow drops topics:** When `topic_buf` is full, discovered topics are silently dropped. Dropped topics are not retried within the same pass, but may be rediscovered in a subsequent pass. The `dropped_count` field tracks this for diagnostic purposes.
2. **No per-domain granularity:** The wildcard subscription `homeassistant/+/{device_id}/#` catches all domains at once. There is no mechanism to clean up specific domains independently.
3. **Relies on retained messages:** The cleanup process depends on the MQTT broker retaining discovery messages. If the broker loses its retained state (e.g., restart without persistence), the cleanup will find nothing to remove and complete immediately.
4. **Single-device scope:** The module is configured with one device ID at a time. Cleaning up multiple devices would require sequential runs or multiple instances.
5. **No publish failure handling:** If `mqtt_client_publish_raw()` fails (e.g., internal queue full), the topic remains queued and will be retried on the next flush call. There is no explicit retry limit or error escalation.
6. **Linear buffer, not ring buffer:** Topics are appended linearly and compacted on flush. This is simple but means the buffer capacity is limited by contiguous space. A ring buffer could improve utilization but adds complexity.
7. **Heap allocation per publish:** Each call to `mqtt_client_publish_raw()` internally allocates a `std::string`. The one-topic-per-flush strategy minimizes peak pressure, but a burst of many topics still causes sequential allocations.
8. **No timeout for overall cleanup:** The module has per-phase timeouts (idle, min subscribe, drain) but no overall timeout. If the broker is slow or unresponsive, cleanup could take a long time. The calling code (`GeappliancesBridge`) is responsible for any overall timeout policy.
