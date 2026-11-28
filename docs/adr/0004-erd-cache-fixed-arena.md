# ADR-0004: The ERD cache is a fixed-size arena with an immutable entry size

Status: accepted (2026-11-27)
Related: ADR-0005, ADR-0007

## Context

The cache holds every ERD value the bridge has seen, keyed by
`(erd, board_address)`, and the MQTT publisher walks it continuously — roughly
1 500 allocations a second if each value owned its own buffer. On an ESP32 with
~52 KB of discovery buffers coming and going, per-entry allocation is what
fragments the heap. Entries also change value constantly, so a per-read change
test would put a `memcmp` of every entry on the read path.

## Decision

`erd_cache_t` is a fixed 300-slot array backed by a 4 096-byte bump arena.

- **No allocation or deallocation after registration.** An update is an in-place
  `memcpy` into the entry's arena slice. Entries are never removed; `erd_cache_clear()`
  invalidates them but keeps the slots and the arena.
- **`data_size` is invariant after registration.** If the appliance reports a
  different size for an ERD, the appliance was lost or reprogrammed and the cache
  is cleared, not resized. Change detection therefore needs only `memcmp`, and it
  uses `existing->data_size` — never the incoming size — so a caller that forgets
  the size check cannot read past the old buffer.
- **The size ceiling is 248 bytes** (`ERD_CACHE_MAX_DATA_SIZE`): the GEA3 maximum
  payload of 255 on-wire bytes minus 7 bytes of overhead. Growing it requires
  confirming the appliance protocol allows a larger payload.
- **Change detection happens at insert/update time**, not at read time. The
  dirty flag is set only on a real data change (new entries always set it), and
  `dirty_count` tracks the false→true transition only, so re-marking an entry
  that is already dirty cannot double-count the backlog.
- **One mutex guards `entries[]` + `arena[]` + `arena_offset`, and it is never
  held across the MQTT publish.** The publisher reads through
  `erd_cache_snapshot_next_updated()`, which takes the flag and copies the arena
  slice out in a single lock hold. Raw accessors (`erd_cache_get_data`, the
  lock-free `get_next_updated`) are documented as requiring the caller to hold
  the lock or to be single-threaded.
- **`erd_cache_clear()` keeps the lock, the throttle rate, and the statistics
  counters.** It is safe while the publisher runs because the publisher marks
  entries by pointer only while they stay valid, and it re-checks validity after
  a snapshot — a cleared entry must not come back dirty, or `dirty_count` skews.
- **`erd_cache_reset()` reinitializes counters single-threaded, outside any
  lock**: init runs it before the mutex exists, destroy after the mutex is
  deleted. Entries are zeroed field-by-field rather than only `memset`, because
  `memset`-ed `bool` fields trip UBSan.
- **`publish_cooldown` is a per-entry rate limit** (0–255 s, 0 = disabled)
  decremented once per second from the main loop, reloaded on a *successful*
  publish. A failed publish calls `erd_cache_mark_unpublished()`, which re-arms
  `update_required` so the value is retried instead of lost.

## Consequences

- Cache memory is bounded and predictable: 300 entries, 4 096 arena bytes, no
  allocator traffic in the steady state.
- Lookup is an O(n) linear scan; there is no index. That is accepted while n is
  a few hundred.
- The publisher's backlog size is known cheaply (`dirty_count`), which is what
  lets it decide when pacing a drain is worth it (ADR-0005).
- Any code that reads arena data without the lock is a bug, not an optimization.
  The snapshot path is the only supported read for the publisher.

## Code anchors

- `erd_cache.h` / `erd_cache.cpp` — `erd_cache_t`, `ERD_CACHE_MAX_DATA_SIZE`,
  `erd_cache_update`, `erd_cache_find`, `erd_cache_snapshot_next_updated`,
  `erd_cache_mark_published`, `erd_cache_mark_unpublished`, `erd_cache_clear`,
  `erd_cache_reset`, `dirty_count`.
- `docs/spec/erd_cache.md` — the module contract.
- `test/tests/erd_cache_test.cpp` — size-invariance, dirty-count, and
  clear-while-publishing tests.
