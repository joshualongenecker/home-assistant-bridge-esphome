# ERD Bridge Polling

## Purpose

Probes a pre-built list of ERDs at a known host address, then settles into steady-state polling. Writes ERD values to the shared ERD cache. Does not perform broadcast discovery — that is the responsibility of `AutodiscoveryManager`.

## Public API

| Function | Description |
|----------|-------------|
| `erd_bridge_poll_init(self, timer_group, erd_client, interval_ms, host_address, appliance_type, probe_list, probe_list_count, cache)` | Initialize with a pre-built probe list and a known host address from autodiscovery |
| `erd_bridge_poll_destroy(self)` | Stop timers, unsubscribe events (no heap cleanup needed) |

## State Machine

```
poll_state_top (parent — handles appliance loss globally)
  ├─ state_probe_list (initial)
  │    ├─ entry: if probe_list empty → state_polling
  │    │         else → read first ERD sequentially
  │    ├─ read_completed: add to polling list + cache → next ERD or state_polling
  │    └─ read_failed: exclude from polling list (insert into erd_set) → next ERD or state_polling
  │
  ├─ state_polling (steady state)
  │    ├─ entry: arm polling timer (cycle idle), fire on_discovery_complete callback
  │    ├─ polling_timer_expired: idle → start cycle via send_cycle_reads() (budgeted);
  │    │   sending → resume sends; awaiting → awaiting_restart (restart on completion)
  │    ├─ read_completed: update cache, count completion, maybe restart cycle
  │    ├─ read_failed: count completion, maybe restart cycle
  │    ├─ sending phase (budget exceeded): the resume timer continues the budgeted sends
  │    ├─ appliance_lost (60 s timeout) → state_probe_list
  │    └─ 3 consecutive full-cycle failures → state_failed
  │
  └─ state_failed (terminal state)
       ├─ entry: disarm timers, log failure
       └─ No automatic recovery — bridge must be reinitialized
```

The cycle machine (`cycle_phase`) gates cycle restarts: a cycle completing in `awaiting` returns to `idle` — the interval timer is still armed, so the next cycle waits for it to fire (respecting the configured interval). This prevents overlapping cycles while allowing fast cycles to chain together without unnecessary delay.

The `awaiting_restart` phase handles the case where the interval timer fires mid-cycle: the in-progress cycle is allowed to finish, then the next cycle starts immediately without waiting for another timer interval.

## Dependencies

- `i_tiny_gea3_erd_client` — GEA3 ERD client interface
- `tiny_hsm` — hierarchical state machine
- `tiny_timer` — polling and appliance-lost timers
- `erd_lists.h` — static ERD lists (used by `erd_poll_list_builder`)
- `erd_bridge_common.h` — shared signals, timing constants, and utility templates
- `erd_cache.h` — ERD cache for change detection

## Key Design Decisions

- **Pre-built probe list**: The probe list is built externally by `erd_poll_list_builder` based on bridge mode and configuration. The bridge receives a pointer to the list and probes each ERD sequentially. This decouples the list-building logic from the state machine.
- **Fixed-capacity polling list**: `erd_polling_list` is a fixed-capacity array (`tiny_erd_t[POLLING_LIST_MAX_SIZE]`). No heap allocation.
- **Fixed-capacity ERD set**: `erd_set` is an `erd_set_t` (sorted array, capacity 649 via `POLLING_LIST_MAX_SIZE`). No heap allocation.
- **ERD cache for "publish on change"**: The shared `erd_cache_t` always caches the last published value per ERD and only publishes changed values to MQTT.
- **Simultaneous polling — all reads at once**: Each polling cycle sends reads for all ERDs in the list simultaneously via `send_cycle_reads()`, which uses `send_poll_read_requests_bounded()` to stay within a time budget (`POLL_CYCLE_SEND_BUDGET_MS = 100` ms). Per-batch yield is `POLL_YIELD_MS = 50` ms. If the budget is exceeded, a resume timer (100 ms) continues sending. This prevents blocking the ESPHome main loop long enough to trigger the ESP32 task watchdog timer.
- **Cycle restarts only when complete AND the interval timer expired**: A new polling cycle (resetting `erd_index` to 0) starts only when **all** ERDs in the current cycle have either read successfully or failed **and** the polling timer (default 10 s) has expired. The `cycle_phase` machine gates this: a cycle that finishes before the timer fires returns to `idle` and waits for it; a cycle still in flight when the timer fires enters `awaiting_restart`, and the next cycle starts immediately at completion. This prevents overlapping cycles from building up pressure in the shared GEA3 ERD client queue while allowing fast cycles to chain together without delay.
- **No overlapping polling cycles**: Because reads are sent in budgeted batches and cycles don't restart until complete, the GEA3 ERD client's fixed-size request queue cannot overflow — preventing heap corruption that previously manifested as FreeRTOS `prvCheckTasksWaitingTermination` crashes.
- **60-second appliance lost timer**: If no read completes within 60 seconds, the bridge transitions back to `state_probe_list` to rediscover the appliance.
- **Failed probe ERDs are excluded**: ERDs that fail during the probe phase (not_supported or retries_exhausted) are inserted into `erd_set` as exclusions, preventing them from being lazily registered in the polling state.
- **Cache NOT cleared on re-entry**: The ERD cache is not reset during `state_probe_list` entry or on appliance-loss re-discovery. The cache may be shared with the subscription bridge; stale entries are overwritten when new data arrives.
- **Known host address preservation**: When initialized with a known address, the bridge stores it in `known_host_address`. On appliance loss, this address is restored instead of falling back to broadcast.
- **No broadcast discovery**: The bridge always receives a known host address from `AutodiscoveryManager`. Broadcast discovery is not performed by the polling bridge.

## Additional Fields

Beyond the core polling state, the struct tracks:

| Field | Type | Description |
|-------|------|-------------|
| `polling_failure_count` | `uint8_t` | Consecutive cycle failure counter. Incremented when any ERD in the cycle fails. Reset on a cycle with no failures. Transitions to `state_failed` when reaching 3. |
| `cycle_has_failure` | `bool` | True if any ERD in the current polling cycle has failed. Cleared at cycle completion; checked there to increment `polling_failure_count`. |
| `cycle_phase` | `poll_cycle_phase_t` | Steady-state cycle machine (`idle`, `sending`, `awaiting`, `awaiting_restart`); encodes which timer is armed and whether a cycle is in flight. See the spec's §5.2. |
| `on_discovery_complete` | `void (*)(void*)` | Callback invoked once when transitioning to `state_polling`. |
| `on_discovery_complete_context` | `void*` | Context pointer for the discovery complete callback. |
| `current_state` | `polling_state_t` | Current HSM state as an enum for external diagnostics. |
| `cycle_start_ms` | `uint32_t` | `millis()` when the current cycle's first read was sent. |
| `last_cycle_time_ms` | `uint32_t` | Duration of the last completed cycle in milliseconds. |
| `cycle_count` | `uint32_t` | Total completed cycles since initialization. |

## Testing

Covered by unit tests in `test/tests/erd_bridge_poll_test.cpp` and integration tests through the full polling bridge flow. Probe phase transitions, ERD caching, appliance loss recovery, and simultaneous polling are tested with simulated ERD client activity.
