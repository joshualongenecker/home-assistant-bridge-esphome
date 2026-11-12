# Polling Bridge Behavior Specification

This document defines the requirements for the GE Appliances polling bridge, covering ERD probe discovery and steady-state polling.

## 1. Probe ERD Discovery

### Requirement 1.1: Definitive Response Per ERD

Each ERD read during the probe phase MUST receive a definitive response from the GEA client before the next ERD read is issued. The probe logic MUST NOT use timers to advance to the next ERD; all retry and timeout behavior is handled by the lower-level GEA2/GEA3 client.

### Requirement 1.2: Successful Read

If an ERD read completes successfully with data:
- The ERD is added to the polling list.
- The ERD data is published to the shared ERD cache.
- The next ERD in the probe list is read.

### Requirement 1.3: Not Supported Response

If an ERD read returns a "not supported" response:
- The ERD is NOT added to the polling list.
- The ERD is permanently excluded from future polling (added to the exclusion set).
- The next ERD in the probe list is read.

### Requirement 1.4: Timeout / No Response

If an ERD read times out (all GEA client retries exhausted):
- The ERD is NOT added to the polling list.
- The ERD is permanently excluded from future polling (added to the exclusion set).
- The next ERD in the probe list is read.

### Requirement 1.5: One ERD at a Time

During the probe phase, only ONE ERD read shall be outstanding in the GEA client read buffer at any time. The next ERD read is issued only after the previous read has received a definitive response (success, not supported, or timeout).

### Implementation

The `state_probe_list` state:
- Sends one read request on entry (via `send_next_read_request`).
- Handles `signal_read_completed` via `handle_discovery_list_signals`, which adds the ERD to the polling list and cache.
- Handles `signal_read_failed` explicitly, inserting the ERD into `erd_set` as an exclusion before advancing.
- Transitions to `state_polling` when all ERDs in the probe list are processed.

### Prohibited

- Using `signal_timer_expired` to advance the probe ERD index.
- Arming retry timers in the probe state.
- Sending multiple ERD reads concurrently during probing.
- Adding ERDs to the polling list without a definitive response.

### Verification

Tests must verify that probing progresses only through `signal_read_completed` and `signal_read_failed` callbacks, never through timer expiration.

---

## 2. Steady-State Polling

### Requirement 2.1: Budgeted Sequential Reads

At the start of each polling cycle (triggered by the polling timer), all valid ERDs in the polling list are sent sequentially in budgeted batches. Each read is queued one at a time via `send_next_poll_read_request()`, advancing `erd_index` through the polling list. To avoid blocking the ESPHome main loop, `send_poll_read_requests_bounded()` enforces a per-batch time budget (`POLL_YIELD_MS`, 50 ms), checking every 10 ERDs; after each batch the main loop is yielded to via `esphome::delay(0)`. If the total send time exceeds `POLL_CYCLE_SEND_BUDGET_MS` (100 ms), `send_cycle_reads()` returns `false` and a resume timer (`POLL_CYCLE_RESUME_MS`, 100 ms) is armed to continue sending the remaining ERDs.

### Requirement 2.2: Cycle Completion Gate

The next polling cycle MUST NOT begin until BOTH conditions are met:
1. All ERDs in the polling list have received a response (successful read or failure).
2. The polling period timer has expired since the previous cycle started.

A failed read (timeout, not supported, or queue full) counts as a "completed" response for cycle-tracking purposes.

### Requirement 2.3: Failed Reads Must Not Block Cycle Completion

Failed ERD reads during steady-state polling MUST NOT block the polling cycle from completing. When any ERD read fails, the cycle completion counter MUST be incremented so the cycle can finish once all ERDs have responded (success or failure). The polling loop MUST never stall indefinitely waiting for a response that will never arrive.

### Requirement 2.4: Restart Pending

If the polling timer fires while a cycle is still in progress (reads outstanding but not all responses received), the cycle continues to completion. The next cycle starts immediately after the last ERD responds, without waiting for another timer expiration.

### Implementation

- `signal_polling_timer_expired` sends all reads via `send_cycle_reads()`.
- `cycle_completed_count` increments on each `signal_read_completed` and `signal_read_failed`.
- When `cycle_completed_count >= polling_list_count`, the cycle is complete.
- If the cycle completes in phase `awaiting_restart` (the timer fired mid-cycle), the next cycle starts immediately.
- If the cycle completes in phase `awaiting` (the interval timer is still armed), the code waits for the timer to fire.
- If the cycle completes in phase `sending` (queue-full drops finished the cycle mid-send), the next cycle starts on the interval timer.

