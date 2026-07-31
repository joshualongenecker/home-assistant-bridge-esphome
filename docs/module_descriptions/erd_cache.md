# ERD Cache

## Purpose

Fixed-size ERD cache with static arena storage. Stores the latest data for up to `ERD_CACHE_CAPACITY` (300) ERDs. All ERD data is stored in a contiguous static arena (bump allocator). ERDs exceeding 248 bytes (GEA3 max payload) are rejected. ERD size is invariant after registration — updates are in-place `memcpy` with no alloc or free. Change detection is done at insert/update time, eliminating per-read `memcmp` overhead.

## Public API

| Function | Description |
|----------|-------------|
| `erd_cache_init(self)` | Initialize the cache (zero all entries, reset arena pointer, reset counters). |
| `erd_cache_destroy(self)` | Reset the cache. |
| `erd_cache_update(self, erd, data, data_size)` | Update or insert ERD data. Returns `true` if `update_required` was set (or entry was new). Returns `false` if data_size > 248, cache is full, arena is full, data is unchanged, or ERD size changed (appliance lost). |
| `erd_cache_set_throttle_rate_seconds(self, rate)` | Set minimum interval (seconds) between publishes per ERD. 1 = default. Range: 0–255 (0=disabled). |
| `erd_cache_mark_published(self, entry)` | Mark an ERD entry as published; reloads the publish_cooldown timer. Static inline, zero overhead when rate limiting is disabled. |
| `erd_cache_tick_cooldowns(self)` | Decrement publish_cooldown for all entries with `update_required = true`. Call once per second. Static inline, no-op when rate limiting is disabled. |
| `erd_cache_get_next_updated(self, iterator)` | Returns the next entry with `update_required = true` and `publish_cooldown = 0`, then clears `update_required`. Skips entries still in cooldown, keeping `update_required = true` for retry. Caller provides an iterator (`uint16_t`) initialized to 0. Returns `NULL` when no more updated entries remain. |
| `erd_cache_get_count(self)` | Returns the number of valid entries currently in the cache. |
| `erd_cache_get_next_entry(self, iterator)` | Returns the next valid entry in the cache, iterating all entries. Does NOT require `update_required = true` and does NOT clear any flags — it is a read-only iteration. Resets iterator to 0 when exhausted. |
| `erd_cache_get_update_rate(self)` | Returns the number of cache updates since the last call, then resets the window counter. |
| `erd_cache_get_required_update_rate(self)` | Returns the number of updates that set `update_required = true` since the last call, then resets the window counter. |
| `erd_cache_mark_all_updated(self)` | Marks all valid entries as `update_required = true`. Used after a long MQTT disconnect to force a full drain of retained values to the broker. |
| `erd_cache_entry_data(self, entry)` | Returns a pointer to the ERD data in the arena. Static inline. |
| `erd_cache_get_arena_usage(self)` | Returns the number of bytes currently used in the arena. |
| `erd_cache_get_arena_usage_percent(self)` | Returns the arena usage as a percentage (0-100). |

## Storage Strategy

### Static Arena

All ERD data is stored in a contiguous `uint8_t[4096]` array (the arena) embedded in the cache struct. Storage is allocated via a bump allocator: each new ERD carves out `data_size` bytes from the arena, and the arena pointer advances.

**Allocation:** On new entry, check if `arena_offset + data_size <= 4096`. If yes, copy data to `&arena[arena_offset]`, set `entry->data_offset`, advance `arena_offset`. If no, log warning and reject.

**Deallocation:** No per-entry deallocation — the arena is reset to offset 0 on init/destroy.

**Size Limits:**
- Max ERD data size: 248 bytes (GEA3 protocol limit)
- ERDs exceeding 248 bytes are rejected at registration

### Storage Selection

Storage is determined on **registration only**. ERD data size is invariant after registration. If a size change is detected on an existing entry, `erd_cache_update()` logs an error and returns `false`, signaling the appliance firmware has changed and the bridge should reinitialize.

## Update Flow

`erd_cache_update()` handles both new entries and updates to existing entries:

