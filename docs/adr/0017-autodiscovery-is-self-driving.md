# ADR-0017: Autodiscovery drives itself from byte-level RX, ignores the Wi-Fi module, and never stalls on a failed probe

Status: accepted (2026-11-27)
Related: ADR-0003, ADR-0011, `docs/spec/autodiscovery_manager.md`, ADR-0007

## Context

Finding the appliance board means listening on a UART that may carry traffic
from several devices, including the appliance's Wi-Fi module, which answers the
appliance-type broadcast too (relaying the appliance's identity) and often
answers *before* the control board. The GEA client interface hands over one
packet at a time through a single-packet buffer, which drops bytes when traffic
arrives faster than it is consumed — so a bridge that relies on it can miss the
only response it was going to get.

## Decision

`AutodiscoveryManager` is self-driving: it owns its timers and event
subscriptions, the bridge only calls `start()`, and the startup HSM polls
`get_state()` to advance past discovery.

- **RX is subscribed at byte level and fed into the manager's own packet
  assembler**, so every packet is seen even when the GEA interface's
  single-packet buffer drops bytes. The assemblers are **per protocol** and each
  tracks its own receive state, so GEA2 and GEA3 packets assemble concurrently.
- **Broadcast answers from the Wi-Fi module's addresses are ignored**, because
  that is not an appliance board the bridge can work with. A configured
  `board_address` still wins: when set, probes target that address instead of
  broadcasting `0xFF`, and the GEA3→GEA2 fallback still applies.
- **A probe attempt always starts its timer and moves to `WAITING`, even when the
  broadcast could not be queued.** Otherwise the state machine stops moving and
  discovery never completes.
- **A single response wins** (`state_probe_list` and the broadcast path both
  accept the first valid answer). `unsupported_erd` is not an answer: skip it and
  keep waiting for a success.
- **Autodiscovery retries indefinitely when no board responds**; the startup HSM
  does not transition until a valid board address is found. This is a deliberate
  non-timeout (ADR-0007).
- **Definitive probe failures exclude an ERD permanently.** Both
  `not_supported` and `retries_exhausted` mean the GEA client has finished its
  work, so the ERD is excluded from the Phase 3 polling list by simply not adding
  it. Phase 2 verification treats any failure — explicit rejection or exhausted
  retries — the same way.
- **The probe phase reads the appliance ERD list, not the polling list.**
  `state_probe_list` advances one ERD at a time through
  `appliance_erd_list`; the polling list is built afterwards.
- **Destroy stops the broadcast-window timer first**, before unsubscribing and
  nulling pointers, so the callback cannot fire mid-teardown. `start()` is
  idempotent if the machine is already past `IDLE`.
- **Init ordering is fixed:** autodiscovery is initialized after the ERD clients
  and UART adapters exist.

## Consequences

- Byte-level assembly costs a small state machine in the manager and buys
  immunity to the vendored interface's buffer limits.
- A bridge in front of a dead appliance stays in discovery forever rather than
  booting into a broken data plane; the operator sees discovery retrying.
- An appliance that reports an ERD as unsupported stops being polled for it, so
  the polling list reflects the appliance rather than the union of every known
  ERD.
- Ignoring the Wi-Fi module means the bridge may wait for the control board
  instead of locking onto a device that cannot serve ERDs.

## Code anchors

- `autodiscovery_manager.h` / `.cpp` — the byte-level subscription, per-protocol
  assemblers, the Wi-Fi address list, `start()`, `destroy()`, the broadcast
  window timer, the probe state machine.
- `erd_bridge_poll.cpp` / `.h` — `state_probe_list`, Phase 2 verification,
  Phase 3 polling-list construction.
- `geappliances_bridge_bridge_init.cpp` — init ordering after ERD clients and
  UART adapters.
- `geappliances_bridge_startup_hsm.cpp` — the wait-for-board gate.
- `test/tests/autodiscovery_manager_test.cpp`.
