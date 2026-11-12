# ERD Cache MQTT Publisher — Specification

## 1. Overview

### 1.1 Purpose

The ERD Cache MQTT Publisher scans the shared ERD cache for entries marked as needing an update and publishes their values to MQTT topics with `retain=true`. Publishing runs in a FreeRTOS background task to avoid blocking the ESPHome main loop on the IDF MQTT mutex. The component requires the ESP-IDF framework.

### 1.2 Responsibilities

- Iterate the ERD cache for entries with `update_required=true`
- Build the MQTT topic from device ID and ERD number (lowercase hex: `0x%04x`)
- Convert binary ERD data to a hex-encoded string
- Publish via `mqtt_client_publish_raw()` with `retain=true`
- Pause publishing when MQTT is disconnected; resume on reconnect
- Maintain publish statistics (total count, missed loops, per-window count)

### 1.3 Not Responsible For

- Cache lifecycle (owned by `GeappliancesBridge`)
- MQTT connection lifecycle (owned by `EsphomeMqttClientAdapter`)
- Write commands (out of scope)
- Determining which ERDs need updating (set by the polling/subscription bridges)

---

## 2. Initialization

### 2.1 Primary Init

```c
void erd_cache_mqtt_publisher_init(
    erd_cache_mqtt_publisher_t* self,
    erd_cache_t* cache,
    i_mqtt_client_t* mqtt_client,
    const char* device_id,
    uint8_t primary_board_address);
```

| Parameter | Description |
|-----------|-------------|
| `self` | Opaque publisher struct (caller-allocated, stack or member). |
| `cache` | Shared ERD cache (snapshot read path). |
| `mqtt_client` | MQTT client interface for publishing and event registration. |
| `device_id` | Device ID string used in MQTT topic construction. |
| `primary_board_address` | The primary board's detected physical address; publishes for this address use the legacy no-prefix topic, every other address gets the `0x{addr}_` prefix. |

`init()` MUST:
- Zero the struct with `memset`
- Store all parameters
- Set `publish_index = 0`
- Set `mqtt_connected = false`, then check the actual MQTT connection state via `esphome::mqtt::global_mqtt_client->is_connected()` and set it to `true` if already connected
- Set `get_time_ms = esphome::millis`
- On ESP-IDF: create three semaphores (`work_semaphore`, `state_mutex`, `done_semaphore`)
- Subscribe to `mqtt_client_on_mqtt_disconnect` and `mqtt_client_on_mqtt_connect` events
- If `mqtt_client` is NULL, skip event subscription (safe for partial init)

### 2.2 Destroy

```c
void erd_cache_mqtt_publisher_destroy(erd_cache_mqtt_publisher_t* self);
```

`destroy()` MUST:
- Call `erd_cache_mqtt_publisher_stop()` to terminate the background task (ESP-IDF)
- Unsubscribe from `mqtt_client_on_mqtt_disconnect` and `mqtt_client_on_mqtt_connect`
- On ESP-IDF: delete all three semaphores (`work_semaphore`, `state_mutex`, `done_semaphore`)
- Zero the struct with `memset`

If `mqtt_client` is NULL, `destroy()` skips event unsubscription and zeros the struct immediately.

### 2.3 Start

```c
void erd_cache_mqtt_publisher_start(erd_cache_mqtt_publisher_t* self);
```

On ESP-IDF:
- Guard against already-running task (`task_handle != NULL`)
- Guard against failed semaphore creation (`work_semaphore == NULL`)
- Set `task_running = true`
- Create the static task via `xTaskCreateStaticPinnedToCore()` (Core 0, dual-core) or `xTaskCreateStatic()` (single-core) with a 4096-byte stack, priority 1, name `"erd_mqtt_pub"`. Core 0 keeps the publisher off ESPHome's loop core (Core 1) so it cannot starve the loopTask watchdog; priority 1 keeps it below the main loop. The larger stack accommodates the ESPHome/IDF MQTT publish call chain under queue pressure.
- On task creation failure: log error, set `task_running = false`

On non-ESP-IDF: no-op.

### 2.4 Stop

```c
void erd_cache_mqtt_publisher_stop(erd_cache_mqtt_publisher_t* self);
```

On ESP-IDF:
- Idempotency guard: returns immediately if `task_running` is already `false`
- Signal `work_semaphore` first to wake the task, then set `task_running = false` so the task sees the flag on wake
- Wait for the task to signal completion via `done_semaphore` (1 s timeout)
- If `done_semaphore` is NULL (creation failed): fallback to polling with 10 ms delays, resetting the task watchdog each iteration, for up to 1 s
- Log a warning if the task does not signal completion within 1 s
- After the task has called `vTaskDelete`, yield to the idle task (100 ms via `vTaskDelay`) so it can run `prvCheckTasksWaitingTermination` and unlink the TCB's list items before `destroy()` memsets the struct
- Set `task_handle = NULL`

