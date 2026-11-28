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
| `HA_CLEANUP_PUBLISH_RETRY_BACKOFF_MS` | 64 | Wait after a dropped removal publish before attempting again. The topic stays queued, so waiting costs only convergence time, while retrying at main-loop rate against a full outbound ring churns the backend's drop counter. Matches `HA_DISCOVERY_PUBLISH_RETRY_BACKOFF_MS`; the phase timeout that bounds it is an idle timeout on progress, so this value trades attempt rate against convergence only. |
| `HA_CLEANUP_TOPIC_BUF_SIZE` | 6144 | Size of the topic queue, allocated by `ha_discovery_cleanup_start()` and released by `ha_discovery_cleanup_destroy()` — never embedded in the struct. Overridable via `HA_CLEANUP_TEST_BUF_SIZE` for tests. |

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

  /* Topic queue — allocated for the run, NULL between runs */
  char* topic_buf;
  void *(*alloc_fn)(size_t size);
  void (*free_fn)(void *buffer);
  uint16_t queue_write_pos;
  uint16_t queue_count;
  uint16_t dropped_count;
  uint32_t total_removed_count;

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
| `topic_buf` | `char*` | Heap-allocated queue for discovered topic strings, sized `HA_CLEANUP_TOPIC_BUF_SIZE` by `start()` and released by `destroy()`. NULL between runs; the callback and the flush both guard on it. Topics are appended linearly as null-terminated strings: `[topic:variable][null:1]`. |
| `queue_write_pos` | `uint16_t` | Current write position in `topic_buf`. |
| `queue_count` | `uint16_t` | Number of topics currently queued in `topic_buf`. |
| `dropped_count` | `uint16_t` | Cumulative count of topics dropped due to buffer full. |
| `total_removed_count` | `uint32_t` | Cumulative count of topics successfully removed this run. Never reset by a pass, so `DiscoveryLifecycleManager` can tell a slow-but-progressing cleanup from a stalled one. |
| `retry_backoff_until_ms` | `uint32_t` | Timestamp until which a dropped removal publish suppresses further attempts. `0` = no window armed. Main-loop only; not guarded by `mux`. |
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
- Release `topic_buf` through `free_fn` (after the drain delay, so no callback can still be appending to it)
- Zero the struct with `memset`

---

## 5. Public API

### 5.1 Start

```c
bool ha_discovery_cleanup_start(ha_discovery_cleanup_t* self);
```

`start()` MUST:
- Release any queue left by a previous run (`alloc_fn`/`free_fn` are the seam; the defaults are `malloc`/`free`)
- Allocate `HA_CLEANUP_TOPIC_BUF_SIZE` bytes for `topic_buf`. On failure: log error "Cleanup refused: could not allocate the N-byte topic queue; the existing discovery configs were left in place", leave `state` untouched and `topic_buf` NULL, and return `false` — the caller must refuse the cleanup rather than start a teardown it cannot track
- Set `state = ha_cleanup_state_cleaning` and return `true`
- Reset all tracking fields via the internal `cleanup_start()` function:
  - `queue_write_pos = 0`, `queue_count = 0`, `dropped_count = 0`
  - `flushed_once = false`
  - `clean_passes = 0`, `pass_found_topics = false`
  - `pass_received_count = 0`, `pass_removed_count = 0`
  - `subscribe_start_ms = 0`, `pass_number = 1`
  - `drain_start_ms = 0`
  - `retry_backoff_until_ms = 0`
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
- If `clean_passes >= 2` but `queue_count > 0`: log "Pass N: K topics still queued for removal, retrying" and return. Two clean passes only prove no NEW topics arrived; removals still in the queue are outstanding work, and finishing there would let the caller record the cleanup as done while stale topics survive in Home Assistant.
- If `clean_passes >= 2` **and `queue_count == 0`**:
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
9. Publish empty retained payload: `mqtt_client_publish_raw(mqtt_client, topic, "", 0, true)` — **before** touching the queue
10. If the publish was dropped: arm `retry_backoff_until_ms` (post-attempt timestamp + `HA_CLEANUP_PUBLISH_RETRY_BACKOFF_MS`), log "Cleanup publish dropped, will retry", and return `queue_count` with the queue **untouched**
11. On success: clear `retry_backoff_until_ms`
12. Enter critical section; **invariant check:** if `queue_count == 0` or `consumed > queue_write_pos`, log "Queue changed under the removal publish; skipping compaction", exit the critical section, and return `queue_count` — the topic stays queued and the next flush republishes it (an empty retained payload, harmless). Only `cleanup_flush_queue()` (main loop) shrinks the queue and `ha_discovery_cleanup_destroy()` is main-loop too, so this bail is unreachable today; it exists so a future caller that breaks the invariant cannot turn `queue_write_pos - consumed` into an underflowed `memmove` length.
13. Compact: `memmove` remaining data to front of `topic_buf`; decrement `queue_write_pos` by `consumed`, decrement `queue_count`, increment `pass_removed_count`
14. Read `remaining = queue_count` while still in critical section; exit critical section
15. Increment `total_removed_count` — **after** the compaction, so the bail path above does not count a removal whose topic is still queued (the retry that eventually removes it counts then)
16. Log debug: "Removed old topic: {topic}"
17. Return `remaining` (number of topics still in queue)

