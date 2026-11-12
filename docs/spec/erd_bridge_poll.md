# ERD Bridge Poll — Specification

## 1. Overview

### 1.1 Purpose

The ERD polling bridge probes a pre-built list of ERDs at a known host address, then settles into steady-state polling. It writes ERD values to the shared ERD cache. It has **zero** direct interaction with the MQTT client.

### 1.2 Responsibilities

- Probe a pre-built list of ERDs to determine which the appliance supports
- Maintain a fixed-capacity polling list of verified ERDs
- Execute steady-state polling cycles at a configured interval
- Write ERD values to the shared ERD cache (change detection via `erd_cache_update`)
- Recover from appliance loss

### 1.3 Not Responsible For

- Subscription-mode operation (see `erd_bridge_subscribe`)
- Any MQTT behavior (publishing, write requests, disconnect handling)
- Building the probe list (see `erd_poll_list_builder`)
- Bridge startup phase management (see `geappliances_bridge_startup_hsm`)
- Broadcast discovery (see `AutodiscoveryManager`)

---

## 2. Initialization

### 2.1 Primary Init

```c
void erd_bridge_poll_init(
    erd_bridge_poll_t* self,
    tiny_timer_group_t* timer_group,
    i_tiny_gea3_erd_client_t* erd_client,
    uint32_t polling_interval_ms,
    uint8_t host_address,
    const probe_entry_t* probe_list,
    uint16_t probe_list_count,
    erd_cache_t* cache);
```

| Parameter | Description |
|-----------|-------------|
| `timer_group` | Shared timer group for polling and appliance-lost timers. |
| `erd_client` | GEA3 ERD client interface. |
| `polling_interval_ms` | Interval between polling cycles (default 10000 ms). |
| `host_address` | The appliance's GEA bus address. The bridge always receives a known host address from autodiscovery — broadcast discovery is the responsibility of `AutodiscoveryManager`. |
| `probe_list` / `probe_list_count` | A pre-built array of probe entries (`erd` + `board_address`) to verify during the probe phase. Built by `erd_poll_list_builder` based on bridge mode and configuration. Each ERD is read once; successful reads are added to the polling list, failed reads are excluded. |
| `cache` | Shared `erd_cache_t` for storing ERD values. |

The probe list pointer must remain valid for the duration of the probe phase (typically the lifetime of the bridge instance). The caller is responsible for ensuring this — the bridge does not copy the probe list.

### 2.2 Destroy

```c
void erd_bridge_poll_destroy(erd_bridge_poll_t* self);
```

Stops timers and unsubscribes all event handlers. All state is stack-allocated or embedded in the struct — no heap cleanup needed. Guards against being called on a never-initialized struct (e.g., in test teardowns).

### 2.3 Discovery-Complete Callback

After init, the caller may set `self->on_discovery_complete` and `self->on_discovery_complete_context`. This callback fires once when the HSM enters `state_polling` (discovery complete). The callback must not send signals back to the polling HSM. It should iterate the cache via `erd_cache_get_next_entry()`.

---

## 3. State Machine

The polling bridge uses a hierarchical state machine (`tiny_hsm`) with a parent state (`poll_state_top`) and three child states.

### 3.1 Parent State: `poll_state_top`

Handles signals that apply regardless of the current child state:

| Signal | Behavior |
|--------|----------|
| `signal_appliance_lost` | Fires after `appliance_lost_timeout` (60 s) with no successful reads. Restores `erd_host_address` to `known_host_address` and transitions to `state_probe_list` for re-probing. |

### 3.2 Child States

#### `state_probe_list` (initial)

Probes each ERD in the pre-built probe list to verify support before adding to the polling list. This is the initial state — the bridge is always initialized with a known host address from autodiscovery.

**On entry:**
- Sets `appliance_erd_list` to `probe_list` and `appliance_erd_list_count` to `probe_list_count`.
- Sets `erd_index = 0` (`send_next_read_request` post-increments it after queuing each read).
- If re-entry (`polling_list_count > 0`): clears `erd_set` and `polling_list_count` via `clear_discovery_state()` (does NOT clear the ERD cache).
- If `probe_list_count > 0`: sends the first read via `send_next_read_request`.
- If `probe_list_count == 0`: transitions directly to `state_polling`, which arms the polling timer with the cycle phase `idle`.