On non-ESP-IDF: no-op.

### 2.5 Signal Work

```c
void erd_cache_mqtt_publisher_signal_work(erd_cache_mqtt_publisher_t* self);
```

On ESP-IDF: gives `work_semaphore` (non-blocking). Used by the main loop, by `resume()`, and by `on_connected()` to notify the background task that cache entries have been updated.

**Backlog pacing.** While the cache reports more than `PUBLISH_PACE_BACKLOG_THRESHOLD` (10) dirty entries, the give is rate-limited to at most one per `PUBLISH_PACE_INTERVAL_MS` (100 ms), tracked in `last_work_signal_ms`. Below the threshold every call gives, so ordinary one-or-two-value changes keep their latency.

- Why: boot and the post-reconnect `erd_cache_mark_all_updated()` republish mark every cached entry dirty at once. One entry per wake at main-loop rate (~30/s) drains 129–151 entries straight into ESPHome's 30-slot outbound MQTT queue and overflows it; the queue has no depth accessor, so by the time `publish()` returns false the drops have already happened. Pacing prevents that instead of reacting to it.
- `last_work_signal_ms == 0` means the gate has never stored a paced wake, so the first wake after boot is never suppressed. Only a wake that passes refreshes the stamp, which is why this clause is needed at all. A paced wake can store `0` again only during the single millisecond in which `get_time_ms()` reads `0` — at boot and at each 2³² ms wrap — which re-arms the bypass for that millisecond; the next paced wake closes it.
- The elapsed test is an **unsigned modulo difference**, deliberately. The task waits with `portMAX_DELAY` and only a wake that *passes* refreshes the stamp, so a window that keeps reading as open against an ancient stamp suppresses every wake forever; a signed difference goes negative for any idle gap above 2³¹ ms (~24.9 days) and would take the bridge out of publishing until the millisecond counter rolls over.
- Coalescing makes the slower drain free: `update_required` is a flag and the snapshot always copies the newest bytes, so pacing delivers fresher values rather than fewer of them.
- `stop()` gives the semaphore directly and is never gated, so shutdown always wakes the task.

On non-ESP-IDF: no-op.

---

## 3. Architecture

### 3.1 ESP-IDF: Background Task

On ESP-IDF, the publisher runs as a FreeRTOS task (`mqtt_publisher_task`) with:

- **Stack**: 4096 bytes, statically allocated via `xTaskCreateStaticPinnedToCore` on dual-core or `xTaskCreateStatic` on single-core (no heap allocation)
- **Priority**: 1 (below the main loop)
- **Name**: `"erd_mqtt_pub"`
- **Scheduling**: Blocks on `work_semaphore` with `portMAX_DELAY` (no timeout). The main loop signals work by calling `erd_cache_mqtt_publisher_signal_work()` when cache entries are updated; that call is itself rate-limited while a large backlog is queued (see §2.5).

**Task loop:**
1. Block on `work_semaphore` (`portMAX_DELAY`)
2. Acquire `state_mutex` (100 ms timeout) to read `mqtt_connected`, `paused`, and validate dependency pointers (`cache`, `mqtt_client`, `device_id`, `get_time_ms`); release the mutex
3. If not connected, paused, or dependencies are invalid: loop back to step 1
4. Call `erd_cache_mqtt_publisher_publish_one()` — the single publish path shared with tests (see §4)
5. Loop back to step 1
6. On `task_running = false`: signal `done_semaphore`, call `vTaskDelete`
**Concurrent task safety:**
- `state_mutex` protects shared state (`mqtt_connected`, `paused`, `first_round_done`, `disconnect_start_ms`, `cache`, `mqtt_client`, `device_id`, `get_time_ms`, `total_published`, `publish_count_window`, `publish_index`) from torn reads during context switches between the background task and the main loop
- `done_semaphore` provides a clean shutdown handshake: the task gives the semaphore before calling `vTaskDelete`, so `stop()` can wait for true termination before proceeding
- Stats updates (`total_published`, `publish_count_window`, `first_round_done`) are performed by `erd_cache_mqtt_publisher_publish_one()` under a second `state_mutex` acquisition (100 ms timeout) — the mutex is never held across the slow MQTT publish

