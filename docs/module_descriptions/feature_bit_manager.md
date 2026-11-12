# FeatureBitManager

## Purpose

Reads and parses appliance API feature bit ERDs (0x0092 through 0x010D), building a filtered list of valid ERDs that the appliance supports. Fully self-driving: owns its own timers and event subscriptions. This list is used by the polling bridge to only poll ERDs that are actually registered, and by the MQTT adapter to filter published values.

## Public API

| Method | Description |
|--------|-------------|
| `init(erd_client, host_address, timer_group)` | Initialize with ERD client, host address, and timer group. Subscribes to ERD client activity events. |
| `cleanup()` | Unsubscribe from events and stop timers. Safe to call multiple times. |
| `start()` | Start the feature-bit reading sequence. Idempotent with `read_queued_` guard. |
| `get_valid_erd_count()` | Returns the number of valid ERDs. |
| `get_skipped_erd_count()` | Returns how many feature-list ERDs were skipped **while being read**. Covers unsupported features, failed reads (the failure reason is not inspected), and out-of-order or empty reads — reported as `skipped (unsupported or unreadable)`. Parse-phase drops (data shorter than expected, or all-zero type/version/bitmap after a successful read) are not counted. |
| `get_valid_erd(idx)` | Returns the valid ERD at the given index (0-based). |
| `get_state()` | Returns the current `FeatureBitState` |
## State Machine

```
FEATURE_BIT_STATE_READING  (single state for all 11 ERD reads)
  → reading_idx_ 0: read ERD 0x0092 (common feature API)
    → reading_idx_ 1: read ERD 0x0093 (appliance feature API 0)
      → ... (reading_idx_ 2-10: ERDs 0x0094-0x010D)
        → FEATURE_BIT_STATE_PARSING
              → FEATURE_BIT_STATE_COMPLETE
                  → valid_list_ready_ = true

ERD 0x0092 failure → FEATURE_BIT_STATE_FAILED
  → bridge falls back to full polling (no feature filtering)

Any other read failure → skip to next ERD in sequence (advance reading_idx_)
```

### Self-Driving Architecture

The manager is fully self-driving with no polling from the bridge:

1. **`init()`** subscribes to `tiny_gea3_erd_client_on_activity` events and stores the `tiny_timer_group_t*`
2. **`start()`** queues the first ERD read (0x0092, Common Feature API). Uses `read_queued_` guard for idempotency - calling `start()` multiple times before the first read completes does not double-queue.
3. **Event handler `on_erd_activity_()`** drives the read sequence:
   - Filters events by `address == host_address_` to ignore responses for other appliances
   - Ignores all events once in PARSING, COMPLETE, or FAILED state to prevent state machine corruption
   - If `read_queued_` is false (queue was full on last attempt), retries `queue_erd_read_()` immediately
   - On `read_completed`: stores ERD data, advances state, queues next read via `queue_erd_read_()`
   - On `read_failed`: calls `skip_to_next_erd_()` to advance state and queue next read
   - If ERD 0x0092 fails: transitions to `FEATURE_BIT_STATE_FAILED` (no further reads)
   - Each completed or failed read immediately triggers the next read in the sequence
4. **After last ERD (0x010D)**: transitions to `FEATURE_BIT_STATE_PARSING` and arms a periodic timer (5ms interval) for incremental parsing
5. **Timer callback `parse_next_step_()`** drives incremental parsing:
   - First tick: clears valid_erds_ and initializes parsing state
   - Next ticks: processes common feature descriptors (4 per tick via `COMMON_PARSE_PER_CALL`)
   - Next ticks: processes one appliance ERD (0x0093-0x010D) per tick
   - Final tick: adds mandatory ERDs, builds valid_erds_vec_, sets `valid_list_ready_ = true`, stops timer, transitions to COMPLETE
6. **Bridge polls `get_state()`** to check progress (HSM checks for COMPLETE or FAILED)

### Parsing Details

- **Common features (ERD 0x0092)**: Parsed first, processes 17 descriptors at 4 per tick (~5 ticks total)
- **Appliance features (ERD 0x0093-0x010D)**: Parsed one per tick (10 ticks total), matched against descriptor tables by appliance type and version
- **Mandatory ERDs**: Always included in final list regardless of feature bits: 0x0001, 0x0002, 0x0008, 0x0092-0x0097, 0x0109-0x010D (14 total)
- **Total parsing ticks**: ~16 ticks (5 common + 10 appliance + 1 final; init happens in the first tick) at 5ms each = ~80ms

### Key Design Decisions

- **No timeout**: The feature bits phase has no timeout. The HSM handles appliance loss.
- **No MQTT dependency**: Removed `i_mqtt_client` dependency. The manager only needs the ERD client and timer group.
- **No `run()` method**: The manager is fully event-driven and timer-driven. The bridge does not poll `run()`.
- **Read failure**: Each ERD read that fails is skipped (not retried) and counted in `skipped_erds_count_`. The manager advances to the next ERD in the sequence. Queue-full retries go through a separate retry path and are not counted as skips.
- **Common Feature API failure**: If ERD 0x0092 fails, the manager transitions to `FEATURE_BIT_STATE_FAILED` instead of continuing. The bridge falls back to full polling (no feature filtering). `is_feature_bits_complete()` returns true for FAILED, allowing the HSM to transition past the feature_bits phase.
- **Queue full retry**: If `tiny_gea3_erd_client_read()` returns false (queue full), the manager arms a one-shot retry timer (50ms). If an ERD activity event arrives before the timer fires and queues the read, the timer becomes a no-op. This prevents indefinite stalling when the queue is persistently full with no other activity.
- **Event filtering**: `on_erd_activity_()` filters by `address == host_address_` AND verifies the event's ERD matches the one the current READING state expects. Unrelated reads (e.g., from the polling bridge) are ignored. Events in PARSING, COMPLETE, or FAILED states are also ignored to prevent state machine corruption.
- **Incremental parsing**: Uses `tiny_timer` with 5ms interval to spread heap allocations across multiple ticks, avoiding the ESP32 Task Watchdog Timer.
- **Idempotent start**: `read_queued_` flag ensures `start()` can be called multiple times safely (e.g., from HSM entry + bridge loop).

## Dependencies

- `tiny_gea3_erd_client` — ERD client interface
- `tiny_event`, `tiny_event_subscription`, `tiny_timer` — event subscription and timer infrastructure
- `appliance_api_feature_lists.h` — generated descriptor tables for feature bit parsing
- `geappliances_bridge_constants.h` — ERD constants

## Testing

Covered by dedicated unit tests in `test/tests/feature_bit_manager_test.cpp` (20+ tests covering initialization, start idempotency, event-driven read sequencing, failure handling, timer-driven incremental parsing, valid ERD list generation, and FAILED state transitions). Also covered by integration tests through the full startup sequence.