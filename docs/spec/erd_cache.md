# ERD Cache — Specification

## 1. Overview

### 1.1 Purpose

The ERD cache is a fixed-size store for the latest data of up to `ERD_CACHE_CAPACITY` (300) ERDs. All ERD data is stored in a contiguous static arena (bump allocator). ERDs exceeding 248 bytes (GEA3 max payload) are rejected. ERD data size is invariant after registration — updates are in-place `memcpy` with no allocation or deallocation. Change detection is performed at insert/update time, eliminating per-read `memcmp` overhead in the publisher loop.

### 1.2 Responsibilities

- Store the latest data for each ERD observed by the bridge
- Allocate storage from static arena at registration (bump allocator)
- Detect data changes via `memcmp` and set `update_required` accordingly
- Provide iteration over updated entries (for publishing) and all entries
- Track update rates via windowed counters

### 1.3 Not Responsible For

- ERD discovery or polling
- Publishing data to MQTT
- Home Assistant discovery payload generation
- Managing the lifecycle of the bridge or ERD client

---

## 2. Initialization

### 2.1 Init

```c
void erd_cache_init(erd_cache_t* self);
```

Initializes the cache for use. Safe to call on a previously-initialized cache (re-init).

**Behavior:**
1. Explicitly zeroes all entry fields (erd, data_offset, data_size, update_required, publish_cooldown, valid) — avoids UBSan issues with bool fields after `memset`
2. Resets `arena_offset` to 0 (arena is not cleared, just the pointer)
3. Resets all counters (`update_count`, `update_count_window`, `required_update_count`, `required_update_count_window`) to zero
4. Sets `self->initialized = true`

No heap deallocation is needed — the arena is static memory.

### 2.2 Destroy

```c
void erd_cache_destroy(erd_cache_t* self);
```

Resets the cache. Guards against being called on a never-initialized struct (checks `self->initialized`).

**Behavior:**
1. If `!self->initialized`, returns immediately
2. Calls `erd_cache_init(self)` to reset all entries and arena pointer
3. Sets `self->initialized = false`

No heap deallocation is needed — the arena is static memory.

---

## 3. Storage Strategy

### 3.1 Static Arena

All ERD data is stored in a contiguous `uint8_t[ERD_CACHE_ARENA_SIZE]` array (4096 bytes) embedded in the `erd_cache_t` struct. Storage is allocated via a bump allocator: each new ERD carves out `data_size` bytes from the arena, and `arena_offset` advances by that amount.

**Allocation:**
- On new entry: check `arena_offset + data_size <= ERD_CACHE_ARENA_SIZE`
- If room: `memcpy(&arena[arena_offset], data, data_size)`, set `entry->data_offset = arena_offset`, advance `arena_offset += data_size`
- If full: log warning (one-time), return `false`

**Deallocation:**
- No per-entry deallocation — the arena is reset to offset 0 on `erd_cache_init()` or `erd_cache_destroy()`
- No fragmentation possible — all data is contiguous

### 3.2 Size Limits

- **Max ERD data size:** 248 bytes (`ERD_CACHE_MAX_DATA_SIZE`)
- **Reason:** GEA3 protocol limit (255 byte frame - 7 byte overhead)
- ERDs exceeding this limit are rejected at registration with a warning log

### 3.3 Size Invariance

ERD data size is invariant after registration. If a size change is detected on an existing entry, `erd_cache_update()` logs an error and returns `false`, signaling the appliance firmware has changed and the bridge should reinitialize.

---

## 4. Update Flow

`erd_cache_update()` handles both new entries and updates to existing entries:

```c
bool erd_cache_update(erd_cache_t* self, tiny_erd_t erd, const uint8_t* data, uint8_t data_size);
```

### 4.1 Existing Entry

1. Find existing entry via `erd_cache_find()` (linear scan of `entries[]`)
2. Increment `update_count` and `update_count_window`
3. **Size check:** If `data_size != existing->data_size`: log error, return `false` (appliance lost)
4. **Change detection:** Call `erd_data_changed()` — `memcmp` of new data against existing buffer using `existing->data_size`
5. **Early exit:** If `!data_changed`: return `false` (no storage churn)
6. **In-place update:** `memcpy` into arena at `&self->arena[existing->data_offset]`
7. **Set flag:** `existing->update_required = data_changed`
8. If `update_required` is true, increment `required_update_count` and `required_update_count_window`
9. Return `existing->update_required`

### 4.2 New Entry

