# ErdRegistry

## Purpose

Be the single authoritative source for which ERDs are valid. Filters against the valid-ERD set — borrowed from FeatureBitManager rather than copied, extended with the custom ERDs it stores itself — and exposes the filter used by the subscription bridge to drop publications for ERDs the appliance does not support.

## Public API

### Setup Methods (called during bridge initialization)

| Method | Description |
|--------|-------------|
| `set_valid_erds(erds, count)` | Adopt the valid-ERD array from FeatureBitManager and enable valid-ERD filtering: the array is sorted in place and borrowed, never copied. An empty set is ignored so filtering stays disabled. |
| `add_valid_erds(erds, count)` | Append additional ERDs to the valid set (e.g., custom ERDs), in the registry's own storage. Deduplicates against the borrowed set and earlier additions, then sorts. No-op if `set_valid_erds()` has not initialized the set. |

### Runtime Methods (called by the subscription bridge during operation)

| Method | Description |
|--------|-------------|
| `has_valid_erds_filter()` | Returns `true` if valid-ERD filtering is active. When `false`, all ERDs pass the filter. |
| `is_valid(erd)` | Returns `true` if the ERD passes the valid-ERD filter (or no filter active). |

### Read-Only Accessors
| Method | Description |
|--------|-------------|
| `valid_erd_count()` | Returns the number of valid ERDs (filter). |
| `valid_erd(idx)` | Returns the valid ERD at the given index (0-based) of the merged sorted order of both sets. Diagnostics only. |

## Data Structures

```cpp
class ErdRegistry {
  const tiny_erd_t* base_erds_{nullptr};            // borrowed from FeatureBitManager
  uint16_t base_count_{0};
  tiny_erd_t extra_erds_[ERD_REGISTRY_MAX_EXTRA];   // 128 entries: custom ERDs
  uint16_t extra_count_{0};
  bool valid_erds_ready_{false};
};
```

`ERD_REGISTRY_MAX_VALID` (645) bounds the borrowed set, `ERD_REGISTRY_MAX_EXTRA` (128) the registry's own additions. Note: `POLLING_LIST_MAX_SIZE` is 649 (defined in `erd_lists.h`); it does not match either constant. The polling bridge uses `POLLING_LIST_MAX_SIZE` for its polling list and ERD set capacity.

## Usage Flow

1. **Startup**: `FeatureBitManager` reads and parses appliance feature bit ERDs, producing a list of valid ERDs.
2. **Bridge init**: `GeappliancesBridge` calls `erd_registry.set_valid_erds()` with the feature bit manager's array (sorted in place, borrowed) to populate the valid-ERD filter, then `add_valid_erds()` to merge custom ERDs so they always pass the filter.
3. **Runtime**: `erd_bridge_subscribe` consults `is_valid()` before caching a subscription publication. When `appliance_api_parsing` is disabled, no filter is active and all ERDs pass.

## Dependencies

- `tiny_erd.h` — `tiny_erd_t` type definition

## Key Design Decisions

- **Borrow instead of copy**: the 645-entry feature-bit set lives once, in `FeatureBitManager`; the registry keeps a pointer plus a 128-entry array for custom ERDs (architecture review rec 9 removed the duplicate copy). The cost is two binary searches in `is_valid()` and an `O(n)` merged walk in `valid_erd()`.
- **Empty set disables filtering**: `set_valid_erds()` with an empty set is ignored — filtering stays disabled. This allows the bridge to operate without feature bit data (e.g., if feature bit reading fails or times out).
- **Sorted runs with binary search**: `is_valid()` runs `std::binary_search` over the borrowed set and then over the additions. `set_valid_erds()` sorts the borrowed array; `add_valid_erds()` sorts its own.
- **No eviction**: The set never evicts entries. The fixed capacity bounds memory usage.

## Testing

Exercised directly by `test/tests/erd_registry_test.cpp` and indirectly through the subscription-bridge tests that verify invalid ERDs are dropped before reaching the cache.
