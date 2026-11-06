# ERD Cache MQTT Publisher

## Purpose

Scans the shared ERD cache and publishes updated ERDs to MQTT topics with `retain=true`. Publishing runs in a FreeRTOS background task to avoid blocking the ESPHome main loop on the IDF MQTT mutex. The component requires the ESP-IDF framework (`framework: type: esp-idf`).

## Public API

| Function | Description |
|----------|-------------|
| `erd_cache_mqtt_publisher_init(self, cache, mqtt_client, device_id, primary_board_address)` | Initialize with cache, MQTT client, device ID, and the primary board's detected address (mapped to the legacy no-prefix topic). Subscribes to MQTT connect/disconnect events. |
| `erd_cache_mqtt_publisher_destroy(self)` | Stop the background task, unsubscribe events, delete semaphores. |
| `erd_cache_mqtt_publisher_start(self)` | Create the static publishing task, pinned to Core 0 at priority 1 (below the main loop). |
| `erd_cache_mqtt_publisher_stop(self)` | Stop the background task. Clean shutdown via `done_semaphore` handshake, then yields for idle task TCB cleanup. Idempotent. |
| `erd_cache_mqtt_publisher_signal_work(self)` | Signal the background task that there is work to do. |
| `erd_cache_mqtt_publisher_publish_one(self)` | Publish at most one updated cache entry: snapshot, topic/hex formatting, retained publish, and publish bookkeeping. No-ops (incrementing `missed_loops`) if MQTT is disconnected or publishing is paused. Called by the background task once per wake; exposed for tests that drive the publish path directly. |
| `erd_cache_mqtt_publisher_on_connected(self)` | Called when MQTT broker connects. Sets `mqtt_connected = true`, resets `first_round_done`. Does not reset `disconnect_start_ms` (cumulative tracking across ESPHome reconnect cycles). If disconnect duration ≥ 60 s, marks all valid cache entries as `update_required` and resets `publish_index` to 0 for full republish. |
| `erd_cache_mqtt_publisher_on_disconnected(self)` | Called when MQTT broker disconnects. Sets `mqtt_connected = false`, records `disconnect_start_ms`. |
| `erd_cache_mqtt_publisher_pause(self)` | Temporarily pause publishing. Used during HA discovery payload generation to reduce MQTT queue contention. |
| `erd_cache_mqtt_publisher_resume(self)` | Resume publishing after a pause and signal work to the task. |
| `erd_cache_mqtt_publisher_set_time_fn(self, get_time_ms)` | Override the time source (defaults to `esphome::millis`). Useful for testing. |
| `erd_cache_mqtt_publisher_get_publish_rate(self)` | Returns the number of ERD publishes in the last 60 seconds, then resets the window. |
| `erd_cache_mqtt_publisher_first_round_done(self)` | Returns true if the publisher has completed a full cache round since the last pause. Thread-safe — acquires the state mutex. |
| `erd_cache_mqtt_publisher_get_disconnect_count(self)` | Returns the total number of MQTT disconnects since init. Thread-safe. |
| `erd_cache_mqtt_publisher_get_last_disconnect_duration_ms(self)` | Returns the duration of the last MQTT disconnect in milliseconds. Thread-safe. |

## Architecture

### Background Task