1. **Size check:** If `data_size > ERD_CACHE_MAX_DATA_SIZE` (248): log warning (one-time), return `false`
2. Scan `entries[]` for the first slot with `valid == false`
3. If no free slot: if not already warned, log a one-time warning and set `s_slot_overflow_warned = true`; return `false`
4. **Arena check:** If `arena_offset + data_size > ERD_CACHE_ARENA_SIZE`: log warning (one-time), return `false`
5. Increment `update_count`, `update_count_window`, `required_update_count`, `required_update_count_window`
6. Initialize entry: set `erd`, `data_offset = arena_offset`, `data_size`, `valid = true`, `update_required = true`, `publish_cooldown = 0`
7. `memcpy(&arena[arena_offset], data, data_size)`
8. Advance `arena_offset += data_size`
9. Return `true`

---

## 5. Change Detection

`erd_data_changed()` compares the new data against the existing entry using `memcmp`:

```c
static bool erd_data_changed(const erd_cache_t* self,
                             const erd_cache_entry_t* existing,
                             const uint8_t* new_data, uint8_t new_size);
```

- The `new_size` parameter is unused (size is invariant); `existing->data_size` is used for the `memcmp` length to guard against OOB reads
- Reads from `&self->arena[existing->data_offset]`
- Returns `true` if the data differs, `false` if identical

---

## 6. Data Structures

### 6.1 Entry

```c
typedef struct {
  tiny_erd_t erd;
  uint16_t data_offset;     /* offset into arena (0 = not allocated) */
  uint8_t data_size;        /* invariant after registration, max 248 */
  bool update_required;
  uint8_t publish_cooldown; /* counts down from max_cooldown to 0; 0 = eligible */
  bool valid;
} erd_cache_entry_t;
```

| Field | Type | Description |
|-------|------|-------------|
| `erd` | `tiny_erd_t` | ERD identifier |
| `data_offset` | `uint16_t` | Offset into arena where data is stored |
| `data_size` | `uint8_t` | Actual data size (invariant after registration, max 248) |
| `update_required` | `bool` | Set during update; cleared by `get_next_updated()` |
| `publish_cooldown` | `uint8_t` | Counts down from `max_cooldown` to 0; 0 means eligible for publishing |
| `valid` | `bool` | True if entry is in use |

### 6.2 Cache
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

| Field | Type | Description |
|-------|------|-------------|
| `entries` | `erd_cache_entry_t[300]` | Fixed array of cache entries |
| `arena` | `uint8_t[4096]` | Static arena for all ERD data |
| `arena_offset` | `uint16_t` | Bump pointer — next free byte in arena |
| `update_count` | `uint32_t` | Total updates since init (cumulative) |
| `update_count_window` | `uint32_t` | Updates since last `get_update_rate()` call |
| `required_update_count` | `uint32_t` | Total updates that set `update_required=true` since init |
| `required_update_count_window` | `uint32_t` | Such updates since last `get_required_update_rate()` call |
| `max_cooldown` | `uint8_t` | Configured rate limit in seconds; 0 = disabled |
| `initialized` | `bool` | Guard for safe init/destroy |

---

## 7. API

### 7.1 Lifecycle

| Function | Signature | Description |
|----------|-----------|-------------|
| `erd_cache_init` | `void erd_cache_init(erd_cache_t* self)` | Initialize the cache. Zeroes all entries, resets arena pointer, resets counters. Safe for re-init. |
| `erd_cache_destroy` | `void erd_cache_destroy(erd_cache_t* self)` | Reset cache, set `initialized = false`. Guards against never-initialized structs. |

### 7.2 Update

| Function | Signature | Description |
|----------|-----------|-------------|
| `erd_cache_update` | `bool erd_cache_update(erd_cache_t* self, tiny_erd_t erd, const uint8_t* data, uint8_t data_size)` | Update or insert ERD data. Returns `true` if `update_required` was set (or entry was new). Returns `false` if data_size > 248, cache is full, arena is full, data is unchanged, or ERD size changed. |

### 7.3 Iteration

| Function | Signature | Description |
|----------|-----------|-------------|
| `erd_cache_get_next_updated` | `erd_cache_entry_t* erd_cache_get_next_updated(erd_cache_t* self, uint16_t* iterator)` | Returns the next entry with `update_required = true`, then clears the flag. Caller provides an iterator (`uint16_t`) initialized to 0. Entries with an active `publish_cooldown` are skipped (rate-limited) when `max_cooldown > 0`. Returns `NULL` when no more updated entries remain; resets iterator to 0. |
| `erd_cache_get_count` | `uint16_t erd_cache_get_count(erd_cache_t* self)` | Returns the number of valid entries currently in the cache. |
| `erd_cache_get_next_entry` | `erd_cache_entry_t* erd_cache_get_next_entry(erd_cache_t* self, uint16_t* iterator)` | Returns the next valid entry in the cache, iterating all entries. Does NOT require `update_required = true` and does NOT clear any flags — it is a read-only iteration. Resets iterator to 0 when exhausted. |
| `erd_cache_mark_all_updated` | `void erd_cache_mark_all_updated(erd_cache_t* self)` | Marks all valid entries as `update_required = true`. Used after a long MQTT disconnect to force a full drain of retained values to the broker. |