**Graceful degradation:**
- If `work_semaphore` creation fails: the task exits immediately via `vTaskDelete`
- If `state_mutex` creation fails: the task reads shared state without protection (acceptable for single-core or low-contention scenarios)
- If `done_semaphore` creation fails: `stop()` falls back to polling with delays

### 3.2 Publish Entry Point

```c
bool erd_cache_mqtt_publisher_publish_one(erd_cache_mqtt_publisher_t* self);
```

The single publish path: the background task calls it once per wake, and tests drive it directly. Returns `true` if an ERD was published, `false` otherwise (no updates available, MQTT disconnected or publishing paused, or missing dependencies). When no entry is found and `first_round_done` is not yet set, it is set to `true`. A disconnected-or-paused call increments `missed_loops`.

---

## 4. Publishing Flow

`erd_cache_mqtt_publisher_publish_one()` implements the whole publish step:

1. Bail out if any dependency is missing (`cache`, `mqtt_client`, `device_id`, `get_time_ms`), or if MQTT is disconnected or publishing is paused (incrementing `missed_loops` in the latter case).
2. Call `erd_cache_snapshot_next_updated(self->cache, &self->publish_index, ...)` to atomically fetch the next entry with `update_required=true` and copy its payload out of the arena under the cache lock. Returns "no entry" when no more updates are available.
3. Build the MQTT topic: `geappliances/{device_id}/erd/0x{ERD:04x}/value` for the primary board, `geappliances/{device_id}/erd/0x{ADDR:02x}_0x{ERD:04x}/value` for secondary boards
   - Topic buffer is 128 bytes; if `snprintf` truncates, log a warning and skip the publish
4. Convert the binary data to a hex string (lowercase, no separators)
   - Hex buffer is 512 bytes (max 248 bytes of data = 496 hex chars + null)
5. Publish via `mqtt_client_publish_raw(self->mqtt_client, topic, hex, data_len * 2, true)` — `retain=true`
6. Measure per-publish elapsed time; log a warning if ≥ 1000 ms
7. On success: call `erd_cache_mark_published(self->cache, entry)` to reload the publish cooldown. On drop: `erd_cache_mark_unpublished()` re-sets `update_required` so the entry is retried on the next wake.
8. Update stats: increment `total_published` and `publish_count_window`; set `first_round_done` when the scan found no entry. All under the state mutex when available.
**One entry per call:** the background task publishes exactly one entry per wake. Wake cadence comes from the main loop's `signal_work()` calls, which `signal_work()` paces while the publish backlog exceeds `PUBLISH_PACE_BACKLOG_THRESHOLD` (see §2.5).

---

## 5. MQTT Connect/Disconnect Handling

The publisher subscribes to `mqtt_client_on_mqtt_connect` and `mqtt_client_on_mqtt_disconnect` events during `init()`.

### 5.1 On Connect

```c
void erd_cache_mqtt_publisher_on_connected(erd_cache_mqtt_publisher_t* self);
- Set `mqtt_connected = true` (under `state_mutex` on ESP-IDF)
- Reset `first_round_done = false` (under `state_mutex` on ESP-IDF)
- Record disconnect duration and reset `disconnect_start_ms` to 0
- If disconnect duration ≥ 60 s: call `erd_cache_mark_all_updated()` to mark all valid cache entries as `update_required`, reset `publish_index` to 0, then log info with the disconnect duration
- If disconnect duration < 60 s: log info: "MQTT reconnected — resuming ERD cache publishing"
- Call `erd_cache_mqtt_publisher_signal_work()` to wake the background task

### 5.2 On Disconnect

```c
void erd_cache_mqtt_publisher_on_disconnected(erd_cache_mqtt_publisher_t* self);
- Set `mqtt_connected = false` (under `state_mutex` on ESP-IDF)
- Record `disconnect_start_ms = get_time_ms()` (under `state_mutex` on ESP-IDF)
- Log warning: "MQTT disconnected — pausing ERD cache publishing"

### 5.3 Behavior While Disconnected

- **`publish_one()`**: returns `false` immediately and increments `missed_loops`
- **Background task**: the task checks `mqtt_connected` each iteration and skips publishing (loops back to block on `work_semaphore`)
- **Conditional full re-publish after reconnect**: if the disconnect duration exceeded 60 s, all valid cache entries are marked as `update_required` on reconnect and `publish_index` is reset to 0, forcing a full drain of retained values to the broker. Short blips (<60 s) resume normally with only newly updated entries published.

---

## 6. Pre-allocated Buffers

