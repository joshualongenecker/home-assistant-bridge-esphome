# AutodiscoveryManager

## Purpose

Manages the appliance autodiscovery process on the GEA bus. Fully self-driving: owns its own timers and event subscriptions. The bridge calls `start()` to begin discovery; the manager fires `on_complete_cb` when a board is found, with retry logic and protocol fallback.

## Public API

| Method | Description |
|--------|-------------|
| `init(timer_group, gea3_client, gea2_client, gea2_adapter, gea3_uart_adapter, gea2_uart_adapter, has_gea3, has_gea2, client_address, on_complete_cb, on_complete_ctx)` | Initialize with timer group, ERD clients, UART availability flags, and a function-pointer completion callback with context |
| `start()` | Begin autodiscovery. Idempotent if already past IDLE. |
| `get_host_address()` | Returns the discovered appliance address |
| `get_active_erd_client()` | Returns the ERD client for the discovered protocol |
| `is_gea2_protocol()` | Returns `true` if a GEA2 appliance was discovered |
| `get_state()` | Returns the current `AutodiscoveryState` |

## State Machine

```
AUTODISCOVERY_IDLE
  └─ start() called →
    AUTODISCOVERY_GEA3_BROADCAST_PENDING → AUTODISCOVERY_GEA3_BROADCAST_WAITING
      ├─ response received (via event subscription) → (marked, waits for timer)
      ├─ timer fires, response marked → AUTODISCOVERY_COMPLETE
      └─ timer fires, no response → retry indefinitely → GEA2 fallback or re-try GEA3

    AUTODISCOVERY_GEA2_BROADCAST_PENDING → AUTODISCOVERY_GEA2_BROADCAST_WAITING
      ├─ response received (via event subscription) → (marked, waits for timer)
      ├─ timer fires, response marked → AUTODISCOVERY_COMPLETE (gea2_protocol_active = true)
      └─ timer fires, no response → retry indefinitely → GEA3 fallback or re-try GEA2

AUTODISCOVERY_COMPLETE (terminal state — only reached on successful discovery)
```

The 5-second startup delay before `start()` is handled by the startup HSM (`startup_state_startup_delay`), not by this module.

Constants:
- `AUTODISCOVERY_BROADCAST_WINDOW_MS = 1000` — wait window for response after each broadcast

## Dependencies

- `i_tiny_gea3_erd_client` — GEA3 ERD client interface
- `i_tiny_gea3_erd_client` (via `i_tiny_gea3_erd_client_t*`) — GEA2 adapter client (used when bridging GEA2 over GEA3 interface)
- `i_tiny_gea2_erd_client` — GEA2 ERD client interface
- `tiny_timer` — one-shot timers for broadcast windows
- `tiny_event` / `tiny_event_subscription` — ERD client activity event subscriptions
- `geappliances_bridge_constants.h` — `GEA_BROADCAST_ADDRESS`, `ERD_APPLIANCE_TYPE`

## Key Design Decisions

- **Fully self-driving**: The manager subscribes directly to ERD client activity events and uses timer-based state transitions. The bridge does not poll `run()` or route ERD client activity to the manager.
- **Separate GEA3/GEA2 callbacks**: Two distinct subscription callbacks (`on_gea3_activity_`, `on_gea2_activity_`) ensure a response on one protocol is not misattributed to the other.
- **Infinite retries**: The manager never gives up — if no board responds, it keeps retrying indefinitely, alternating between GEA3 and GEA2 (if both UARTs are configured). The startup HSM will not transition past the autodiscovery phase until a valid board is discovered.
- **Protocol fallback**: If both GEA3 and GEA2 UARTs are configured, failure to find a GEA3 board falls back to GEA2 (and vice versa) before retrying the same protocol.
- **Single response wins**: The first valid broadcast response sets the host address; subsequent responses are ignored.
- **Completion callback**: The `on_complete_cb` is invoked once when transitioning to `AUTODISCOVERY_COMPLETE`, allowing the bridge to proceed to device ID generation.

## Testing

Covered by unit tests in `test/tests/autodiscovery_manager_test.cpp` that exercise the full self-driving flow: `init()`, `start()`, event-based broadcast responses, timer-driven completion, retry logic, and protocol fallback.