**No delay:** The function does not call `vTaskDelay()` — it flushes one topic per call to keep the main loop responsive.

**Publish before remove:** the compaction `memmove`s the buffer, which overwrites the front topic's bytes; restoring `queue_count`/`queue_write_pos` afterwards cannot bring the topic back. An earlier revision compacted first and "undid" the compact on a drop, which silently skipped the dropped topic and left its stale config in Home Assistant. Only `cleanup_flush_queue()` removes from the front (the callback only appends), so the topic copied in step 5 is still at the front when the publish lands even if callbacks appended while it was in flight.

**Backoff gate:** steps 3 onward are skipped entirely while `retry_backoff_until_ms` is armed, returning `queue_count`. The `!= 0` sentinel is tested before the rollover-safe signed diff `(int32_t)(now - retry_backoff_until_ms) < 0`, which reads `now - 0` as negative once `millis()` passes 2^31 and would otherwise stall every removal until the counter wraps.

### 8.3 Buffer Capacity

The default `HA_CLEANUP_TOPIC_BUF_SIZE` of 6144 bytes can hold approximately 100–150 typical HA discovery topic strings (average ~50–60 bytes each including null terminator). When the buffer is full, topics are silently dropped and `dropped_count` is incremented. Dropped topics are not retried within the same pass, but may be discovered again in a subsequent pass.

---

## 9. Memory Management

### 9.1 The Topic Queue Is Allocated Per Run

`ha_discovery_cleanup_start()` allocates `HA_CLEANUP_TOPIC_BUF_SIZE` bytes through `alloc_fn` (default `malloc`) and `ha_discovery_cleanup_destroy()` releases them through `free_fn` (default `free`). The struct carries only the pointer: it is embedded in the permanent `ha_discovery_manager_t`, so a fixed array there is DRAM every bridge pays for its whole life, inside the pool a publish run later needs 53 KB of contiguously (architecture review rec 10 leftover; measured −6 128 B RAM on `docs/test-compile.yaml`, esp32c3).

| Buffer | Size | Location | Purpose |
|--------|------|----------|---------|
| `topic_buf` | 6144 bytes (default) | Heap, for the duration of a run | Queuing discovered topic strings |
| `topic` | 256 bytes | Stack in `flush_queue()` | Temporary copy before compact |
| `sub_topic` | 128 bytes | Stack in `run()` / `destroy()` | Wildcard subscription topic string |

Allocation failure is a refusal, not a degraded run: `start()` returns `false` and leaves the module idle with nothing allocated, and `DiscoveryLifecycleManager::begin_cleanup_()` then refuses the cleanup **before** arming the pending-publish marker — nothing has been removed, so there is nothing for a later boot to recover. `start()` is idempotent: a restart releases the previous queue first.

The queue also has to survive its owner being reset: `ha_discovery_manager_init()` runs on every Discovery Refresh and on an aborted run and `memset()`s the whole manager, so it calls `ha_discovery_cleanup_release_queue()` next to the four run-buffer frees. `destroy()` is not usable there — it unsubscribes and delays, which a re-init must not do to a struct that may hold garbage.

The remaining heap traffic is indirect, via `mqtt_client_publish_raw()`, which allocates a `std::string` internally.

### 9.2 Heap Fragmentation Monitoring