To avoid heap allocation during publishing, the publisher uses fixed-size buffers embedded in the struct:
| Buffer | Size | Purpose |
|--------|------|---------|
| `task_topic` | 128 bytes | MQTT topic string. |
| `task_hex` | 512 bytes | Hex-encoded payload. |

The 512-byte hex buffer supports up to 248 bytes of binary data (`ERD_CACHE_MAX_DATA_SIZE`, 248 × 2 = 496 hex characters + null terminator), with headroom for the protocol's 255-byte on-wire maximum.

---

## 7. Stats

| Field | Type | Description |
|-------|------|-------------|
| `total_published` | `uint32_t` | Total ERD publishes since `init()`. Incremented after each successful publish. |
| `missed_loops` | `uint32_t` | Number of `erd_cache_mqtt_publisher_publish_one()` calls skipped while MQTT was disconnected or publishing was paused. |
| `publish_count_window` | `uint32_t` | Publishes in the last 60-second window. Reset to 0 by `get_publish_rate()`. |

### 7.1 Publish Rate

```c
uint32_t erd_cache_mqtt_publisher_get_publish_rate(erd_cache_mqtt_publisher_t* self);
```

Returns `publish_count_window` and resets it to 0. On ESP-IDF, the read-and-reset is performed under `state_mutex` (100 ms timeout).

### 7.2 Time Source Override

```c
void erd_cache_mqtt_publisher_set_time_fn(
    erd_cache_mqtt_publisher_t* self,
    uint32_t (*get_time_ms)(void));
```

Override the time source (defaults to `esphome::millis`). Used for testing to control time progression.

---

## 8. Invariants

1. **No cache ownership:** The publisher holds a raw pointer to the cache but never frees or modifies its lifecycle. Cache ownership belongs to `GeappliancesBridge`.
2. **No MQTT lifecycle ownership:** The publisher does not create, connect, or destroy the MQTT client. It reacts to connect/disconnect events from `EsphomeMqttClientAdapter`.
3. **Pessimistic connection state:** `mqtt_connected` is initialized to `false` in `init()`. The actual connection state is then checked via `esphome::mqtt::global_mqtt_client->is_connected()`, and set to `true` if the broker is already connected. If not yet connected, the first `on_connected()` event will set the flag.
4. **Publish index is monotonic:** `publish_index` advances through the cache entries via `erd_cache_snapshot_next_updated()`, wrapping around as the cache implementation dictates.
5. **Retain flag is always true:** All publishes use `retain=true` to ensure the broker retains the last known value for each ERD topic.
6. **Clean destroy:** All event subscriptions are unsubscribed and all semaphores are deleted before the struct is zeroed. Null guards prevent crashes on partial init.

---

## 9. Dependencies

| Dependency | Role |
|------------|------|
| `erd_cache.h` | Shared ERD cache (snapshot read path via `erd_cache_snapshot_next_updated`) |
| `i_mqtt_client.h` | MQTT publish interface (`mqtt_client_publish_raw`) and connect/disconnect event accessors |
| `i_tiny_event.h` | Event subscription for MQTT connect/disconnect (`tiny_event_subscribe`, `tiny_event_unsubscribe`) |
| `esphome/core/hal.h` | Time source (`esphome::millis`) |
| `esphome/core/log.h` | Logging (`ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE`) |
| `freertos/FreeRTOS.h`, `freertos/task.h`, `freertos/semphr.h` | FreeRTOS task and semaphore APIs (ESP-IDF only) |
| `esp_task_wdt.h` | Task watchdog reset (ESP-IDF only, used in `stop()` fallback) |

---

## 10. Known Limitations

1. **Broker restart with short disconnect:** If the MQTT broker restarts and the disconnect duration is <60 s, the publisher will not re-publish all cached ERDs. The 60 s threshold is a heuristic — broker restarts that complete within this window are treated as transient network blips.
2. **Retry via re-marking:** If `mqtt_client_publish_raw()` fails (e.g., internal queue full), `erd_cache_mark_unpublished()` re-sets `update_required` so the entry is retried on the next wake — but the scan iterator has already advanced, so the retry happens on the next full cache pass.
3. **Single device ID:** The publisher is configured with one device ID at init time. Supporting multiple devices would require multiple publisher instances.
4. **Hex encoding is CPU-intensive:** Converting binary data to hex via `snprintf` per byte is simple but not optimal for large payloads. A lookup table or bit-manipulation approach would be faster.
5. **One publish per wake:** The ESP-IDF background task publishes exactly one entry per wake, then blocks on `work_semaphore` until it is signalled again. Cadence is set by `signal_work()`, which paces while a large backlog is queued (§2.5) — so a full-cache republish drains in roughly `count / 10` seconds rather than at main-loop rate.
6. **Semaphore failure is degraded, not fatal:** If any semaphore creation fails in `init()`, the publisher continues with reduced safety (no mutex protection, no clean shutdown handshake). This is acceptable for the single-core ESP32-C3 target where context switches provide natural serialization, but could lead to data races on dual-core ESP32 variants.

