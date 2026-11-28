# ADR-0005: ERD publishing runs in a pinned FreeRTOS task that publishes from a snapshot

Status: accepted (2026-11-27)
Related: ADR-0004, ADR-0006, issue #209

## Context

Publishing an ERD value goes through ESPHome's MQTT client, which under the
ESP-IDF framework takes the IDF MQTT mutex and can block for a long time when
the broker queue is congested. Doing that on the ESPHome loop core stalls the
whole main loop — appliance polling, discovery, and the loop-task watchdog.
Doing it on a task that is not carefully placed starves the loop task instead.

## Decision

`erd_cache_mqtt_publisher` runs a FreeRTOS background task when built with the
ESP-IDF framework.

- **Pinned to core 0 at priority 1.** Core 0 is not ESPHome's loop core, and
  priority 1 is below the main loop, so the publisher can never starve the
  `loopTask` watchdog.
- **It never mutates the cache.** Each publish takes an atomic snapshot:
  `erd_cache_snapshot_next_updated()` clears the flag and copies the payload out
  of the arena in one lock hold, so the main loop cannot overwrite the value
  mid-publish (ADR-0004).
- **The state mutex guards publisher state from torn reads and is never held
  across the slow publish.** `publish_index` is deliberately not covered: it is
  the scan iterator advanced by the snapshot under the *cache* lock, its only
  other writer is `on_connected`'s reset to 0, and the race is benign (aligned
  `uint16_t`, idempotent).
- **The task stack is sized independently of its buffers.** ESPHome's MQTT client
  can add a substantial IDF call stack while the broker queue is congested, so the
  stack budget covers the call depth; the publish buffers are pre-allocated in the
  bridge object and do not consume task stack.
- **Shutdown is a two-signal handshake.** Stop sets the wake semaphore *first*,
  then clears `task_running`, so the task sees the flag on wake; the task gives
  `done_semaphore` before calling `vTaskDelete`, so the waiter knows it entered
  the termination path.
- **`destroy()` must yield after the task exits.** After `vTaskDelete()` the task
  sits on `xTasksWaitingTermination` until the idle task unlinks its TCB list
  items with `uxListRemove()`. The `StaticTask_t` TCB memory is caller-owned, so
  FreeRTOS does not free it but does touch its list pointers. `destroy()` memsets
  the publisher struct; without a yield that zeroes the list pointers mid-unlink.

## Consequences

- The main loop stays responsive while a congested broker slows publishing, and
  loop-time spikes during discovery are attributable to discovery, not to the
  publisher.
- Publisher statistics (`get_publish_rate`, `first_round_done`) are read under
  the state mutex, so the main loop sees a consistent view.
- Any new writer of publisher state must decide explicitly whether it belongs
  under the state mutex; `publish_index` is the documented exception.
- Teardown order is load-bearing: semaphore, flag, wait, yield, then clear. A
  "simplification" that drops the yield produces corruption inside FreeRTOS.

## Code anchors

- `erd_cache_mqtt_publisher.h` / `.cpp` — `mqtt_publisher_task()`,
  `start()`, `stop()`, `destroy()`, `on_connected()`, `publish_one_()`,
  `done_semaphore`, `state_mutex`, task-stack and buffer constants.
- `erd_cache_snapshot_next_updated()` in `erd_cache.cpp` (ADR-0004).
- `geappliances_bridge.cpp` — the bridge owns the cache and the publisher.
- `test/tests/erd_cache_mqtt_publisher_test.cpp`,
  `erd_cache_mqtt_publisher_pause_test.cpp`.
