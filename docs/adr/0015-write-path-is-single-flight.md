# ADR-0015: The ERD write path is single-flight, and accepted writes are copied at accept time

Status: accepted (2026-11-27)
Related: ADR-0011, ADR-0016, `docs/spec/erd_write_bridge.md`

## Context

Home Assistant automations routinely publish two commands back to back (set a
mode, then a setpoint) while the first is still on the bus. The GEA bus can have
exactly one write outstanding, and the write-request event carries only a
pointer into the publisher's stack buffer — by the time a queued request would
be sent, that buffer belongs to the next message.

## Decision

`erd_write_bridge` is a single-flight HSM with an accept-time copy:

- **Only `state_ready` pumps the queue.** `state_writing` pops the head onto the
  bus and stays there until the ERD client reports the result, so exactly one
  write is in flight. `in_flight` is valid only in `state_writing`.
- **Accepted writes wait in a ring buffer in arrival order.** The payload is
  copied at accept time, because the write-request event carries a pointer into
  the publisher's stack buffer.
- **A request that cannot be honored is rejected with a `write_result`, never
  dropped silently.** That includes requests the ERD client cannot accept (they
  never become in-flight) and requests that arrive while the queue is full.
- **Identity is copied out before popping.** Popping only moves the head, and the
  next iteration may overwrite that slot.
- **Hex payloads are decoded into a local stack buffer before
  `tiny_event_publish()`.** The publish is synchronous — the subscriber callback
  runs to completion before it returns — so the stack buffer is valid for the
  whole delivery, and a new MQTT message arriving later cannot corrupt it.
- **Write results come back on the topic the write was issued on**, with the
  same `board_address` as the request (ADR-0011).

## Consequences

- Back-to-back commands from automations are serialized instead of colliding, and
  the caller always learns why a write did not happen.
- The bridge drops nothing silently, so an automation that overdrives the bus
  shows up as `write_result` errors rather than missing state.
- Any new write source must go through the queue; publishing straight to the bus
  would break the single-flight invariant.
- Callers must not hold a pointer into the request payload past the accept call.

## Code anchors

- `erd_write_bridge.h` / `.cpp` — `state_ready`, `state_writing`, the request
  ring, `in_flight`, `mqtt_client_on_write_request_args_t`, the reject-with-
  `write_result` paths.
- `erd_bridge_common.h` — the write-request event payload.
- `test/tests/write_flow_integration_test.cpp` — single-flight and rejection
  tests (one write per test: a second request during an in-flight write is
  dropped by design).