### Prohibited

- Starting a new polling cycle before all ERDs have responded.
- Starting a new polling cycle before the polling timer has expired (unless the cycle is in phase `awaiting_restart`).
- Using retry timers for individual ERD reads in steady-state polling.
- Allowing failed reads to stall the cycle indefinitely.

### Verification

Tests must verify that:
- All ERDs are sent sequentially in budgeted batches at cycle start, with batch yielding via `esphome::delay(0)`; cycle completion requires both full response coverage and timer expiration.
- Failed reads (timeout, not supported) increment the cycle completion counter and allow the cycle to finish.
- Cycles with mixed success and failure outcomes complete normally and subsequent cycles begin when the polling timer fires.
- Cycles where all reads fail still complete and do not require the appliance_lost_timeout to recover.

---

## 3. Custom ERD Handling

### Requirement 3.1: Subscription Mode — Separate Polling Bridge After Registration Settles

When custom ERDs are defined in subscription or auto mode (with subscription active), the custom ERD polling phase MUST NOT start until the subscription registration phase has settled. Settlement is defined as a quiet window period (default 10 seconds) with no new ERDs seen via subscription publications.

Custom ERDs are polled by a **separate** polling bridge instance running alongside the subscription bridge. The subscription bridge continues to handle all standard ERD publications; the polling bridge handles only the custom ERDs that may not be covered by subscription.

### Requirement 3.2: Poll Mode — Included in Probe List

When custom ERDs are defined in poll mode (or auto mode that falls back to poll), custom ERDs MUST be included in the probe list built by `erd_poll_list_builder` and go through the same probe phase as standard ERDs. A single polling instance handles both standard and custom ERDs.

Custom ERDs that respond successfully during probing are added to the polling list. Custom ERDs that do not respond (not supported or timeout) are excluded from the polling list, just like any other ERD.

### Requirement 3.3: Auto Mode Fallback

When auto mode falls back from subscription to poll mode, custom ERDs are handled per Requirement 3.2 (included in the probe list built by `erd_poll_list_builder`).

### Implementation

**Subscription mode:**
- `maybe_start_custom_erd_polling_()` gates on three conditions: in subscription mode, subscription activity confirmed, and `custom_erd_subscription_last_activity_` older than the quiet window period (10s).
- `start_custom_erd_polling_()` builds the probe list via `build_poll_list_()` (which calls `erd_poll_list_builder`), then initializes a separate polling bridge with the probe list.
- The subscription bridge is NOT destroyed; both bridges share the same ERD client.

**Poll mode:**
- `build_poll_list_()` calls `erd_poll_list_builder` with the current mode and configuration. The builder includes custom ERDs in the probe list based on the mode and `appliance_api_parsing` setting.

### Prohibited

- Starting custom ERD polling before the subscription quiet window has elapsed in subscription mode.
- Using multiple polling bridge instances in poll mode.
- Starting custom ERD polling when no custom ERDs are defined.

### Verification

Tests must verify that custom ERD polling in subscription mode does not start until the quiet window elapses, and that in poll mode, custom ERDs go through the same probe phase as standard ERDs — successful ones are polled, failed ones are excluded.

---

## 4. Probe List Building

### Requirement 4.1: Pure Function

The probe list is built by `erd_poll_list_builder`, a pure function with no side effects. It takes the current bridge mode, subscription state, feature-bit results, custom ERDs, and appliance type, and returns a deduplicated list of ERDs to probe.

### Requirement 4.2: Decision Logic

| Mode | Condition | Probe List Contents |
|------|-----------|---------------------|
| SUBSCRIBE | subscription confirmed | custom ERDs only |
| POLL | appliance_api_parsing = true | feature-bit valid ERDs + custom ERDs |
| POLL | appliance_api_parsing = false | common + energy + appliance API feature + appliance-specific + custom ERDs |
| AUTO | subscription active | custom ERDs only |
| AUTO | subscription not active (fallback) | same as POLL with current appliance_api_parsing |

### Requirement 4.3: Deduplication

The returned list is deduplicated. Order is: standard ERDs first (in their original group order), then custom ERDs.

### Implementation

The `build_poll_list_()` helper in `geappliances_bridge_bridge_init.cpp` constructs an `ErdPollListConfig` from the bridge's current state and calls `build_erd_poll_list()`. The result is stored in `poll_probe_list_` and passed to `erd_bridge_poll_init()`.
