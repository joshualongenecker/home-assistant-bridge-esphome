# ERD Registry — Specification

## 1. Overview

### 1.1 Purpose

The ERD registry is the single authoritative source for which ERDs are valid (supported by the appliance). It is populated at startup by the Feature Bit Manager, optionally extended with user-configured custom ERDs, and consulted by the subscription bridge to drop publications for ERDs the appliance does not support. The feature-bit half of the set is borrowed from the Feature Bit Manager rather than copied, so the 645-entry list lives in one place.

### 1.2 Responsibilities

- Filter against the valid-ERD set — borrowed from the Feature Bit Manager, extended with the custom ERDs it stores itself
- Expose the valid-ERD filter used by the subscription bridge to gate cache writes
- Expose read-only accessors used by diagnostics

### 1.3 Not Responsible For

- Reading ERDs from the appliance, and owning the storage of the feature-bit valid set (Feature Bit Manager does both)
- Publishing MQTT messages (EsphomeMqttClientAdapter does that)
- Bridge lifecycle or startup management
- ERD value caching or polling

---

## 2. Setup Methods

### 2.1 Set Valid ERDs

```cpp
void set_valid_erds(tiny_erd_t* erds, uint16_t count);

```

Adopts the Feature Bit Manager's valid-ERD array and enables valid-ERD filtering. The registry never copies the set: it sorts the caller's array in place and keeps the pointer (architecture review rec 9 removed the registry's own 645-entry copy of the same set).

| Parameter | Description |
|-----------|-------------|
| `erds` | The Feature Bit Manager's valid-ERD array. It is sorted in place by this call, and must outlive the filter — it is a fixed member array of the manager, so it does. |
| `count` | Number of valid ERDs in the array. |

**Behavior:**
- If `erds` is null or `count` is zero: returns immediately; the current filter state is unchanged (if filtering was previously enabled, it stays enabled). This prevents an empty set from silently suppressing all publishes.
- If `count` exceeds `ERD_REGISTRY_MAX_VALID`: only the first `ERD_REGISTRY_MAX_VALID` entries are adopted.
- On success: sorts the borrowed array ascending (for binary search), records pointer and count, discards any earlier additions (they were deduplicated against the previous base set), and sets `valid_erds_ready_` to `true`, activating the filter.

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
- Skips values already present in the borrowed set (binary search) and duplicates within the incoming batch.
- Additions get their own storage, bounded by `ERD_REGISTRY_MAX_EXTRA` (128, matching `GeappliancesBridge::CUSTOM_ERDS_MAX`), and the merged set never exceeds `ERD_REGISTRY_MAX_VALID`. Excess entries are dropped.
- Sorts the additions afterwards to preserve binary-search correctness.

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
- Otherwise: binary-searches the borrowed set, then the registry's own additions, and returns `true` if found in either.

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

Returns the ERD at the given 0-based index of the merged sorted order of the borrowed set and the additions. If `idx` is out of range, returns `0`. The merge is walked per call (`O(n)`) rather than materialized — this accessor is diagnostics-only.

---

## 5. Data Structures

```cpp
#define ERD_REGISTRY_MAX_VALID 645
#define ERD_REGISTRY_MAX_EXTRA 128

class ErdRegistry {
    const tiny_erd_t* base_erds_{nullptr};
    uint16_t base_count_{0};
    tiny_erd_t extra_erds_[ERD_REGISTRY_MAX_EXTRA];
    uint16_t extra_count_{0};
    bool valid_erds_ready_{false};
};
```

| Field | Type | Description |
|-------|------|-------------|
| `base_erds_` | `const tiny_erd_t*` | The valid-ERD set, borrowed from the Feature Bit Manager. Never owned here. |
| `base_count_` | `uint16_t` | Number of borrowed entries (clamped to `ERD_REGISTRY_MAX_VALID`). |
| `extra_erds_` | `tiny_erd_t[128]` | Entries the registry owns: the custom ERDs merged in by `add_valid_erds()`. |
| `extra_count_` | `uint16_t` | Number of owned entries. |
| `valid_erds_ready_` | `bool` | Gate: when `true`, the valid-ERD filter is active. |

The borrowed set is capped at `ERD_REGISTRY_MAX_VALID = 645` and the owned additions at `ERD_REGISTRY_MAX_EXTRA = 128`. Both are written at bridge initialization (`set_valid_erds()` then `add_valid_erds()`) and read-only afterwards. Borrowing instead of copying is what keeps the 645-entry set living in exactly one place; the cost is a second binary search in `is_valid()` and an `O(n)` merged walk in `valid_erd()`.

---

## 6. Usage Flow

```
Startup
  │
  ├─ Feature Bit Manager reads and parses feature bit ERDs
  │
  └─ Bridge init calls set_valid_erds(valid_erds, count)
       │
       ├─ If count > 0: sorts the caller's array in place, borrows it,
       │                valid_erds_ready_ = true (filter active)
       └─ If count == 0: valid_erds_ready_ = false (no filter)

Bridge init (continued)
  │
  └─ add_valid_erds(custom_erds, n) — merge custom ERDs into extra_erds_

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

1. **Bounded storage, no dynamic allocation:** the borrowed set is capped at `ERD_REGISTRY_MAX_VALID` (645) and the registry's own additions at `ERD_REGISTRY_MAX_EXTRA` (128).
2. **Empty valid set preserves filter state:** If `set_valid_erds()` receives a null pointer or zero count, it returns immediately without modifying `valid_erds_ready_`. If filtering was previously disabled, it stays disabled and `is_valid()` returns `true` for all ERDs. If filtering was previously enabled, it stays enabled.
3. **`add_valid_erds()` requires an initialized set:** Without a prior `set_valid_erds()`, it is a no-op — custom ERDs never enable filtering on their own.
4. **No eviction:** Once an ERD is marked valid, it remains in the set for the lifetime of the registry. There is no removal or rotation.
5. **Out-of-bounds accessor returns zero:** `valid_erd(idx)` returns `0` when `idx` exceeds the current count.
6. **Sorted order:** the borrowed set is sorted ascending by `set_valid_erds()` (in the caller's storage), and `extra_erds_` is sorted by `add_valid_erds()`. Both runs stay sorted so `is_valid()` can binary search them.

---

## 8. Dependencies

| Dependency | Role |
|------------|------|
| `tiny_erd.h` | Defines `tiny_erd_t` type used for all ERD values. |

---

## 9. Known Limitations

1. **Fixed capacity:** the merged set is capped at 645 entries. If the appliance reports more than 645 valid ERDs, excess entries are silently dropped; the same cap also limits how many custom ERDs can be merged once the base set is near full.
2. **Two binary searches for `is_valid()`:** the borrowed set and the additions are searched separately, so the borrowed run cannot be searched as one merged array. O(log n) each, bounded by the caps above.
3. **No removal:** ERDs cannot be removed from the valid set. If the appliance's supported ERD set changes at runtime (e.g., firmware update), the registry must be re-initialized.
