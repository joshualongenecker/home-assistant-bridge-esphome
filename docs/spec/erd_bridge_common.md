# ERD Bridge Common — Specification

## 1. Overview

### 1.1 Purpose

Provides shared timing constants, HSM signal identifiers, state enums, and utility templates used by both the subscription bridge (`erd_bridge_subscribe.cpp`) and the polling bridge (`erd_bridge_poll.cpp`).

### 1.2 Responsibilities

- Declare shared timing constants (retry delay, resubscribe delay, quiet period, retention period, appliance lost timeout).
- Define HSM signal identifiers shared by both bridge state machines.
- Provide `subscription_state_t` and `polling_state_t` enums with name helpers.
- Implement `erd_set_t`, a fixed-capacity sorted ERD set that replaces `std::set<tiny_erd_t>` to eliminate heap allocations.
- Provide `arm_timer` / `disarm_timer` utility templates.

### 1.3 Not Responsible For

- Any bridge state or lifecycle logic.
- Anything not shared between both bridge implementations.
- Any MQTT interaction (bridges write to `erd_cache` only).

---

## 2. Interface

### 2.1 Timing Constants

```c
enum {
  resubscribe_delay             = 1000,
  subscription_retention_period = 30 * 1000,
  subscription_quiet_period     = 2 * 1000,
  appliance_lost_timeout        = 60000
};
```

| Constant | Value (ms) | Description |
|----------|-----------|-------------|
| `resubscribe_delay` | 1,000 | Wait before retrying a subscription request after failure. |
| `subscription_retention_period` | 30,000 | Maximum time a subscription is retained without activity before being considered stale. |
| `subscription_quiet_period` | 2,000 | Quiet period after subscribing before considering the subscription steady. |
| `appliance_lost_timeout` | 60,000 | Time without appliance communication before marking the appliance as lost. |

### 2.2 Subscription State Machine

```c
typedef enum {
  subscription_state_none,
  subscription_state_subscribing,
  subscription_state_subscribed,
  subscription_state_steady,
  subscription_state_failed
} subscription_state_t;
```

| State | Description |
|-------|-------------|
| `subscription_state_none` | Initial / uninitialized state. |
| `subscription_state_subscribing` | Subscription request has been sent; awaiting response. |
| `subscription_state_subscribed` | Subscription acknowledged by the appliance. |
| `subscription_state_steady` | Subscription has been active past the quiet period; considered stable. |
| `subscription_state_failed` | Subscription failed and will not be retried. |

**Helpers:**

| Function | Return | Description |
|----------|--------|-------------|
| `subscription_state_name(state)` | `const char*` | Returns a human-readable name for the state (or `nullptr` for `none`). |
| `subscription_is_active(state)` | `bool` | Returns `true` for `subscribing`, `subscribed`, and `steady`; `false` for `none` and `failed`. |

### 2.3 Polling State Machine

```c
typedef enum {
  polling_state_none,
  polling_state_probing,
  polling_state_polling,
  polling_state_failed
} polling_state_t;
```

| State | Description |
|-------|-------------|
| `polling_state_none` | Initial / uninitialized state. |
| `polling_state_probing` | Bridge is probing the appliance to determine available ERDs. |
| `polling_state_polling` | Active polling cycle; ERDs are being read on schedule. |
| `polling_state_failed` | Polling failed and will not be retried. |

**Helper:**

| Function | Return | Description |
|----------|--------|-------------|
| `polling_state_name(state)` | `const char*` | Returns a human-readable name for the state (or `nullptr` for `none`). |

### 2.4 Shared HSM Signals

```c
enum {
  signal_timer_expired = tiny_hsm_signal_user_start,
  signal_polling_timer_expired,
  signal_subscription_failed,
  signal_subscription_added_or_retained,
  signal_subscription_host_came_online,
  signal_subscription_publication_received,
  signal_quiet_period_expired,
  signal_read_failed,
  signal_read_completed,
  signal_appliance_lost
};
```