### 7.4 Rate Counters

| Function | Signature | Description |
|----------|-----------|-------------|
| `erd_cache_get_update_rate` | `uint32_t erd_cache_get_update_rate(erd_cache_t* self)` | Returns the number of cache updates since the last call, then resets `update_count_window` to zero. |
| `erd_cache_get_required_update_rate` | `uint32_t erd_cache_get_required_update_rate(erd_cache_t* self)` | Returns the number of updates that set `update_required = true` since the last call, then resets `required_update_count_window` to zero. |

The window is determined by the call interval of the consumer (e.g., ~60 s if called once per minute).

### 7.5 Data Access

| Function | Signature | Description |
|----------|-----------|-------------|
| `erd_cache_entry_data` | `static inline const uint8_t* erd_cache_entry_data(const erd_cache_t* self, const erd_cache_entry_t* entry)` | Returns a pointer to the ERD data in the arena. Static inline — zero overhead. Returns `NULL` if entry is `NULL` or `data_offset == 0`. |
| `erd_cache_get_arena_usage` | `static inline uint16_t erd_cache_get_arena_usage(const erd_cache_t* self)` | Returns the number of bytes currently used in the arena. |
| `erd_cache_get_arena_usage_percent` | `static inline uint8_t erd_cache_get_arena_usage_percent(const erd_cache_t* self)` | Returns the arena usage as a percentage (0-100). |

---

## 8. Invariants

1. **Size invariance:** ERD data size never changes after registration. A size mismatch on update is treated as an appliance firmware change — `erd_cache_update()` logs an error and returns `false`.

2. **Size limit:** ERD data size never exceeds 248 bytes (GEA3 max payload). ERDs exceeding this limit are rejected at registration.

3. **No eviction:** The cache has a fixed capacity with no eviction policy. If the cache is full and a new ERD arrives that isn't already cached, the update is silently dropped (with a one-time warning).

4. **No per-update allocation:** Updates to existing entries use in-place `memcpy` — no allocation on the update path.

5. **No heap allocation:** All ERD data is stored in the static arena. No `new`, `delete`, or heap fragmentation.

6. **Early exit on unchanged data:** When data hasn't changed, the update returns immediately without touching storage.

7. **Change detection at update time:** `update_required` is set during `erd_cache_update()`, not during iteration. This eliminates per-read `memcmp` overhead in the publisher loop.

8. **New entries always marked updated:** New entries always set `update_required = true`.

9. **Iterator reset on exhaustion:** Both `get_next_updated()` and `get_next_entry()` reset the iterator to 0 when no more entries are found, allowing the caller to restart iteration.

10. **Safe init/destroy:** `erd_cache_init()` and `erd_cache_destroy()` guard against non-initialized structs.

---

## 9. Dependencies

| Dependency | Purpose |
|------------|---------|
| `tiny_gea3_erd_client.h` | `tiny_erd_t` type definition |
| `<cstring>` | `memcmp` for change detection, `memcpy` for data copy |
| `esphome/core/log.h` | `ESP_LOGD`, `ESP_LOGW`, `ESP_LOGE` for logging |

---

## 10. Known Limitations

1. **Fixed capacity with no eviction:** The cache holds at most 300 entries. If the cache is full and a new ERD arrives that isn't already cached, the update is silently dropped.

2. **Fixed arena with no eviction:** The arena holds at most 4096 bytes of ERD data. If the arena is full and a new ERD arrives, the update is silently dropped.

3. **Max ERD size limit:** ERDs exceeding 248 bytes (GEA3 max payload) are rejected. This is a protocol limitation, not a cache limitation.

4. **Linear scan for lookup:** `erd_cache_find()` scans entries sequentially. With 300 entries this is bounded and fast, but a hash map would be O(1).

5. **Linear scan for free slot:** New entry insertion scans for the first invalid slot, which is O(n) in the worst case.

6. **No batch republish:** `get_next_updated()` clears `update_required` per entry. If publishing fails mid-iteration (e.g., MQTT disconnect), the cleared entries are not re-marked. A batch republish mechanism would need to iterate all entries via `get_next_entry()` and re-publish.