1. **Find existing entry** via `erd_cache_find()` (linear scan of `entries[]`)
2. **If found:**
    - Increment `update_count` and `update_count_window`
    - Check if data has changed via `erd_data_changed()` (memcmp only — size is invariant)
   - If data unchanged: return `false` (early exit, no storage churn)
    - If data size differs from existing: log error, return `false` (appliance lost)
    - In-place `memcpy` into arena
   - Set `update_required = data_changed`
3. **If not found:**
    - Check data_size <= 248 (GEA3 max), reject if exceeded
    - Scan `entries[]` for the first slot with `valid == false`
    - If no free slot: return `false` (cache full)
    - Check arena has room, reject if full
    - Initialize entry, allocate from arena
    - Set `valid = true` and `update_required = true`

## Change Detection

`erd_data_changed()` compares the new data against the existing entry using `memcmp`. ERD size is invariant after registration, so only the data content is compared — no size check is needed.

## Entry Structure

```c
typedef struct {
  tiny_erd_t erd;
  uint16_t data_offset;     /* offset into arena */
  uint8_t data_size;        /* invariant after registration, max 248 */
  bool update_required;
  uint8_t publish_cooldown; /* counts down from max_cooldown to 0; 0 = eligible */
  bool valid;
} erd_cache_entry_t;
```

## Cache Structure

```c
typedef struct erd_cache_t {
  erd_cache_entry_t entries[ERD_CACHE_CAPACITY];  // 300 entries
  uint8_t arena[ERD_CACHE_ARENA_SIZE];            // 4096-byte static arena
  uint16_t arena_offset;                          // next free byte in arena
  uint32_t update_count;              // total cache updates since init
  uint32_t update_count_window;       // updates since last get_update_rate() call
  uint32_t required_update_count;     // total updates setting update_required=true since init
  uint32_t required_update_count_window; // such updates since last get_required_update_rate() call
  uint8_t max_cooldown;              // configured rate limit in seconds; 0 = disabled
  bool initialized;                   // true after first successful erd_cache_init()
} erd_cache_t;
```

## Dependencies

- `tiny_gea3_erd_client.h` — `tiny_erd_t` type definition
- `<cstring>` — for `memcmp`
- `esphome/core/log.h` — for `ESP_LOGD`, `ESP_LOGW`, `ESP_LOGE`

## Key Design Decisions

- **Static arena for all ERDs**: All ERD data is stored in a contiguous 4 KB arena — zero heap allocation, zero fragmentation. This covers the vast majority of appliance ERD sets (typical usage is 2-8 KB).
- **Bump allocator**: Storage is allocated by advancing a pointer — O(1) allocation, no free list complexity.
- **No per-entry deallocation**: The arena is reset to offset 0 on init/destroy. No need to track individual allocations.
- **Size limit enforcement**: ERDs > 248 bytes (GEA3 max payload) are rejected at registration. This is a protocol limitation.
- **In-place updates**: Existing entries are updated with a single `memcpy` into the arena — no free, no alloc. This eliminates all storage churn on every poll cycle.
- **Size invariance**: ERD data size never changes after registration. A size mismatch is treated as an appliance firmware change — `erd_cache_update()` logs an error and returns `false`, signaling the bridge should reinitialize.
- **Early exit on unchanged data**: When data hasn't changed, the update returns immediately without touching storage.
- **Change detection at update time**: `update_required` is set during `erd_cache_update()`, not during iteration. This eliminates per-read `memcmp` overhead in the publisher loop.
- **Two iterators**: `erd_cache_get_next_updated()` for the publisher (clears `update_required` flag) and `erd_cache_get_next_entry()` for read-only iteration.
- **Rate counters**: `update_count_window` and `required_update_count_window` accumulate updates and are reset by `get_update_rate()` and `get_required_update_rate()`. The window is determined by the call interval of the consumer (e.g. ~60s if called once per minute).
- **No eviction**: The cache has a fixed capacity with no eviction policy. If the cache is full and a new ERD arrives that isn't already cached, the update is silently dropped. This is acceptable because the ERD set is bounded by the appliance's supported ERDs, which is typically well under 200.
- **Arena usage monitoring**: The arena usage (bytes and percentage) is logged when the appliance reaches steady state, providing visibility into memory utilization.

## Testing

Covered indirectly through the unit tests for `erd_bridge_subscribe`, `erd_bridge_poll`, and `erd_cache_mqtt_publisher`. The cache is exercised through all bridge operations and publishing flows.
