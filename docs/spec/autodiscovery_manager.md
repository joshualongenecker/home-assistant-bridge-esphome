# Autodiscovery Manager — Specification

## 1. Overview

### 1.1 Purpose

The Autodiscovery Manager locates the connected appliance on the GEA bus by broadcasting to address `0xFF` and recording the first responding device's address, protocol type, and active ERD client. It is fully self-driving: owns its own timers and event subscriptions. The bridge calls `start()` to begin discovery; the startup HSM polls `get_state()` for completion.

### 1.2 Responsibilities

- Broadcast to `GEA_BROADCAST_ADDRESS` (0xFF) reading `ERD_APPLIANCE_TYPE` to discover the appliance
- Manage the GEA3 → GEA2 fallback broadcast discovery sequence when both UARTs are configured
- Retry indefinitely until an appliance responds
- Own a timer-based state machine — no polling from the bridge
- Subscribe directly to ERD client activity events to detect broadcast responses
- Expose the discovered host address, active ERD client, and protocol type via getters

### 1.3 Not Responsible For

- The 5-second startup delay before `start()` is called (handled by the startup HSM)
- Reading any ERDs beyond `ERD_APPLIANCE_TYPE` for the broadcast response
- Managing MQTT or bridge connections
- Any post-discovery work (device ID generation, feature bit parsing, polling setup)

---

## 2. Initialization

### 2.1 Primary Init

```cpp
void init(tiny_timer_group_t* timer_group,
          i_tiny_gea3_erd_client_t* gea3_erd_client,
          i_tiny_gea2_erd_client_t* gea2_erd_client,
          i_tiny_gea3_erd_client_t* gea2_adapter_client,
          esphome_uart_adapter_t* gea3_uart_adapter,
          esphome_uart_adapter_t* gea2_uart_adapter,
          bool has_gea3_uart,
          bool has_gea2_uart,
          uint8_t client_address);
```

| Parameter | Description |
|-----------|-------------|
| `timer_group` | Shared timer group for the broadcast window timer. |
| `gea3_erd_client` | GEA3 ERD client interface for sending GEA3 broadcasts. |
| `gea2_erd_client` | GEA2 ERD client interface (raw GEA2 protocol). |
| `gea2_adapter_client` | GEA2 adapter — a GEA3-compatible client wrapping the GEA2 ERD client. Used as the `active_erd_client_` return value when a GEA2 board is discovered. |
| `has_gea3_uart` | Whether a GEA3 UART is configured. Determines if GEA3 broadcast is attempted. |
| `has_gea2_uart` | Whether a GEA2 UART is configured. Determines if GEA2 broadcast is attempted. |

**Init behavior:**
- Subscribes to `tiny_gea3_erd_client_on_activity` on the GEA3 client (if `has_gea3_uart` and client non-null)
- Subscribes to `tiny_gea3_erd_client_on_activity` on the GEA2 adapter client (if `has_gea2_uart` and adapter non-null)
- Resets all state to initial values: `state_ = AUTODISCOVERY_IDLE`, `host_address_ = 0`, `active_erd_client_ = nullptr`, `gea2_protocol_active_ = false`

---

## 3. Public API

| Method | Description |
|--------|-------------|
| `start()` | Begin autodiscovery. Idempotent if already past `AUTODISCOVERY_IDLE`. |
| `get_host_address()` | Returns the discovered appliance address (0 until discovery completes). |
| `get_active_erd_client()` | Returns the ERD client for the discovered protocol (null until discovery completes). |
| `is_gea2_protocol()` | Returns `true` if a GEA2 appliance was discovered. |
| `get_state()` | Returns the current `AutodiscoveryState`. |

### 3.1 `start()`

Transitions from `AUTODISCOVERY_IDLE` to the first pending broadcast state based on available UARTs:
- If `has_gea3_uart_` is true: transitions to `AUTODISCOVERY_GEA3_BROADCAST_PENDING`
- Otherwise: transitions to `AUTODISCOVERY_GEA2_BROADCAST_PENDING`