---

## 11. Pause / Resume / First Round Done

The publisher supports temporarily pausing and resuming publishing, with a mechanism to detect when a full cache round has completed after resuming. These functions are primarily used to reduce MQTT queue contention during Home Assistant discovery cleanup.

### 11.1 Pause

```c
void erd_cache_mqtt_publisher_pause(erd_cache_mqtt_publisher_t* self);
```

- Sets `paused = true` and resets `first_round_done = false`
- **Thread-safe**: acquires `state_mutex` (100 ms timeout) before modifying state; falls back to unprotected write if mutex creation failed
- The `paused` flag is checked by the background task in `mqtt_publisher_task()` and by `erd_cache_mqtt_publisher_publish_one()`: when `paused` is `true`, publishing is skipped

### 11.2 Resume

```c
void erd_cache_mqtt_publisher_resume(erd_cache_mqtt_publisher_t* self);
```

- Sets `paused = false`
- **Thread-safe**: acquires `state_mutex` (100 ms timeout) before modifying state; falls back to unprotected write if mutex creation failed. If the mutex exists but the take times out, `resume()` does nothing at all — `paused` stays set and no wake is signalled (`erd_cache_mqtt_publisher.cpp:512-523`). Nothing retries inside the call; recovery comes from the main loop, which calls `resume()` on every iteration while a cache is attached and discovery is idle (`geappliances_bridge.cpp:331`)
- After clearing `paused`, calls `erd_cache_mqtt_publisher_signal_work()` so the background task can resume draining. That call is gated: while more than `PUBLISH_PACE_BACKLOG_THRESHOLD` entries are dirty the wake can be deferred by up to `PUBLISH_PACE_INTERVAL_MS` (see §2.5), so `resume()` does not guarantee a give — the main loop calls `signal_work()` again on its next iteration
- Does **not** reset `first_round_done` — the flag is set by the publish loop itself when a full round completes

### 11.3 First Round Done

```c
bool erd_cache_mqtt_publisher_first_round_done(erd_cache_mqtt_publisher_t* self);
```

- Returns `true` if the publisher has completed a full cache round since the last `pause()` or `on_connected()` call
- **Thread-safe**: acquires `state_mutex` (100 ms timeout) to read `first_round_done`; falls back to unprotected read if mutex creation failed

**How `first_round_done` is set:**
- `erd_cache_mqtt_publisher_publish_one()` sets `first_round_done = true` when the snapshot finds no entry (full cache scanned with no pending entries) and `first_round_done` is not yet true
- `first_round_done` is reset to `false` by `pause()` and by `on_connected()` (MQTT reconnect), ensuring the flag reflects completion relative to the most recent pause or reconnect event

### 11.4 Interaction with the Publish Loop

**Background task (`mqtt_publisher_task`):**
- On each wake (from `work_semaphore` signal), the task acquires `state_mutex` and reads `mqtt_connected`, `paused`, and dependency pointers
- If `paused` is `true`, the task skips publishing and returns to waiting on `work_semaphore` — no cache entries are published
- When `resume()` is called, it clears `paused` and calls `signal_work()`, which normally wakes the task immediately; while a large backlog is queued that wake can be deferred by up to `PUBLISH_PACE_INTERVAL_MS` (§2.5). The deferral is bounded by one interval plus one main-loop iteration, because the loop re-signals every pass
- The task publishes one entry per wake; when the snapshot finds no entry and `first_round_done` is not yet set, it is set to `true`

### 11.5 Use Case: Pausing During HA Discovery

The primary use case for pause/resume is to avoid MQTT config storms during Home Assistant discovery cleanup:

1. Before starting HA discovery cleanup, call `erd_cache_mqtt_publisher_pause()` to stop the background task from publishing cache updates
2. Perform discovery cleanup (which may generate a burst of MQTT config messages)
3. Call `erd_cache_mqtt_publisher_resume()` to restart the background task
4. Optionally poll `erd_cache_mqtt_publisher_first_round_done()` to confirm the publisher has drained all accumulated cache updates before proceeding

This prevents the background task from competing for MQTT queue space with discovery messages, reducing the risk of queue overflow or dropped messages during the transition.
