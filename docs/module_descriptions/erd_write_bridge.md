# ERD Write Bridge

## Purpose

Relays write requests from MQTT to the GEA3 ERD client and reports results back. Provides a thin relay so that the polling and subscription bridges can remain pure data sources (read → cache). Handles one write at a time, gating writes on appliance identification (host address must not be broadcast).

## Public API

| Function | Description |
|----------|-------------|
| `erd_write_bridge_init(self, timer_group, erd_client, mqtt_client, host_address)` | Initialize the write bridge: store dependencies, subscribe to MQTT write requests and ERD client activity events, initialize the HSM in `state_ready`. |
| `erd_write_bridge_destroy(self)` | Unsubscribe from both event sources (with null guards for partial init), disarm the HSM. No heap cleanup needed — all state is embedded in the struct. |

## Struct

```c
typedef struct {
  tiny_timer_group_t* timer_group;
  i_tiny_gea3_erd_client_t* erd_client;
  i_mqtt_client_t* mqtt_client;
  uint8_t erd_host_address;
  tiny_hsm_t hsm;
  tiny_event_subscription_t mqtt_write_request_subscription;
  tiny_event_subscription_t erd_client_activity_subscription;
  // Pending write state (one write at a time)
  tiny_gea3_erd_client_request_id_t pending_request_id;
  tiny_erd_t pending_erd;
} erd_write_bridge_t;
```

## State Machine

```
write_state_top (parent state — defers all signals)
  ├─ state_ready
  │    ├─ signal_write_requested:
  │    │   ├─ if host_address is broadcast: drop, report "not supported" via MQTT
  │    │   ├─ call tiny_gea3_erd_client_write()
  │    │   │   ├─ if queue fails: report "retries exhausted" via MQTT
  │    │   │   └─ if queued: store request_id and erd, transition to state_writing
  │    │   └─ defer all other signals
  │    └─ (no timer — state is event-driven)
  │
  └─ state_writing
       ├─ signal_write_requested: drop with warning (write already in progress)
       ├─ signal_write_completed:
       │   ├─ if request_id matches pending: report success via MQTT, transition to state_ready
       │   └─ if stale: log warning, ignore
       ├─ signal_write_failed:
       │   ├─ if request_id matches pending: report failure (with reason) via MQTT, transition to state_ready
       │   └─ if stale: log warning, ignore
       └─ defer all other signals
```

### Signals

Write bridge-specific signals (defined in `erd_write_bridge.cpp`):
- `signal_write_requested` — fired by the MQTT write request event subscription
- `signal_write_completed` — fired by the ERD client activity subscription on successful write
- `signal_write_failed` — fired by the ERD client activity subscription on write failure

## Write Flow

1. MQTT adapter receives a write command on a Home Assistant entity topic (primary board: `0x{ERD}/write`, secondary board: `0x{ADDR}_0x{ERD}/write`)
2. `mqtt_client_on_write_request` event fires with `erd`, `board_address`, `value`, and `size`
3. Bridge checks if the appliance is identified (host address ≠ broadcast)
4. If identified, resolves the target address (`board_address != 0xFF` → secondary, `board_address == 0xFF` → host address) and `tiny_gea3_erd_client_write()` queues the write to the ERD client
5. Bridge transitions to `state_writing`, storing the request ID, ERD, and board address
6. ERD client completes or fails the write asynchronously
7. `tiny_gea3_erd_client_on_activity` event fires with the result
8. Bridge matches the request ID, reports the result via `mqtt_client_update_erd_write_result` (with the stored board address so the result topic matches), and returns to `state_ready`

## Dependencies

- `i_tiny_gea3_erd_client.h` — GEA3 ERD client for queuing writes and receiving activity events
- `i_mqtt_client.h` — MQTT client interface for receiving write requests and reporting results
- `tiny_hsm.h` — hierarchical state machine
- `tiny_event.h` — event subscription for MQTT write requests and ERD client activity
- `tiny_timer.h` — timer group (stored but not directly used; present for API consistency)
- `erd_bridge_common.h` — shared signal definitions and timing constants

## Key Design Decisions

- **One write at a time**: The bridge serializes writes. If a write request arrives while another is in progress, it is dropped with a warning. This avoids request ID collisions and simplifies result matching.
- **Gated on appliance identification**: Writes are rejected if the host address is still the broadcast address (`tiny_gea_broadcast_address`). This prevents writes before the appliance is identified during startup.
- **Request ID matching**: The bridge stores the pending request ID and ERD, then validates incoming completion/failure events against them. Stale events (from a previous write or a different client) are logged and ignored.
- **Immediate failure reporting**: If `tiny_gea3_erd_client_write()` fails to queue (e.g., internal queue full), the failure is reported immediately via MQTT with reason `retries_exhausted` — no state transition occurs.
- **No timer dependency**: The state machine is purely event-driven. The `timer_group` field is stored for API consistency with other bridge modules but is not used.
- **Clean destroy with guards**: `erd_write_bridge_destroy()` guards against never-initialized structs (checks `timer_group`) and partial init (checks `mqtt_client` and `erd_client` individually before unsubscribing).

## Testing

Covered by unit tests in `test/tests/erd_write_bridge_test.cpp`. The state machine transitions, write gating, and result reporting are tested with simulated MQTT write request events and ERD client activity events.