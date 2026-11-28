# ADR-0008: The valid-ERD filter borrows one sorted array; discovery re-publishes when the filter changes

Status: accepted (2026-11-27)
Related: ADR-0004, ADR-0009, architecture review rec 9

## Context

Home Assistant discovery must not publish entities for ERDs the appliance does
not actually report. The authoritative set comes from the feature-bit ERD list
plus the YAML `custom_ha_discovery_erd_addresses[]` additions. The first
implementation copied that set into the registry, which duplicated a few hundred
`uint16_t` per bridge and, worse, let the copy drift from the array the
feature-bit manager keeps updating. Architecture review rec 9 removed the copy.

## Decision

`ErdRegistry` filters by borrowing, never by owning the base set.

- `set_valid_erds()` **sorts the caller's array in place and borrows it**. The
  registry is the only consumer that depends on that order. The caller must keep
  the array alive while the filter is active; `FeatureBitManager`'s array is a
  fixed member array, so it outlives the registry.
- **An empty set is ignored**, leaving filtering disabled. An empty set would
  otherwise silently suppress every publish.
- **Two caps, both enforced.** The merged set never exceeds
  `ERD_REGISTRY_MAX_VALID` (sized to `FEATURE_BIT_MAX_ERDS`, because the
  feature-bit array is the only base set); the base set is truncated at that cap
  rather than letting entries past it be silently suppressed by `is_valid()`.
  The registry's own storage for additions is bounded by
  `ERD_REGISTRY_MAX_EXTRA`, pinned to `GeappliancesBridge::CUSTOM_ERDS_MAX` by a
  `static_assert` in `geappliances_bridge.h`.
- `add_valid_erds()` deduplicates against the borrowed set and against earlier
  additions. The dedup uses a linear scan because the additions are only sorted
  once the whole batch is in. If `set_valid_erds()` has not run, the call is a
  no-op and filtering stays disabled.
- `is_valid()` is **two binary searches** — one over the borrowed set, one over
  the registry's own additions — instead of the single search a merged copy would
  allow. `get_valid_erd(index)` walks the two sorted runs as if they were merged,
  so the index means what it meant when both sets lived in one array; it is O(n)
  and diagnostics-only.
- `clear_valid_erds()` forgets the set and disables filtering until
  `set_valid_erds()` runs again — the appliance was lost or reprogrammed and its
  ERD set may differ.
- **The subscribe bridge holds a borrowed `ErdRegistry *`** owned by
  `GeappliancesBridge`; `nullptr` disables filtering. Any code path that
  re-initializes the subscribe bridge must re-attach the registry, or the bridge
  publishes unfiltered ERDs.
- **ERD sets are fixed arrays, not `std::set`.** `erd_set` and
  `erd_polling_list` are fixed-capacity arrays embedded in their structs, so
  there is no heap cleanup and no allocation during polling. Inserts are O(n),
  accepted because n is bounded by the probe list or the subscription set.
- **The valid-ERD fingerprint is part of the discovery change check.** It is an
  FNV-1a hash over what discovery filters by: the host address, the secondary
  boards the configuration names (custom ERDs with an explicit board address),
  and the sorted valid-ERD set. It is 0 when there is no filter at all (feature
  bits unreadable, appliance API parsing off), and the change check ignores 0 so
  an unfiltered bridge does not thrash republishes.

## Consequences

- One array, one sort, no copy: the filter cannot drift from the feature-bit
  manager's live data.
- The lifetime contract is explicit and one-directional — the registry must never
  outlive the array it borrows. Nothing enforces it at compile time; the anchor
  is that only `FeatureBitManager` ever calls `set_valid_erds()`.
- Discovery re-publishes when the appliance's reported ERD set changes, so an
  appliance that starts or stops reporting a feature gets its entities added or
  removed without an operator action.
- Raising either cap requires updating the `static_assert` that ties
  `ERD_REGISTRY_MAX_EXTRA` to `CUSTOM_ERDS_MAX`.

## Code anchors

- `erd_registry.h` / `erd_registry.cpp` — `set_valid_erds()`, `add_valid_erds()`,
  `clear_valid_erds()`, `is_valid()`, `get_valid_erd()`,
  `ERD_REGISTRY_MAX_VALID`, `ERD_REGISTRY_MAX_EXTRA`.
- `feature_bit_manager.cpp` — the borrowed base array and its sort.
- `erd_bridge_subscribe.h` / `.cpp` — `erd_bridge_subscribe_set_erd_registry()`,
  the `nullptr`-disables-filtering contract.
- `geappliances_bridge.cpp` — `valid_erds_fingerprint_()`, the change check that
  consumes it; `CUSTOM_ERDS_MAX` and its `static_assert`.
- `erd_bridge_common.h` — the fixed-capacity ERD set.
