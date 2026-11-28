# GEA2 ERD Client Adapter Specification

## Overview

The adapter wraps a GEA2 ERD client (`i_tiny_gea2_erd_client_t`) and presents it as a GEA3 ERD client (`i_tiny_gea3_erd_client_t`). This allows the polling bridge, write bridge, and any other GEA3-aware component to operate over a GEA2 bus without modification.

### Purpose

Provide a transparent GEA2-to-GEA3 interface shim so the rest of the bridge stack is protocol-agnostic.

### Responsibilities

- Delegate `read()` and `write()` calls to the underlying GEA2 client.
- Translate GEA2 request IDs to GEA3 request IDs (uint8_t cast).
- Re-publish GEA2 activity events as GEA3 activity events via the adapter's own `tiny_event`.
- Report that subscriptions are not supported (`subscribe()` always returns `false`; `retain_subscription()` always returns `false`).

### Not Responsible For

- Implementing subscription support — GEA2 does not support it.
- Transforming ERD data, addresses, or event payloads.
- Managing the lifecycle of the underlying GEA2 client.
- Scheduling or retrying reads/writes.

## Public API

```c
void gea2_erd_client_adapter_init(
  gea2_erd_client_adapter_t* self,
  i_tiny_gea2_erd_client_t* gea2_client);
```

| Parameter | Description |
|-----------|-------------|
| `self` | Pointer to the adapter struct (zero-initialized by caller) |
| `gea2_client` | GEA2 ERD client to wrap |

Initialization sets up the GEA3 API vtable, stores the GEA2 client pointer, initializes the adapter's activity event, and subscribes to the GEA2 client's activity event for re-publishing.

### `gea2_erd_client_adapter_destroy(self, gea2_client)`

Unsubscribes the adapter from the GEA2 client's activity event. Guards against null `self`, null `gea2_client`, and mismatched client pointer. Idempotent — safe to call multiple times. Sets `self->gea2_client` to `nullptr` after unsubscribe.

| Parameter | Description |
|-----------|-------------|
| `self` | Pointer to the adapter struct (may be null) |
| `gea2_client` | GEA2 client that was passed to `init()` (may be null) |

## i_tiny_gea3_erd_client API Implementation

The adapter implements `i_tiny_gea3_erd_client_api_t` with the following callbacks:

### `adapter_read(self, request_id, address, erd)`

Delegates to `tiny_gea2_erd_client_read(gea2_client, &gea2_id, address, erd)`. The GEA2 request ID is cast to `tiny_gea3_erd_client_request_id_t` and written to `*request_id` if non-null. Returns the GEA2 result directly.

### `adapter_write(self, request_id, address, erd, data, data_size)`

Delegates to `tiny_gea2_erd_client_write(gea2_client, &gea2_id, address, erd, data, data_size)`. The GEA2 request ID is cast to `tiny_gea3_erd_client_request_id_t` and written to `*request_id` if non-null. Returns the GEA2 result directly.

### `adapter_subscribe(self, address)`

Always returns `false`. GEA2 does not support subscriptions; callers should fall back to polling.

### `adapter_retain_subscription(self, address)`

Always returns `false`. GEA2 does not support subscriptions.

### `adapter_on_activity(self)`

Returns a pointer to the adapter's `on_activity` event interface. Callers subscribe to this event to receive GEA2 activity events presented as GEA3 activity events.

## Layout-Compatible Activity Args

GEA2 and GEA3 activity argument structs are layout-compatible for the event types used by the polling and write bridges:

| Type | Event | Layout Compatible |
|------|-------|-------------------|
| 0 | `read_completed` | Yes |
| 1 | `read_failed` | Yes |
| 2 | `write_completed` | Yes |
| 3 | `write_failed` | Yes |

The adapter re-publishes GEA2 activity args directly to its `on_activity` event without transformation. The `on_gea2_activity()` callback receives `const void* args` from the GEA2 client and passes it straight to `tiny_event_publish(&self->on_activity, args)`.

## Request ID Translation

GEA2 uses `tiny_gea2_erd_client_request_id_t` (uint8_t). GEA3 uses `tiny_gea3_erd_client_request_id_t` (uint8_t). The adapter casts the GEA2 ID to the GEA3 type with no transformation — both are `uint8_t`. This is safe because:

- The polling bridge correlates completion/failure events by request ID.
- The write bridge validates request IDs against `pending_request_id`.
- Both types have identical representation.

## Data Structures

