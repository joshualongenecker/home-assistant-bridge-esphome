# ERD Cache MQTT Publisher — Specification

## 1. Overview

### 1.1 Purpose

The ERD Cache MQTT Publisher scans the shared ERD cache for entries marked as needing an update and publishes their values to MQTT topics with `retain=true`. On ESP-IDF platforms, publishing runs in a FreeRTOS background task to avoid blocking the ESPHome main loop on the IDF MQTT mutex. On non-ESP-IDF platforms, `erd_cache_mqtt_publisher_loop()` is called directly from the main loop.

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
    const char* device_id);
```

| Parameter | Description |
|-----------|-------------|
| `self` | Opaque publisher struct (caller-allocated, stack or member). |
| `cache` | Shared ERD cache (read-only iteration). |
| `mqtt_client` | MQTT client interface for publishing and event registration. |
| `device_id` | Device ID string used in MQTT topic construction. |

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
- Create the static task via `xTaskCreateStaticPinnedToCore()` (Core 1, dual-core) or `xTaskCreateStatic()` (single-core) with a 4096-byte stack, priority 2, name `"erd_mqtt_pub"`. The larger stack accommodates the ESPHome/IDF MQTT publish call chain under queue pressure.
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

On ESP-IDF: gives `work_semaphore` (non-blocking). Used by the main loop to notify the background task that cache entries have been updated.

On non-ESP-IDF: no-op.

---

## 3. Architecture

### 3.1 ESP-IDF: Background Task

On ESP-IDF, the publisher runs as a FreeRTOS task (`mqtt_publisher_task`) with:

- **Stack**: 2048 bytes, statically allocated via `xTaskCreateStaticPinnedToCore` on dual-core or `xTaskCreateStatic` on single-core (no heap allocation)
- **Priority**: 2
- **Name**: `"erd_mqtt_pub"`
- **Scheduling**: Blocks on `work_semaphore` with `portMAX_DELAY` (no timeout). The main loop controls pacing by calling `erd_cache_mqtt_publisher_signal_work()` when cache entries are updated.

**Task loop:**
1. Block on `work_semaphore` (`portMAX_DELAY`)
2. Acquire `state_mutex` (100 ms timeout) to read `mqtt_connected`, `paused`, and validate dependency pointers (`cache`, `mqtt_client`, `device_id`, `get_time_ms`)
3. If not connected, paused, or dependencies are invalid: release mutex and loop back to step 1
4. Publish one cache entry: call `erd_cache_get_next_updated()`, build topic, encode hex, publish via `mqtt_client_publish_raw()` with `retain=true`, mark published, update stats
5. If no entry was found and `first_round_done` is not yet set: set `first_round_done = true`
6. Release `state_mutex` and loop back to step 1
7. On `task_running = false`: signal `done_semaphore`, call `vTaskDelete`
**Concurrent task safety:**
- `state_mutex` protects shared state (`mqtt_connected`, `paused`, `first_round_done`, `disconnect_start_ms`, `cache`, `mqtt_client`, `device_id`, `get_time_ms`, `total_published`, `publish_count_window`, `publish_index`) from torn reads during context switches between the background task and the main loop
- `done_semaphore` provides a clean shutdown handshake: the task gives the semaphore before calling `vTaskDelete`, so `stop()` can wait for true termination before proceeding
- Stats updates (`total_published`, `publish_count_window`) are performed inline under the outer `state_mutex` hold (100 ms timeout), not under a separate mutex acquisition

**Graceful degradation:**
- If `work_semaphore` creation fails: the task exits immediately via `vTaskDelete`
- If `state_mutex` creation fails: the task reads shared state without protection (acceptable for single-core or low-contention scenarios)
- If `done_semaphore` creation fails: `stop()` falls back to polling with delays

### 3.2 Non-ESP-IDF: Main Loop

On non-ESP-IDF platforms, `erd_cache_mqtt_publisher_loop()` is called directly from the main loop. No background task, no semaphores, no mutex. Publishes one entry per call.

```c
bool erd_cache_mqtt_publisher_loop(erd_cache_mqtt_publisher_t* self);
```

Returns `true` if an ERD was published, `false` otherwise (no updates available, MQTT disconnected, or missing dependencies). When no entry is found and `first_round_done` is not yet set, it is set to `true`.

---

## 4. Publishing Flow

Both the background task (ESP-IDF) and the main-loop function (non-ESP-IDF) follow the same core publishing logic:

1. Call `erd_cache_get_next_updated(self->cache, &self->publish_index)` to get the next entry with `update_required=true`. Returns NULL when no more updates are available.
2. Determine the data pointer:
   - If `entry->uses_heap && entry->ext_data != NULL`: use `entry->ext_data`
   - Otherwise: use `entry->inline_data`
3. Build the MQTT topic: `geappliances/{device_id}/erd/0x{ERD:04x}/value`
   - Topic buffer is 128 bytes; if `snprintf` truncates, log a warning and stop publishing
4. Convert the binary data to a hex string (lowercase, no separators)
   - Hex buffer is 512 bytes (max 255 bytes of data = 510 hex chars + null)
5. Publish via `mqtt_client_publish_raw(self->mqtt_client, topic, hex, data_len * 2, true)` — `retain=true`
6. Measure per-publish elapsed time; log a warning if ≥ 1000 ms
7. Call `erd_cache_mark_published(self->cache, entry)` to reload the publish cooldown
8. Update stats: increment `total_published` and `publish_count_window`
**One entry per call:** both the background task and the main-loop function publish exactly one entry per invocation. The main loop controls pacing by calling `signal_work()` or `loop()` at its own cadence.

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

- **Main-loop mode**: `erd_cache_mqtt_publisher_loop()` returns `false` immediately and increments `missed_loops`
- **Background task**: the task checks `mqtt_connected` each iteration and skips publishing (loops back to block on `work_semaphore`)
- **Conditional full re-publish after reconnect**: if the disconnect duration exceeded 60 s, all valid cache entries are marked as `update_required` on reconnect and `publish_index` is reset to 0, forcing a full drain of retained values to the broker. Short blips (<60 s) resume normally with only newly updated entries published.

---

## 6. Pre-allocated Buffers

To avoid heap allocation during publishing, the publisher uses fixed-size buffers:
| Buffer | Size | Purpose |
|--------|------|---------|
| `task_topic` | 128 bytes | MQTT topic string for the background task (ESP-IDF only). |
| `task_hex` | 512 bytes | Hex-encoded payload for the background task (ESP-IDF only). |

The `erd_cache_mqtt_publisher_loop()` function (non-ESP-IDF) declares local stack arrays (`char topic[128]`, `char hex[512]`) for each call.

The 512-byte hex buffer supports up to 255 bytes of binary data (255 × 2 = 510 hex characters + null terminator), which matches the maximum `data_size` field width in the cache entry.

---

## 7. Stats

| Field | Type | Description |
|-------|------|-------------|
| `total_published` | `uint32_t` | Total ERD publishes since `init()`. Incremented after each successful publish. |
| `missed_loops` | `uint32_t` | Number of `erd_cache_mqtt_publisher_loop()` calls skipped while MQTT was disconnected. Only incremented in main-loop mode. |
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
4. **Publish index is monotonic:** `publish_index` advances through the cache entries via `erd_cache_get_next_updated()`, wrapping around as the cache implementation dictates.
5. **Retain flag is always true:** All publishes use `retain=true` to ensure the broker retains the last known value for each ERD topic.
6. **Clean destroy:** All event subscriptions are unsubscribed and all semaphores are deleted before the struct is zeroed. Null guards prevent crashes on partial init.

---

## 9. Dependencies

| Dependency | Role |
|------------|------|
| `erd_cache.h` | Shared ERD cache (read-only iteration via `erd_cache_get_next_updated`) |
| `i_mqtt_client.h` | MQTT publish interface (`mqtt_client_publish_raw`) and connect/disconnect event accessors |
| `i_tiny_event.h` | Event subscription for MQTT connect/disconnect (`tiny_event_subscribe`, `tiny_event_unsubscribe`) |
| `esphome/core/hal.h` | Time source (`esphome::millis`) |
| `esphome/core/log.h` | Logging (`ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE`) |
| `freertos/FreeRTOS.h`, `freertos/task.h`, `freertos/semphr.h` | FreeRTOS task and semaphore APIs (ESP-IDF only) |
| `esp_task_wdt.h` | Task watchdog reset (ESP-IDF only, used in `stop()` fallback) |

---

## 10. Known Limitations

1. **Broker restart with short disconnect:** If the MQTT broker restarts and the disconnect duration is <60 s, the publisher will not re-publish all cached ERDs. The 60 s threshold is a heuristic — broker restarts that complete within this window are treated as transient network blips.
2. **No publish retry:** If `mqtt_client_publish_raw()` fails (e.g., internal queue full), the entry is not retried. `erd_cache_mark_published()` is called unconditionally after the publish attempt, and `erd_cache_get_next_updated()` already cleared `update_required` when it returned the entry. The entry will not be republished until the bridge updates it again.
3. **Single device ID:** The publisher is configured with one device ID at init time. Supporting multiple devices would require multiple publisher instances.
4. **Hex encoding is CPU-intensive:** Converting binary data to hex via `snprintf` per byte is simple but not optimal for large payloads. A lookup table or bit-manipulation approach would be faster.
5. **One publish per wake:** The ESP-IDF background task publishes exactly one entry per wake, then blocks on `work_semaphore` until the main loop signals more work. Pacing is controlled by the main loop via `signal_work()`.
6. **Semaphore failure is degraded, not fatal:** If any semaphore creation fails in `init()`, the publisher continues with reduced safety (no mutex protection, no clean shutdown handshake). This is acceptable for the single-core ESP32-C3 target where context switches provide natural serialization, but could lead to data races on dual-core ESP32 variants.

---

## 11. Pause / Resume / First Round Done

The publisher supports temporarily pausing and resuming publishing, with a mechanism to detect when a full cache round has completed after resuming. These functions are primarily used to reduce MQTT queue contention during Home Assistant discovery cleanup.

### 11.1 Pause

```c
void erd_cache_mqtt_publisher_pause(erd_cache_mqtt_publisher_t* self);
```

- Sets `paused = true` and resets `first_round_done = false`
- **Thread-safe on ESP-IDF**: acquires `state_mutex` (100 ms timeout) before modifying state; falls back to unprotected write if mutex creation failed
- **Non-ESP-IDF**: sets `paused = true` and `first_round_done = false` directly (no mutex)
- The `paused` flag is checked by the ESP-IDF background task in `mqtt_publisher_task()`: when `paused` is `true`, the task skips the drain loop and returns to waiting on `work_semaphore`
- The non-ESP-IDF `erd_cache_mqtt_publisher_loop()` does **not** check `paused` — pause/resume has no effect in main-loop mode. This is intentional: the main loop controls its own pacing by calling `loop()` at its own cadence.

### 11.2 Resume

```c
void erd_cache_mqtt_publisher_resume(erd_cache_mqtt_publisher_t* self);
```

- Sets `paused = false`
- **Thread-safe on ESP-IDF**: acquires `state_mutex` (100 ms timeout) before modifying state; falls back to unprotected write if mutex creation failed
- **On ESP-IDF**: after clearing `paused`, calls `erd_cache_mqtt_publisher_signal_work()` to wake the background task so it can immediately resume draining
- **Non-ESP-IDF**: sets `paused = false` directly (no-op in practice since the main loop doesn't check the flag)
- Does **not** reset `first_round_done` — the flag is set by the publish loop itself when a full round completes

### 11.3 First Round Done

```c
bool erd_cache_mqtt_publisher_first_round_done(erd_cache_mqtt_publisher_t* self);
```

- Returns `true` if the publisher has completed a full cache round since the last `pause()` or `on_connected()` call
- **Thread-safe on ESP-IDF**: acquires `state_mutex` (100 ms timeout) to read `first_round_done`; falls back to unprotected read if mutex creation failed
- **Non-ESP-IDF**: reads `first_round_done` directly

**How `first_round_done` is set:**
- The background task (ESP-IDF) sets `first_round_done = true` when `erd_cache_get_next_updated()` returns NULL (full cache scanned with no pending entries) and `first_round_done` is not yet true
- The main-loop function (non-ESP-IDF) sets `first_round_done = true` under the same condition
- `first_round_done` is reset to `false` by `pause()` and by `on_connected()` (MQTT reconnect), ensuring the flag reflects completion relative to the most recent pause or reconnect event

### 11.4 Interaction with the Publish Loop

**ESP-IDF background task (`mqtt_publisher_task`):**
- On each wake (from `work_semaphore` signal), the task acquires `state_mutex` and reads `mqtt_connected`, `paused`, and dependency pointers
- If `paused` is `true`, the task skips publishing and returns to waiting on `work_semaphore` — no cache entries are published
- When `resume()` is called, it clears `paused` and signals `work_semaphore`, waking the task to resume immediately
- The task publishes one entry per wake; when `erd_cache_get_next_updated()` returns NULL and `first_round_done` is not yet set, it is set to `true`

**Non-ESP-IDF main loop (`erd_cache_mqtt_publisher_loop`):**
- The loop checks `mqtt_connected` but does **not** check `paused`
- `pause()` and `resume()` are no-ops in terms of publish gating on non-ESP-IDF platforms
- `first_round_done` is set when `erd_cache_get_next_updated()` returns NULL, so `first_round_done()` remains useful as a completion indicator even in main-loop mode

### 11.5 Use Case: Pausing During HA Discovery

The primary use case for pause/resume is to avoid MQTT config storms during Home Assistant discovery cleanup:

1. Before starting HA discovery cleanup, call `erd_cache_mqtt_publisher_pause()` to stop the background task from publishing cache updates
2. Perform discovery cleanup (which may generate a burst of MQTT config messages)
3. Call `erd_cache_mqtt_publisher_resume()` to restart the background task
4. Optionally poll `erd_cache_mqtt_publisher_first_round_done()` to confirm the publisher has drained all accumulated cache updates before proceeding

This prevents the background task from competing for MQTT queue space with discovery messages, reducing the risk of queue overflow or dropped messages during the transition.
