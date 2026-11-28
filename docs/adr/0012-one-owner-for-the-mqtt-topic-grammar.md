# ADR-0012: One module owns the ERD MQTT topic grammar, and the primary board keeps the legacy no-prefix form

Status: accepted (2026-11-27)
Related: ADR-0011, `docs/reference/mqtt-topics.md`

## Context

Single-board appliances have always published
`geappliances/{device_id}/erd/0x{ERD}/value`. Adding secondary boards needed a
second form without breaking existing Home Assistant entities, and several call
sites (publisher, write bridge, MQTT adapter, discovery cleanup) were each
building or parsing topic strings by hand.

## Decision

`erd_topic.{h,cpp}` is the single definition of the grammar:

```
geappliances/{device_id}/erd/0x{ERD}/{op}                 primary board (legacy)
geappliances/{device_id}/erd/0x{ADDR}_0x{ERD}/{op}        secondary board
```

with `{ADDR}` exactly 2 hex digits, `{ERD}` exactly 4, and `{op}` one of `value`,
`write`, `write_result`. The `_` separator after the two address digits is what
distinguishes the secondary form from the legacy form.

- **The cache always stores physical board addresses; the publisher maps the
  primary host's detected address back to the legacy no-prefix form**, so topics
  stay backward compatible while diagnostics show the real address.
- **Parsing is strict.** The `geappliances/{device_id}/erd/` prefix is anchored,
  the hex fields must be exactly 2 and 4 digits, and the operation must be one
  of the three; `erd_topic_parse()` returns a bool. Topics that do not match are
  dropped — and logged, because silently dropping them made a write look like it
  was ignored. A no-prefix topic parses to the `ERD_TOPIC_PRIMARY` (`0xFF`)
  sentinel (ADR-0011); a secondary board at `0x00` parses as `0x00` and stays
  distinct.
- **`0xFF` is rejected as a formatter argument** rather than silently aliased
  onto the primary form.
- **Topic buffers are `ERD_TOPIC_BUF_SIZE` (128 bytes)**, carried over from the
  buffers this grammar was migrated from: enough for the longest `device_id` in
  use with margin. Discovery-config topics are sized separately at 256 bytes
  (`homeassistant/{domain}/{device_id}/{entity_id}/config`).
- **`erd_topic_format()` returns the character count excluding the NUL, or -1**
  if the arguments are invalid or the result would not fit; on -1 the caller must
  not publish. `erd_topic_format_write_filter()` builds the
  `geappliances/{device_id}/erd/+/write` subscribe filter with the same return
  semantics.
- **The op enum is iterated as a contiguous range**, so a non-contiguous enum
  would silently break op matching.
- **Availability reuses ESPHome's own birth/LWT topic** (default
  `<topic_prefix>/status`) so Home Assistant marks appliance entities
  unavailable when the bridge drops offline. An empty topic (birth/will
  disabled) leaves availability off.

## Consequences

- Topic shape changes happen in one file, and every consumer — publisher, write
  bridge, adapter, cleanup — gets the same grammar.
- The legacy form is frozen for single-board appliances: no new consumer may
  require the address prefix.
- Strict parsing plus logging turns a malformed topic into a visible bug instead
  of a silently missing entity or write.
- Any buffer-size change must be made against the longest real `device_id`, not
  guessed.

## Code anchors

- `erd_topic.h` / `erd_topic.cpp` — `erd_topic_format()`,
  `erd_topic_format_write_filter()`, `erd_topic_parse()`, `ERD_TOPIC_PRIMARY`,
  `ERD_TOPIC_BUF_SIZE`.
- `esphome_mqtt_client_adapter.cpp` — the write-request parse path.
- `erd_cache_mqtt_publisher.cpp` — detected-address to legacy-form mapping.
- `ha_discovery_cleanup.cpp` — the 256-byte config-topic bound.
- `test/tests/erd_topic_test.cpp`.