| Signal | Description |
|--------|-------------|
| `signal_timer_expired` | Generic timer callback; fires when `arm_timer`'s duration elapses. |
| `signal_polling_timer_expired` | Polling cycle timer expired; triggers the next read batch. |
| `signal_subscription_failed` | Subscription request was rejected or timed out. |
| `signal_subscription_added_or_retained` | A new subscription was added or an existing one was retained. |
| `signal_subscription_host_came_online` | The MQTT host (appliance) became available after being offline. |
| `signal_subscription_publication_received` | A published value arrived for a subscribed ERD. |
| `signal_quiet_period_expired` | The quiet period after subscribing elapsed; transition to `steady`. |
| `signal_read_failed` | An ERD read operation failed. |
| `signal_read_completed` | An ERD read operation completed successfully. |
| `signal_appliance_lost` | The appliance has been unresponsive for `appliance_lost_timeout`. |

### 2.5 ERD Set (`erd_set_t`)

```c
#define ERD_SET_CAPACITY POLLING_LIST_MAX_SIZE

typedef struct {
  tiny_erd_t data[ERD_SET_CAPACITY];
  uint16_t count;
} erd_set_t;
```

A fixed-capacity sorted array that replaces `std::set<tiny_erd_t>`. Capacity is bounded by `POLLING_LIST_MAX_SIZE`. Uses binary search for lookups and inserts, maintaining sorted order.

| Function | Return | Description |
|----------|--------|-------------|
| `erd_set_init(self)` | `void` | Resets `count` to zero. |
| `erd_set_contains(self, erd)` | `bool` | Binary search; returns `true` if `erd` is present. |
| `erd_set_insert(self, erd)` | `bool` | Inserts `erd` in sorted order. Returns `false` if already present or capacity is full. |
| `erd_set_clear(self)` | `void` | Resets `count` to zero without clearing the underlying array. |


### 2.6 Board Tally (`board_tally_t`)

```c
#define BOARD_TALLY_MAX 4
#define BOARD_TALLY_BUF_SIZE 64

typedef struct {
  uint8_t address[BOARD_TALLY_MAX];
  uint16_t count[BOARD_TALLY_MAX];
  uint16_t untracked;
} board_tally_t;
```

A fixed-capacity "how many per board" counter used by the boot summary lines, so a multi-board appliance cannot emit one log line per board. Boards are kept in first-seen order; the fifth and later boards collapse into `untracked`.

| Function | Return | Description |
|----------|--------|-------------|
| `board_tally_init(self)` | `void` | Zeroes the tally. |
| `board_tally_add(self, address)` | `void` | Increments the slot for `address`, taking a free slot on first sight, otherwise counting into `untracked`. |
| `board_tally_format(self, out, out_len)` | `int` | Renders `primary 41, 0xC0 24`; an empty tally renders `none`; extra boards render `+N more`. Returns characters stored, excluding the terminator. |

Contract:

- `PROBE_ENTRY_DEFAULT_ADDRESS` (`0xFF`, the "primary board" sentinel) renders as `primary`, never as `0xFF`. A physical board address is always `0x00`–`0xFE`, so `0x00` stays a distinct board and is never conflated with the sentinel.
- Never writes past `out_len`, and always NUL-terminates: `out[0] = '\0'` is armed before any write, so a one-byte buffer with a non-empty tally yields the empty string instead of an unterminated buffer. A non-empty tally never renders as `none` — that would read as "no boards" on a multi-board appliance.
- The widest line the type can render is 62 characters (`primary 65535, 0xC0 65535, 0xC1 65535, 0xC2 65535, +65535 more`); `BOARD_TALLY_BUF_SIZE` is 64 for exactly that reason. A shorter buffer truncates wherever `snprintf` stopped and returns the stored length, not the would-be length.

### 2.7 Polled-ERDs Summary Line (`board_polled_summary_format`)

```c
typedef struct {
  bool polling_bridge_initialized;
  uint16_t custom_erds_count;
  uint16_t unseen_custom_count;
} polled_erds_state_t;

#define BOARD_POLLED_SUMMARY_BUF_SIZE (BOARD_TALLY_BUF_SIZE + 32)

static inline int board_polled_summary_format(const polled_erds_state_t* st,
                                              const uint8_t* polled_addresses,
                                              uint16_t polled_count,
                                              char* out, size_t out_len);
```