`cleanup_start()` and the completion path in `run()` log heap statistics:
- Free heap size (`heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`)
- Largest free block (`heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)`)
- Fragmentation percentage: `(1.0 - largest_free / free_heap) * 100.0`

This provides diagnostic visibility into heap pressure during the cleanup process.

### 9.3 Critical Sections

Both `cleanup_topic_callback()` and `cleanup_flush_queue()` use `taskENTER_CRITICAL(&self->mux)` / `taskEXIT_CRITICAL(&self->mux)` — a per-instance `portMUX_TYPE` spinlock — to protect shared access to `topic_buf`, `queue_write_pos`, `queue_count`, and related fields. The callback runs in the MQTT task context; `flush_queue()` runs in the main loop context. On single-core (C3/C6) the critical section disables interrupts; on dual-core (S3) the `portMUX_TYPE` is a spinlock, so concurrent access is prevented on both. The mux is initialized in `ha_discovery_cleanup_init()` and re-initialized in `ha_discovery_cleanup_destroy()`, where the `memset()` that zeroes the struct would otherwise leave the embedded spinlock in an invalid state for a late callback.

The queue pointer is part of what the mux guards: a callback that observed a non-NULL `get_time_ms` before `destroy()` poisoned it can still be inside its critical section, which is why `destroy()` releases the queue only after its 50 ms drain delay and before the `memset()`.

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

`DiscoveryLifecycleManager` triggers cleanup when a discovery refresh is requested or a discovery-affecting change is detected at steady state:

1. `ha_discovery_cleanup_configure()` — pass device ID, MQTT client, and time source
2. `ha_discovery_cleanup_start()` — begin cleanup process
3. Each loop iteration: `ha_discovery_cleanup_run()` — advance the state machine
4. When `ha_discovery_cleanup_is_done()` returns true: destroy cleanup module, configure and start discovery manager for fresh publishing, drive publishing until complete, run the `confirm_firmware` hook and save the discovery record, wait 5 seconds, then call `safe_reboot()`

### 10.4 Independence

The cleanup module is independent of the discovery manager's own state and buffers. It has no knowledge of which entities are currently active or what discovery topics are "correct" — it simply discovers everything on the broker and removes it. The discovery manager then republishes the current configuration.

---

## 11. Invariants