```c
typedef struct {
  i_tiny_gea3_erd_client_t interface;
  i_tiny_gea2_erd_client_t* gea2_client;
  tiny_event_t on_activity;
  tiny_event_subscription_t gea2_sub;
} gea2_erd_client_adapter_t;
```

All members are embedded — no heap allocation. The struct is stack-allocated or embedded in the bridge.

## Invariants

1. **No subscription support:** `subscribe()` always returns `false`; `retain_subscription()` always returns `false`. The adapter never attempts to subscribe with the GEA2 client.
2. **Transparent to polling bridge:** Read requests and their completions/failures pass through without modification. The polling bridge sees the adapter as a normal GEA3 client.
3. **Event re-publish without transformation:** GEA2 activity events are forwarded to the adapter's `on_activity` event with the raw args pointer — no field conversion, no copy.
4. **Single GEA2 client:** The adapter wraps exactly one GEA2 client for its lifetime. There is no reconfiguration API.
5. **No memory allocation:** The adapter uses only stack-embedded state.

## Dependencies

| Dependency | Role |
|------------|------|
| `i_tiny_gea2_erd_client.h` | GEA2 ERD client interface (delegated to) |
| `i_tiny_gea3_erd_client.h` | GEA3 ERD client interface (presented by adapter) |
| `tiny_event.h` | Event infrastructure for re-publishing activity |

## Known Limitations

1. **No subscription support:** GEA2 does not support ERD subscriptions. The adapter always returns `false` for `subscribe()`, forcing callers to use polling. This is a protocol limitation, not an implementation gap.
2. **Request ID cast assumes compatible types:** The adapter casts `uint8_t` GEA2 request IDs to GEA3 request IDs. If either type changes in the future, the adapter must be updated.
3. **No error translation:** GEA2 failure reasons are passed through as-is. If GEA2 and GEA3 use different failure reason enums, the caller may see unexpected values.
4. **No reconfiguration:** Once initialized, the adapter cannot be pointed at a different GEA2 client. Re-initialization would require re-subscribing to the new client's activity event.

---

## Notes

Module-local decisions and hazards. The code carries no comments for these; they live here.

1. **The activity-args layout match is exact, and limited to types 0–3.** `tiny_gea2_erd_client_on_activity_args_t` and `tiny_gea3_erd_client_on_activity_args_t` (`i_tiny_gea2_erd_client.h`, `i_tiny_gea3_erd_client.h`) share the same header — `type` (`uint8_t`) then `address` (`uint8_t`) — and their `read_completed`, `read_failed`, `write_completed` and `write_failed` union members carry identical field sequences: `request_id` (`uint8_t` in both), `erd` (`tiny_erd_t`), then `data` + `data_size`, plus `reason` for `write_failed`. The activity-type enumerators also coincide in the same order: 0 = `read_completed`, 1 = `read_failed`, 2 = `write_completed`, 3 = `write_failed`. That identity is the only reason `on_gea2_activity()` may pass the GEA2 args pointer straight into the GEA3-typed `on_activity` event with no copy and no field conversion.

2. **The pass-through is safe only because GEA2 cannot emit activity type ≥ 4.** GEA3 adds four subscription activity types — 4 `subscription_added_or_retained`, 5 `subscribe_failed`, 6 `subscription_publication_received`, 7 `subscription_host_came_online` — and a fifth union member (`subscription_publication_received`) with no GEA2 counterpart. A subscriber reading those members of a re-published GEA2 event would be reading fields the GEA2 struct does not contain. GEA2's enum stops at 3, so those types never reach the adapter; if the GEA2 interface ever grows an activity type, `on_gea2_activity()` and every GEA3 subscriber MUST be re-checked against this note.

3. **Failure reasons pass through unchanged, and are value-compatible today.** GEA2 declares only `tiny_gea2_erd_client_read_failure_reason_retries_exhausted` and `tiny_gea2_erd_client_write_failure_reason_retries_exhausted`, both value 0; GEA3 declares `retries_exhausted` (0), `not_supported` (1) and `incorrect_size` (2). A GEA2 `reason` therefore always reads as GEA3 `retries_exhausted`. This is the concrete case behind Known Limitation 3.

4. **`subscribe()` / `retain_subscription()` return `false` because the GEA2 interface exposes no subscription call at all.** `i_tiny_gea2_erd_client_api_t` carries only `read`, `write` and `on_activity` — there is nothing to delegate to. The `false` from `adapter_subscribe()` drives the `tiny_gea3_erd_client_subscribe()` failure branch in `erd_bridge_subscribe.cpp` (`signal_subscription_failed` → `state_failed`, `current_state = subscription_state_failed`), which leaves the polling bridge polling every ERD. A GEA2 bus is polling-only; nothing may treat a successful `read()` as evidence that subscriptions work.