Renders the `Polled ERDs:` line into `out`; returns characters stored, or 0 with `out[0] == '\0'` when the line would add nothing. It is a pure function rather than inline log calls because the line exists only inside an `ESP_LOGD`, and `ESP_LOGx` expands to nothing in the unit build — every branch choice would otherwise be invisible to the suite.

| Condition | Output |
|-----------|--------|
| Polling bridge running, polled set spans two or more boards | `Polled ERDs: 53 (primary 43, 0xC0 10)` |
| Polling bridge running, single board, or list not filled yet | empty — the bridge logs `Entered steady-state polling: N ERDs` at INFO on the same transition, from the same count |
| No polling bridge, no custom ERDs configured | `Polled ERDs: none - no custom ERDs configured` |
| No polling bridge, every custom ERD covered by the subscription | `Polled ERDs: none - custom ERDs all arrive by subscription` |
| No polling bridge, custom ERDs left uncovered | `Polled ERDs: none - N custom ERDs uncovered, no polling bridge` |

`polled_addresses` / `polled_count` MUST come from the running polling bridge — `erd_bridge_poll_t.erd_polling_addresses` and `.polling_list_count`, written as a pair by `add_erd_to_polling_list()` — and not from the list the bridge was built from. The probe phase excludes every ERD whose read failed, so the built list is strictly larger than what is polled whenever an ERD is unresponsive (observed on hardware: 60 built, 53 polled), and a split taken from it would not sum to the count printed beside it.

---

## 3. Behavior

### 3.1 ERD Set Operations

#### Requirement 3.1.1: Sorted Invariant

`erd_set_insert` MUST maintain the sorted order of `data[]`. Insertion uses binary search to find the correct position, then shifts subsequent elements to make room.

**Rationale:** Sorted order enables binary search in `erd_set_contains`, giving O(log n) lookups instead of O(n). Given the small capacity (bounded by `POLLING_LIST_MAX_SIZE`), the shift cost of insertion is acceptable.

**Implementation:** `components/geappliances_bridge/erd_bridge_common.h` lines 138–161.

**Verification:** After inserting a sequence of ERD IDs, confirm `data[0] < data[1] < ... < data[count-1]`.

#### Requirement 3.1.2: No-Op on Duplicate

`erd_set_insert` MUST return `false` when the ERD is already present, without modifying the set.

**Rationale:** Prevents duplicate entries and preserves the sorted invariant.

**Implementation:** `components/geappliances_bridge/erd_bridge_common.h` lines 149–150.

#### Requirement 3.1.3: Capacity Guard

`erd_set_insert` MUST return `false` when `count >= ERD_SET_CAPACITY`, without writing past the array bounds.

**Rationale:** Prevents buffer overflow. The capacity is fixed at compile time; exceeding it is a configuration error that should be detected.

**Implementation:** `components/geappliances_bridge/erd_bridge_common.h` line 153.

### 3.2 Timer Templates

#### Requirement 3.2.1: Signal Delivery on Expiry

`arm_timer` MUST deliver `signal_timer_expired` to the HSM when the timer expires.

**Rationale:** Both bridge implementations use the same timer signal to drive state transitions (retry, quiet period expiry, polling cycle). A single template eliminates code duplication.

**Implementation:** `components/geappliances_bridge/erd_bridge_common.h` lines 172–179.

---

## 4. Notes

1. **No heap allocations.** The `erd_set_t` structure replaces `std::set<tiny_erd_t>` to eliminate heap node allocations. This is consistent with the project's design principle of avoiding dynamic collections on the ESP32.

2. **Binary search over linear.** Despite the comment in the header referencing "linear search," the actual implementation uses binary search for both `erd_set_contains` and `erd_set_insert`. This is the correct choice for maintaining sorted order efficiently.

3. **Implicit inline templates.** The `arm_timer` and `disarm_timer` templates are implicitly inline (defined in a header). Each translation unit gets its own copy, avoiding ODR violations.

4. **Signal enum starts at `tiny_hsm_signal_user_start`.** The first signal (`signal_timer_expired`) is anchored to `tiny_hsm_signal_user_start`, ensuring all bridge signals occupy the user-defined signal range of the HSM.
