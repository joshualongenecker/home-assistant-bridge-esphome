# ADR-0020: Generated files are generated, and the sync gate regenerates in place

Status: accepted (2026-11-27)
Related: ADR-0001, ADR-0008, ADR-0014, ADR-0019

## Context

The appliance ERD tables and the Home Assistant discovery payloads come from two generators
reading `lib/public-appliance-api-documentation`. The same knowledge reaches firmware twice —
C arrays and compressed JSONL — so a hand-edit is overwritten at the next regeneration while an
un-run generator change leaves firmware behind the definitions. The generators read the
submodule's **working tree**, not the recorded gitlink, and a custom discovery header is
compiled straight in, so replacing it has to be noticed by the device.

## Decision

Generated output is never hand-edited: fix the source, regenerate, commit source and output
together.

- Generator to output map (`scripts/check_generated_sync.sh`): `generate_erd_lists.py` →
  `erd_lists.{h,cpp}`, `appliance_api_feature_lists.{h,cpp}`, `appliance_type_map.h`;
  `ha_discovery/run_pipeline.py` → `ha_discovery_data.{h,cpp}`, `ha_discovery/*.jsonl`,
  `scripts/ha_discovery/appliance_api_erd_definitions_processed.json`.
- The gate is `make check-generated` → `scripts/check_generated_sync.sh`, run in CI
  (`.github/workflows/test.yml`). It is **not read-only**: it runs both generators into the
  working tree, then fails on any `git status --porcelain` entry (modified, deleted, or newly
  created) in the generated set, leaving those files dirty; restore them or commit them.
- Submodule drift is the gate's known false positive: it checks only that the submodule's JSON
  files exist, not that the checkout matches the recorded gitlink, so a working tree at another
  commit reports drift that is not in the branch. Fix the checkout first with
  `git submodule update --checkout` for that path, never over uncommitted submodule work.
- One source of truth for the appliance type → category mapping:
  `scripts/appliance_type_categories.py`, imported by both generators;
  `generate_ha_discovery.py` fails the pipeline when a token is not a `ha_constants.CATEGORIES`
  key rather than emitting a firmware table that never matches.
- Device-class validation is per-domain and silent: `_is_valid_device_class()` accepts any class
  for a domain with no entry in `ha_constants.VALID_DEVICE_CLASSES`, and a class invalid for its
  domain is dropped from the emitted config with no warning — only the listed domains are
  checked, so an unlisted domain never fails the pipeline over a bad class.
- Polling-list cap: the generator computes `POLLING_LIST_MAX_SIZE` (659 in `erd_lists.h`) as
  common + energy + the 11 feature ERDs (`0x0092-0x0097`, `0x0109-0x010D`) + the worst case
  across the eight appliance-specific categories, because that is the largest list
  `build_erd_poll_list()` can assemble. It is the single size source for `ERD_SET_CAPACITY`,
  `ERD_POLL_LIST_MAX_SIZE`, `HA_DISCOVERY_MAX_ERDS`, and the bridge's poll arrays. Size it from
  one category, or hand-tune it down, and `append_standard_erds()`/`append_custom_erds()`
  truncate the list with a warning while `erd_set_add()` fails silently at capacity.
- Registry caps: `ERD_REGISTRY_MAX_VALID` (645) is hand-written in `erd_registry.h` and
  `static_assert`-pinned to `FEATURE_BIT_MAX_ERDS` (645); `set_valid_erds()` truncates the base
  set to it and `add_valid_erds()` drops anything that would push the merged set past it. The
  registry's own side array `ERD_REGISTRY_MAX_EXTRA` (128) is `static_assert`-pinned to
  `CUSTOM_ERDS_MAX` (128), because a smaller side array drops custom ERDs past the cap without
  a word; over 128 custom ERDs is instead rejected at config validation. The poll-list cap
  stays larger than the valid-ERD cap on purpose — one sizes the worst-case category union, the
  other the feature-bit array.
- Change detection: `compress_ha_discovery.py` takes an FNV-1a 32-bit hash of every category's
  compressed bytes, concatenated in `ha_constants.CATEGORIES` order, and emits it as
  `HA_DISCOVERY_DATA_HASH`, so any definition, override, or category-order change moves it.
  `ha_discovery_manager_data_hash()` mixes that with the custom profile's
  `CUSTOM_HA_DISCOVERY_DATA_HASH` and the availability strings; `DiscoveryLifecycleManager`
  compares it against the persisted record (ADR-0014) and a mismatch cleans the old topics and
  republishes. A custom header without that token gets `zlib.crc32` of its contents at codegen,
  so replacing a pre-hash header still triggers the cleanup. Codegen-set custom pointers
  survive `ha_discovery_manager_init()`'s `memset`.

## Consequences

- Hand-editing a generated file is a change CI reverses; the fix is the submodule, the
  overrides in `scripts/ha_discovery/pipeline/post_process.py`, or the generator.
- `make check-generated` mutates the tree, so it is not a read-only "am I clean" probe, and a
  drift report is only trustworthy once the submodule is confirmed at the recorded gitlink.
- Generated files are exempt from the comment ratchet (ADR-0001). Preprocessor lines inside the
  generator's f-strings (the `#ifndef ERD_LISTS_H` guard, the `POLLING_LIST_MAX_SIZE` define)
  are output text, not comments: deleting them breaks the generated header.
- Changing the discovery payloads without bumping their hash leaves a device holding topics the
  firmware no longer publishes; the hash is what turns a firmware update into a republish.

## Code anchors

- `scripts/generate_erd_lists.py` — `polling_list_max_size`, the `erd_lists.h` template.
- `scripts/ha_discovery/run_pipeline.py` and
  `scripts/ha_discovery/generators/compress_ha_discovery.py` — `fnv1a_32`,
  `HA_DISCOVERY_DATA_HASH`.
- `scripts/ha_discovery/generators/generate_ha_discovery.py` and
  `scripts/appliance_type_categories.py` — the shared `APPLIANCE_TYPE_CATEGORY` import and the
  unknown-category failure.
- `scripts/check_generated_sync.sh` — `GENERATED`, the submodule presence check, the
  `git status --porcelain` diff; `Makefile` `check-generated`; `.github/workflows/test.yml`.
- `components/geappliances_bridge/erd_lists.h` — `POLLING_LIST_MAX_SIZE`; `erd_bridge_common.h`
  — `ERD_SET_CAPACITY`; `erd_poll_list_builder.{h,cpp}` — `ERD_POLL_LIST_MAX_SIZE` and the
  truncating `append_*`; `ha_discovery_manager.h` — `HA_DISCOVERY_MAX_ERDS`.
- `components/geappliances_bridge/erd_registry.h` — `ERD_REGISTRY_MAX_VALID`,
  `ERD_REGISTRY_MAX_EXTRA`; `feature_bit_manager.h` — `FEATURE_BIT_MAX_ERDS`;
  `geappliances_bridge.h` — `CUSTOM_ERDS_MAX` and its `static_assert`; `__init__.py` — the
  custom-ERD count limit and the `zlib.crc32` legacy-hash fallback.
- `components/geappliances_bridge/ha_discovery_manager.cpp` — `ha_discovery_manager_data_hash()`
  and `ha_discovery_manager_init()` pointer preservation; `discovery_lifecycle_manager.cpp` —
  the stored-hash change check; `appliance_type_category.h`.