Calls `run()` to execute the pending broadcast. Subsequent calls are no-ops (idempotent).

---

## 4. State Machine

### 4.1 States

```cpp
enum AutodiscoveryState {
  AUTODISCOVERY_IDLE,
  AUTODISCOVERY_GEA3_BROADCAST_PENDING,
  AUTODISCOVERY_GEA3_BROADCAST_WAITING,
  AUTODISCOVERY_GEA2_BROADCAST_PENDING,
  AUTODISCOVERY_GEA2_BROADCAST_WAITING,
  AUTODISCOVERY_COMPLETE
};
```

| State | Description |
|-------|-------------|
| `AUTODISCOVERY_IDLE` | Initial state. Waiting for `start()` to be called. |
| `AUTODISCOVERY_GEA3_BROADCAST_PENDING` | A GEA3 broadcast read is queued. If the queue is full, the state is held until `run()` is called again. |
| `AUTODISCOVERY_GEA3_BROADCAST_WAITING` | GEA3 broadcast sent. Waiting for a response or the broadcast window timer to expire. |
| `AUTODISCOVERY_GEA2_BROADCAST_PENDING` | A GEA2 broadcast read is queued. If the queue is full, the state is held until `run()` is called again. |
| `AUTODISCOVERY_GEA2_BROADCAST_WAITING` | GEA2 broadcast sent. Waiting for a response or the broadcast window timer to expire. |
| `AUTODISCOVERY_COMPLETE` | Terminal state. A board was discovered. No further action. |

### 4.2 State Transitions

```
AUTODISCOVERY_IDLE
  └─ start() called
       ├─ has_gea3_uart → AUTODISCOVERY_GEA3_BROADCAST_PENDING
       └─ !has_gea3_uart → AUTODISCOVERY_GEA2_BROADCAST_PENDING

AUTODISCOVERY_GEA3_BROADCAST_PENDING
  ├─ read queued successfully → AUTODISCOVERY_GEA3_BROADCAST_WAITING
  └─ queue full → stay in PENDING (retry on next run() call)

AUTODISCOVERY_GEA3_BROADCAST_WAITING
  ├─ response received (via on_gea3_activity_) → response marked, wait for timer
  ├─ timer fires + response marked → AUTODISCOVERY_COMPLETE
  └─ timer fires + no response → schedule_next_broadcast_()
       ├─ both UARTs → AUTODISCOVERY_GEA2_BROADCAST_PENDING (fallback)
       └─ GEA3 only → AUTODISCOVERY_GEA3_BROADCAST_PENDING (retry)

AUTODISCOVERY_GEA2_BROADCAST_PENDING
  ├─ read queued successfully → AUTODISCOVERY_GEA2_BROADCAST_WAITING
  └─ queue full → stay in PENDING (retry on next run() call)

AUTODISCOVERY_GEA2_BROADCAST_WAITING
  ├─ response received (via on_gea2_activity_) → response marked, wait for timer
  ├─ timer fires + response marked → AUTODISCOVERY_COMPLETE (gea2_protocol_active = true)
  └─ timer fires + no response → schedule_next_broadcast_()
       ├─ both UARTs → AUTODISCOVERY_GEA3_BROADCAST_PENDING (fallback)
       └─ GEA2 only → AUTODISCOVERY_GEA2_BROADCAST_PENDING (retry)

AUTODISCOVERY_COMPLETE (terminal — no transitions out)
```

### 4.3 State Diagram

```
IDLE
  │  start()
  ▼
GEA3_BROADCAST_PENDING ── broadcast sent ──► GEA3_BROADCAST_WAITING
  ▲                                                     │
  │  retry (GEA3 only)                                  │ response received
  │  fallback from GEA2                                 │ (marked, wait for timer)
  │                                                     ▼
  │                                              timer fires + response
  │                                                     │
  │                                                     ▼
  │                                              AUTODISCOVERY_COMPLETE ◄──┐
  │                                                     ▲                  │
  │  timer fires + no response                          │ timer fires +    │
  │  ───────────────────────────────────────────────────┘ response         │
  │                                                                        │
  └── fallback from GEA2 ──► GEA2_BROADCAST_PENDING ──► GEA2_BROADCAST_WAITING
                                      │                          │
                                      │ response received        │ timer fires + no response
                                      │ (marked, wait for timer) │ ──► fallback to GEA3
                                      ▼                          │
                                                timer fires +    │
                                                response ────────┘
```

