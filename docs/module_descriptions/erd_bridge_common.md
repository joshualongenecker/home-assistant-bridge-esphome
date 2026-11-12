# ERD Bridge Common

## Purpose

Header-only shared utilities used by both the subscription bridge (`erd_bridge_subscribe.cpp`) and the polling bridge (`erd_bridge_poll.cpp`). All functions are either template functions (implicitly inline) or declared `static inline` so that each translation unit gets its own copy without ODR violations.

## Shared Timing Constants

Defined as an anonymous `enum` (compile-time constants, no runtime storage):

| Constant | Value | Description |
|----------|-------|-------------|
| `resubscribe_delay` | 1000 ms | Wait before retrying `subscribe()` after failure (subscription bridge) |
| `subscription_retention_period` | 30000 ms (30 s) | Interval for retaining subscriptions (subscription bridge) |
| `subscription_quiet_period` | 2000 ms (2 s) | Quiet period after subscription before transitioning to steady state |
| `appliance_lost_timeout` | 60000 ms (60 s) | Time without a successful read before declaring appliance lost (polling bridge) |

## Shared Signal Identifiers

Signal IDs shared between both bridge state machines, defined as an anonymous `enum` starting from `tiny_hsm_signal_user_start`:

| Signal | Used By | Description |
|--------|---------|-------------|
| `signal_timer_expired` | Both | Generic timer fired (retry timer in subscription, retention timer in subscription) |
| `signal_polling_timer_expired` | Polling only | Polling cycle interval timer fired |
| `signal_subscription_failed` | Subscription only | `subscribe()` call failed |
| `signal_subscription_added_or_retained` | Subscription only | Subscription was added or retained successfully |
| `signal_subscription_host_came_online` | Subscription only | Appliance host restarted and came back online |
| `signal_subscription_publication_received` | Subscription only | ERD publication received via subscription |
| `signal_quiet_period_expired` | Subscription only | Quiet period timer fired; transitions to steady or failed state |
| `signal_read_failed` | Polling only | ERD read request failed |
| `signal_read_completed` | Polling only | ERD read request completed successfully |
| `signal_appliance_lost` | Polling only | 60 s appliance-lost timer fired |

## Fixed-Capacity ERD Set

The `erd_set_t` struct replaces the previous `std::set<tiny_erd_t>` to eliminate heap node allocations. It is a sorted array with binary search, providing O(log n) lookups and O(n) inserts (n is small: bounded by probe list or subscription ERDs).

| Field | Type | Description |
|-------|------|-------------|
| `data` | `tiny_erd_t[ERD_SET_CAPACITY]` | Sorted array of ERD identifiers |
| `count` | `uint16_t` | Number of entries in the set |

`ERD_SET_CAPACITY` is defined as `POLLING_LIST_MAX_SIZE` (649).

### API

| Function | Description |
|----------|-------------|
| `erd_set_init(self)` | Initialize an empty set (sets `count` to 0) |
| `erd_set_contains(self, erd)` | Binary search; returns `true` if the ERD is present |
| `erd_set_insert(self, erd)` | Inserts at the correct sorted position; returns `false` if already present or capacity exceeded |
| `erd_set_clear(self)` | Resets `count` to 0 (does not zero the data array) |

## Board Tally and Boot Summary Lines

`board_tally_t` counts "how many ERDs per board" in a fixed four-slot table so the boot summary stays one line on a multi-board appliance; the fifth and later boards collapse into `+N more`. `PROBE_ENTRY_DEFAULT_ADDRESS` (the `0xFF` primary-board sentinel) renders as `primary`, never as an address, and a physical `0x00` stays a distinct board. `board_tally_format` never writes past `out_len`, always NUL-terminates (the terminator is armed before any write), and returns the number of characters stored. `BOARD_TALLY_BUF_SIZE` (64) holds the widest line the type can render, 62 characters.

`board_polled_summary_format` renders the `Polled ERDs:` line from `polled_erds_state_t` plus the running polling bridge's own `erd_polling_addresses[]`/`polling_list_count`, and returns 0 (empty line) when it would add nothing — the bridge already logs `Entered steady-state polling: N ERDs` at INFO, so a single-board split is suppressed. The split must come from the polled list rather than the list the bridge was built from: the probe phase drops every ERD whose read failed, so the built list is larger than what is polled whenever an ERD is unresponsive.

