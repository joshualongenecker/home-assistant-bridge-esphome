# ErdRegistry

## Purpose

Be the single authoritative source for which ERDs are valid. Owns the valid-ERD set (populated by FeatureBitManager at startup, optionally extended with custom ERDs) and exposes the filter used by the subscription bridge to drop publications for ERDs the appliance does not support.

## Public API

### Setup Methods (called during bridge initialization)

| Method | Description |
|--------|-------------|
| `set_valid_erds(erds, count)` | Copy the valid-ERD set from FeatureBitManager and enable valid-ERD filtering. An empty set is ignored so filtering stays disabled. |
| `add_valid_erds(erds, count)` | Append additional ERDs to the valid set (e.g., custom ERDs). Deduplicates against existing entries and re-sorts. No-op if `set_valid_erds()` has not initialized the set. |

### Runtime Methods (called by the subscription bridge during operation)

| Method | Description |
|--------|-------------|
| `has_valid_erds_filter()` | Returns `true` if valid-ERD filtering is active. When `false`, all ERDs pass the filter. |
| `is_valid(erd)` | Returns `true` if the ERD passes the valid-ERD filter (or no filter active). |

### Read-Only Accessors
| Method | Description |
|--------|-------------|
| `valid_erd_count()` | Returns the number of valid ERDs (filter). |
| `valid_erd(idx)` | Returns the valid ERD at the given index (0-based). |

## Data Structures

```cpp
class ErdRegistry {
  tiny_erd_t valid_erds_[ERD_REGISTRY_MAX_VALID];  // 645 entries
  uint16_t valid_erds_count_{0};
  bool valid_erds_ready_{false};
};
```

`ERD_REGISTRY_MAX_VALID` is 645. Note: `POLLING_LIST_MAX_SIZE` is 649 (defined in `erd_lists.h`); they do not match. The registry uses its own constant for the valid-ERD array, while the polling bridge uses `POLLING_LIST_MAX_SIZE` for its polling list and ERD set capacity.

## Usage Flow

1. **Startup**: `FeatureBitManager` reads and parses appliance feature bit ERDs, producing a list of valid ERDs.
2. **Bridge init**: `GeappliancesBridge` calls `erd_registry.set_valid_erds()` to populate the valid-ERD filter, then `add_valid_erds()` to merge custom ERDs so they always pass the filter.
3. **Runtime**: `erd_bridge_subscribe` consults `is_valid()` before caching a subscription publication. When `appliance_api_parsing` is disabled, no filter is active and all ERDs pass.

## Dependencies

- `tiny_erd.h` — `tiny_erd_t` type definition

## Key Design Decisions

- **Fixed-capacity array**: `valid_erds_` is bounded by `ERD_REGISTRY_MAX_VALID` (645) to avoid heap allocation. This matches the bounded nature of the ERD space.
- **Empty set disables filtering**: `set_valid_erds()` with an empty set is ignored — filtering stays disabled. This allows the bridge to operate without feature bit data (e.g., if feature bit reading fails or times out).
- **Sorted array with binary search**: `is_valid()` runs `std::binary_search` over the sorted array. `add_valid_erds()` re-sorts after appending.
- **No eviction**: The set never evicts entries. The fixed capacity bounds memory usage.

## Testing

Exercised directly by `test/tests/erd_registry_test.cpp` and indirectly through the subscription-bridge tests that verify invalid ERDs are dropped before reaching the cache.