### 4.4 `run()` Method

The `run()` method drives the state machine forward:

- **IDLE:** No-op.
- **GEA3_BROADCAST_PENDING:** Sends `ERD_APPLIANCE_TYPE` read to `GEA_BROADCAST_ADDRESS` via `gea3_erd_client_`. On success, arms the broadcast window timer and transitions to `GEA3_BROADCAST_WAITING`. On queue-full failure, stays in `PENDING`.
- **GEA3_BROADCAST_WAITING:** No-op (waiting for timer or event subscription).
- **GEA2_BROADCAST_PENDING:** Sends `ERD_APPLIANCE_TYPE` read to `GEA_BROADCAST_ADDRESS` via `gea2_erd_client_`. On success, arms the broadcast window timer and transitions to `GEA2_BROADCAST_WAITING`. On queue-full failure, stays in `PENDING`.
- **GEA2_BROADCAST_WAITING:** No-op (waiting for timer or event subscription).
- **COMPLETE:** No-op.

---

## 5. Self-Driving Architecture

### 5.1 Event Subscriptions

The manager subscribes directly to ERD client activity events at init time:

- **GEA3 subscription:** `tiny_event_subscribe(tiny_gea3_erd_client_on_activity(gea3_erd_client_), &gea3_activity_subscription_)`
- **GEA2 subscription:** `tiny_event_subscribe(tiny_gea3_erd_client_on_activity(gea2_adapter_client_), &gea2_activity_subscription_)`

Each subscription is only created if the corresponding UART is available and the client pointer is non-null.

### 5.2 `on_gea3_activity_()` Callback

Filters incoming GEA3 activity events:
1. Must be `read_completed` type
2. Must be for `ERD_APPLIANCE_TYPE`
3. Must have `data_size >= 1`
4. Must not have already discovered a board (`active_erd_client_ == nullptr`)
5. Must be in `AUTODISCOVERY_GEA3_BROADCAST_WAITING` state

If all conditions pass, calls `on_broadcast_response(address, app_type, true)`.

### 5.3 `on_gea2_activity_()` Callback

Filters incoming GEA2 adapter activity events:
1. Must be `read_completed` type
2. Must be for `ERD_APPLIANCE_TYPE`
3. Must have `data_size >= 1`
4. Must not have already discovered a board (`active_erd_client_ == nullptr`)
5. Must be in `AUTODISCOVERY_GEA2_BROADCAST_WAITING` state

If all conditions pass, calls `on_broadcast_response(address, app_type, false)`.

### 5.4 Timer-Based Transitions

The broadcast window timer (`broadcast_window_timer_`) fires after `AUTODISCOVERY_BROADCAST_WINDOW_MS` (1000 ms). The timer callback (`timer_callback_`) checks:
- If `active_erd_client_ != nullptr`: a response was received during the window. Sets `gea2_protocol_active_` if in GEA2 waiting state and transitions to `AUTODISCOVERY_COMPLETE`.
- If `active_erd_client_ == nullptr`: no response. Calls `schedule_next_broadcast_()` to retry with fallback logic.

The timer fires **after** the response is received to ensure the response is fully recorded before transitioning to complete. This prevents a race where the timer fires before the event subscription callback has set `active_erd_client_`.

---

## 6. Protocol Fallback

### 6.1 `schedule_next_broadcast_()`

Determines the next broadcast protocol based on UART availability and current state:

| UART Configuration | Current State | Next State |
|--------------------|---------------|------------|
| Both GEA3 + GEA2 | `GEA3_BROADCAST_WAITING` | `GEA2_BROADCAST_PENDING` (fallback to GEA2) |
| Both GEA3 + GEA2 | `GEA2_BROADCAST_WAITING` | `GEA3_BROADCAST_PENDING` (fallback to GEA3) |
| GEA3 only | any | `GEA3_BROADCAST_PENDING` (retry GEA3) |
| GEA2 only | any | `GEA2_BROADCAST_PENDING` (retry GEA2) |