These are pure functions in the header rather than inline log calls because the summary exists only inside an `ESP_LOGD`, and `ESP_LOGx` expands to nothing in the unit build — the branch choices would otherwise be untestable.

### API

| Function | Description |
|----------|-------------|
| `board_tally_init(self)` | Zeroes the tally |
| `board_tally_add(self, address)` | Counts one ERD for `address`, or into `untracked` past four boards |
| `board_tally_format(self, out, out_len)` | Renders `primary 41, 0xC0 24` (empty → `none`); returns characters stored |
| `board_polled_summary_format(st, polled_addresses, polled_count, out, out_len)` | Renders the `Polled ERDs:` line, or 0 with an empty buffer when it adds nothing |

## Utility Templates

### `arm_timer(T* self, tiny_timer_ticks_t ticks)`

Starts a one-shot timer that sends `signal_timer_expired` to the HSM when it fires. Template parameter `T` is the bridge type (`erd_bridge_subscribe_t` or `erd_bridge_poll_t`).

### `disarm_timer(T* self)`

Stops the timer. Safe to call even if the timer is not active.


## Subscription State Machine States

Defined as `subscription_state_t` enum:

| Value | Description |
|-------|-------------|
| `subscription_state_none` | Initial/uninitialized state |
| `subscription_state_subscribing` | Actively attempting to subscribe |
| `subscription_state_subscribed` | Subscription established, waiting for publications |
| `subscription_state_steady` | Subscription stable, no new ERDs for quiet period |
| `subscription_state_failed` | Subscription not supported by appliance |

### Helper Functions

| Function | Description |
|----------|-------------|
| `subscription_state_name(state)` | Returns a human-readable string for the state |
| `subscription_is_active(state)` | Returns `true` if state is neither `none` nor `failed` |

## Polling State Machine States

Defined as `polling_state_t` enum:

| Value | Description |
|-------|-------------|
| `polling_state_none` | Initial/uninitialized state |
| `polling_state_probing` | Discovery phase — probing ERDs |
| `polling_state_polling` | Steady-state polling |
| `polling_state_failed` | Polling failed (consecutive cycle failures) |

### Helper Functions

| Function | Description |
|----------|-------------|
| `polling_state_name(state)` | Returns a human-readable string for the state |

## Dependencies

- `tiny_hsm` — state machine framework (for `tiny_hsm_send_signal`)
- `tiny_timer` — timer framework (for `tiny_timer_start`, `tiny_timer_stop`)
- `tiny_erd` — ERD type definition
- `tiny_utils` — utility functions
- `tiny_gea_constants` — GEA protocol constants
- `erd_lists.h` — `POLLING_LIST_MAX_SIZE` (defines `ERD_SET_CAPACITY`)

## Key Design Decisions

- **Header-only with inline**: All functions are template or `static inline`, avoiding ODR violations when both `erd_bridge_subscribe.cpp` and `erd_bridge_poll.cpp` include this header.
- **Implicit inline templates.** The `arm_timer` and `disarm_timer` templates are implicitly inline (defined in a header). Each translation unit gets its own copy, avoiding ODR violations.
- **Fixed-capacity ERD set**: Replaced `std::set<tiny_erd_t>` with a sorted array (`erd_set_t`) to eliminate per-ERD heap allocations. This was a critical fix — the previous `std::set` caused heap fragmentation and memory pressure on the ESP32. Binary search keeps lookups fast (O(log n)) for the small set sizes involved.
- **No MQTT dependency**: This header does not reference `i_mqtt_client`, `i_tiny_gea3_erd_client`, or `<set>`. Write handling and MQTT disconnect handling have been extracted to `erd_write_bridge` and `erd_cache_mqtt_publisher` respectively.
- **No heap allocation**: The `erd_set_t` struct is embedded directly in each bridge struct — no dynamic allocation, no `void*` casting.

## Testing

Exercised indirectly through the unit tests for `erd_bridge_subscribe` and `erd_bridge_poll`. The shared utilities themselves are not tested in isolation.
