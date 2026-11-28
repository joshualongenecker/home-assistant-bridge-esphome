# ERD Bridge Subscribe Specification

## Overview

The subscription bridge manages the GEA3 ERD subscription lifecycle. It subscribes to all ERD publications from a single appliance address, retains the subscription periodically, and writes received ERD values to the shared ERD cache. It has no dependency on MQTT — write handling and MQTT disconnect handling are delegated to `erd_write_bridge` and `erd_cache_mqtt_publisher` respectively.

## Public API

```c
void erd_bridge_subscribe_init(
  erd_bridge_subscribe_t* self,
  tiny_timer_group_t* timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  uint8_t address,
  erd_cache_t* cache);

void erd_bridge_subscribe_destroy(erd_bridge_subscribe_t* self);
```

| Parameter | Description |
|-----------|-------------|
| `self` | Pointer to the bridge struct (zero-initialized by caller) |
| `timer_group` | Shared timer group for retention timer |
| `erd_client` | GEA3 ERD client with subscription support |
| `address` | Appliance host address (not broadcast) |
| `cache` | Shared ERD cache — updated with each received publication |

## Struct Layout

```c
typedef struct {
  tiny_timer_group_t* timer_group;
  i_tiny_gea3_erd_client_t* erd_client;
  tiny_timer_t timer;
  tiny_timer_t quiet_timer;
  tiny_event_subscription_t erd_client_activity_subscription;
  erd_set_t erd_set;
  erd_cache_t* erd_cache;
  esphome::geappliances_bridge::ErdRegistry* erd_registry;  // valid-ERD filter; nullptr disables filtering
  tiny_hsm_t hsm;
  uint8_t erd_host_address;
  subscription_state_t current_state;
  uint8_t subscribe_failure_count;
} erd_bridge_subscribe_t;
```

## State Machine
```
sub_state_top (parent — handles publication signals globally)
  ├─ state_subscribing (initial)
  │    ├─ entry: disarm retention timer; set current_state; fall through to signal_subscription_failed
  │    ├─ subscription_host_came_online: clear ERD set, then fall through to entry
  │    ├─ subscription_failed: increment failure counter; on 3rd failure → state_failed; otherwise fall through to subscribe
  │    ├─ timer_expired: attempt subscribe(); arm retry timer only if subscribe() fails
  │    ├─ subscription_added_or_retained → state_subscribed
  │    └─ exit: disarm timer
  │
  ├─ state_subscribed
  │    ├─ entry: reset subscribe_failure_count; arm periodic timer (30 s retention) + quiet timer (2 s)
  │    ├─ subscription_host_came_online → state_subscribing
  │    └─ exit: disarm quiet timer only (retention timer persists)
  │
  ├─ state_steady
  │    ├─ entry: stop quiet timer; current_state = subscription_state_steady
  │    ├─ subscription_host_came_online → state_subscribing
  │    └─ exit: no-op (retention timer persists)
  │
  └─ state_failed
       ├─ entry: disarm all timers; current_state = subscription_state_failed
       └─ exit: no-op
```

### `sub_state_top` (Parent)

Handles signals globally across all child states:

**`signal_subscription_publication_received`:**
- Inserts the ERD into `erd_set` (if not already present)
- Updates the ERD cache with the received data
- If the ERD is **new** (not already in `erd_set`): transitions to `state_subscribed`, restarting the quiet period

**`signal_quiet_period_expired`:**
- If `erd_set.count == 0` (no ERDs were ever published), transitions to `state_failed` — the appliance accepted the subscription but never published anything.
- Otherwise, transitions to `state_steady` (no new ERDs registered for the quiet period).

**`signal_timer_expired`:**
- Calls `tiny_gea3_erd_client_retain_subscription()` to keep the appliance publishing
- Fires in both `state_subscribed` and `state_steady` (the retention timer is not disarmed when transitioning between them)

### `state_subscribing` (Initial)

