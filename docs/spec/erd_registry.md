# ERD Registry — Specification

## 1. Overview

### 1.1 Purpose

The ERD registry is the single authoritative source for which ERDs are valid (supported by the appliance). It is populated at startup by the Feature Bit Manager, optionally extended with user-configured custom ERDs, and consulted by the subscription bridge to drop publications for ERDs the appliance does not support.

### 1.2 Responsibilities

- Own the valid-ERD set, populated at startup from the Feature Bit Manager and extended with custom ERDs
- Expose the valid-ERD filter used by the subscription bridge to gate cache writes
- Expose read-only accessors used by diagnostics

### 1.3 Not Responsible For

- Reading ERDs from the appliance (Feature Bit Manager does that)
- Publishing MQTT messages (EsphomeMqttClientAdapter does that)
- Bridge lifecycle or startup management
- ERD value caching or polling

---

## 2. Setup Methods

### 2.1 Set Valid ERDs

```cpp
void set_valid_erds(const tiny_erd_t* erds, uint16_t count);

```

Copies the valid-ERD set from the Feature Bit Manager and enables valid-ERD filtering.

| Parameter | Description |
|-----------|-------------|
| `erds` | Pointer to the array of valid ERDs from the Feature Bit Manager. |
| `count` | Number of valid ERDs in the array. |

**Behavior:**
- If `erds` is null or `count` is zero: returns immediately; the current filter state is unchanged (if filtering was previously enabled, it stays enabled). This prevents an empty set from silently suppressing all publishes.
- If `count` exceeds `ERD_REGISTRY_MAX_VALID`: copies only the first `ERD_REGISTRY_MAX_VALID` entries.
- On success: sorts `valid_erds_` in ascending order (for binary search), then sets `valid_erds_ready_` to `true`, activating the filter.

Called once during bridge initialization, after the Feature Bit Manager completes parsing.

### 2.2 Add Valid ERDs

```cpp
void add_valid_erds(const tiny_erd_t* erds, uint16_t count);
```

Appends additional ERDs (e.g., user-configured custom ERDs) to the valid set so they always pass the filter.

| Parameter | Description |
|-----------|-------------|
| `erds` | Pointer to the array of additional ERDs. |
| `count` | Number of ERDs in the array. |

**Behavior:**
- If the filter has not been initialized by `set_valid_erds()`, or `erds` is null or `count` is zero: no-op.
- Skips values already present in the sorted set (binary search) and duplicates within the incoming batch.
- Appends until `ERD_REGISTRY_MAX_VALID` is reached; excess entries are dropped.
- Re-sorts the full set afterwards to preserve binary-search correctness.

---

## 3. Queries

### 3.1 Has Valid ERDs Filter

```cpp
bool has_valid_erds_filter() const;
```

Returns `valid_erds_ready_`. When `false`, no filter is active and all ERDs pass validation. When `true`, only ERDs present in the valid set are accepted.

### 3.2 Is Valid

```cpp
bool is_valid(tiny_erd_t erd) const;
```

Returns `true` if the ERD passes the valid-ERD filter.

**Behavior:**
- If `valid_erds_ready_` is `false`: returns `true` (no filter active — all ERDs are valid).
- Otherwise: performs a binary search on `valid_erds_`; returns `true` if found, `false` otherwise.

---

## 4. Read-Only Accessors

### 4.1 Count

```cpp
uint16_t valid_erd_count() const;
```

Returns the current number of valid ERDs.

### 4.2 Indexed Access

```cpp
tiny_erd_t valid_erd(uint16_t idx) const;
```

Returns the ERD at the given 0-based index. If `idx` is out of range, returns `0`.

---

## 5. Data Structures

```cpp
#define ERD_REGISTRY_MAX_VALID 645

class ErdRegistry {
    tiny_erd_t valid_erds_[ERD_REGISTRY_MAX_VALID];
    uint16_t valid_erds_count_{0};
    bool valid_erds_ready_{false};
};
```

| Field | Type | Description |
|-------|------|-------------|
| `valid_erds_` | `tiny_erd_t[645]` | Fixed-capacity array of valid ERDs. Populated at startup, extended with custom ERDs. |
| `valid_erds_count_` | `uint16_t` | Number of valid ERDs in the array. |
| `valid_erds_ready_` | `bool` | Gate: when `true`, the valid-ERD filter is active. |

The array capacity is `ERD_REGISTRY_MAX_VALID = 645`. The set is written at bridge initialization (`set_valid_erds()` then `add_valid_erds()`) and read-only afterwards.

---

## 6. Usage Flow

```
Startup
  │
  ├─ Feature Bit Manager reads and parses feature bit ERDs
  │
  └─ Bridge init calls set_valid_erds(valid_erds, count)
       │
       ├─ If count > 0: valid_erds_ready_ = true (filter active)
       └─ If count == 0: valid_erds_ready_ = false (no filter)

Bridge init (continued)
  │
  └─ add_valid_erds(custom_erds, n) — merge custom ERDs, re-sort

Runtime
  │
  └─ Subscription bridge calls is_valid(erd) before caching a
       subscription publication; invalid ERDs are dropped at ingestion

Diagnostics
  │
  ├─ valid_erd_count() / valid_erd(idx) — enumerate valid ERDs
  └─ is_valid(erd) — filter publication decisions
```

---

## 7. Invariants

1. **Fixed-capacity array:** `valid_erds_` is bounded by `ERD_REGISTRY_MAX_VALID` (645). No dynamic allocation.
2. **Empty valid set preserves filter state:** If `set_valid_erds()` receives a null pointer or zero count, it returns immediately without modifying `valid_erds_ready_`. If filtering was previously disabled, it stays disabled and `is_valid()` returns `true` for all ERDs. If filtering was previously enabled, it stays enabled.
3. **`add_valid_erds()` requires an initialized set:** Without a prior `set_valid_erds()`, it is a no-op — custom ERDs never enable filtering on their own.
4. **No eviction:** Once an ERD is marked valid, it remains in the set for the lifetime of the registry. There is no removal or rotation.
5. **Out-of-bounds accessor returns zero:** `valid_erd(idx)` returns `0` when `idx` exceeds the current count.
6. **Sorted order:** `valid_erds_` is kept sorted ascending so `is_valid()` can binary search. `add_valid_erds()` re-sorts after appending.

---

## 8. Dependencies

| Dependency | Role |
|------------|------|
| `tiny_erd.h` | Defines `tiny_erd_t` type used for all ERD values. |

---

## 9. Known Limitations

1. **Fixed capacity:** The array is capped at 645 entries. If the appliance reports more than 645 valid ERDs, excess entries are silently dropped.
2. **Binary search for `is_valid()`:** Validation uses `std::binary_search` on the sorted valid set. O(log n) lookup, bounded by the 645-entry capacity.
3. **No removal:** ERDs cannot be removed from the valid set. If the appliance's supported ERD set changes at runtime (e.g., firmware update), the registry must be re-initialized.