**On `signal_read_completed`:**
- Adds the ERD to the polling list via `add_erd_to_polling_list()` (deduped via `erd_set`).
- Updates the ERD cache via `erd_cache_update()`.
- Advances to the next ERD or transitions to `state_polling`.

**On `signal_read_failed`:**
- Inserts the ERD into `erd_set` as an exclusion (prevents lazy registration in `state_polling`).
- Advances to the next ERD or transitions to `state_polling`.

#### `state_polling`

Steady-state polling. See §5.

#### `state_failed`

Terminal state entered after 3 consecutive failed polling cycles. See §5.4.

### 3.3 State Diagram

```
poll_state_top (parent — handles appliance_lost globally)
  ├─ state_probe_list (initial)
  │    ├─ entry: if probe_list empty → state_polling
  │    │         else → read first ERD
  │    ├─ read_completed: add to polling list + cache → next ERD or state_polling
  │    └─ read_failed: exclude from polling list → next ERD or state_polling
  │
  ├─ state_polling (steady state)
  │    ├─ entry: arm polling timer (cycle idle), fire callback
  │    ├─ polling_timer_expired: idle → start cycle; sending → resume sends;
  │    │   awaiting → awaiting_restart (restart when the cycle completes)
  │    ├─ read_completed: update cache, count completion, maybe restart cycle
  │    ├─ read_failed: track failure, count completion, maybe restart cycle;
  │    │               if 3 consecutive failed cycles → state_failed
  │    └─ appliance_lost → state_probe_list
  │
  └─ state_failed
       ├─ entry: set current_state = polling_state_failed
       ├─ appliance_lost: reset failure count → state_probe_list
       └─ (terminal — no timer or cycle activity)
```

---

## 4. Probe Phase Behavior

### 4.1 Sequential Probing

During the probe phase, only one ERD read is outstanding at any time. The next read is issued only after receiving a definitive response (`signal_read_completed` or `signal_read_failed`). No timers are used to advance the probe index.

### 4.2 Successful Probe

When a probe read completes successfully:
- The ERD is added to `erd_polling_list` (deduped via `erd_set`).
- The ERD data is written to the shared ERD cache via `erd_cache_update()`.
- The next ERD in the probe list is read.

### 4.3 Failed Probe

When a probe read fails (either `not_supported` or `retries_exhausted`):
- The ERD is **not** added to the polling list.
- The ERD is inserted into `erd_set` as an exclusion — this prevents the ERD from being lazily registered in `state_polling` if a late response arrives.
- The next ERD in the probe list is read.

### 4.4 Shared Discovery Handler

The `handle_discovery_list_signals()` function handles `signal_read_completed` and `signal_read_failed` for `state_probe_list`. It adds successful ERDs to the polling list and cache, then advances to the next ERD or transitions to `state_polling`.

**Note:** `state_probe_list` handles `signal_read_failed` explicitly (inserting into `erd_set` for exclusion) before delegating to `handle_discovery_list_signals` for `signal_read_completed`. This ensures failed probe ERDs are excluded from lazy registration in the polling state.

### 4.5 Late Response Handling

If a probe read response arrives while `state_polling` is active (e.g., the appliance responded slower than expected), the polling state handles it via `signal_read_completed`: the ERD is added to the polling list (if not already present) and the cache is updated.

---
## 5. Steady-State Polling

### 5.1 Entry

On entering `state_polling`:
- Resets `erd_index = 0`, `cycle_completed_count = 0`.
- Sets the cycle phase to `idle` and arms the polling timer for `polling_interval_ms`.
- Sets `current_state = polling_state_polling`.
- Calls `on_discovery_complete` callback if set.

### 5.2 Cycle Semantics

A polling cycle consists of sending reads for all ERDs in `erd_polling_list` and waiting for all responses.

**Cycle phases (`cycle_phase`):**

The polling timer doubles as the cycle-start timer, the resume timer, and the in-flight wait timer; `cycle_phase` records which role it is playing:

| Phase | Meaning | Timer expiry leads to |
|-------|---------|----------------------|
| `idle` | No cycle in flight; interval timer armed | Next cycle starts (`idle` → `sending`/`awaiting`) |
| `sending` | Budgeted sends in progress; resume timer armed | Sending resumes (`sending` → `sending`/`awaiting`) |
| `awaiting` | All reads queued; interval timer armed | Interval elapsed mid-cycle → `awaiting_restart` |
| `awaiting_restart` | Interval elapsed while reads were in flight; no timer armed | — (the cycle completes, then restarts immediately) |

