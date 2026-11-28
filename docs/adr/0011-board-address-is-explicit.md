# ADR-0011: A board address is always explicit, and 0xFF is a sentinel that can never be one

Status: accepted (2026-11-27)
Related: ADR-0004, ADR-0012, `docs/reference/mqtt-topics.md`

## Context

A GE appliance can have several boards on the same UART: the primary host plus
secondary boards at addresses `0x00`–`0xFE`. The same ERD ID can exist on more
than one board, so anything keyed on ERD ID alone mixes their data. GEA3 uses
`0xFF` as the broadcast address (`tiny_gea_broadcast_address`), which means the
protocol already reserves the one value that could serve as an "unspecified
board" marker.

## Decision

Board identity is carried explicitly through every layer, and `0xFF` is reused
as the "primary board" sentinel because it can never be a physical address.

- **Addresses are 1 byte (`uint8_t`) and ERDs are 2 bytes (`uint16_t`)
  everywhere.** Physical board addresses are `0x00`–`0xFE`; `0xFF` is the GEA
  broadcast address and can never name a physical board.
- **The cache keys on `(erd, board_address)` and stores only physical
  addresses.** The primary host stores its *detected* address, not `0xFF`.
  `erd_topic.cpp` rejects `0xFF` rather than silently aliasing it onto the
  primary-board form.
- **The write path uses `0xFF` as the primary-board sentinel.** Primary-board
  write topics (`board_address == 0xFF`) route to the detected host address;
  secondary-board topics route to the parsed address. The write-result topic
  mirrors the request's `board_address` so the result comes back on the topic
  the write was issued on. Keeping `0xFF` unambiguous is what makes a secondary
  board at address `0x00` work.
- **The polling list uses a different sentinel for a different purpose.**
  `probe_entry_t.board_address == PROBE_ENTRY_DEFAULT_ADDRESS` (`0xFF`) means
  "use the primary host address", and the list deduplicates on
  `(erd, address)` pairs. That is a poll-list concept, not a cache key; a cache
  lookup must always include the physical address.
- **Completion callbacks carry the source board address.**
  `erd_bridge_poll` never infers it from the ERD list: an ERD ID present on two
  boards would resolve to the first match and mix data. The address reported by
  the client is preserved into the cache entry.
- **Subscription mode only ever sees the primary host.** A matching ERD ID
  observed there does not cover the same ERD on another board, so every
  explicit non-primary address in the discovery configuration is polled
  regardless of subscription state.
- **Addresses survive mode fallback and appliance loss.** When subscription mode
  falls back to polling, the address is retained with the ERD — a bare ERD ID
  would route every fallback read to the primary board. On a transient read
  failure the poll bridge stores the pre-known appliance address so
  `signal_appliance_lost` can re-probe at the correct address.
- **Autodiscovery may be pinned to one address.** When configured, probes target
  that address instead of broadcasting `0xFF`; the GEA3→GEA2 fallback still
  applies.

## Consequences

- A `0x00` secondary board is unambiguous in every code path, because `0xFF`
  never appears as a physical address in the cache or the topic grammar.
- Any new cache lookup, MQTT topic, or write route must carry a board address;
  an ERD-only key is a bug by construction.
- The two `0xFF` meanings (write-path primary sentinel, poll-list "use host
  address") are documented at their definitions; do not unify them, they are
  resolved in different layers.
- Autodiscovery must complete before the write bridge is initialized, so the
  bridge knows the real host address from the broadcast `FF 0x0008` response.

## Code anchors

- `tiny_gea_constants.h` — `tiny_gea_broadcast_address`.
- `erd_cache.h` — the `(erd, board_address)` key and the detected-address field.
- `erd_topic.h` / `erd_topic.cpp` — the topic grammar and the `0xFF` rejection.
- `erd_write_bridge.h` / `.cpp` — `mqtt_client_on_write_request_args_t`,
  write-result routing.
- `erd_bridge_poll.cpp` / `.h` — completion address handling, address retention
  on appliance loss.
- `erd_poll_list_builder.h` — `PROBE_ENTRY_DEFAULT_ADDRESS`.
- `autodiscovery_manager.h` — the pinned-address probe option.
- `geappliances_bridge_bridge_init.cpp` — init ordering after autodiscovery.