- On entry: disarms the retention timer (to prevent spurious subscribe retries), sets `current_state` to `subscription_state_subscribing`, then **falls through** to the `signal_subscription_failed` case, which increments `subscribe_failure_count` before attempting `subscribe()`. This means every entry to `state_subscribing` (including initial init and host restart) increments the failure counter.
- On `signal_subscription_host_came_online`: clears `erd_set` (appliance may have changed its ERD set), then **falls through** to the entry case (disarm timer, increment failure counter, attempt subscribe).
- On `signal_subscription_failed`: increments `subscribe_failure_count`. On the 3rd failure, transitions to `state_failed`. On the 1st or 2nd failure, **falls through** to `signal_timer_expired` to retry subscribe immediately.
- On `signal_timer_expired`: attempts subscribe with `tiny_gea3_erd_client_subscribe()`. The retry timer is **only armed if `subscribe()` fails** (i.e. returns false); if it succeeds, no timer is armed.
- On `signal_subscription_added_or_retained`: transitions to `state_subscribed`
- On exit: disarms the retry timer

### `state_subscribed`

- On entry: sets `current_state` to `subscription_state_subscribed`, **resets `subscribe_failure_count` to 0**, arms a periodic retention timer at `subscription_retention_period` (30 s) and a one-shot quiet timer at `subscription_quiet_period` (2 s)
- On `signal_subscription_host_came_online`: transitions to `state_subscribing`
- On exit: disarms the quiet timer only; the retention timer persists across transitions to `state_steady`

### `state_steady`

- On entry: stops the quiet timer (belt-and-suspenders; it was already stopped on `state_subscribed` exit); sets `current_state` to `subscription_state_steady`
- The retention timer continues firing every 30 s (it was not disarmed on the transition from `state_subscribed`)
- On `signal_subscription_host_came_online`: transitions to `state_subscribing`
- On a new ERD publication (handled by `sub_state_top`): transitions back to `state_subscribed`, restarting the quiet period
- On exit: no-op (retention timer persists if transitioning back to `state_subscribed`)

### `state_failed`

- On entry: disarms all timers (retention and quiet); sets `current_state` to `subscription_state_failed`
- Terminal state — no transitions out. The main bridge detects this state and falls back to polling.
- On exit: no-op

## Event Subscription

The bridge subscribes to `tiny_gea3_erd_client_on_activity` during `init()`. The callback filters events by `address == erd_host_address` and routes them to HSM signals:

| Activity Type | HSM Signal |
|---|---|
| `subscription_added_or_retained` | `signal_subscription_added_or_retained` |
| `subscription_publication_received` | `signal_subscription_publication_received` |
| `subscription_host_came_online` | `signal_subscription_host_came_online` |
| `subscribe_failed` | `signal_subscription_failed` |
| `write_completed` / `write_failed` | Ignored (handled by `erd_write_bridge`) |

## ERD Set

The `erd_set_t` is a fixed-capacity sorted array (capacity 649). It tracks which ERDs have been seen via subscription publications. It is cleared on `signal_subscription_host_came_online` (appliance restart) so that new publications are re-registered.

## Dependencies

- `i_tiny_gea3_erd_client` — GEA3 ERD client with subscription support
- `tiny_hsm` — hierarchical state machine
- `tiny_timer` — periodic retention timer
- `erd_bridge_common.h` — shared signals, timing constants, and utility templates
- `erd_cache.h` — shared ERD cache for publishing values

## Key Design Decisions