5. **`destroy()` is idempotent and only ever unsubscribes the client it was initialized with.** `gea2_erd_client_adapter_destroy()` returns without touching state if `self` or `self->gea2_client` is null (`init()` never ran), and returns without unsubscribing if the `gea2_client` argument differs from the stored pointer. On success it unsubscribes `gea2_sub` from `tiny_gea2_erd_client_on_activity(gea2_client)` and clears `gea2_client`, making any later call a no-op. Passing a client other than the one given to `gea2_erd_client_adapter_init()` leaves the subscription live — the caller MUST pass the same client, and there is no reconfiguration API (Known Limitation 4).

6. **One bridge handler serves both protocols because the adapter re-publishes.** `GeappliancesBridge` subscribes `erd_client_activity_subscription_` to `tiny_gea3_erd_client_on_activity(protocol_stack_.get_gea3_client())` when `has_gea3_uart()`, and `gea2_activity_subscription_` to `tiny_gea3_erd_client_on_activity(protocol_stack_.get_gea2_adapter_client())` when `has_gea2_uart()` (`geappliances_bridge.cpp:104-131`). Both callbacks cast the args to `tiny_gea3_erd_client_on_activity_args_t` and call the same `handle_erd_client_activity_()`. That single-handler design is sound only because of the layout match in Note 1: the adapter forwards the pointer, translating nothing, so the bridge cannot tell a GEA2 event from a GEA3 one — and nothing else in the stack re-checks the protocol.

7. **Why `gea2_pending` gating exists.** `GeappliancesBridge::loop()` computes `gea2_pending = autodiscovery_manager_.is_gea2_protocol() || get_state() == AUTODISCOVERY_GEA2_BROADCAST_PENDING || get_state() == AUTODISCOVERY_GEA2_BROADCAST_WAITING` and passes it to `protocol_stack_.run(gea2_pending)`; the stack resolves it to `need_gea2_loop = gea2_uart_ != nullptr && (gea2_pending || gea2_active_)` (`protocol_stack.cpp:173`). The OR matters for manual `device_id` configurations that declare only a GEA2 UART — autodiscovery never runs there, so the stack's own `gea2_active_` latch is the only signal — and the `gea2_uart_ != nullptr` term keeps the path inert when no GEA2 UART is declared. Without `gea2_pending`, the GEA2 tight loop (`kGea2LoopDurationMs` = 100 ms, hard cap `kGea2LoopHardCapMs` = 200 ms; GEA3 uses 10 ms / 20 ms) would not run while GEA2 autodiscovery is in flight, and the broadcast TX→RX cycle at 19200 baud would not complete inside one `loop()`.

8. **The framing the adapter's traffic rides (constants from `lib/tiny-gea-api`).** `tiny_gea_esc` = 0xE0, `tiny_gea_stx` = 0xE2, `tiny_gea_etx` = 0xE3, `tiny_gea_crc_seed` = 0x1021 (`tiny_gea_constants.h`). Escaping is exact, not "all high bytes": `needs_escape(byte)` is `(byte & 0xFC) == tiny_gea_esc`, i.e. only **0xE0, 0xE1, 0xE2, 0xE3** are escaped; a sender emits `tiny_gea_esc` first and the raw byte on the next send step (`tiny_gea3_interface.c:175-183`, `tiny_gea2_interface.c:168`), and a receiver takes the byte after an escape as-is. STX resets the assembler and seeds the CRC; an ETX with no preceding STX is not a valid packet; a valid packet's running CRC reduces to 0 by ETX; the minimum assembled packet is 6 bytes — dst(1) + len(1) + src(1) + payload(1) + CRC(2). On the wire the order is destination, payload_length, source, then payload, so the command byte is `payload[0]` (`tiny_gea_packet_t`, `tiny_gea_packet.h`). Command bytes: GEA3 read request 0xA0 and read response 0xA1 (`tiny_gea3_erd_api.h`); GEA2 read request **and** read response are both 0xF0 (`tiny_gea2_erd_api.h`), so a GEA2 response cannot be distinguished from a request by command byte — direction is the only discriminator, which is why the byte-level assembler in `autodiscovery_manager.cpp` filters on it and on the payload shape. Read-response ERD identifiers are big-endian (`(buffer[6] << 8) | buffer[7]`). The assembler buffer is `uint8_t buffer[256]`: max payload 248 + 7 bytes of overhead = 255, one byte of slack.