**Cycle start** (from `idle`):
- Triggered by `signal_polling_timer_expired`.
- All reads are sent via `send_cycle_reads()`, which uses `send_poll_read_requests_bounded()` to stay within `POLL_CYCLE_SEND_BUDGET_MS` (100 ms). If the budget is exceeded, the phase stays `sending` and the function returns `false`; a resume timer (`POLL_CYCLE_RESUME_MS` = 100 ms) is armed to continue sending.
- `cycle_start_ms` is set to `millis()`.

**Cycle completion:**
- `cycle_completed_count` increments on each `signal_read_completed` and `signal_read_failed`.
- When `cycle_completed_count >= polling_list_count`, the cycle is complete.
- `on_polling_cycle_complete()` tracks consecutive failures: if `cycle_has_failure` is true, `polling_failure_count` is incremented; otherwise it is reset to 0. `cycle_has_failure` is then cleared.
- The next phase depends on the phase the cycle completed in:
  - From `awaiting`: returns to `idle` — the interval timer is still armed, so the next cycle waits for it to fire (respects the interval).
  - From `awaiting_restart`: starts the next cycle immediately (the interval already elapsed mid-cycle).
  - From `sending`: queue-full drops completed the cycle mid-send; returns to `idle` and arms the interval timer for the next cycle.

**Timer mid-cycle:**
- If the polling timer fires while reads are still in flight (phase `awaiting`): the phase becomes `awaiting_restart` and the cycle finishes naturally. The cycle-completion handler then starts the next cycle immediately.

**Timer resume:**
- If the phase is `sending` when the (resume) timer fires: resumes sending reads via `send_cycle_reads()`. If all reads are sent, the phase becomes `awaiting` and the interval timer is armed; otherwise the phase stays `sending` and the resume timer is re-armed.

### 5.3 Read Completion

**On `signal_read_completed`:**
- Resets the appliance-lost timer.
- If the ERD is not in `erd_set`: adds it to the polling list via `add_erd_to_polling_list()` (handles late probe responses that arrive during polling).
- Updates the ERD cache via `erd_cache_update()` (always publishes only on change).
- Increments `cycle_completed_count`; if cycle is complete, calls `on_polling_cycle_complete()`.

**On `signal_read_failed`:**
- Resets the appliance-lost timer.
- Sets `cycle_has_failure = true` to mark the current cycle as having at least one failure.
- Logs the failed ERD at debug level.
- Increments `cycle_completed_count`; if cycle is complete, calls `on_polling_cycle_complete()`.
- After `on_polling_cycle_complete()`, if `polling_failure_count >= 3`, transitions to `state_failed`.
- **Does not remove the ERD from the polling list.** Failed ERDs remain in the list and are retried each cycle.

### 5.4 Failed State

After 3 consecutive polling cycles where at least one ERD in each cycle failed, the bridge transitions to `state_failed`.

**On entry:**
- Sets `current_state = polling_state_failed`.
- Logs an error with the failure count.

**On `signal_appliance_lost`:**
- Restores `erd_host_address` to `known_host_address`.
- Resets `polling_failure_count = 0`.
- Transitions back to `state_probe_list` for re-probing (appliance came back online).

**No other signals are handled.** The state is terminal until the appliance-loss timer fires, which triggers recovery. No polling timers are armed in this state.

---

## 6. Data Structures

### 6.1 Polling List

- `erd_polling_list`: fixed-capacity array of `tiny_erd_t[POLLING_LIST_MAX_SIZE]`. No heap allocation.
- `polling_list_count`: number of valid entries.

### 6.2 ERD Set

- `erd_set`: `erd_set_t` — a fixed-capacity sorted array (capacity 645). Used for deduplication during discovery and polling list management. No heap allocation.

### 6.3 Probe List

- `probe_list`: pointer to the pre-built array of ERDs to probe. Set by the caller before `erd_bridge_poll_init()`. The bridge reads from this array during the probe phase but does not own or modify it.
- `probe_list_count`: number of ERDs in the probe list.

### 6.4 Cache Publish Behavior

The ERD cache always publishes only on change. Unchanged polled values are silently dropped.
### 6.5 Timers