- **No MQTT dependency**: The bridge writes to `erd_cache` only. MQTT publishing is handled by `erd_cache_mqtt_publisher`. Write handling is delegated to `erd_write_bridge`.
- **No `signal_mqtt_disconnected`**: MQTT disconnect handling has been moved to `erd_cache_mqtt_publisher`. The subscription bridge does not react to MQTT disconnects.
- **No `signal_write_requested`**: Write request handling has been extracted to `erd_write_bridge`.
- **Fixed-capacity ERD set**: Uses `erd_set_t` (sorted array) instead of `std::set` to eliminate heap node allocations.
- **30-second retention**: The subscription is retained every 30 seconds (`subscription_retention_period`) to keep the appliance publishing ERD values.
- **2-second quiet period**: After 2 seconds (`subscription_quiet_period`) with no new ERD registrations, the bridge transitions from `state_subscribed` to `state_steady`. This signals to the main bridge that the subscription has settled, allowing custom ERD polling to start.
- **No-publications fallback**: If the quiet period expires while in `state_subscribed` with zero ERDs in `erd_set`, the bridge transitions to `state_failed` instead of `state_steady`. This handles the case where the appliance accepts the subscription but never publishes any ERD values. The main bridge detects this and falls back to polling.
- **Failed state after 3 subscribe failures**: If `subscribe()` fails 3 times consecutively, the bridge transitions to `state_failed` and stops retrying. The main bridge detects this via `get_subscription_state()` returning `subscription_state_failed` and falls back to polling.
- **1-second resubscribe delay**: If `subscribe()` fails, the bridge waits 1 second (`resubscribe_delay`) before retrying.
- **Retention timer persists across subscribed/steady**: The retention timer is not disarmed when transitioning between `state_subscribed` and `state_steady`, ensuring continuous 30-second retention without gaps.
- **New ERD exits steady state**: When a new ERD is published while in `state_steady`, the bridge transitions back to `state_subscribed`, restarting the quiet period.
- **Retention timer disarmed in subscribing**: The retention timer is disarmed on entry to `state_subscribing` to prevent spurious subscribe retries (where `signal_timer_expired` triggers a subscribe attempt).
- **Clean destroy**: The event subscription is unsubscribed before the struct is freed, preventing use-after-free if events fire after destroy.
- **Cache NOT cleared on host restart**: The ERD cache is not reset during `signal_subscription_host_came_online`. The cache may be shared with the polling bridge; stale entries are overwritten when new data arrives.

## Testing

Covered by unit tests in `test/tests/erd_bridge_subscribe_test.cpp` and integration tests through the full bridge subscription flow. The state machine transitions are tested with simulated ERD client activity events, including steady-state transitions and retention timer behavior across state changes.

## Notes

1. **Two timers with deliberately different lifetimes.** `timer` is the periodic retention timer, armed with `subscription_retention_period` = 30 000 ms on entry to `state_subscribed`; `quiet_timer` is the one-shot quiet timer, armed with `subscription_quiet_period` = 2 000 ms in the same entry handler (`arm_quiet_timer()` stops the timer before re-arming, so it is a one-shot that restarts rather than a periodic timer). The retention timer is NOT disarmed on exit from `state_subscribed` or `state_steady` — it persists across the `subscribed` ↔ `steady` churn, and `state_steady` entry does not re-arm it because it is already armed. Re-arming it on every transition would reset the appliance's subscription retention clock and let the appliance drop subscriptions it still considers live.
2. **The retention timer is disarmed on entry to `state_subscribing` on purpose.** In that state `signal_timer_expired` is the subscribe-retry path, so a retention timer left over from a previous `state_subscribed` would fire a spurious re-subscribe. It is re-armed when the bridge transitions to `state_subscribed`; `state_failed` disarms both timers.
3. **The retention signal is handled by `sub_state_top`, and that is only sound while the child is `state_subscribed` or `state_steady`.** `signal_timer_expired` in `sub_state_top` calls `tiny_gea3_erd_client_retain_subscription(self->erd_client, self->erd_host_address)`; `state_subscribed` and `state_steady` defer it, which is what routes it to the parent, while `state_subscribing` handles the same signal itself as the subscribe-retry path. Any new child state added under `sub_state_top` must therefore be one of those two or handle the signal itself, otherwise a retain call is issued in a phase where no subscription is held.
4. **Quiet means "no new ERDs", not "no publications".** A publication for an ERD not yet in `erd_set` re-enters `state_subscribed`, which re-arms the quiet timer — so a steady bridge that suddenly publishes a previously unseen ERD leaves steady state and has to go quiet again before it is considered settled. `state_steady` entry stops the quiet timer outright because there is nothing left to wait for.
5. **A quiet period that expires with an empty ERD set is how "this appliance does not support subscriptions" is detected.** `signal_quiet_period_expired` with `erd_set.count == 0` transitions to `state_failed` instead of `state_steady`: the appliance never published anything, so there is nothing to be quiet about. `subscription_is_active()` excludes `subscription_state_failed`, which is what lets `erd_poll_list_builder` and `GeappliancesBridge::handle_subscription_failed()` fall back to polling in AUTO mode; the cache is left untouched because it may be shared with the polling bridge.