- **Stack**: 4096 bytes (static allocation via `xTaskCreateStaticPinnedToCore` on dual-core ESP32, `xTaskCreateStatic` on single-core)
- **Priority**: 1 — below the main loop, so the loop always preempts the publisher
- **Core**: pinned to Core 0 (off ESPHome's loop core on dual-core parts; ignored on single-core) so the publisher cannot starve the loopTask watchdog
- **Scheduling**: Blocks on `work_semaphore` with `portMAX_DELAY` (no timeout). The main loop controls pacing via `erd_cache_mqtt_publisher_signal_work()`.

**Concurrent task safety:**
- `state_mutex` protects shared state (`mqtt_connected`, `paused`, counters, `first_round_done`) from torn reads during context switches between the background task and the main loop. It is never held across the slow MQTT publish.
- The cache is protected by its own mutex (`erd_cache_t.lock`); the publisher never mutates the cache directly — it snapshots under the cache lock.
- `done_semaphore` provides a clean shutdown handshake: the task gives the semaphore before calling `vTaskDelete`, so `stop()` can wait for true termination.

**Task lifecycle:**
1. `init()` creates three semaphores: `work_semaphore`, `state_mutex`, `done_semaphore`
2. `start()` creates the static task
3. Main loop calls `signal_work()` when cache entries are updated
4. Task wakes, acquires `state_mutex` to check `mqtt_connected`, `paused`, and dependency validity
5. Task calls `erd_cache_mqtt_publisher_publish_one()` once, then blocks on `work_semaphore`
6. `stop()` signals `work_semaphore` first, then sets `task_running = false`, waits on `done_semaphore` (1 s timeout), then yields for idle task TCB cleanup (100 ms). Idempotent — safe to call multiple times.
7. `destroy()` calls `stop()`, unsubscribes events, deletes semaphores

## Publishing Flow (`erd_cache_mqtt_publisher_publish_one`)

1. Bail out (returning `false`, incrementing `missed_loops`) if MQTT is disconnected or publishing is paused
2. Fetch the next eligible entry via `erd_cache_snapshot_next_updated()` — atomically copies the payload out of the arena under the cache lock and clears `update_required`; entries with `publish_cooldown > 0` are skipped (rate limited), keeping `update_required = true` for retry
3. Build the MQTT topic: `geappliances/{device_id}/erd/0x{ERD:04x}/value` for the primary board, `geappliances/{device_id}/erd/0x{ADDR:02x}_0x{ERD:04x}/value` for secondary boards
4. Convert the binary payload to a lowercase hex string
5. Publish via `mqtt_client_publish_raw()` with `retain = true`
6. On success: `erd_cache_mark_published()` reloads the publish cooldown timer. On drop: `erd_cache_mark_unpublished()` re-sets `update_required` so the entry is picked up on the next wake.
7. Update counters (`total_published`, `publish_count_window`, `first_round_done`) under the state mutex

## Additional Fields

| Field | Type | Description |
|-------|------|-------------|
| `paused` | `bool` | True when publishing is temporarily paused (via `pause()`). `publish_one()` skips publishing while paused. |
| `first_round_done` | `bool` | True after the publisher has scanned the full cache with no pending entries since the last `pause()` or `on_connected()`. Set when the snapshot returns no entry. |
| `get_time_ms` | `uint32_t (*)(void)` | Time source function pointer. Defaults to `esphome::millis`. |
| `disconnect_start_ms` | `uint32_t` | `millis()` when MQTT disconnected; 0 if connected. Used to determine if a full republish is needed on reconnect. |

## MQTT Connect/Disconnect Handling

- On connect: sets `mqtt_connected = true`, resets `first_round_done`. Does not reset `disconnect_start_ms` (cumulative tracking across ESPHome reconnect cycles). If disconnect duration ≥ 60 s, marks all valid cache entries as `update_required` for full republish.
- On disconnect: sets `mqtt_connected = false`, records `disconnect_start_ms`

When `mqtt_connected` is false, `publish_one()` skips publishing and increments `missed_loops`; the background task also short-circuits before calling it.

## Pre-allocated Buffers

The publish path uses pre-allocated buffers in the publisher struct to keep the task stack small:
- `task_topic[128]` — MQTT topic string
- `task_hex[512]` — hex payload (max 248 bytes of data = 496 hex chars + null)

## Stats

| Field | Description |
|-------|-------------|
| `total_published` | Total ERD publishes since init |
| `missed_loops` | Publish attempts skipped while MQTT disconnected or paused |
| `publish_count_window` | Publishes in the last 60 s window (reset by `get_publish_rate()`) |

## Dependencies

- `erd_cache.h` — shared ERD cache (snapshot read path)
- `i_mqtt_client.h` — MQTT publish interface and connect/disconnect events
- `i_tiny_event.h` — event subscription for MQTT connect/disconnect
- `freertos/FreeRTOS.h`, `freertos/task.h`, `freertos/semphr.h` — FreeRTOS task and semaphore APIs

## Key Design Decisions

- **Background task**: Publishing runs in a dedicated FreeRTOS task to avoid blocking the ESPHome main loop on the IDF MQTT mutex. This was a critical fix — synchronous publishing during startup (50+ ERDs to flush) caused task watchdog timeouts.
- **Single publish path**: The task loop and the tests share one `erd_cache_mqtt_publisher_publish_one()`; there is no second synchronous implementation to drift.
- **Concurrent task safety**: `state_mutex` protects shared state from torn reads, and `done_semaphore` ensures safe shutdown. Cache access goes through the cache's own mutex.
- **Pre-allocated buffers**: The publish path uses struct-allocated buffers (`task_topic`, `task_hex`) to avoid heap allocation and keep the task stack small.
- **Semaphore-based signaling**: The main loop signals work via `work_semaphore`; the task blocks with `portMAX_DELAY` (no timeout). The main loop controls pacing.
- **Graceful degradation**: If semaphore creation fails, the task exits immediately. If `state_mutex` creation fails, shared state is read without protection (acceptable for single-core or low-contention scenarios).
- **No cache ownership**: The publisher does not own the cache — it only reads from it. Cache lifecycle is managed by `GeappliancesBridge`.
- **No MQTT lifecycle ownership**: The publisher does not manage MQTT connections — it reacts to connect/disconnect events from `EsphomeMqttClientAdapter`.

## Testing

Covered by unit tests in `test/tests/erd_cache_mqtt_publisher_test.cpp` and `test/tests/erd_cache_mqtt_publisher_pause_test.cpp` (initialization, publish flow, pause/resume, MQTT connect/disconnect handling, rate limiting, retry-on-drop, and publish-rate tracking). Tests drive the publish path through `erd_cache_mqtt_publisher_publish_one()` and assert on the MQTT test double's recorded publishes.
