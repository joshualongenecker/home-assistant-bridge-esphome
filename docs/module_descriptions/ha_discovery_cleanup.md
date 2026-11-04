# HaDiscoveryCleanup

## Purpose

Discovers and removes old Home Assistant MQTT discovery topics for a device. Used by both the OTA reboot flow and the Discovery Refresh button. Independent of the discovery manager — no knowledge of discovery state or buffers. Uses a wildcard subscription to catch all retained discovery topics across all domains, then republishes each with an empty payload to clear the retained message on the broker.

## Public API

| Function | Description |
|----------|-------------|
| `ha_discovery_cleanup_init(self)` | Zero-initialize the context; set state to `idle` |
| `ha_discovery_cleanup_configure(self, device_id, mqtt_client, get_time_ms)` | Set device ID, MQTT client, and time function |
| `ha_discovery_cleanup_start(self)` | Transition to `cleaning` state; reset pass counters and queue |
| `ha_discovery_cleanup_run(self)` | Main loop entry point: subscribe, collect, flush, verify, and transition to `done` |
| `ha_discovery_cleanup_destroy(self)` | Poison callback, unsubscribe, and zero the struct |
| `ha_discovery_cleanup_get_state(self)` | Return the current `ha_cleanup_state_t` |
| `ha_discovery_cleanup_is_done(self)` | Return `true` if state is `done` |

## State Machine

```
IDLE
  └─ start() →
    CLEANING
      ├─ subscribe to homeassistant/+/{device_id}/#
      ├─ collect topics via callback (appends to ring buffer)
      ├─ flush one topic per run() call (publish empty retained payload)
      ├─ idle timeout → unsubscribe, wait for drain, then re-subscribe for next pass
      ├─ pass found topics → retry (increment pass_number)
      ├─ pass clean → increment clean_passes
      └─ two consecutive clean passes → DONE
    DONE (terminal — all old discovery topics removed)
```

Constants:
- `HA_CLEANUP_IDLE_TIMEOUT_MS = 1000` — no new callbacks for this long signals end of a batch
- `HA_CLEANUP_MIN_SUBSCRIBE_MS = 1000` — minimum time subscribed before considering a pass complete
- `HA_CLEANUP_DRAIN_WAIT_MS = 1000` — wait after unsubscribe for inbound MQTT event queue to drain
- `HA_CLEANUP_FLUSH_BATCH = 1` — flush one topic per `run()` call to minimize heap pressure
- `HA_CLEANUP_TOPIC_BUF_SIZE = 6144` — ring buffer size (overridable via `HA_CLEANUP_TEST_BUF_SIZE`)

## Data Structures

`ha_discovery_cleanup_t`:
- `mqtt_client` — `i_mqtt_client_t*` for subscribe/unsubscribe/publish
- `device_id` — `const char*` used to build the wildcard subscription topic
- `state` — `ha_cleanup_state_t` (`idle`, `cleaning`, `done`)
- `get_time_ms` — `uint32_t (*)(void)` time function for timeout tracking
- `topic_buf` — `char[HA_CLEANUP_TOPIC_BUF_SIZE]` ring buffer for queued topic strings
- `queue_write_pos` — `uint16_t` offset within `topic_buf` for next append
- `queue_count` — `uint16_t` number of topics currently queued
- `dropped_count` — `uint16_t` topics dropped due to buffer full
- `last_activity_ms` — `uint32_t` time of the most recent callback
- `subscribe_start_ms` — `uint32_t` time when the current subscription was created
- `subscribed` — `bool` whether the wildcard subscription is active
- `flushed_once` — `bool` at least one flush has occurred after subscribing
- `clean_passes` — `uint8_t` consecutive passes that found no topics
- `pass_found_topics` — `bool` whether the current pass received any callbacks
- `pass_received_count` — `uint16_t` total callbacks received in the current pass
- `pass_removed_count` — `uint16_t` topics successfully removed in the current pass
- `pass_number` — `uint8_t` sequential pass counter
- `drain_start_ms` — `uint32_t` time when drain wait began after unsubscribe

## Key Design Decisions

- **Single wildcard subscription**: Subscribes to `homeassistant/+/{device_id}/#` to catch all discovery topics across all domains in one shot, avoiding per-domain subscribe/unsubscribe cycles.
- **Callback-to-main-loop decoupling**: The MQTT callback (`cleanup_topic_callback`) only appends the topic string to a ring buffer — it never publishes. Flushing happens in `run()` on the main loop to avoid blocking the ESP-IDF MQTT task's inbound queue.
- **Critical section in callback**: Shared state (`topic_buf`, `queue_write_pos`, `queue_count`) is protected by `taskENTER_CRITICAL(&self->mux)` / `taskEXIT_CRITICAL(&self->mux)` using a per-instance `portMUX_TYPE`. On single-core (C3/C6) this disables interrupts; on dual-core (S3) the `portMUX_TYPE` acts as a spinlock, so the same code is safe on both.
- **One topic per flush**: Each `run()` call processes one queued topic (`HA_CLEANUP_FLUSH_BATCH = 1`). Each publish allocates a `std::string` on the heap; processing one at a time minimizes peak heap pressure.
- **Buffer compaction with `memmove`**: Topics are copied to a stack buffer before `memmove` compacts the ring buffer, avoiding pointer invalidation.
- **Drain wait between passes**: After unsubscribing, the module waits for `HA_CLEANUP_DRAIN_WAIT_MS` of no new callbacks before re-subscribing. This ensures the inbound MQTT event queue is empty, preventing stale callbacks from being attributed to the next pass.
- **Two clean passes to confirm done**: Requires two consecutive passes with no topics before transitioning to `done`, guarding against race conditions where a retained message arrives between the flush and the unsubscribe.
- **Echo detection**: Callbacks with an empty payload are skipped — they are echoes of the module's own cleanup publishes.
- **Config-only filter**: Only topics ending in `/config` are processed; non-config topics (e.g., state topics) are ignored.
- **Destroy safety**: `get_time_ms` is nulled first in `destroy()` to poison the callback before the struct is zeroed, preventing use-after-free on a partially destroyed context.
- **Graceful skip**: If `mqtt_client`, `device_id`, or `get_time_ms` is `NULL`, `run()` transitions to `done` immediately with a log message.

## Dependencies

- `i_mqtt_client` — `i_mqtt_client_t` interface for `mqtt_client_subscribe`, `mqtt_client_unsubscribe`, `mqtt_client_publish_raw`
- `esp_log` — logging via `ESP_LOGI`, `ESP_LOGW`, `ESP_LOGD`, `ESP_LOGV`
- `esp_heap_caps` — `heap_caps_get_free_size`, `heap_caps_get_largest_free_block` for heap diagnostics
- `freertos/FreeRTOS.h` — `taskENTER_CRITICAL`, `taskEXIT_CRITICAL` with a per-instance `portMUX_TYPE` for thread safety
- `geappliances_bridge_log.h` — `GEA_TAG` macro for log tag definition