- `polling_timer`: armed for `polling_interval_ms` after each cycle starts. Fires `signal_polling_timer_expired`.
- `appliance_lost_timer`: armed for `appliance_lost_timeout` (60 s) on each successful read. Fires `signal_appliance_lost`.

### 6.6 Health Metrics

- `cycle_start_ms`: `millis()` when the current cycle's first read was sent.
- `last_cycle_time_ms`: duration of the last completed cycle in milliseconds.
- `cycle_count`: total number of completed cycles since init.

### 6.7 State Tracking

- `current_state`: `polling_state_t` enum (`polling_state_none`, `polling_state_probing`, `polling_state_polling`, `polling_state_failed`). Updated on each state entry so callers can monitor bridge health without coupling to ESP logging headers.
- `cycle_phase`: `poll_cycle_phase_t` enum (`idle`, `sending`, `awaiting`, `awaiting_restart`). Tracks the steady-state cycle machine; encodes which timer is armed. See §5.2.
- `polling_failure_count`: consecutive cycle failure counter. Incremented when a cycle completes with `cycle_has_failure = true`; reset to 0 on a successful cycle. Triggers transition to `state_failed` when reaching 3.
- `cycle_has_failure`: set to `true` when any ERD in the current cycle fails; reset at cycle completion in `on_polling_cycle_complete()`.

---

## 7. Timing Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `POLL_YIELD_MS` | 50 ms | Per-batch time budget in `send_poll_read_requests_bounded()`. |
| `POLL_CYCLE_SEND_BUDGET_MS` | 100 ms | Maximum time for `send_cycle_reads()` before yielding. |
| `POLL_CYCLE_RESUME_MS` | 100 ms | Timer interval when send budget is exceeded. |
| `appliance_lost_timeout` | 60000 ms | Time without successful reads before triggering appliance loss. |
| `POLLING_LIST_MAX_SIZE` | (from `erd_lists.h`) | Hard cap on polling list capacity. |

---

## 8. Invariants

1. **No overlapping cycles:** A new cycle does not start until the previous one has completed (all ERDs have responded) and the interval timer has expired — or fired mid-cycle, which puts the machine in `awaiting_restart` so the next cycle starts as soon as the in-flight one completes.
2. **One read at a time during probe:** The next probe read is issued only after the previous one has a definitive response.
3. **No polling list growth on re-entry:** `erd_set` and `polling_list_count` are cleared on re-entry to `state_probe_list` after appliance loss, preventing duplicate ERD additions.
4. **Failed probe ERDs are excluded:** In `state_probe_list`, failed ERDs are inserted into `erd_set` as exclusions, preventing them from being lazily registered in `state_polling`.
5. **Cache NOT cleared on probe re-entry:** The ERD cache is not reset during `state_probe_list` entry or on appliance-loss re-discovery. The cache may be shared with the subscription bridge; stale entries are overwritten when new data arrives.
6. **Known host address preserved:** The bridge stores the host address in `known_host_address`. On appliance loss, this address is restored and re-probing begins at the same address.
7. **No heap allocation:** All data structures are fixed-capacity arrays embedded in the struct. No `new`/`malloc`/`std::set`/`void*` casting.
8. **Failure detection after 3 consecutive failed cycles:** When `polling_failure_count` reaches 3, the bridge transitions to `state_failed` and stops polling. Recovery is triggered by `signal_appliance_lost`, which resets the failure counter and re-probes from scratch.

---

## 9. Dependencies

| Dependency | Role |
|------------|------|
| `i_tiny_gea3_erd_client` | GEA3 ERD client interface (read, activity events) |
| `tiny_hsm` | Hierarchical state machine |
| `tiny_timer` | Timer group and timer instances |
| `erd_bridge_common.h` | Shared signals, timing constants, utility templates (`erd_set`, `arm_timer`) |
| `erd_lists.h` | Static ERD lists (used by `erd_poll_list_builder`) |
| `erd_cache.h` | ERD cache for change detection (`erd_cache_init`, `erd_cache_update`, `erd_cache_get_next_entry`) |

---

## 10. Known Limitations

1. **Failed ERDs never evicted:** An ERD that permanently fails during steady-state polling (e.g., removed by a firmware update) remains in the polling list indefinitely, consuming bus bandwidth and inflating cycle time.
2. **Probe list pointer lifetime:** The bridge does not copy the probe list — the caller must ensure the pointer remains valid for the duration of the probe phase (typically the bridge's lifetime).