1. **ESP-IDF only:** The entire module is guarded by `#ifdef USE_ESP_IDF`.
2. **No discovery state knowledge:** The cleanup module has no awareness of the discovery manager's internal state, buffers, or entity list. It operates purely on what the broker returns via the wildcard subscription.
3. **Two-pass verification, empty queue required:** Cleanup is not considered complete until two consecutive passes find no topics AND `queue_count == 0`. The two passes handle the race where topics discovered in one pass are not fully flushed before the next subscription fires; the queue check handles removals that are still waiting on a backoff window.
4. **Drain between passes:** After unsubscribing, the module waits for the inbound MQTT event queue to drain before re-subscribing. This prevents stale callbacks from a previous pass contaminating the next pass's results.
5. **One topic per flush:** `cleanup_flush_queue()` processes exactly one topic per call, minimizing peak heap pressure from the `std::string` allocation inside `mqtt_client_publish_raw()`.
6. **Critical section safety:** Shared state between the MQTT callback task and the main loop is protected by `taskENTER_CRITICAL(&self->mux)` / `taskEXIT_CRITICAL(&self->mux)` using a per-instance `portMUX_TYPE`.
7. **Config topics only:** Only topics ending with `/config` are queued for removal. Availability and other non-config topics are ignored.
8. **Echo suppression:** Topics with empty payloads are skipped — they are the module's own echo from previous clear operations.
9. **Optimistic drain detection:** The drain wait uses `last_activity_ms` to detect when the inbound queue is truly empty. If a late callback fires after drain started, the timer is reset from the new activity time.
10. **Clean destroy:** `destroy()` unsubscribes if still subscribed, preventing dangling callbacks after the struct is zeroed.
11. **Publish before remove:** a removal publish is attempted before the queue is compacted, so a dropped publish leaves the topic queued. The compaction overwrites the front topic's bytes and cannot be undone by restoring the counters. The publish's front is stable: the callback only appends (never shrinks `queue_write_pos` or moves the front) and `destroy()` runs on the main loop, the same task as the flush, so the topic copied in step 5 is still at the front when the compaction runs. The step-12 invariant check defends exactly this assumption: if a future caller breaks it, the flush bails instead of underflowing.

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
5. **No retry limit on dropped removals:** a dropped publish leaves the topic queued and arms `HA_CLEANUP_PUBLISH_RETRY_BACKOFF_MS`; there is no attempt counter or error escalation. The topic is retried until it goes through or the caller aborts the phase (`DiscoveryLifecycleManager`'s idle timeout on `total_removed_count`).
6. **Linear buffer, not ring buffer:** Topics are appended linearly and compacted on flush. This is simple but means the buffer capacity is limited by contiguous space. A ring buffer could improve utilization but adds complexity.
7. **Heap allocation per publish:** Each call to `mqtt_client_publish_raw()` internally allocates a `std::string`. The one-topic-per-flush strategy minimizes peak pressure, but a burst of many topics still causes sequential allocations.
8. **No overall timeout inside the module:** the module has per-phase timeouts (idle, min subscribe, drain) but no overall bound. The caller owns that: `DiscoveryLifecycleManager` aborts `kPhaseCleaning` after `kPhaseTimeoutMs` with no topic removed. Because the bound is on progress rather than duration, a congested-but-draining cleanup is never aborted — and deliberately, no absolute cap exists either: an external publisher that re-announces stale topics faster than removals can defer done indefinitely (each pass still ends after its idle timeout, but the module loops passes forever, and the phase machine rejects new requests while Cleaning). The topics that arrive during a cleanup are the device's own retained configs, so this needs a rogue publisher; the pre-change wall-clock bound traded this wedge for aborting healthy congested cleanups. If that trade is ever revisited, it belongs in the caller, not here.

---

## 14. Notes

1. **Synchronization is one per-instance `portMUX_TYPE` (`mux`), not a global lock or a task notification.** `cleanup_topic_callback()` runs in the ESP-IDF framework MQTT task context; `cleanup_flush_queue()`, `ha_discovery_cleanup_start()` and `ha_discovery_cleanup_destroy()` run in the main loop. The mux guards `topic_buf`, `queue_write_pos`, `queue_count` and the counters the callback touches. On single-core (C3/C6) the critical section disables interrupts; on dual-core (S3) `portMUX_TYPE` is a spinlock, so cross-core contention is handled regardless of which core the unpinned MQTT task runs on. Rejected alternative: flushing from the MQTT callback — it would block the framework task on the outbound ring.
2. **`cleanup_flush_queue()` is called from `cleanup_run()` only, and it leaves the critical section before touching the network** (issue #198). On the single-core parts `taskENTER_CRITICAL` disables interrupts, so a publish that blocks on the outbound ring keeps them disabled long enough to trip the interrupt watchdog, and any log the publish path emits aborts inside newlib's stdio lock ("recursive mutexes make no sense in ISR context"). This is safe because appends from the MQTT callback only ever move the buffer's tail, so the front topic and `consumed` stay valid across the unlocked window.
3. **The queue front has exactly one mutator.** Only `cleanup_flush_queue()` (main loop) shrinks the queue and `ha_discovery_cleanup_destroy()` is main-loop too, so the front a publish used cannot have moved. The `queue_count == 0 || consumed > queue_write_pos` bail (§8.2 step 12) is belt-and-braces against a future caller that breaks that invariant: it bails instead of letting `queue_write_pos - consumed` underflow into the `memmove` length and smear the heap. The topic stays queued and the next flush republishes it, which is harmless (an empty retained payload).
4. **The topic queue stays a per-run heap allocation; a fixed array in `ha_discovery_cleanup_t` is forbidden.** The struct is embedded in the permanent `ha_discovery_manager_t`, so a fixed array is 6 144 B of DRAM every bridge pays for its whole life whether or not a cleanup ever runs, and it sits inside the pool a publish run later needs 53 KB of contiguously (architecture-review rec 10 leftover). Enforced by `static_assert(sizeof(ha_discovery_cleanup_t) <= 128, "ha_discovery_cleanup_t must not embed per-run buffers")`.
5. **The default allocator is plain `malloc()` on purpose** (`cleanup_default_alloc`/`cleanup_default_free`): the queue and the discovery run buffers then draw from the same pool that `DiscoveryLifecycleManager::begin_publish_()`'s largest-free-block gate measures. A capability-specific allocator would make that gate measure a different pool.
6. **`ha_discovery_cleanup_release_queue()` is public because the owner is not always the cleanup module.** `ha_discovery_manager_init()` `memset()`s the whole manager on every re-init (Discovery Refresh, an aborted run) and MUST call it first, or the queue is orphaned; `destroy()` cannot be used there because it unsubscribes and delays. It is NULL-safe and idempotent (a restart without `destroy()` must not leak the previous queue) and safe on a zero-initialized struct — the same contract the manager's four run-buffer frees rely on. The `topic_buf` store happens under the same mux the callback's NULL check and `memcpy` run in, so a callback that arrives after the NULL store bails and one already in its critical section finishes its `memcpy` before the free (it holds the lock); the free itself runs after the lock is released, so a callback never sees a freed-but-non-NULL pointer and no caller of `release_queue()` runs inside a critical section.
7. **Allocation failure is a refusal, not a degraded run.** `ha_discovery_cleanup_start()` returns `false` with the module left idle and nothing allocated, and the caller MUST refuse the cleanup rather than start a teardown it cannot track. Refusing is recoverable precisely because nothing has been removed yet: Home Assistant's retained configs are left alone and the next boot tries again.
8. **`alloc_fn`/`free_fn` are a test seam**, installed by `ha_discovery_cleanup_init()` with the default `malloc`/`free` pair and replaced by tests to drive the allocation-failure refusal and to watch the queue's lifetime. `ha_discovery_cleanup_start()` falls back to the defaults when the struct was built without `init()` (test fixtures `memset` a struct and set only the fields they use). Such a struct must still carry a valid mux: the pointer store enters the spinlock, and a zeroed mux is `owner=0` on hardware — `make_cleanup()` in the test suite initializes it for exactly that reason.
9. **Publish before remove.** The compaction `memmove`s the front topic's bytes away, and restoring `queue_count`/`queue_write_pos` afterwards cannot bring them back: the "compact first, undo the compact on a drop" ordering this replaced left the next topic at the front under the old counts, so a dropped removal silently skipped a topic and left its stale config in Home Assistant. `total_removed_count` is incremented only after the compaction succeeded, so the bail path does not count a removal whose topic is still queued — the retry that eventually removes it counts then.
10. **The front topic is copied to the 256-byte stack buffer before any compaction**, because `memmove` shifts data forward into `topic_buf` and invalidates any pointer into the buffer. A topic longer than that buffer is detected by `strlen(topic) != strlen(topic_buf)`, compacted out of the queue, and skipped rather than published — a truncated topic would be a publish to a malformed topic string.
11. **The counter clamps are not redundant.** `consumed` is clamped to `queue_write_pos` before the `memmove` so a corrupted buffer cannot turn `queue_write_pos - consumed` into a huge length. In the callback, `topic_len >= HA_CLEANUP_TOPIC_BUF_SIZE` is rejected before the `uint16_t` cast of `topic_len + 1`, which could otherwise overflow to 0 and queue a zero-length entry.
12. **Linear append, not a ring buffer** (see §13.6). A NULL `topic_buf` in the callback means the run never allocated, or `destroy()` freed the queue while a delayed callback was still in flight; the callback counts it the way a full queue is counted (`dropped_count`), so the pass reports it and a later pass rediscovers the topic.
13. **The timing constants are sized to the MQTT task's inbound queue, not to convenience.** `HA_CLEANUP_IDLE_TIMEOUT_MS`, `HA_CLEANUP_MIN_SUBSCRIBE_MS` and `HA_CLEANUP_DRAIN_WAIT_MS` are all 1000 ms because the broker delivers and the MQTT task processes ~32 messages per inbound-queue cycle: the idle timeout must outlast a delivered batch before a flush is judged, a pass is not complete until at least one batch of retained messages could have arrived even if `ha_discovery_cleanup_run()` is called rarely, and the drain between passes is detected by callback silence — `drain_start_ms` is re-armed from `last_activity_ms` whenever a callback lands after the drain started, because the queue is not empty yet. `flushed_once` forces at least one flush after subscribing before a pass may be declared empty.
14. **`HA_CLEANUP_PUBLISH_RETRY_BACKOFF_MS` = 64 ms** matches `HA_DISCOVERY_PUBLISH_RETRY_BACKOFF_MS` in `ha_discovery_manager.cpp` and the same publish storm removed from the cache publisher (issue #183): the topic stays queued, so waiting loses nothing, while retrying on every main-loop pass against a full outbound ring churns the backend's drop counter and flaps the MQTT component's publish warning. The phase timeout that bounds the window is an idle timeout on progress, so the value trades attempt rate against convergence time only. The `!= 0` sentinel MUST be tested before the rollover-safe signed diff `(int32_t)(now - retry_backoff_until_ms) < 0`, which reads `now - 0` as negative once `millis()` passes 2^31 and would otherwise stall every removal until the counter wraps. `retry_backoff_until_ms` is main-loop-only and never touched by the MQTT callback, so the mux does not guard it; the `queue_count` returned on the drop path is read unlocked on purpose — a single aligned `uint16_t` load, an estimate for the caller's logging, not a decision.
15. **Two clean passes plus an empty queue, read and declared under the mux.** Two clean passes only prove no NEW topics arrived; topics still queued from a dropped removal publish are outstanding work, and declaring done there would leave stale discovery topics in Home Assistant while the caller records the cleanup as having run. The `queue_count` read and the `state = ha_cleanup_state_done` store share one critical section because an unlocked read could see 0 while a callback is appending on the MQTT task. A topic a callback lands after that check still sits queued until the next run rediscovers it — the same residual loss the two-pass design already accepts, now with an accurate done report.
16. **One topic per flush** (`HA_CLEANUP_FLUSH_BATCH` = 1): each `mqtt_client_publish_raw()` allocates a `std::string` on the heap, so flushing one topic at a time minimizes peak heap pressure. There is no `vTaskDelay()` in the flush; pacing comes from the main-loop cadence, which also keeps the loop responsive.
17. **The wildcard subscription is deliberately device-wide.** One subscription on `homeassistant/+/{device_id}/#` catches all retained discovery topics across all domains at once, which is what lets the module tear down a configuration it has no model of. The cost is the echo: the module's own empty retained payloads come back on that subscription, so `payload_len == 0` is skipped as our own echo from a previous clear.
18. **Module boundary.** `ha_discovery_cleanup_t` MUST stay independent of the discovery manager — no knowledge of discovery state, buffers, or which entities are currently correct. That independence is what makes it safe to run a full teardown before any republish, and what keeps `ha_discovery_manager_t` free of cleanup logic.
19. **`ha_discovery_cleanup_destroy()` poisons the time source first, then waits, then frees.** Order: null `get_time_ms` → unsubscribe → `vTaskDelay(pdMS_TO_TICKS(50))` → `ha_discovery_cleanup_release_queue()` → `memset()` → `portMUX_INITIALIZE(&self->mux)`. The 50 ms waits for any callback that read a non-NULL `get_time_ms` before the poison to finish its critical section; it is a conservative margin, ~1000x the actual critical-section duration, and the queue is released only after it for the same reason. The unsubscribe is keyed on `subscribed`, not `device_id`: a NULL `device_id` only means "never configured", and a double-destroy could have already zeroed it. Re-initializing the mux after the `memset` is mandatory — the `memset` leaves it at `owner=0`, an invalid state, and a delayed callback on the dead struct would otherwise spin forever in `spinlock_acquire` (deadlock → watchdog reset) on dual-core.
20. **The callback reads `self->get_time_ms` into a local exactly once** and uses that value for both the NULL guard and the later `last_activity_ms` call. `destroy()` may null `get_time_ms` (and later `memset` the struct) at any point; a second, separate read could observe the post-poison NULL even when the guard's read was non-NULL, and only the optimizer's read-merging would close that window.
21. **The ERD cache publisher is not paused during the cleanup phase.** `GeappliancesBridge::update_publisher_state_()` calls `erd_cache_mqtt_publisher_pause()` only while `ha_discovery_manager_is_processing()` is true — `ha_discovery_state_building` or `ha_discovery_state_discovering`, i.e. discovery payload generation. During `DiscoveryLifecycleManager`'s cleaning phase the ERD publisher keeps running, so removal publishes contend with the data path; that contention is bounded by the one-topic-per-flush rule and the 64 ms backoff, not by pausing. A source comment claimed the pause exists "during HA discovery cleanup"; it does not.