When both UARTs are configured, the manager alternates between GEA3 and GEA2 on each failed attempt. When only one UART is available, it retries the same protocol indefinitely.

---

## 7. Timing Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `AUTODISCOVERY_BROADCAST_WINDOW_MS` | 1000 | Wait window in milliseconds for a response after each broadcast. |

---

## 8. Data Structures

```cpp
class AutodiscoveryManager {
    tiny_timer_group_t* timer_group_;
    i_tiny_gea3_erd_client_t* gea3_erd_client_;
    i_tiny_gea2_erd_client_t* gea2_erd_client_;
    i_tiny_gea3_erd_client_t* gea2_adapter_client_;
    bool has_gea3_uart_;
    bool has_gea2_uart_;

    AutodiscoveryState state_;
    tiny_timer_t broadcast_window_timer_;

    uint8_t host_address_;
    i_tiny_gea3_erd_client_t* active_erd_client_;
    bool gea2_protocol_active_;

    tiny_event_subscription_t gea3_activity_subscription_;
    tiny_event_subscription_t gea2_activity_subscription_;
};
```

---

## 9. Invariants

1. **Infinite retries:** The manager never gives up. If no board responds, it keeps retrying indefinitely, alternating between GEA3 and GEA2 when both UARTs are configured. The startup HSM will not transition past the autodiscovery phase until a valid board is discovered.

2. **Single response wins:** The first valid broadcast response sets `host_address_` and `active_erd_client_`. Subsequent responses are ignored (guarded by `active_erd_client_ != nullptr` check in both activity callbacks and `on_broadcast_response()`).

3. **Separate GEA3/GEA2 callbacks:** Two distinct subscription callbacks (`on_gea3_activity_` and `on_gea2_activity_`) ensure a response on one protocol is not misattributed to the other. Each callback also checks that the current state matches its protocol's waiting state.

4. **Completion is terminal:** `state_` is set to `AUTODISCOVERY_COMPLETE` exactly once and never leaves it, so the startup HSM can poll `get_state()` without a completion callback.

5. **Response marked before timer fires:** A response received during the broadcast window sets `active_erd_client_` immediately. The timer callback checks this flag — if set, it completes; if not, it retries. This ensures the response is captured even if it arrives just before the timer fires.

6. **Idempotent start:** Calling `start()` when not in `AUTODISCOVERY_IDLE` is a no-op.

7. **No heap allocation:** All state is embedded in the class. No `new`/`malloc`.

---

## 10. Dependencies

| Dependency | Role |
|------------|------|
| `i_tiny_gea3_erd_client` | GEA3 ERD client interface (broadcast read, activity events) |
| `i_tiny_gea2_erd_client` | GEA2 ERD client interface (broadcast read) |
| `tiny_timer` | One-shot timer for broadcast window |
| `tiny_event` / `tiny_event_subscription` | ERD client activity event subscriptions |
| `geappliances_bridge_constants.h` | `GEA_BROADCAST_ADDRESS`, `ERD_APPLIANCE_TYPE` |

---

## 11. Known Limitations

1. **No timeout:** The manager has no internal timeout — it retries indefinitely. The startup HSM handles the overall timeout for the autodiscovery phase.
2. **No response to late arrivals:** If a board responds after the timer fires and `schedule_next_broadcast_()` has already moved to the next protocol, the late response is ignored (the manager is no longer in the matching WAITING state).
3. **Single appliance:** The manager discovers one appliance. It does not support multiple appliances on the same bus.
4. **GEA2 uses GEA3 adapter client as active client:** When a GEA2 board is discovered, `get_active_erd_client()` returns the GEA2 adapter client (`gea2_adapter_client_`), which presents a GEA3-compatible interface. Callers must check `is_gea2_protocol()` to know the actual underlying protocol.
